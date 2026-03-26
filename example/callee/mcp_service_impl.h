#pragma once

// 引入咱们由 mcp.proto 生成的 protobuf 头文件
// 注意：如果你的 mcp.pb.h 生成在 src/ 目录下，这里可能需要写成 #include "../../src/mcp.pb.h"
#include "../../src/mcp.pb.h"
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

// 极其高级的 C++ 动态回调类型定义：
// 任何满足 "传入 string，返回 string" 的函数，都可以作为大模型的底层工具！
using ToolCallback = std::function<std::string(const std::string&)>;

class McpServiceImpl : public mcp::McpToolService {
private:
    // 动态存储这台机器拥有的工具列表（菜单，给网关看的）
    std::vector<mcp::Tool> local_tools_;
    
    // 动态映射表：工具名字 -> 具体的 C++ 执行函数（给本机执行用的）
    std::unordered_map<std::string, ToolCallback> tool_handlers_;

public:
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
            //  找到了！调用业务层的代码（比如 DoSearchDataBase）
            std::string result = it->second(args); 
            
            response->set_is_error(false);
            response->set_content(result);
        } else {
            // 没找到？说明大模型幻觉了，或者网关派发错了
            response->set_is_error(true);
            response->set_content("致命错误：本节点未注册工具 [" + tool_name + "]");
        }

        done->Run();
    }
};