#include "mprpcchannel.h"
#include <string>
#include "rpcheader.pb.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <error.h>
#include <mprpcapplication.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include "mprpccontroller.h"
#include "zookeeperutil.h"
#include <memory>
/*

header_size  +   service name method_name args_size   +  args

*/
// 所有通过stub代理对象调用的rpc方法  都走到这里了  统一做rpc方法调用的数据序列化和网络发送
void MprpcChannel ::CallMethod(const google::protobuf::MethodDescriptor *method,
                               google::protobuf::RpcController *controller, const google::protobuf::Message *request,
                               google::protobuf::Message *response, google::protobuf::Closure *done)
{

    // 获取智能指针，保住当前 Channel 的命
    auto self = shared_from_this();
    const google::protobuf::ServiceDescriptor *sd = method->service();
    std::string service_name = sd->name();    // service_name
    std::string method_name = method->name(); // method_name

    // 获取参数的序列化字符串长度 args_size
    uint32_t args_size = 0;
    std::string args_str;
    if (request->SerializePartialToString(&args_str))
    {
        args_size = args_str.size();
    }
    else
    {
        controller->SetFailed("serialize request error!");
        if (done)
            done->Run();
        return;
    }

    // 定义rpc的请求header
    mprpc::RpcHeader rpcHeader;
    rpcHeader.set_service_name(service_name);
    rpcHeader.set_method_name(method_name);
    rpcHeader.set_args_size(args_size);

    uint32_t header_size = 0;
    std::string rpc_header_str;
    if (rpcHeader.SerializePartialToString(&rpc_header_str))
    {
        header_size = rpc_header_str.size();
    }
    else
    {
        controller->SetFailed("serialize rpc header error!");
        if (done)
            done->Run();
        return;
    }

    // 组织待发送的rpc请求的字符串
    std::string send_rpc_str;
    uint32_t header_size_net = htonl(header_size);                    // 转换为网络字节序
    send_rpc_str.insert(0, std::string((char *)&header_size_net, 4)); // header_size
    send_rpc_str += rpc_header_str;                                   // rpcheader
    send_rpc_str += args_str;                                         // args
                                                                      // 打印调试信息
    std::cout << "======================================" << std::endl;
    std::cout << "header_size:" << header_size << std::endl;
    std::cout << "rpc_header_str:" << rpc_header_str << std::endl;
    std::cout << "service_name:" << service_name << std::endl;
    std::cout << "method_name:" << method_name << std::endl;
    std::cout << "args_str:" << args_str << std::endl;
    std::cout << "======================================" << std::endl;

    threadpool->addTask([self, send_rpc_str, service_name, method_name, controller, response, done, args_str]()
                        {
                            // std::string ip=MprpcApplication::GetInstance().GetConfig().Load("rpcserverip");
                            // uint16_t port=atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserverport").c_str());
                            // ZkClient zkCli;
                            // zkCli.Start();
                            // // /UserServiceRpc/Login
                            // std::string method_path = "/" + service_name + "/" + method_name;
                            // std::string host_data = zkCli.GetData(method_path.c_str());
                            // if (host_data == "")
                            // {
                            //     controller->SetFailed(method_path + "is not exist!");
                            //     if (done)
                            //         done->Run();
                            //     return;
                            // }
                            // int idx = host_data.find(":");
                            // if (idx == -1)
                            // {
                            //     controller->SetFailed(method_path + "address is invalid!");
                            //     if (done)
                            //         done->Run();
                            //     return;
                            // }
                            // std::string ip = host_data.substr(0, idx);
                            // uint16_t port = atoi(host_data.substr(idx + 1, host_data.size() - idx).c_str());

                            // =========================================================
                            // 【升级版】：高性能路由寻址 (带本地缓存)
                            // =========================================================
                            // =========================================================
                            //  【终极进化】：一致性哈希负载均衡路由 (Consistent Hashing)
                            // =========================================================
// =========================================================
                            // ⚖️ 【终极进化】：一致性哈希负载均衡路由 (动态服务发现)
                            // =========================================================
                            std::string method_path = "/" + service_name + "/" + method_name;
                            std::string host_data = "";

                            // 静态的哈希环实例
                            static ConsistentHash chash_ring(100); 
                            static bool is_ring_initialized = false;
                            static std::mutex ring_init_mutex;

                            // 🌟 动态服务发现：向 Zookeeper 索要所有活着的物理机
                            if (!is_ring_initialized)
                            {
                                std::lock_guard<std::mutex> lock(ring_init_mutex);
                                if (!is_ring_initialized)
                                {
                                    ZkClient zkCli;
                                    zkCli.Start();
                                    
                                    // 大招：拿到 /AgentServiceRpc/Execute 下面的所有子节点（即 IP 列表）
                                    // 注意：前提是你已经在 zookeeperutil.h/.cpp 里加上了 GetChildren 的实现！
                                    std::vector<std::string> live_nodes = zkCli.GetChildren(method_path.c_str());

                                    if (live_nodes.empty()) {
                                        controller->SetFailed(method_path + " 没有任何存活的物理节点！");
                                        if (done) done->Run();
                                        return;
                                    }

                                    // 把拿到的所有真实 IP，全部装进一致性哈希环！
                                    std::cout << "  🌐 [服务发现] 从 ZK 获取到 " << live_nodes.size() << " 个存活节点，正在挂载至哈希环...\n";
                                    for (const auto& node_ip : live_nodes) {
                                        chash_ring.AddNode(node_ip);
                                        std::cout << "     -> 挂载节点: " << node_ip << "\n";
                                    }
                                    
                                    is_ring_initialized = true;
                                }
                            }

                            // 核心寻址：拿着大模型的 SQL 请求 (args_str) 去寻址！
                            host_data = chash_ring.GetNode(args_str);

                            if (host_data.empty())
                            {
                                controller->SetFailed("Consistent Hash Ring is empty! No available nodes.");
                                if (done) done->Run();
                                return;
                            }

                            std::cout << "  [一致性哈希] 路由至物理节点: " << host_data << "\n";

                            // --- 拿到合法的 host_data 后，继续解析 IP 和 Port ---
                            int idx = host_data.find(":");
                            if (idx == -1)
                            {
                                controller->SetFailed(method_path + " address is invalid!");
                                if (done) done->Run();
                                return;
                            }
                            // ... 后面连接池的逻辑不用动 ...
                            std::string ip = host_data.substr(0, idx);
                            uint16_t port = atoi(host_data.substr(idx + 1, host_data.size() - idx).c_str());
                            std::string ip_port = ip + ":" + std::to_string(port);

                            // int clientfd = -1;
                            // // 1. 【借连接】：去池子里找找看，有没有别人用完的 socket？
                            // {
                            //     std::lock_guard<std::mutex> lock(self->conn_mutex_);
                            //     auto it = self->conn_pool_.find(ip_port);
                            //     if (it != self->conn_pool_.end() && !it->second.empty())
                            //     {
                            //         clientfd = it->second.front(); // 拿到队头的 socket
                            //         it->second.pop();              // 把它从空闲队列里踢出去
                            //         std::cout << " [连接池命中] 成功复用 TCP 长连接 (fd: " << clientfd << ")\n";
                            //     }
                            // }

                            // if (clientfd == -1)
                            // {
                            //     // 使用tcp编程  完成rpc方法的远程调用
                            //     clientfd = socket(AF_INET, SOCK_STREAM, 0);
                            //     if (-1 == clientfd)
                            //     {
                            //         char errtxt[512] = {0};
                            //         sprintf(errtxt, "create socket error! errno:%d", errno);
                            //         controller->SetFailed(errtxt);
                            //         // close(clientfd);
                            //         // exit(EXIT_FAILURE);
                            //         if (done)
                            //             done->Run(); // 通知调用方失败
                            //         return;
                            //     }

                            //     struct sockaddr_in server_addr;
                            //     server_addr.sin_family = AF_INET;
                            //     server_addr.sin_port = htons(port);
                            //     server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

                            //     if (-1 == connect(clientfd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
                            //     {
                            //         char errtxt[512] = {0};
                            //         sprintf(errtxt, "create  error! errno:%d", errno);
                            //         controller->SetFailed(errtxt);
                            //         close(clientfd);
                            //         // exit(EXIT_FAILURE);
                            //         if (done)
                            //             done->Run(); // 通知调用方失败
                            //         return;
                            //     }
                            // }

                            // // 发送rpc请求
                            // if (-1 == send(clientfd, send_rpc_str.c_str(), send_rpc_str.size(), 0))
                            // {
                            //     char errtxt[512] = {0};
                            //     sprintf(errtxt, "send socket error! errno:%d", errno);
                            //     controller->SetFailed(errtxt);
                            //     close(clientfd);
                            //     if (done)
                            //         done->Run(); // 通知调用方失败
                            //     return;
                            // }

                            // // //接收rpc请求的响应值
                            // // char recv_buf[1024]={0};
                            // // int recv_size=0;
                            // // if(-1==(recv_size=recv(clientfd,recv_buf,1024,0)))
                            // // {
                            // //     char errtxt[512]={0};
                            // //     sprintf(errtxt,"recv  error! errno:%d",errno);
                            // //     controller->SetFailed(errtxt);
                            // //     close(clientfd);
                            // //     if(done)done->Run();//通知调用方失败
                            // //     return;
                            // // }

                            // // //反序列化rpc调用的响应数据
                            // // std::string response_str(recv_buf,0,recv_size);//bug出现问题 recv_buf中遇到\0后面的数据就存不下来了  导致反序列化失败
                            // // //if(!response->ParseFromString(response_str))
                            // // if(!response->ParseFromArray(recv_buf,recv_size))
                            // // {
                            // //     char errtxt[512]={0};
                            // //     sprintf(errtxt,"parse error! response_str:%s",recv_buf);
                            // //     controller->SetFailed(errtxt);
                            // //     close(clientfd);
                            // //     if(done)done->Run();//通知调用方失败
                            // //     return;
                            // // }

                            // // std::string response_str;  // 用srd::string作为动态增长的无上限蓄水池
                            // // char recv_buf[4096] = {0}; // 每次缓冲区扩容到4kb
                            // // int recv_size = 0;

                            // // // 只要服务器端没发完并且还没断开(recv>0)，就一直读取
                            // // while ((recv_size = recv(clientfd, recv_buf, sizeof(recv_buf), 0)) > 0)
                            // // {
                            // //     response_str.append(recv_buf, recv_size); // 拼接真实的字节流
                            // // }
                            // // if (recv_size == -1 && response_str.empty())
                            // // {
                            // //     char errtxt[512] = {0};
                            // //     sprintf(errtxt, "recv error! errno:%d", errno);
                            // //     controller->SetFailed(errtxt);
                            // //     close(clientfd);
                            // //     if (done)
                            // //         done->Run();
                            // //     return;
                            // // }

                            // // 第一步 精确读取4个字节的报头长度
                            // uint32_t body_size_net = 0;
                            // // MSG_WAITALL 保证不读满 4 个字节绝对不返回
                            // int n = recv(clientfd, &body_size_net, 4, MSG_WAITALL);
                            // if (n != 4)
                            // {
                            //     char errtxt[512] = {0};
                            //     sprintf(errtxt, "recv header error or connection closed! errno:%d", errno);
                            //     controller->SetFailed(errtxt);
                            //     close(clientfd); // 电话真断了，扔掉旧手机
                            //     if (done)
                            //         done->Run();
                            //     return;
                            // }
                            // // 转换为主机字节序  得到真正的数据长度
                            // uint32_t body_size = ntohl(body_size_net);
                            // std::cout << "  [协议解析] 成功读取包头，准备接收包体大小: " << body_size << " 字节\n";
                            // // 第二步 根据计算的长度 ，精确读取胞体数据
                            // std::string response_str;
                            // response_str.resize(body_size); // 极其高效：提前在内存里开辟好确切的空间

                            // int total_read = 0;
                            // // 只要还没读够body_size 就一直读 但绝对不多读半个字节
                            // while (total_read < body_size)
                            // {
                            //     n = recv(clientfd, &response_str[total_read], body_size - total_read, 0);
                            //     if (n <= 0)
                            //     {
                            //         controller->SetFailed("recv body error!");
                            //         close(clientfd); // 出现异常，废弃此连接
                            //         if (done)
                            //             done->Run();
                            //         return;
                            //     }
                            //     total_read += n;
                            // }

                            // // 反序列化  这次装的是100%完整的数据
                            // // 拿着装满数据的 response_str 直接反序列化，绝对不会再有 parse error！
                            // if (!response->ParseFromString(response_str))
                            // {
                            //     char errtxt[512] = {0};
                            //     sprintf(errtxt, "parse error! 接收到的数据总长度:%zu", response_str.size());
                            //     controller->SetFailed(errtxt);
                            //     close(clientfd);
                            //     if (done)
                            //         done->Run();
                            //     return;
                            // }

                            // // close(clientfd);
                            // // if (done != nullptr)
                            // // {
                            // //     done->Run();
                            // // }

                            // {
                            //     std::lock_guard<std::mutex> lock(self->conn_mutex_);
                            //     self->conn_pool_[ip_port].push(clientfd);
                            //     std::cout << "   [归还连接] RPC 调用完毕，TCP 连接已放回连接池。\n";
                            // }
                            // // 唤醒主线程
                            // if (done != nullptr)
                            // {
                            //     done->Run();
                            // }

                            // =========================================================
                            // 🛡️ 工业级容错：3 次循环重试机制 (Retry & Failover)
                            // =========================================================
                            int max_retries = 3;
                            bool call_success = false; // 标记是否最终成功

                            for (int retry = 0; retry < max_retries; ++retry)
                            {
                                int clientfd = -1;

                                if (retry > 0)
                                {
                                    std::cout << "  ⚠️ [RPC 重试] 第 " << retry << " 次重新尝试连接节点 " << ip_port << "...\n";
                                }

                                // 1. 【借连接】：去池子里找找看
                                {
                                    std::lock_guard<std::mutex> lock(self->conn_mutex_);
                                    auto it = self->conn_pool_.find(ip_port);
                                    if (it != self->conn_pool_.end() && !it->second.empty())
                                    {
                                        clientfd = it->second.front();
                                        it->second.pop();
                                        std::cout << "  ♻️ [连接池命中] 成功复用 TCP 长连接 (fd: " << clientfd << ")\n";
                                    }
                                }

                                // 2. 【建连接】：如果池子里没有，新建一个
                                if (clientfd == -1)
                                {
                                    clientfd = socket(AF_INET, SOCK_STREAM, 0);
                                    if (-1 == clientfd)
                                        continue; // 创建失败？重试！

                                    struct sockaddr_in server_addr;
                                    server_addr.sin_family = AF_INET;
                                    server_addr.sin_port = htons(port);
                                    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

                                    if (-1 == connect(clientfd, (struct sockaddr *)&server_addr, sizeof(server_addr)))
                                    {
                                        close(clientfd); // 连不上？销毁废弃句柄
                                        continue;        // 重试！
                                    }
                                }

                                // 3. 【发数据】
                                if (-1 == send(clientfd, send_rpc_str.c_str(), send_rpc_str.size(), 0))
                                {
                                    close(clientfd); // 发送失败？这根线断了，销毁！
                                    continue;        // 重试！
                                }

                                // 4. 【精准收数据 - 读包头】
                                uint32_t body_size_net = 0;
                                int n = recv(clientfd, &body_size_net, 4, MSG_WAITALL);
                                if (n != 4)
                                {
                                    close(clientfd); // 没读满4字节？对面可能挂断了，销毁！
                                    continue;        // 重试！
                                }

                                uint32_t body_size = ntohl(body_size_net);
                                std::cout << "  [协议解析] 成功读取包头，准备接收包体大小: " << body_size << " 字节\n";

                                // 5. 【精准收数据 - 读包体】
                                std::string response_str;
                                response_str.resize(body_size);
                                int total_read = 0;
                                bool recv_body_success = true;

                                while (total_read < body_size)
                                {
                                    n = recv(clientfd, &response_str[total_read], body_size - total_read, 0);
                                    if (n <= 0)
                                    {
                                        recv_body_success = false;
                                        break; // 读包体一半失败了，跳出 while 循环
                                    }
                                    total_read += n;
                                }

                                if (!recv_body_success)
                                {
                                    close(clientfd); // 接收失败，销毁！
                                    continue;        // 重试！
                                }

                                // 6. 【反序列化】
                                if (!response->ParseFromString(response_str))
                                {
                                    close(clientfd); // 数据脏了，销毁！
                                    continue;        // 重试！
                                }

                                // ==========================================================
                                // 🌟 能活着走到这里的，说明 100% 成功了！
                                // ==========================================================
                                {
                                    std::lock_guard<std::mutex> lock(self->conn_mutex_);
                                    self->conn_pool_[ip_port].push(clientfd);
                                    std::cout << "  [归还连接] RPC 调用完毕，TCP 连接已放回连接池。\n";
                                }

                                call_success = true; // 打上成功标记
                                break;               // 极其关键：一旦成功，立刻跳出 for 重试循环！
                            }

                            // --- for 循环结束，进行最终结算 ---

                            // 如果 3 次全部失败，才真正向外层报错
                            if (!call_success)
                            {
                                controller->SetFailed("RPC call failed after " + std::to_string(max_retries) + " retries! Target: " + ip_port);
                            }

                            // 无论最终是成功还是彻底失败，统一下发闭包，唤醒主线程！
                            if (done != nullptr)
                            {
                                done->Run();
                            } });
}