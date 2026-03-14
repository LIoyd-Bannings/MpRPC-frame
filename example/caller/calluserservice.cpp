#include<iostream>
#include"mprpcapplication.h"
#include"user.pb.h"
#include"mprpcchannel.h"
#include<semaphore.h>


struct RpcContext{
    sem_t *sem;
    fixbug::LoginResponce *responce;
    MprpcController *controller;
};


void OnloginResponse(RpcContext *ctx)
{
    if (!ctx->controller->Failed()) {
        std::cout << "rpc login success" << std::endl;
    } else {
        std::cout << "rpc login failed: " << ctx->controller->ErrorText() << std::endl;
    }
    sem_post(ctx->sem); // 唤醒主线程
}

void OnRegisterResponse(sem_t *s)
{
    sem_post(s); // 仅仅负责唤醒信号量
}

int main(int argc,char** argv)
{   
    //整个程序启动以后 想使用mprpc框架来享受rpc服务调用 一定需要先调用框架的初始化函数(只初始化一次)
    MprpcApplication::Init(argc,argv);
    
    // 2. 创建一个线程池供 RPC 频道使用
    // 建议开启 2-4 个线程处理网络 I/O 任务
    ThreadPool pool(4);
    //演示调用远程发布的rpc方法Login
    fixbug::UserServiceRpc_Stub stub(new MprpcChannel(&pool));

    //rpc方法的请求参数
    auto request=new fixbug::LoginRequest();
    request->set_name("zhang san");
    request->set_pwd("123456");
    //rpc方法的响应
    auto  response=new fixbug::LoginResponce();

   auto controller=new MprpcController();

    sem_t sem;
    sem_init(&sem,0,0);

    // 2. 将数据打包进 Context
    RpcContext *ctx = new RpcContext{&sem, response, controller};


    //定义回调函数
    google::protobuf::Closure* done=google::protobuf::NewCallback(OnloginResponse,ctx);


    //发起rpc方法的调用  同步的rpc调用过程  MprpcChannel::callmethod
    stub.Login(controller,request,response,done);//RpcChannel->RpcChannel::callMethod 集中来做所有rpc方法调用的参数序列化和网络发送
    
    sem_wait(&sem);

    //释放内存


    std::cout << "Client task finished." << std::endl;


  
    //一次rpc调用的完成  读调用的结果
    if(0==response->result().errcode())
    {
        //没有错误
        std::cout<<"rpc login response:"<<response->sucess()<<std::endl;
    }
    else
    {
        std::cout<<"rpc login response error:"<<response->result().errmsg()<<std::endl;
    }

   // --- 【修正】Register 调用 (演示) ---
    fixbug::RegisterRequest req; 
    req.set_id(2000); 
    req.set_name("mprpc"); 
    req.set_pwd("666"); 
    fixbug::RegisterResponce rsp; 
    
    // 注意：Register 也需要异步处理，否则直接读结果还是空的
    MprpcController reg_ctrl;
    sem_init(&sem, 0, 0); // 重置信号量
    auto done_reg = google::protobuf::NewCallback<sem_t*>(OnRegisterResponse, &sem);
    
    stub.Regiester(&reg_ctrl, &req, &rsp, done_reg); 
    sem_wait(&sem);

    if (0 == rsp.result().errcode()) { 
        // 修正：这里应该打印 rsp.sucess() 
        std::cout << "rpc Register response success: " << rsp.sucess() << std::endl;
    }

    // 最后统一释放堆内存 
    delete request;
    delete response;
    delete controller;
    sem_destroy(&sem);
    delete ctx;
    return 0; // 程序在这里结束
}