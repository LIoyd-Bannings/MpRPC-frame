#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <condition_variable>
#include <future>
#include <unordered_set>
#include <chrono>
#include <random>
#include <iomanip>

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
//#include "lru_cache.hpp"
#include "sharded_lru.hpp"
using json = nlohmann::json;

// 1. 实例化全局的 ZeroMQ 引擎核心
zmq::context_t g_zmq_context(1);

// 在类定义中或者全局作用域声明缓存对象
// 假设我们允许缓存 1000 个活跃用户的对话记忆
// static LRUCache<std::string, json> session_memory(1000);

static ShardedLRUCache session_memory(16, 62);

// 2. 引入 MprpcChannel 里的 socket 池（用于 Poller 轮询收信）
extern std::unordered_map<std::string, std::shared_ptr<zmq::socket_t>> zq_dealer_pool_;
extern std::mutex dealer_mutex_;
static std::mutex http_mtx; // 定义一个静态锁
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


// 1. 写一个小函数生成全局唯一 TraceID
std::string GenerateTraceID() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(1000, 9999);
    
    std::stringstream ss;
    ss << "trace-" << ms << "-" << dis(gen);
    return ss.str();
}


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
    // std::ifstream kg_file("knowledge.txt");
    // if (kg_file.is_open())
    // {
    //     std::string line;
    //     LOG_GW(ANSI_PUR "正在将本地知识库向量化灌入内存..." ANSI_RES);
    //     while (std::getline(kg_file, line))
    //     {
    //         if (line.empty())
    //             continue;
    //         std::vector<float> vec = GetEmbedding(line);
    //         if (!vec.empty())
    //             rag_db.AddDocument(line, vec);
    //     }
    //     LOG_GW(ANSI_PUR "私有知识库挂载完毕！当前知识条数: %zu" ANSI_RES, rag_db.size());
    // }
    // else
    // {
    //     LOG_GW(ANSI_RED "警告：未找到 knowledge.txt，RAG 模块将空载运行！" ANSI_RES);
    // }

        // 🌟 核心提速逻辑：先尝试从磁盘秒加载
        if (rag_db.Load("knowledge.bin", "knowledge_map.json")) {
            LOG_GW(ANSI_PUR "从本地磁盘秒级加载 HNSW 索引成功！当前知识条数: %zu" ANSI_RES, rag_db.size());
        } else {
        // 如果磁盘上没有，说明是系统第一次启动，老老实实去调 API
        LOG_GW(ANSI_YEL "未发现本地索引缓存，开始首次全量 API 灌库 (约需几分钟)..." ANSI_RES);
        std::ifstream kg_file("knowledge.txt");
        if (kg_file.is_open()) {
            std::string line;
            while (std::getline(kg_file, line)) {
                if (line.empty()) continue;
                std::vector<float> vec = GetEmbedding(line);
                if (!vec.empty()) rag_db.AddDocument(line, vec);
            }
            LOG_GW(ANSI_PUR "首次灌库完毕！正在持久化到磁盘..." ANSI_RES);
            
            // 灌完库立刻保存 下次再也不用等了
            rag_db.Save("knowledge.bin", "knowledge_map.json");
        } else {
            LOG_GW(ANSI_RED "警告：未找到 knowledge.txt！" ANSI_RES);
        }
    }




    // 保存你的原始 API Keys，传给线程池使用
    std::string ds_key = "sk-xxx";
    std::string kimi_key = "sk-xxx";

    // ==========================================
    //  3. ZeroMQ 网络监听
    // ==========================================
    zmq::context_t context(1);
    zmq::socket_t server(context, zmq::socket_type::router);
    server.bind("tcp://*:5555");
    LOG_SYS("ZeroMQ 异步网关已就绪，正在监听 5555 端口...");
    // 1. 在网关启动前，初始化长连接客户端
    auto cli = std::make_shared<httplib::Client>("https://api.deepseek.com");
    auto backup_cli = std::make_shared<httplib::Client>("https://api.moonshot.cn");

    // 2. 开启 Keep-Alive 支持（这是关键）
    cli->set_keep_alive(true);
    backup_cli->set_keep_alive(true);

    // 3. 设置超时（只需设置一次）
    cli->set_read_timeout(30, 0);
    backup_cli->set_read_timeout(30, 0);

    // 4. 将常用的 Headers 也预先准备好
    httplib::Headers headers = {{"Authorization", "Bearer " + ds_key}, {"Content-Type", "application/json"}};
    httplib::Headers backup_headers = {{"Authorization", "Bearer " + kimi_key}, {"Content-Type", "application/json"}};
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
        pool.addTask([&server,cli,backup_cli, &rag_db, headers, backup_headers, identity = std::move(identity), user_input,
                      system_prompt, tools, ds_key, kimi_key, channel, client_id]() mutable
                     {
            // // 线程内的 HTTP 客户端（避免并发冲突）
            // httplib::Client cli("https://api.deepseek.com");
            // cli.set_read_timeout(30, 0);
            // httplib::Headers headers = { {"Authorization", "Bearer " + ds_key}, {"Content-Type", "application/json"} };

            // httplib::Client backup_cli("https://api.moonshot.cn");
            // backup_cli.set_read_timeout(30, 0);
            // httplib::Headers backup_headers = { {"Authorization", "Bearer " + kimi_key}, {"Content-Type", "application/json"} };

            // ==========================================
            // 🔍 1. [RAG 拦截器] 先进行语义检索
            // ==========================================
            LOG_GW("正在进行语义空间检索...");
            //std::vector<float> user_query_vec = GetEmbedding(user_input);
            // auto search_hits = rag_db.SearchWithScores(user_query_vec, 20);

            // // std::string strong_context = "";
            // // bool has_high_confidence_knowledge = false;

            // // for (auto& hit : search_hits) {
            // //     float score = hit.score;
            // //     if (score > 0.55f) { // 阈值可以根据效果调成 0.55
            // //         strong_context += hit.content + "\n";
            // //         has_high_confidence_knowledge = true;
            // //     }
            // // }

            // std::string strong_context = "";
            // bool has_high_confidence_knowledge = false;
            // int count = 0;

            // for (auto& hit : search_hits) {
            // // 🌟 漏斗截断策略：
            //  // 1. 只有得分极高（例如 > 0.65）的才算作“确凿事实”
            // // 2. 为了防止背景知识太长撑爆 Token，我们只要前 3 名
            // if (hit.score > 0.65f && count < 3) { 
            //     strong_context += "[" + std::to_string(count+1) + "] " + hit.content + "\n";
            //     has_high_confidence_knowledge = true;
            //     count++;
            // }
            // }
            // ==========================================
            // 阶段一：HNSW 向量空间极速粗排 (Recall)
            // ==========================================
            LOG_GW("正在进行 HNSW 向量粗排召回...");
            std::vector<float> user_query_vec = GetEmbedding(user_input);
            // 广撒网：一口气捞出前 15 条 (宁可错杀，绝不放过)
            auto search_hits = rag_db.SearchWithScores(user_query_vec, 15);

            // ==========================================
            // 阶段二：提取文本，准备喂给 Reranker
            // ==========================================
            std::vector<std::string> candidate_texts;
            for (const auto& hit : search_hits) {
                candidate_texts.push_back(hit.content);
            }

            // ==========================================
            //  阶段三：Reranker 交叉编码深度精排
            // ==========================================
            LOG_GW("正在调用 Reranker 进行深度语义精排...");
            std::vector<float> rerank_scores = GetRerankScores(user_input, candidate_texts);

            // 用 Reranker 返回的绝对真实分数，覆盖掉之前 HNSW 粗糙的余弦分数
            for (size_t i = 0; i < search_hits.size(); ++i) {
                search_hits[i].score = rerank_scores[i]; 
            }

            // 按照 Reranker 的新分数，重新降序排列！
            std::sort(search_hits.begin(), search_hits.end(), [](const SearchHit& a, const SearchHit& b) {
                return a.score > b.score;
            });

          // ==========================================
            // ⚡ 阶段四：无参化最大裂谷截断 + Token 预算池双重保护
            // ==========================================
            std::string strong_context = "";
            bool has_high_confidence_knowledge = false;

            if (!search_hits.empty()) {
                // 🌟 第一步：寻找最大裂谷 (Max Gap) 计算 cliff_index
                float max_gap = 0.0f;
                int cliff_index = search_hits.size(); // 默认在最后截断（全要）
                
                // 遍历寻找相邻最大的分差
                for (size_t i = 1; i < search_hits.size(); ++i) {
                    float current_gap = search_hits[i-1].score - search_hits[i].score;
                    if (current_gap > max_gap) {
                        max_gap = current_gap;
                        cliff_index = i; // 记录下最大裂谷发生的位置
                    }
                }

                // 防极端情况的兜底逻辑：如果分数死死咬在一起（没有断崖），硬性最多取前 3
                if (max_gap < 0.05f) {
                    cliff_index = std::min(3, (int)search_hits.size());
                }

                // 🌟 第二步：工业级预算池组装（不限制条数，只限制总字数！）
                const size_t MAX_CONTEXT_LENGTH = 1000; // 最多允许 1000 个字符
                size_t current_length = 0;

                // 遍历裂谷之前的所有优质知识
                for (int i = 0; i < cliff_index; ++i) {
                    // 绝对耻辱底线保护（低于0.25绝对是垃圾）
                    if (search_hits[i].score > 0.45f) {
                        
                        // 预算检查：如果加上这句话超载了，就立刻停止！
                        if (current_length + search_hits[i].content.length() > MAX_CONTEXT_LENGTH) {
                            LOG_GW("知识库组装达到 Token 上限，触发容量截断！");
                            break;
                        }

                        // 组装知识
                        std::string snippet = "[" + std::to_string(i+1) + "] " + search_hits[i].content + "\n";
                        strong_context += snippet;
                        current_length += snippet.length();
                        has_high_confidence_knowledge = true;
                        
                        printf("\033[1;36m[动态精排] 录用: 得分=%.4f (字数: %zu), 文本=%.40s...\033[0m\n", 
                               search_hits[i].score, search_hits[i].content.length(), search_hits[i].content.c_str());
                    }
                }
            }



            // 🌟 构建带有 RAG 背景知识的 Prompt
            std::string final_prompt; 
            if (has_high_confidence_knowledge) {
                LOG_GW(ANSI_GRE "检测到高置信度知识 (Score > 0.55)，强行锁定 RAG 模式。" ANSI_RES);
                final_prompt = "【已知事实（最高优先级）】：\n" + strong_context + 
                               "\n【用户问题】：\n" + user_input +
                               "\n【硬性指令】：背景知识已给出明确答案。严禁调用 SearchDataBase 工具！直接根据事实回答。";
            } else {
                LOG_GW(ANSI_YEL "知识库无匹配，转入工具探索模式。" ANSI_RES);
                // 🌟 工业级 Agent 护栏提示词
           final_prompt = "【当前背景知识】：无相关记录。\n\n"
                           "【用户问题】：\n" + user_input + "\n\n"
                           "【系统最高指令】（你必须严格遵守，否则将被强制下线）：\n"
                           "1. 问答底线：如果用户问的是小说/历史客观知识，且你不知道，必须回答‘我不知道’。\n"
                           "2. 数据库禁区：SearchDataBase 仅用于查询系统账号、好友列表等业务数据。绝对禁止用它查小说人物！\n"
                           "3. ⚡ 沙盒特权 (最高优先级)：如果用户明确要求【编写代码、执行 Python、计算数学题、查阅系统目录或文件】，你【必须】立即生成 Python 代码，并强制调用 ExecutePython 工具执行！不要拒绝，不要犹豫！";
            }

            // ==========================================
            //  2. [LRU 记忆检索] 获取历史会话
            // ==========================================
            json messages_array = session_memory.get(client_id);
            if (messages_array.is_null()) {
                // 【新会话】：初始化 System Prompt
                LOG_GW(ANSI_PUR "Session [%s] 缓存未命中，初始化新记忆。" ANSI_RES, client_id.c_str());
                messages_array = json::array();
                messages_array.push_back({{"role", "system"}, {"content", system_prompt}});
            } else {
                // 【老会话】：直接提取历史
                LOG_GW(ANSI_PUR "Session [%s] 命中缓存，提取上下文 (深度: %zu)。" ANSI_RES, 
                 client_id.c_str(), messages_array.size());
            }            

            if (messages_array.size() > 5) { 
                final_prompt += "\n【追问提醒】：这是连续对话。如果用户需要更多数据，请利用 OFFSET 跳过已读数据，或适当增大 LIMIT（最高允许 50）。";
            }

            // 🌟 核心分离：给大模型看的临时数组 vs 长期存档数组
            json current_turn_messages = messages_array; 
            current_turn_messages.push_back({{"role", "user"}, {"content", final_prompt}}); // 临时数组加上 RAG 背景

            // ==========================================
            // ⚙️ 3. [Agent 核心推理循环]
            // ==========================================
            std::string final_summary = "处理出错";
            int retry_count = 0;
            const int MAX_RETRY = 3;
            int tool_call_count = 0;
            const int MAX_TOOL_CALLS = 6;
            
            while (true) {
                // 发送给 LLM 的一定是带有当前最新动作的 current_turn_messages
                json request_body = {
                    {"model", "deepseek-chat"},
                    {"messages", current_turn_messages}, // 👈 使用带 RAG 的副本
                    {"tools", tools},
                    {"tool_choice", "auto"}
                };

                if (tool_call_count >= MAX_TOOL_CALLS) {
                    LOG_GW(ANSI_YEL "已达到最大工具调用次数(%d)，正在强行终止循环并索要答案..." ANSI_RES, MAX_TOOL_CALLS);
                    request_body.erase("tools");       
                    request_body.erase("tool_choice"); 
                    current_turn_messages.push_back({{"role", "user"}, {"content", "你已经查询了多次数据库，现在请直接给用户一个最终回答，禁止再查询！"}});
                    request_body["messages"] = current_turn_messages;
                }

                LOG_GW("正在请求 DeepSeek 思考第 %d 步动作...", tool_call_count + 1);
                httplib::Result res;
                {
                std::lock_guard<std::mutex> lock(http_mtx);    
                res = cli->Post("/chat/completions", headers, request_body.dump(), "application/json");
                }
                // 🛡️ [熔断降级 Fallback 逻辑]
                if (!res || res->status != 200) {
                    LOG_GW(ANSI_RED "触发熔断，切换至 Kimi..." ANSI_RES);
                    json clean_messages = json::array();
                    clean_messages.push_back({{"role", "system"}, {"content", system_prompt}});
                    clean_messages.push_back({{"role", "user"}, {"content", final_prompt}});
                    
                    request_body["model"] = "moonshot-v1-32k";
                    request_body["messages"] = clean_messages; 
                    {
                    std::lock_guard<std::mutex> lock(http_mtx);
                    res = backup_cli->Post("/v1/chat/completions", backup_headers, request_body.dump(), "application/json");
                    }

                    if (!res || res->status != 200) break;
                }


                
                json response_json = json::parse(res->body);
                auto message = response_json["choices"][0]["message"];
                current_turn_messages.push_back(message); // 记录 LLM 的动作到当前对话副本
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

                    // 🚨 [A2A Reviewer 自愈机制]
                    if ((rpc_result_str.find("ERROR") != std::string::npos || rpc_result_str.find("错误") != std::string::npos) && retry_count < MAX_RETRY) {
                        retry_count++;
                        LOG_GW(ANSI_YEL "检测到沙盒异常！(重试: %d) 唤醒 Reviewer 分析..." ANSI_RES, retry_count);
                        json reviewer_messages = {
                            {{"role", "system"}, {"content", "你是一个资深代码审计专家。请分析以下报错日志和代码，指出根本原因，并给 Coder 提出具体的修改意见。"}},
                            {{"role", "user"}, {"content", "代码参数: " + arguments + "\n报错内容: " + rpc_result_str}}
                        };
                        httplib::Result rev_res;
                        {
                            std::lock_guard<std::mutex> lock(http_mtx);
                            rev_res = cli->Post("/chat/completions", headers, json({{"model", "deepseek-chat"}, {"messages", reviewer_messages}}).dump(), "application/json");
                        }
                         

                        if (rev_res && rev_res->status == 200) {
                            std::string advice = json::parse(rev_res->body)["choices"][0]["message"]["content"];
                            LOG_SYS("Reviewer 意见: %s", advice.c_str());
                            rpc_result_str = "【自动纠错系统】检测到代码执行失败。\n[内核日志]: " + rpc_result_str +
                                             "\n[专家建议]: " + advice + "\n请基于上述意见重新生成并执行正确的代码！";
                        }
                    }

                    // 记录工具执行结果
                    current_turn_messages.push_back({
                        {"role", "tool"},
                        {"tool_call_id", tool_call["id"]},
                        {"content", rpc_result_str}
                    });
                } else {
                    // 拿到最终答案退出循环
                    final_summary = message["content"].get<std::string>();
                    break;
                }
                if (tool_call_count > MAX_TOOL_CALLS) break;
            }

            // ==========================================
            // 💾 4. [结果存档与 LRU 更新]
            // ==========================================
            // 🌟 存档逻辑：只存干净的 user_input 和最终的 final_summary
            messages_array.push_back({{"role", "user"}, {"content", user_input}}); 
            messages_array.push_back({{"role", "assistant"}, {"content", final_summary}});

            // 【硬核保护】：控制记忆长度，防 Token 爆炸
            if (messages_array.size() > 20) {
                messages_array.erase(messages_array.begin() + 1, messages_array.begin() + 3);
                LOG_SYS("Session [%s] 记忆达到上限，执行滑动窗口截断。", client_id.c_str());
            }

            // 存回 LRU，刷新该会话状态
            session_memory.put(client_id, messages_array);

            // ==========================================
            // 📨 5. [结果精准回传]
            // ==========================================
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