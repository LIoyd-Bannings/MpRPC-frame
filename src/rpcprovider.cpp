#include "rpcprovider.h"
#include "mprpcapplication.h"
#include "rpcheader.pb.h"
#include "logger.h"
#include "zookeeperutil.h"
#include "mprpcclosure.h"
#include <arpa/inet.h>
/*
service_name ==>service描述
                        =》service* 记录服务对象
                            method_name=》method方法
*/
// 这里是框架提供给外部使用的。可以发布rpc方法的函数接口
void RpcProvider::NotifyService(google::protobuf::Service *service)
{

    ServiceInfo service_info;

    // 获取了服务对象的描述信息
    const google::protobuf::ServiceDescriptor *pserviceDesc = service->GetDescriptor();
    // 获取服务的名字
    std::string service_name = pserviceDesc->name();
    // 获取服务对象service的方法的数量
    int methodCnt = pserviceDesc->method_count();

    // std::cout<<"service_name:"<<service_name<<std::endl;
    LOG_INFO("service_name:%s", service_name.c_str());
    for (int i = 0; i < methodCnt; ++i)
    {
        // 获取了服务对象指定下标的服务方法的描述(抽象描述)
        const google::protobuf::MethodDescriptor *pmethodDesc = pserviceDesc->method(i);
        std::string method_name = pmethodDesc->name();
        service_info.m_methodMap.insert({method_name, pmethodDesc});

        LOG_INFO("method_name:%s", method_name.c_str());
    }
    service_info.m_service = service;
    m_serviceMap.insert({service_name, service_info});
}

