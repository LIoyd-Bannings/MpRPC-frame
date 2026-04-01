#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <condition_variable>
#include <future>
#include <unordered_set>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include "mprpcapplication.h"
#include "../src/mcp.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "threadpool.h"
#include "mprpcclosure.h"
#include "zookeeperutil.h"
#include "logger.h"
#include "mini_rag.hpp"
#include <zmq.hpp>

using json = nlohmann::json;

// 1. 实例化全局的 ZeroMQ 引擎核心
zmq::context_t g_zmq_context(1);

// 2. 引入 MprpcChannel 里的 socket 池（用于 Poller 轮询收信）
extern std::unordered_map<std::string, std::shared_ptr<zmq::socket_t>> zq_dealer_pool_;
extern std::mutex dealer_mutex_;

// ==========================================
// 🎨 ANSI 颜色日志宏
// ==========================================
#define ANSI_RES "\033[0m"
#define ANSI_RED "\033[1;31m"
#define ANSI_GRE "\033[1;32m"
#define ANSI_YEL "\033[1;33m"
#define ANSI_BLU "\033[1;34m"
#define ANSI_PUR "\033[1;35m"
#define ANSI_CYA "\033[1;36m"

#define LOG_GW(format, ...) printf(ANSI_BLU "[GATEWAY] " format ANSI_RES "\n", ##__VA_ARGS__)
#define LOG_LLM(format, ...) printf(ANSI_GRE "[DEEPSEEK] " format ANSI_RES "\n", ##__VA_ARGS__)
#define LOG_RPC(format, ...) printf(ANSI_YEL "[RPC_NODE] " format ANSI_RES "\n", ##__VA_ARGS__)
#define LOG_SYS(format, ...) printf(ANSI_CYA "[SYSTEM] " format ANSI_RES "\n", ##__VA_ARGS__)

std::string LoadPrompt(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
        return "你是一个有用的助手。";
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    if (!content.empty() && content.back() == '\n')
        content.pop_back();
    return content;
}

// 全局 ZMQ 发送锁
std::mutex zmq_send_mutex;

