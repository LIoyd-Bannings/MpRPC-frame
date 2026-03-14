#include <iostream>
#include <string>
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "json.hpp"
#include <mutex>
#include <condition_variable>
#include "mprpcapplication.h"
#include "agent.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "threadpool.h"


using json = nlohmann::json;


//线程安全的同步等待器
class  SyncClosure :public google::protobuf::Closure{

public:
    void Run()override
    {
        std::lock_guard<std::mutex>lock(mtx_);
        is_done_=true;
        cv_.notify_all();//唤醒被卡住的主线程
    }

    void Wait()
    {
        std::unique_lock<std::mutex>lock(mtx_);
        cv_.wait(lock,[this](){return is_done_;});
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    bool is_done_=false;

};


int main(int argc, char **argv) {


    // ==========================================
    // 0. RPC 神经系统初始化 (必须在最前面!)
    // ==========================================

    MprpcApplication::Init(argc, argv);
    ThreadPool pool(4);//准备4个网络IO线程
    ai_agent::AgentServiceRpc_Stub stub(new MprpcChannel(&pool));

    // ==========================================
    // 1. 资源初始化（只做一次，不要放进循环里）
    // ==========================================
    httplib::Client cli("https://api.deepseek.com");
    cli.set_read_timeout(30, 0);

    httplib::Headers headers = {
        {"Authorization", "Bearer sk-0cc31c9c975c449aafcea1846040f2af"}, // 记得填Key
        {"Content-Type", "application/json"}
    };

    // “核武器”工具箱，稳稳地放在内存里
    json tools = json::array({
        {
            {"type", "function"},
            {"function", {
                {"name", "SearchDataBase"},
                {"description", "你是高级数据库管理员。你可以执行任何合法的 MySQL 查询语句。如果你不知道当前数据库中有哪些表，请必须先生成并执行 'SHOW TABLES;' 进行探索；如果你不知道某张表的结构，请先生成并执行 'DESC 表名;'。只有在你完全清楚表结构后，再去执行最终的业务查询！绝对不要瞎猜表名和字段！"},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"sql", {
                            {"type", "string"},
                            {"description", "要执行的真实 MySQL 查询语句，例如: select count(*) from user"}
                        }}
                    }},
                    {"required", json::array({"sql"})}
                }}
            }}
        },
        {
            {"type", "function"},
            {"function", {
                {"name", "ExecutePython"},
                {"description", "用于在本地物理机沙盒中执行 Python 3 代码。当用户需要进行复杂的数学计算、算法推演时，必须调用此工具。"},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"code", {
                            {"type", "string"},
                            {"description", "要执行的真实 Python 脚本代码，例如: import math\nprint(math.factorial(10))"}
                        }}
                    }},
                    {"required", json::array({"code"})}
                }}
            }}
        }
    }); 

    std::cout << "==================================================\n";
    std::cout << "  AI Agent 网关已启动！(输入 'exit' 退出系统)\n";
    std::cout << "==================================================\n";

    // ==========================================
    // 2. 核心主循环：等待人类指令
    // ==========================================
    while (true) {
        std::string user_input;
        std::cout << "\n[Lele 的终端] > ";
        std::getline(std::cin, user_input); // 读取一整行，哪怕中间有空格

        // 退出机制
        if (user_input == "exit" || user_input == "quit") {
            std::cout << "网关下线，Bye！\n";
            break;
        }
        // 防误触机制（按了回车什么都没输）
        if (user_input.empty()) {
            continue;
        }

        // // 3. 动态组装发包载荷：把你的话塞给大模型！
        // json request_body = {
        //     {"model", "deepseek-chat"},
        //     {"messages", json::array({
        //         {{"role", "user"}, {"content", user_input}} // <--- 这里不再是死代码，而是变量
        //     })},
        //     {"tools", tools}, 
        //     {"tool_choice", "auto"} 
        // };

        // std::cout << "  [网关路由中...] 正在请求 DeepSeek 大脑...\n";

        // // 4. 发起 HTTP 请求
        // auto res = cli.Post("/chat/completions", headers, request_body.dump(), "application/json");
        
        // if (res && res->status == 200) {
        //     json response_json = json::parse(res->body);
        //     auto message = response_json["choices"][0]["message"];
            
        //     if (message.contains("tool_calls")) {
        //         auto tool_call = message["tool_calls"][0];
        //         std::string function_name = tool_call["function"]["name"];
        //         std::string arguments = tool_call["function"]["arguments"];

        //         std::cout << "\n   [意图识别成功] 触发底层 RPC 调度！\n";
        //         std::cout << "  ----------------------------------------\n";
        //         std::cout << "  【目标 RPC 工具】: " << function_name << "\n";
        //         std::cout << "  【动态生成参数】: " << arguments << "\n";
        //         std::cout << "  ----------------------------------------\n";

            json messages_array=json::array({
            {
            {"role", "user"}, 
            {"content", user_input}
            }
        });

            // 开启 ReAct 思考执行循环（直到它不再调用工具为止）
           while (true) {
            json request_body = {
                {"model", "deepseek-chat"},
                {"messages", messages_array}, // 每次循环，发过去的记忆都会变厚！
                {"tools", tools},
                {"tool_choice", "auto"}
            };

            std::cout << "  [网关状态机] 正在请求大模型思考下一阶段动作...\n";
            auto res = cli.Post("/chat/completions", headers, request_body.dump(), "application/json");
            
            if (res && res->status == 200) {
                json response_json = json::parse(res->body);
                auto message = response_json["choices"][0]["message"];
                
                // 【极其重要】：把大模型的动作追加到记忆里！
                messages_array.push_back(message);

                if (message.contains("tool_calls")) {
                    auto tool_call = message["tool_calls"][0];
                    std::string function_name = tool_call["function"]["name"];
                    std::string arguments = tool_call["function"]["arguments"];

                    std::cout << "\n  [大模型决定执行动作]: " << function_name << "\n";
                    std::cout << "   [参数]: " << arguments << "\n";

                    // ================= 跨界打击：执行 RPC =================
                    ai_agent::ToolCallRequest rpc_request;
                    rpc_request.set_trace_id("trace-" + std::to_string(time(nullptr)));
                    rpc_request.set_tool_name(function_name);
                    rpc_request.set_args_json(arguments);

                    ai_agent::ToolCallResponse rpc_response;
                    MprpcController controller;
                    SyncClosure* done = new SyncClosure();

                    std::cout << "  [RPC 调度中...] 正在穿透内网，将指令下发至物理节点...\n";
                    stub.Execute(&controller, &rpc_request, &rpc_response, done);
                    done->Wait();

                    std::string rpc_result_str;
                    if (!controller.Failed()) {
                        if (rpc_response.result().errcode() == 0) {
                            rpc_result_str = rpc_response.output();
                            std::cout << "  [物理执行完毕] 获取数据成功，反哺进记忆链条...\n";
                        } else {
                            rpc_result_str = "执行报错: " + rpc_response.result().errmsg();
                            std::cout << "  [物理执行异常] 物理机报错，交由大模型思考对策...\n";
                        }
                    } else {
                        rpc_result_str = "RPC网络通信崩溃: " + controller.ErrorText();
                        std::cout << "   [RPC 网络通信崩溃]: " << controller.ErrorText() << "\n";
                    }
                    delete done;

                    // 【核心归环】：把物理机的战报追加到记忆里！
                    // 然后什么都不用写，while 循环会自动进入下一轮，把新记忆发给大模型！
                    messages_array.push_back({
                        {"role", "tool"},
                        {"tool_call_id", tool_call["id"]},
                        {"content", rpc_result_str}
                    });

                } else {
                    // 大模型没有 tool_calls，说明它思考完了，给出了最终总结！
                    std::cout << "\n==================================================\n";
                    std::cout << " [DeepSeek 终极总结]: \n" << message["content"] << "\n";
                    std::cout << "==================================================\n";
                    break; // 打破 ReAct 内部思考循环，去等待人类的下一个问题！
                }
            } else {
                std::cout << "   [HTTP 请求失败] 状态码: " << (res ? std::to_string(res->status) : "网络断开") << std::endl;
                break;
            }
        } // 结束 ReAct 内部 while 循环

    } // 结束最外层的等待人类输入的 while(true) 循环

          
    return 0;
}