// 启动rpc服务节点 开始提供rpc远程调用服务
void RpcProvider::Run()
{

    // std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    // uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());

    // muduo::net::InetAddress address(ip, port);

    // // 创建TcpServer对象
    // muduo::net::TcpServer server(&m_eventLopp, address, "RpcProvider");
    // // 绑定连接回调和消息读写回调方法 分离了网络代码和业务代码
    // server.setConnectionCallback(std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
    // server.setMessageCallback(std::bind(&RpcProvider::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    // // 设置muduo库的线程数量
    // server.setThreadNum(4);

    // //把当前的rpc节点要发布的服务全部注册到zk上面  让rpc client可以在zk上发现服务
    // //session timeout 30s zkclient 网络IO线程 1/3*timeout时间发送ping消息
    // ZkClient zkCli;
    // zkCli.Start();
    // //service_name为永久性节点  method_name为临时节点
    // for(auto &sp:m_serviceMap)
    // {
    //     // service_name /UserServiceRpc
    //     std::string service_path="/"+sp.first;
    //      zkCli.Create(service_path.c_str(),nullptr,0);
    //     for(auto &mp:sp.second.m_methodMap)
    //     {
    //             // 1. 把 method 变成持久性节点（相当于创建了一个文件夹）
    //             std::string method_path = service_path + "/" + mp.first;
    //             zkCli.Create(method_path.c_str(), nullptr, 0, 0); // 0 代表 ZOO_PERSISTENT (永久节点)

    //             // 2. 组装真正的物理机 IP 字符串
    //             char ip_port[128] = {0};
    //             sprintf(ip_port, "%s:%d", ip.c_str(), port);

    //             // 3. 把 IP 作为临时节点，挂在 method 文件夹下面！
    //             std::string node_path = method_path + "/" + ip_port;

    //             // 这里才使用 ZOO_EPHEMERAL，机器一断电，这个 IP 节点自动消失！
    //             zkCli.Create(node_path.c_str(), ip_port, strlen(ip_port), ZOO_EPHEMERAL);

    //             std::cout << "[服务注册] 成功挂载节点: " << node_path << "\n";
    //     }
    // }

    // m_threadpool.reset(new ThreadPool(4));

    // std::cout << "RpcProvider start service at ip:" << ip << " port:" << port << std::endl;
    // server.start();
    // m_eventLopp.loop();

    std::string ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
    uint16_t port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());

    // 1. ZK 注册逻辑 (完全保留你的心血)
    ZkClient zkCli;
    zkCli.Start();
    for (auto &sp : m_serviceMap)
    {
        std::string service_path = "/" + sp.first;
        zkCli.Create(service_path.c_str(), nullptr, 0);
        for (auto &mp : sp.second.m_methodMap)
        {
            std::string method_path = service_path + "/" + mp.first;
            zkCli.Create(method_path.c_str(), nullptr, 0, 0);
            char ip_port[128] = {0};
            sprintf(ip_port, "%s:%d", ip.c_str(), port);
            std::string node_path = method_path + "/" + ip_port;
            zkCli.Create(node_path.c_str(), ip_port, strlen(ip_port), ZOO_EPHEMERAL);
            std::cout << "[服务注册] 成功挂载节点: " << node_path << "\n";
        }
    }

    // 2. 初始化线程池
    m_threadpool.reset(new ThreadPool(16)); // 给物理节点多点工人，抗压！

    // ==================================================================
    //  核心大换血：启动 ZMQ ROUTER 引擎
    // ==================================================================
    zmq_context_ = std::make_unique<zmq::context_t>(1);
    router_socket_ = std::make_unique<zmq::socket_t>(*zmq_context_, zmq::socket_type::router);

    std::string bind_addr = "tcp://*:" + std::to_string(port);
    router_socket_->bind(bind_addr);
    std::cout << "  RpcProvider (ZMQ ROUTER) start service at " << bind_addr << std::endl;

    // 3. 阻塞循环：永远不死，静候网关来信
    while (true)
    {
        zmq::message_t identity_msg;
        zmq::message_t req_id_msg;
        zmq::message_t payload_msg;

        // ROUTER 模式极度硬核的三帧接收：[返程信封] -> [请求ID] -> [真实数据]
        router_socket_->recv(identity_msg, zmq::recv_flags::none);
        router_socket_->recv(req_id_msg, zmq::recv_flags::none);
        router_socket_->recv(payload_msg, zmq::recv_flags::none);

        std::string identity_str = identity_msg.to_string();
        std::string req_id_str = req_id_msg.to_string();
        std::string recv_buf = payload_msg.to_string();

        // 将繁重的反序列化和业务执行抛给线程池，立刻回头接下一封信！
        m_threadpool->addTask([this, identity_str, req_id_str, recv_buf]()
                              {
            
            // --- 你的原生解析逻辑 (完美保留) ---
            uint32_t header_size = 0;
            recv_buf.copy((char *)&header_size, 4, 0);
            header_size = ntohl(header_size);
            
            std::string rpc_header_str = recv_buf.substr(4, header_size);
            mprpc::RpcHeader rpcHeader;
            std::string service_name, method_name;
            uint32_t args_size;
            
            if (rpcHeader.ParseFromString(rpc_header_str)) {
                service_name = rpcHeader.service_name();
                method_name = rpcHeader.method_name();
                args_size = rpcHeader.args_size();
            } else {
                std::cout << "rpc_header_str parse error!" << std::endl; return;
            }
            
            std::string args_str = recv_buf.substr(4 + header_size, args_size);

            // 查找服务
            auto it = m_serviceMap.find(service_name);
            if (it == m_serviceMap.end()) return;
            auto mit = it->second.m_methodMap.find(method_name);
            if (mit == it->second.m_methodMap.end()) return;

            google::protobuf::Service *service = it->second.m_service;
            const google::protobuf::MethodDescriptor *method = mit->second;

            // 生成参数
            google::protobuf::Message *request = service->GetRequestPrototype(method).New();
            if (!request->ParseFromString(args_str)) return;
            google::protobuf::Message *response = service->GetResponsePrototype(method).New();

            // 🌟 最精华的一步：动态生成 Closure！
            // 把原路退回的信封 (identity) 和流水号 (req_id) 全部塞进 lambda 肚子里！
            google::protobuf::Closure *done = new MprpcClosure([this, identity_str, req_id_str, response]() {
                this->SendRpcResponse(identity_str, req_id_str, response);
            });

            // 真正去执行比如 DoSearchDataBase 这样的业务！
            service->CallMethod(method, nullptr, request, response, done); });
    }
}

void RpcProvider::SendRpcResponse(std::string identity_str, std::string req_id_str, google::protobuf::Message *response)
{
    std::string response_str;
    if (response->SerializeToString(&response_str)) 
    {
        // 锁住 Socket！因为可能有 16 个线程同时算完了大模型，同时抢着网线要发数据！
        std::lock_guard<std::mutex> lock(socket_mutex_);
        
        // ROUTER 三帧退信：[地址] -> [流水号] -> [干货数据]
        router_socket_->send(zmq::buffer(identity_str), zmq::send_flags::sndmore);
        router_socket_->send(zmq::buffer(req_id_str), zmq::send_flags::sndmore);
        
        // 极度注意：
        // 之前的 Muduo 代码里，拼接了一个 4 字节的包头。
        // 但 ZeroMQ 是消息边界安全的！而且我们的网关 HandleRpcResponse 直接用 ParseFromString 读的！
        // 所以这里千万不能再加 4 字节包头了，直接把 Protobuf 序列化结果裸发过去！
        router_socket_->send(zmq::buffer(response_str), zmq::send_flags::none);
    }   
    else
    {
        std::cout << "serialize response_str error" << std::endl;
    }
}

// void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr &conn)
// {
//     if (!conn->connected())
//     {
//         // 和rpc client的连接断开了
//         conn->shutdown();
//     }
// }

