#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include <mutex>
#include <condition_variable>
#include "mprpcapplication.h"
#include "../src/mcp.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "threadpool.h"
#include <future>
#include "mprpcclosure.h"
#include "zookeeperutil.h"
#include "logger.h"
#include <unordered_set>
using json = nlohmann::json;

// ==========================================
// 🎨 ANSI 颜色日志宏 (让终端变身赛博监控台)
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

// ==========================================
// 📖 外部 Prompt 动态加载
// ==========================================
std::string LoadPrompt(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        LOG_GW(ANSI_RED " 无法打开外部策略文件 %s，将使用默认空策略！", filename.c_str());
        return "你是一个有用的助手。"; // 兜底策略
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    if (!content.empty() && content.back() == '\n')
        content.pop_back();
    LOG_GW(ANSI_PUR " 成功加载外部 Prompt 策略: %s", filename.c_str());
    return content;
}

int main(int argc, char **argv)
{
    MprpcApplication::Init(argc, argv);
    ThreadPool pool(4);
    std::shared_ptr<MprpcChannel> channel = std::make_shared<MprpcChannel>(&pool);

    // 1. 动态加载系统提示词
    std::string system_prompt = LoadPrompt("prompts/agent_system_prompt.txt");

    // 2. 资源初始化
    httplib::Client cli("https://api.deepseek.com");
    cli.set_read_timeout(30, 0);
    httplib::Headers headers = {

        // 注意！把新申请的 API Key 替换到下面这里：
        {"Authorization", "Bearer sk-0cc31c9c975c449aafcea1846040f2af"},

        {"Content-Type", "application/json"}};

    // ==========================================================
    //  MCP 动态发现逻辑
    // ==========================================================
    LOG_SYS("正在从 Zookeeper 同步全网 MCP 工具列表...");

    ZkClient zkCli;
    zkCli.Start();
    std::vector<std::string> nodes = zkCli.GetChildren("/McpToolService/ListTools");

    json tools = json::array();
    // 新增：用于给全局工具去重的哈希集合
    std::unordered_set<std::string> unique_tool_names;
    for (const auto &ip_port : nodes)
    {
        LOG_SYS(" -> 正在同步节点 [%s] 的技能树...", ip_port.c_str());

        auto lt_controller = std::make_shared<MprpcController>();
        auto lt_request = std::make_shared<mcp::ListToolsRequest>();
        auto lt_response = std::make_shared<mcp::ListToolsResponse>();
        auto prom = std::make_shared<std::promise<void>>();
        std::future<void> fut = prom->get_future();

        google::protobuf::Closure *done = new MprpcClosure([prom]()
                                                           { prom->set_value(); });

        mcp::McpToolService_Stub mcp_stub(channel.get());
        mcp_stub.ListTools(lt_controller.get(), lt_request.get(), lt_response.get(), done);

        if (fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready)
        {
            if (!lt_controller->Failed())
            {
                for (int i = 0; i < lt_response->tools_size(); ++i)
                {
                    const auto &tool = lt_response->tools(i);
                    // 核心去重逻辑：如果这个工具名字之前已经加过了，就直接跳过！
                    if (unique_tool_names.find(tool.name()) != unique_tool_names.end()) {
                        continue; 
                    }
                    unique_tool_names.insert(tool.name()); // 记录下这个名字
                    json tool_json;
                    tool_json["type"] = "function";
                    tool_json["function"]["name"] = tool.name();
                    tool_json["function"]["description"] = tool.description();
                    try
                    {
                        tool_json["function"]["parameters"] = json::parse(tool.input_schema());
                        tools.push_back(tool_json);
                        LOG_SYS("工具挂载成功: %s", tool.name().c_str());
                    }
                    catch (...)
                    {
                        continue;
                    }
                }
            }
        }
    }
    LOG_GW("MCP 同步完成！AI 脑内挂载工具总数: %zu", tools.size());

    // ==========================================
    // 2. 核心主循环
    // ==========================================
    while (true)
    {
        std::string user_input;
        std::cout << "\n" ANSI_PUR "[Lele 的终端] > " ANSI_RES;
        std::getline(std::cin, user_input);

        if (user_input == "exit" || user_input == "quit")
            break;
        if (user_input.empty())
            continue;

        // 将外部加载的 prompt 塞进对话第一句
        json messages_array = json::array({{{"role", "system"}, {"content", system_prompt}},
                                           {{"role", "user"}, {"content", user_input}}});

        while (true)
        {
           if (messages_array.size() > 12) 
            {
                // 安全检查：确保我们要删的 [2] 和 [3] 确实是一对！
                if (messages_array.size() >= 4 && 
                    messages_array[2]["role"] == "assistant" && 
                    messages_array[3]["role"] == "tool") 
                {
                    // 永远保留 [0] System 和 [1] User
                    messages_array.erase(2); // 删掉最老的 assistant 动作
                    messages_array.erase(2); // 删掉后，原来的 [3] 变成了 [2]，继续删掉它对应的 tool 结果
                    
                    LOG_GW("[注意力修剪] 成功清理最老的一轮工具记忆。当前记忆深度: %zu", messages_array.size());
                } 
                else 
                {
                    // 如果格式意外不匹配，为了安全，只删除一条
                    messages_array.erase(2);
                }
            }

            json request_body = {
                {"model", "deepseek-chat"},
                {"messages", messages_array},
                {"tools", tools},
                {"tool_choice", "auto"}};

            LOG_GW("正在请求 DeepSeek 思考下一阶段动作...");
            auto res = cli.Post("/chat/completions", headers, request_body.dump(), "application/json");

            // 🌟 强力护盾：现在就算断网断费，也能一眼看出来！
            if (!res)
            {
                LOG_GW(ANSI_RED "❌ 网络通信致命崩溃！错误码: %d", (int)res.error());
                break;
            }
            if (res->status != 200)
            {
                LOG_GW(ANSI_RED "❌ API 报错！状态码: %d", res->status);
                std::cout << "详情: " << res->body << std::endl;
                break;
            }

            json response_json = json::parse(res->body);
            auto message = response_json["choices"][0]["message"];
            messages_array.push_back(message);

            if (message.contains("tool_calls"))
            {
                auto tool_call = message["tool_calls"][0];
                std::string function_name = tool_call["function"]["name"];
                std::string arguments = tool_call["function"]["arguments"];

                LOG_LLM("大模型决定执行动作: %s", function_name.c_str());
                LOG_RPC("下发参数: %s", arguments.c_str());

                auto rpc_controller = std::make_shared<MprpcController>();
                auto mcp_request = std::make_shared<mcp::CallToolRequest>();
                auto mcp_response = std::make_shared<mcp::CallToolResponse>();

                mcp_request->set_name(function_name);
                mcp_request->set_arguments(arguments);

                auto prom = std::make_shared<std::promise<void>>();
                std::future<void> fut = prom->get_future();

                google::protobuf::Closure *done = new MprpcClosure([prom]()
                                                                   { prom->set_value(); });

                mcp::McpToolService_Stub mcp_stub(channel.get());
                mcp_stub.CallTool(rpc_controller.get(), mcp_request.get(), mcp_response.get(), done);

                std::future_status status = fut.wait_for(std::chrono::seconds(10));
                std::string rpc_result_str;

                if (status == std::future_status::timeout)
                {
                    rpc_result_str = "错误：物理节点执行超时。";
                    LOG_RPC(ANSI_RED "❌ 节点执行超时！");
                }
                else
                {
                    if (!rpc_controller->Failed())
                    {
                        rpc_result_str = mcp_response->content();
                        LOG_RPC(" 物理执行完毕，反哺进记忆链条...");
                    }
                    else
                    {
                        rpc_result_str = "RPC 通信崩溃: " + rpc_controller->ErrorText();
                    }
                }

                messages_array.push_back({{"role", "tool"},
                                          {"tool_call_id", tool_call["id"]},
                                          {"content", rpc_result_str}});
            }
            else
            {
                std::cout << "\n" ANSI_GRE "==================================================\n";
                std::cout << " [DeepSeek 终极总结]: \n"
                          << message["content"].get<std::string>() << "\n";
                std::cout << "==================================================" ANSI_RES "\n";
                break;
            }
        }
    }
    return 0;
}