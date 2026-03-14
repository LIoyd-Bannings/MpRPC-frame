#include <iostream>
#include <string>
#include <mutex>
#include <condition_variable>
#include "mprpcapplication.h"
#include "agent.pb.h"
#include "mprpcchannel.h"
#include "mprpccontroller.h"
#include "threadpool.h"

// 【大厂硬核操作】继承 Protobuf 的 Closure，手写一个绝对线程安全的同步等待器！
class SyncClosure : public google::protobuf::Closure {
public:
    void Run() override {
        // 当 RPC 底层真正收到服务器回包时，会触发这个函数
        std::lock_guard<std::mutex> lock(mtx_);
        is_done_ = true;
        cv_.notify_all(); // 唤醒被卡住的主线程！
    }

    void Wait() {
        // 主线程调用这个函数，死死卡住，绝对不往下走！
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this]() { return is_done_; });
    }

private:
    std::mutex mtx_;
    std::condition_variable cv_;
    bool is_done_ = false;
};

int main(int argc, char **argv)
{
    // 1. 初始化 RPC 框架
    MprpcApplication::Init(argc, argv);

    // 2. 建立通信桩
    ThreadPool pool(4);
    ai_agent::AgentServiceRpc_Stub stub(new MprpcChannel(&pool));

    // 3. 伪装成大模型，拼装 JSON 指令
    ai_agent::ToolCallRequest request;
    request.set_trace_id("trace-llm-999");
    request.set_tool_name("ExecutePython"); 
    
    // 大模型发威：计算 999 的 999 次方！
    std::string mock_llm_json = "{\"code\": \"print(999** 999)\"}";
    request.set_args_json(mock_llm_json);

    ai_agent::ToolCallResponse response;
    MprpcController controller;

    // 4. 祭出我们的同步锁神器！
    SyncClosure* done = new SyncClosure();

    std::cout << "正在向 Agent Server 发射高压计算指令，等待物理机 Python 沙盒响应..." << std::endl;
    
    // 发起异步调用，并把我们的神器塞进去
    stub.Execute(&controller, &request, &response, done);

    // 【防坠毁防线】主线程在这里死死卡住！直到 done 的 Run() 被底层唤醒！
    done->Wait();

    // 5. 此时的数据，才是真正从服务器回来的数据！
    if (!controller.Failed()) {
        std::cout << "\n========== RPC 物理执行成功! ==========" << std::endl;
        std::cout << "状态码: " << response.result().errcode() << std::endl;
        std::cout << "Agent 沙盒真实输出:\n" << response.output() << std::endl;
        std::cout << "=======================================" << std::endl;
    } else {
        std::cout << "\nRPC 调用失败: " << controller.ErrorText() << std::endl;
    }

    // 清理战场
    delete done;

    return 0;
}