/*
在框架内部 RpcProvider 和RpcConsumer协商好之间通信用的protobuf数据类型
service_name  method_name args  定义proto的message类型  进行数据头的序列化和反序列化
                                            service_name method_name args_size
header_size(4个字节) +  header_str  +args_str

*/

// // 已建立连接用户的读写事件回调
// void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp time)
// {
//     // 网络上接收的远程rpc调用请求的字符流  Login args
//     std::string recv_buf = buffer->retrieveAllAsString();

//     // 从字符流中读取前四个字节的内容
//     uint32_t header_size = 0;
//     recv_buf.copy((char *)&header_size, 4, 0);
//     header_size = ntohl(header_size);
//     // 根据header_size读取数据头的原始字符流 反序列化数据 得到RPC请求的详细信息
//     std::string rpc_header_str = recv_buf.substr(4, header_size);
//     mprpc::RpcHeader rpcHeader;
//     std::string service_name;
//     std::string method_name;
//     uint32_t args_size;
//     if (rpcHeader.ParseFromString(rpc_header_str))
//     {
//         // 数据头反序列化成功
//         service_name = rpcHeader.service_name();
//         method_name = rpcHeader.method_name();
//         args_size = rpcHeader.args_size();
//     }
//     else
//     {
//         // 数据头反序列化失败
//         std::cout << "rpc_header_str" << rpc_header_str << "parse error!" << std::endl;
//     }
//     std::string args_str = recv_buf.substr(4 + header_size, args_size);
//     // 打印调试信息
//     std::cout << "======================================" << std::endl;
//     std::cout << "header_size:" << header_size << std::endl;
//     std::cout << "rpc_header_str:" << rpc_header_str << std::endl;
//     std::cout << "service_name:" << service_name << std::endl;
//     std::cout << "method_name:" << method_name << std::endl;
//     std::cout << "args_str:" << args_str << std::endl;
//     std::cout << "======================================" << std::endl;

//     // 获取service对象和method对象
//     auto it = m_serviceMap.find(service_name);
//     if (it == m_serviceMap.end())
//     {
//         std::cout << service_name << "is not exist!" << std::endl;
//         return;
//     }

//     auto mit = it->second.m_methodMap.find(method_name);
//     if (mit == it->second.m_methodMap.end())
//     {
//         std::cout << service_name << ":" << method_name << "is not exist!" << std::endl;
//         return;
//     }

//     google::protobuf::Service *service = it->second.m_service; // 获取service对象 new UserService

//     const google::protobuf::MethodDescriptor *method = mit->second; // 获取method对象 Login

//     // 生成rpc方法调用的请求request和响应response参数
//     google::protobuf::Message *request = service->GetRequestPrototype(method).New();
//     if (!request->ParseFromString(args_str))
//     {
//         std::cout << "request parse error! content:" << args_str << std::endl;
//         return;
//     }
//     google::protobuf::Message *response = service->GetResponsePrototype(method).New();

//     // 给下面method方法的调用 绑定一个Closure的回调函数
//     google::protobuf::Closure *done = google::protobuf::NewCallback<RpcProvider,
//                                                                     const muduo::net::TcpConnectionPtr &,
//                                                                     google::protobuf::Message *>(this,
//                                                                                                  &RpcProvider::SendRpcResponse,

//                                                                                                  conn, response);

//     // 在框架上根据远端rpc请求  调用当前rpc节点上发布的方法
//     // new UserService().Login(controller request, response ,done)
//     m_threadpool->addTask([service, method, request, response, done]
//                           { service->CallMethod(method, nullptr, request, response, done); });
// }

// // Closure的回调操作 用于序列化rpc的响应和网络发送
// void RpcProvider::SendRpcResponse(const muduo::net::TcpConnectionPtr &conn, google::protobuf::Message *response)
// {
//     std::string response_str;
//     if (response->SerializeToString(&response_str)) // response序列化
//     {
//         // // 序列化成功后  通过网络把rpc方法执行的结果发送回rpc的调用方
//         // conn->send(response_str);
//         // 1. 计算出序列化后的真实数据长度
//         uint32_t body_size = response_str.size();

//         // 2.转换为网络字节序
//         uint32_t body_size_net = htonl(body_size);

//         // 组装最终的发送数据 4字节包头 +正式数据包体
//         std::string send_str;
//         send_str.insert(0, std::string((char *)&body_size_net, 4));
//         send_str += response_str;
//         // 一次性发给网关
//         conn->send(send_str);
//     }
//     else
//     {
//         std::cout << "serialize  response_str error" << std::endl;
//     }
//     // conn->shutdown(); // 模拟http的短链接服务 由rpcprovider主动断开连接
// }