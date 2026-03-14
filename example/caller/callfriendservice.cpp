#include <iostream>
#include "mprpcapplication.h"
#include "friend.pb.h"
#include "mprpcchannel.h"
#include <semaphore.h>
#include"threadpool.h"
// 为 FriendService 定义的上下文结构体
struct FriendContext {
    sem_t *sem;
    fixbug::GetFriendListResponce *response;
    MprpcController *controller;
};

// 异步回调函数
void OnGetFriendListResponse(FriendContext *ctx)
{
    if (!ctx->controller->Failed()) {
        std::cout << "rpc GetFriendList success" << std::endl;
    } else {
        std::cout << "rpc GetFriendList failed: " << ctx->controller->ErrorText() << std::endl;
    }
    sem_post(ctx->sem); // 唤醒主线程
}

int main(int argc, char **argv)
{
    // 1. 初始化框架
    MprpcApplication::Init(argc, argv);

    // 2. 准备异步执行环境
    ThreadPool pool(4);
    // 传入线程池地址，修复编译错误
    fixbug::FriendServiceRpc_Stub stub(new MprpcChannel(&pool));

    // 3. 准备参数（改用堆内存，确保异步调用时数据有效）
    auto request = new fixbug::GetFriendListRequest();
    request->set_userid(1000);

    auto response = new fixbug::GetFriendListResponce();
    auto controller = new MprpcController();

    sem_t sem;
    sem_init(&sem, 0, 0);

    // 4. 打包上下文并创建回调
    FriendContext *ctx = new FriendContext{&sem, response, controller};
    google::protobuf::Closure* done = google::protobuf::NewCallback(OnGetFriendListResponse, ctx);

    // 5. 发起异步 RPC 调用
    stub.GetFriendList(controller, request, response, done);

    // 6. 等待结果
    sem_wait(&sem);

    // 7. 处理返回结果
    if (!controller->Failed()) {
        if (0 == response->result().errcode()) {
            std::cout << "rpc GetFriendList response success!" << std::endl;
            int size = response->friends_size();
            for (int i = 0; i < size; ++i) {
                std::cout << "index:" << (i + 1) << " name:" << response->friends(i) << std::endl;
            }
        } else {
            std::cout << "rpc GetFriendList response error:" << response->result().errmsg() << std::endl;
        }
    }

    // 8. 释放资源
    delete request;
    delete response;
    delete controller;
    delete ctx;
    sem_destroy(&sem);

    std::cout << "FriendService client task finished." << std::endl;
    return 0;
}