int main(int argc, char **argv)
{
    MprpcApplication::Init(argc, argv);
    ThreadPool pool(16);
    auto channel = std::make_shared<MprpcChannel>(&pool);

    // =========================================================================
    // [极客核心] 启动独立的 ZeroMQ Poller 后台守护线程 (扫地僧)
    // 它的唯一任务：死死盯住所有的 DEALER 通道，一有回信，立刻唤醒业务！
    // =========================================================================
    std::thread poller_thread([&pool]()
                              {
        LOG_SYS("ZMQ Poller 后台守护线程已启动，正在监听回信...");
        while (true) {
            std::vector<std::shared_ptr<zmq::socket_t>> current_sockets;
            {
                // 快照获取当前所有的 socket，避免长时间占用锁
                std::lock_guard<std::mutex> lock(dealer_mutex_);
                for (auto& pair : zq_dealer_pool_) {
                    current_sockets.push_back(pair.second);
                }
            }

            bool got_message = false;
            // 轮询所有的物理节点通道
            for (auto& dealer : current_sockets) {
                zmq::message_t req_id_msg;
                // 使用 dontwait (非阻塞) 尝试收信，没有信立刻返回！
                if (dealer->recv(req_id_msg, zmq::recv_flags::dontwait)) {
                    zmq::message_t data_msg;
                    // 如果收到了 ID，说明一定有数据跟着来，阻塞收数据包
                    dealer->recv(data_msg, zmq::recv_flags::none); 

                    std::string req_id = req_id_msg.to_string();
                    std::string data = data_msg.to_string();

                    // 🌟 极其优雅！调用我们刚才写的派发函数，把魂魄找回来！
                    MprpcChannel::HandleRpcResponse(req_id, data, &pool);
                    got_message = true;
                }
            }

            // 如果这一轮没有任何信件，稍微睡 1 毫秒，防止 CPU 100% 空转
            if (!got_message) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } });
    poller_thread.detach(); // 把扫地僧放进后台独立运行

    // ==========================================
    //  1. 工具发现逻辑 (原汁原味)
    // ==========================================
    json tools = json::array();
    std::unordered_set<std::string> unique_tool_names;

    ZkClient zkCli;
    zkCli.Start();
    LOG_SYS("正在从 Zookeeper 同步全网 MCP 工具列表...");
    std::vector<std::string> nodes = zkCli.GetChildren("/McpToolService/ListTools");

    for (const auto &ip_port : nodes)
    {
        auto lt_controller = std::make_shared<MprpcController>();
        auto lt_request = std::make_shared<mcp::ListToolsRequest>();
        auto lt_response = std::make_shared<mcp::ListToolsResponse>();

        // 🌟 使用 shared_ptr 保证生命周期绝对安全
        auto prom = std::make_shared<std::promise<void>>();
        std::future<void> fut = prom->get_future();

        mcp::McpToolService_Stub mcp_stub(channel.get());
        mcp_stub.ListTools(lt_controller.get(), lt_request.get(), lt_response.get(),
                           new MprpcClosure([prom]()
                                            { prom->set_value(); })); // 👈 值捕获智能指针

        // std::promise<void> prom;
        // std::future<void> fut = prom.get_future();

        // mcp::McpToolService_Stub mcp_stub(channel.get());
        // mcp_stub.ListTools(lt_controller.get(), lt_request.get(), lt_response.get(),
        //                    new MprpcClosure([&prom]()
        //                                     { prom.set_value(); }));

        if (fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready && !lt_controller->Failed())
        {
            for (int i = 0; i < lt_response->tools_size(); ++i)
            {
                const auto &tool = lt_response->tools(i);
                if (unique_tool_names.insert(tool.name()).second)
                {
                    json tool_json;
                    tool_json["type"] = "function";
                    tool_json["function"]["name"] = tool.name();
                    tool_json["function"]["description"] = tool.description();
                    tool_json["function"]["parameters"] = json::parse(tool.input_schema());
                    tools.push_back(tool_json);
                    LOG_SYS("工具挂载成功: %s", tool.name().c_str());
                }
            }
        }
    }
    LOG_GW("MCP 同步完成！AI 脑内工具总数: %zu", tools.size());

    // ==========================================
    // 🧠 2. RAG 初始化
    // ==========================================
    std::string system_prompt = LoadPrompt("prompts/agent_system_prompt.txt");
    MiniVectorDB rag_db;
    std::ifstream kg_file("knowledge.txt");
    if (kg_file.is_open())
    {
        std::string line;
        LOG_GW(ANSI_PUR "正在将本地知识库向量化灌入内存..." ANSI_RES);
        while (std::getline(kg_file, line))
        {
            if (line.empty())
                continue;
            std::vector<float> vec = GetEmbedding(line);
            if (!vec.empty())
                rag_db.AddDocument(line, vec);
        }
        LOG_GW(ANSI_PUR "私有知识库挂载完毕！当前知识条数: %zu" ANSI_RES, rag_db.size());
    }
    else
    {
        LOG_GW(ANSI_RED "警告：未找到 knowledge.txt，RAG 模块将空载运行！" ANSI_RES);
    }

    // 保存你的原始 API Keys，传给线程池使用
    std::string ds_key = "sk-0cc31c9c975c449aafcea1846040f2af";
    std::string kimi_key = "sk-x6p7nbgGvhCeDRXodkCiXfCQQjGz6oAnBw47XywDREOMU6To";

    // ==========================================
    //  3. ZeroMQ 网络监听
    // ==========================================
    zmq::context_t context(1);
    zmq::socket_t server(context, zmq::socket_type::router);
    server.bind("tcp://*:5555");
    LOG_SYS("ZeroMQ 异步网关已就绪，正在监听 5555 端口...");

    while (true)
    {
        zmq::message_t identity, empty, request;
        server.recv(identity);
        server.recv(empty);
        server.recv(request);

        std::string user_input = request.to_string();
        std::string client_id = identity.to_string();
        LOG_GW("监听到新请求 [ID: %s]，立即抛入线程池异步处理...", client_id.c_str());

        // =====================================================================
        // 4. 线程池并发任务 (完美还原双 API 容灾和 Reviewer 机制)
        // =====================================================================
        pool.addTask([&server, &rag_db, identity = std::move(identity), user_input,
                      system_prompt, tools, ds_key, kimi_key, channel]() mutable
                     {
            
            // 线程内的 HTTP 客户端（避免并发冲突）
            httplib::Client cli("https://api.deepseek.com");
            cli.set_read_timeout(30, 0);
            httplib::Headers headers = { {"Authorization", "Bearer " + ds_key}, {"Content-Type", "application/json"} };

            httplib::Client backup_cli("https://api.moonshot.cn");
            backup_cli.set_read_timeout(30, 0);
            httplib::Headers backup_headers = { {"Authorization", "Bearer " + kimi_key}, {"Content-Type", "application/json"} };

            json messages_array = json::array();
            messages_array.push_back({{"role", "system"}, {"content", system_prompt}});

            // 🔍 [RAG 拦截器]
            LOG_GW("正在进行语义空间检索...");
            
// 🔍 [RAG 拦截器] 
std::vector<float> user_query_vec = GetEmbedding(user_input);

// 1. 🌟 只保留带得分的检索
auto search_hits = rag_db.SearchWithScores(user_query_vec, 4);

std::string strong_context = "";
bool has_high_confidence_knowledge = false;

for (auto& hit : search_hits) {
    float score = hit.score;
    if (score > 0.55f) { // 阈值可以根据效果调成 0.55
        strong_context += hit.content + "\n";
        has_high_confidence_knowledge = true;
    }
}

// 2. 🌟 最终 Prompt 构建（只构建一次！）
std::string final_prompt; 
if (has_high_confidence_knowledge) {
    LOG_GW(ANSI_GRE "检测到高置信度知识 (Score > 0.55)，强行锁定 RAG 模式。" ANSI_RES);
    final_prompt = "【已知事实（最高优先级）】：\n" + strong_context + 
                   "\n【用户问题】：\n" + user_input +
                   "\n【硬性指令】：背景知识已给出明确答案。严禁调用 SearchDataBase 工具！直接根据事实回答。";
} else {
            LOG_GW(ANSI_YEL "知识库无匹配，转入工具探索模式。" ANSI_RES);
                final_prompt = "【当前背景知识】：无相关记录。\n" +
                  std::string( "【用户问题】：\n") + user_input +
                   "\n【指令】：请调用工具查询数据库以获取准确答案。";
}
    if (messages_array.size() > 5) { 
    final_prompt += "\n【追问提醒】：这是连续对话。如果用户在要更多数据，请不要重复刚才查过的 SQL！"
                    "请利用 OFFSET 跳过已读数据，或适当增大 LIMIT（最高允许 50）。";
    }

// 🌟 删掉后面那段冗余的 rag_db.Search(user_query_vec, 5) 和 重复的 final_prompt 定义！

messages_array.push_back({{"role", "user"}, {"content", final_prompt}});
            

            // ⚙️ [Agent 核心循环]
            std::string final_summary = "处理出错";
            int retry_count = 0;
            const int MAX_RETRY = 3;
            int tool_call_count = 0;      // 工具调用计数器
                const int MAX_TOOL_CALLS = 6;
            while (true) {
                // 滑动窗口截断
                if (messages_array.size() > 12) messages_array.erase(messages_array.begin() + 1);

                json request_body = {
                    {"model", "deepseek-chat"},
                    {"messages", messages_array},
                    {"tools", tools},
                    {"tool_choice", "auto"}};

                if (tool_call_count >= MAX_TOOL_CALLS) {
            LOG_GW(ANSI_YEL "已达到最大工具调用次数(%d)，正在强行终止循环并索要答案..." ANSI_RES, MAX_TOOL_CALLS);
            request_body.erase("tools");       // 撤走工具箱
             request_body.erase("tool_choice"); 
            messages_array.push_back({{"role", "user"}, {"content", "你已经查询了多次数据库，现在请根据你手里已有的信息（包括背景知识和之前的查询结果），直接给用户一个最终回答，禁止再查询！"}});
         request_body["messages"] = messages_array;
    }


                LOG_GW("正在请求 DeepSeek 思考第 %d 步动作...", tool_call_count + 1);
                auto res = cli.Post("/chat/completions", headers, request_body.dump(), "application/json");

                // 🛡️ [熔断降级 (Fallback) 完美恢复]
                if (!res || res->status != 200) {
                    LOG_GW(ANSI_RED "触发熔断，切换至 Kimi..." ANSI_RES);
                        // 1.  创建一个“干净”的记忆包
                    json clean_messages = json::array();
                 clean_messages.push_back({{"role", "system"}, {"content", system_prompt}});
                    // 2. 把带 RAG 增强的那个 user prompt 塞进去
                    clean_messages.push_back({{"role", "user"}, {"content", final_prompt}});


                    // 3. 准备 Kimi 请求
                 request_body["model"] = "moonshot-v1-32k"; // 换个大窗口的
                request_body["messages"] = clean_messages; // 传这个干净的
                    
            
                    res = backup_cli.Post("/v1/chat/completions", backup_headers, request_body.dump(), "application/json");
                    if (!res || res->status != 200) break;
                }

                json response_json = json::parse(res->body);
                auto message = response_json["choices"][0]["message"];
                messages_array.push_back(message);

                if (message.contains("tool_calls")) {
                    tool_call_count++;
                    auto tool_call = message["tool_calls"][0];
                    std::string function_name = tool_call["function"]["name"];
                    std::string arguments = tool_call["function"]["arguments"];

                    LOG_LLM("大模型决定执行动作: %s", function_name.c_str());

                    auto rpc_controller = std::make_shared<MprpcController>();
                    auto mcp_request = std::make_shared<mcp::CallToolRequest>();
                    auto mcp_response = std::make_shared<mcp::CallToolResponse>();
                    mcp_request->set_name(function_name);
                    mcp_request->set_arguments(arguments);

                    auto prom = std::make_shared<std::promise<void>>();
                    std::future<void> fut = prom->get_future();
                    mcp::McpToolService_Stub mcp_stub(channel.get());
                    mcp_stub.CallTool(rpc_controller.get(), mcp_request.get(), mcp_response.get(), 
                                      new MprpcClosure([prom](){ prom->set_value(); }));

                    std::future_status status = fut.wait_for(std::chrono::seconds(10));
                    std::string rpc_result_str;

                    if (status == std::future_status::timeout) rpc_result_str = "错误：物理节点执行超时。";
                    else if (rpc_controller->Failed()) rpc_result_str = "RPC 崩溃: " + rpc_controller->ErrorText();
                    else rpc_result_str = mcp_response->content();

                    // 🕵️ [A2A 核心进化区：Reviewer 唤醒机制完美恢复]
                    if ((rpc_result_str.find("ERROR") != std::string::npos || rpc_result_str.find("错误") != std::string::npos) && retry_count < MAX_RETRY) {
                        retry_count++;
                        LOG_GW(ANSI_YEL "检测到沙盒执行异常！(当前重试: %d) 正在唤醒审查员 Reviewer 分析..." ANSI_RES, retry_count);

                        json reviewer_messages = {
                            {{"role", "system"}, {"content", "你是一个资深代码审计专家。请分析以下报错日志和代码，指出根本原因，并给 Coder 提出具体的修改意见。"}},
                            {{"role", "user"}, {"content", "代码参数: " + arguments + "\n报错内容: " + rpc_result_str}}
                        };

                        auto rev_res = cli.Post("/chat/completions", headers, json({{"model", "deepseek-chat"}, {"messages", reviewer_messages}}).dump(), "application/json");

                        if (rev_res && rev_res->status == 200) {
                            std::string advice = json::parse(rev_res->body)["choices"][0]["message"]["content"];
                            LOG_SYS("Reviewer 审查意见: %s", advice.c_str());
                            rpc_result_str = "【自动纠错系统】检测到代码执行失败。\n[内核日志]: " + rpc_result_str +
                                             "\n[架构师专家建议]: " + advice + "\n请基于上述意见重新生成并执行正确的代码！";
                        }
                    }

                    messages_array.push_back({
                        {"role", "tool"},
                        {"tool_call_id", tool_call["id"]},
                        {"content", rpc_result_str}
                    });
                } else {
                    // 拿到最终答案
                    final_summary = message["content"].get<std::string>();
                    break;
                }
                // 兜底：如果强制总结阶段也跑完了，退出
            if (tool_call_count > MAX_TOOL_CALLS) break;
            }

            // 📨 5. 结果精准回传
            {
                std::lock_guard<std::mutex> lock(zmq_send_mutex);
                server.send(identity, zmq::send_flags::sndmore);
                server.send(zmq::message_t(0), zmq::send_flags::sndmore);
                server.send(zmq::buffer(final_summary), zmq::send_flags::none);
            }
            LOG_GW("任务完成，已回传至 Client !"); });
    }
    return 0;
}