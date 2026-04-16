#pragma once

// 引入咱们由 mcp.proto 生成的 protobuf 头文件
// 注意：如果你的 mcp.pb.h 生成在 src/ 目录下，这里可能需要写成 #include "../../src/mcp.pb.h"
#include "../../src/mcp.pb.h"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>         //  新增：用于 unique_ptr
#include "threadpool.h"   // 新增：引入线程池
// 极其高级的 C++ 动态回调类型定义：
// 任何满足 "传入 string，返回 string" 的函数，都可以作为大模型的底层工具！
using ToolCallback = std::function<std::string(const std::string&)>;

class McpServiceImpl : public mcp::McpToolService {
private:
    // 动态存储这台机器拥有的工具列表（菜单，给网关看的）
    std::vector<mcp::Tool> local_tools_;
    
    // 动态映射表：工具名字 -> 具体的 C++ 执行函数（给本机执行用的）
    std::unordered_map<std::string, ToolCallback> tool_handlers_;


    // 新增：定义舱壁隔离的两个独立线程池
    std::unique_ptr<ThreadPool> fast_pool_;
    std::unique_ptr<ThreadPool> io_pool_;

public:

    McpServiceImpl() {
        fast_pool_ = std::make_unique<ThreadPool>(12); // 主战舱：处理查 DB 等极速任务
        io_pool_ = std::make_unique<ThreadPool>(4);    // 隔离舱：处理 Python 等高危慢速任务
        std::cout << "[SYSTEM] 舱壁隔离模式启动: FastPool(12线程), IOPool(4线程)\n";
    }

    // ==============================================================
    // 🌟 核心大招：对外暴露的动态注册接口 (供 agentserver 的 main 函数调用)
    // ==============================================================
    void RegisterTool(const std::string& name, const std::string& desc, const std::string& schema, ToolCallback handler) {
        mcp::Tool t;
        t.set_name(name);
        t.set_description(desc);
        t.set_input_schema(schema);
        
        local_tools_.push_back(t);
        tool_handlers_[name] = handler; // 把真正的 C++ 函数指针存进 Hash 表
        
        std::cout << "[MCP 框架] 成功装载物理工具: " << name << "\n";
    }

    // ==============================================================
    // 1. 实现 ListTools：网关来查，把 local_tools_ 掏出来给它看
    // ==============================================================
    void ListTools(::google::protobuf::RpcController* controller,
                   const ::mcp::ListToolsRequest* request,
                   ::mcp::ListToolsResponse* response,
                   ::google::protobuf::Closure* done) override 
    {
        for (const auto& t : local_tools_) {
            mcp::Tool* tool = response->add_tools();
            *tool = t; // protobuf 对象的拷贝
        }
        
        // 这里的通信不再打印日志，避免被网关高频拉取时刷屏
        done->Run();
    }

    // ==============================================================
    // 2. 实现 CallTool：网关发来指令，路由到刚才注册的 C++ 函数
    // ==============================================================
    void CallTool(::google::protobuf::RpcController* controller,
                  const ::mcp::CallToolRequest* request,
                  ::mcp::CallToolResponse* response,
                  ::google::protobuf::Closure* done) override
    {
        std::string tool_name = request->name();
        std::string args = request->arguments();

        // 去哈希表里找，业务层注册过这个工具吗？
        auto it = tool_handlers_.find(tool_name);
        
        if (it != tool_handlers_.end()) {
            // 拿到真实的 C++ 函数闭包 (比如 DoSearchDataBase)
            auto handler = it->second;

            //  核心架构：舱壁隔离路由
            if (tool_name == "ExecutePython") {
                // [高危操作] 打入 4 线程的 IO 隔离舱
                io_pool_->addTask([handler, args, response, done]() {
                    std::string result = handler(args);
                    response->set_is_error(false);
                    response->set_content(result);
                    done->Run(); //  只有在子线程算完后，才通知底层的 ZMQ 发送回包！
                });
            } else {
                // [安全操作] 打入 12 线程的主战舱
                fast_pool_->addTask([handler, args, response, done]() {
                    std::string result = handler(args);
                    response->set_is_error(false);
                    response->set_content(result);
                    done->Run(); // 🌟 算完再发送
                });
            }
        } else {
            // 没找到？大模型幻觉了
            response->set_is_error(true);
            response->set_content("致命错误：本节点未注册工具 [" + tool_name + "]");
            done->Run(); // 这种直接报错的，当前线程立刻回传
        }
    }
};