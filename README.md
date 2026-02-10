**🚀 项目简介**
本项目是一个基于 C++ 开发的轻量级分布式网络通信框架（RPC）。通过将复杂的网络传输、数据序列化及服务治理封装在框架层，使开发者能够像调用本地函数一样调用远程服务器上的方法。

项目核心基于**Muduo 网络库** 实现高并发通信，利用 **Protobuf** 进行数据的高效序列化，并集成**Zookeeper**实现服务的自动注册与发现 。
+3

🛠️ 核心架构与技术细节
1. 并发模型与网络 I/O

高性能网络底座：底层集成**Muduo**库，采用**Multi-Reactor**并发模型。**主Reactor**负责处理连接，**子Reactor**负责处理已建立连接的I/O事件 。
+1


业务 I/O 分离：框架内部集成**自定义ThreadPool**。在OnMessage接收到请求后，将其封装为任务投递至线程池，避免耗时的业务逻辑阻塞EventLoop线程，大幅提升吞吐量 。
+1

2. 服务治理（Zookeeper）

自动注册与发现：服务提供方（Provider）在启动时通过**ZkClient**将服务名及主机地址发布到 Zookeeper 节点 。
+1

临时节点机制：利用**Zookeeper**的临时节点(ZOO_EPHEMERAL），实现服务宕机自动下线，保证服务列表的实时有效性 。


同步连接优化：使用 **信号量 (sem_t)** 将**Zookeeper**的异步连接过程同步化，确保在连接真正建立后再进行后续的节点创建操作 。

3. 通信协议设计
为了解决**TCP的粘包和半包问题**，自定义了“三段式”协议报文：

**[Header Size (4B)] + [RpcHeader (Protobuf)] + [Args (Protobuf)]**


自描述头：**RpcHeader** 包含 **service_name**、**method_name** 及 **args_size**，实现了协议的自描述 。
+1


序列化：使用**Protobuf**序列化技术，相比**JSON/XML**具有更小的体积和更快的编解码速度 。
+1

4. 异步日志系统

生产者-消费者模型：设计单例模式的 Logger，业务线程通过宏 LOG_INFO 快速将日志推入 LockQueue 缓冲区 。
+2


守护线程写入：专门开启一个**后台写日志线程**，从队列中读取数据并持久化到磁盘，最大限度降低日志记录对业务响应延迟的影响 。
+1

📁 项目结构
Plaintext
.
├── bin/                 # 生成的可执行文件
├── lib/                 # 生成的静态库文件 (libmprpc.a)
├── example/             # 框架使用示例 (Provider/Consumer)
├── src/
│   ├── include/         # 头文件 (.h)
│   │   ├── rpcprovider.h    # 服务分发器
│   │   ├── mprpcchannel.h   # 客户端通道
│   │   ├── lockqueue.h      # 同步队列
│   │   └── ...
│   ├── rpcprovider.cpp  # 网络分发逻辑实现
│   ├── mprpcchannel.cpp # 序列化与路由逻辑实现
│   ├── zookeeperutil.cpp# Zookeeper 接口封装
│   └── ...
└── CMakeLists.txt       # 项目构建脚本
🔨 快速开始
环境依赖
Linux 环境 (Ubuntu/CentOS)

CMake (3.0 及以上版本) 

Muduo 网络库

Protobuf 序列化库 

Zookeeper C API 库 

编译构建·
Bash
git clone https://github.com/your-username/mprpc.git
cd mprpc
sh build.sh # 或者手动使用 cmake .. && make
运行示例
启动 Zookeeper 服务：zkServer.sh start


启动 Provider：./bin/provider -i config.conf 


启动 Consumer：./bin/consumer -i config.conf 


