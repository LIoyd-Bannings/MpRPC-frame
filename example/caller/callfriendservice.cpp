#include <iostream>
#include "mprpcapplication.h"
#include "friend.pb.h"

int main(int argc, char **argv)
{
    // 整个程序启动以后 想使用mprpc框架来享受rpc服务调用 一定需要先调用框架的初始化函数(只初始化一次)
    MprpcApplication::Init(argc, argv);
    // 演示调用远程发布的rpc方法getfrendlist
    fixbug::FriendServiceRpc_Stub stub(new MprpcChannel());

    // rpc方法的请求参数
    fixbug::GetFriendListRequest request;
    request.set_userid(1000);

    // rpc方法的响应
    fixbug::GetFriendListResponce response;

    // 发起rpc方法的调用  同步的rpc调用过程  MprpcChannel::callmethod
    MprpcController controller;
    stub.GetFriendList(&controller, &request, &response, nullptr); //RpcChannel->RpcChannel::callMethod集中来做所有rpc方法调用的参数序列化和网络发送

    if (controller.Failed())
    {
        std::cout << controller.ErrorText() << std::endl;
    }
    else
    {
        // 一次rpc调用的完成  读调用的结果
        if (0 == response.result().errcode())
        {
            // 没有错误
            std::cout << "rpc Getfriendlist response" << std::endl;
            int size = response.friends_size();
            for (int i = 0; i < size; ++i)
            {
                std::cout << "index:" << (i + 1) << " name:" << response.friends(i) << std::endl;
            }
        }
        else
        {
            std::cout << "rpc Getfriendlist response error:" << response.result().errmsg() << std::endl;
        }
    }

    return 0;
}