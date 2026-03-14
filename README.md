# 🚀 C++ Distributed AI Agent Gateway (分布式智能体网关)

## 📖 项目简介
本项目是一个基于 C++ 底层构建的 **分布式 AI 智能体调度网关 (AI Agent Gateway)**。

与市面上常见的基于 Python 封装调包（如 LangChain）的脚本程序不同，本项目从底层网络通信与架构设计出发，将 **大语言模型 (LLM)** 的逻辑推理能力与 **分布式微服务 (RPC)** 的物理执行能力进行了深度整合。

网关内部实现了一套纯 C++ 驱动的 **ReAct (Reason + Act) 状态机**，使大模型能够打破单纯的文本对话限制，自主规划任务、跨越网络边界进行 Zookeeper 服务寻址，并动态调度底层物理机节点上的真实资源（如 MySQL 数据库、Python 物理沙盒），实现高度自治的复杂任务闭环。

## ✨ 核心特性

* **🧠 纯 C++ 驱动的 ReAct 状态机**: 摒弃臃肿的第三方 Agent 框架，纯手写实现动态记忆链条 (`messages_array`) 与多轮对话上下文管理，支持大模型在后台进行多次“思考 -> 调用工具 -> 获取观察结果 -> 总结”的自主推理闭环。
* **🌉 异构双协议栈通信**: 北向采用 HTTPS (`cpp-httplib` + OpenSSL) 直连公网大模型 API；南向采用基于 Muduo/Epoll 的高性能自定义 RPC 协议穿透内网，实现云端大脑与本地四肢的解耦。
* **🔍 动态服务注册与发现**: 深度集成 Zookeeper，底层执行节点 (Provider) 启动时注册临时节点，网关拦截 LLM 的 Tool Call 指令后，动态寻址路由到存活的物理节点。
* **🛠️ 跨语言沙盒与数据探针**: 
    * **ExecutePython**: 底层节点基于 `popen` 管道通信机制，拉起本地解释器动态执行大模型生成的 Python 3 复杂计算代码并捕获标准输出。
    * **SearchDataBase**: 赋予大模型高级 DBA 权限，支持动态生成 `SHOW TABLES`, `DESC` 等 SQL 语句进行 Schema 探索，并安全执行业务查询 。
* **🛡️ 健壮的并发与异常容灾**: 运用 `std::condition_variable` 与 `std::unique_lock` 实现极其严谨的 `SyncClosure` 同步等待器，防止网络 I/O 阻塞导致的主线程坠毁；并具备完备的 JSON 反序列化异常捕获机制 。

## 🛠️ 技术栈 (Tech Stack)

* **开发语言**: C++11 / C++14
* **网络框架**: Muduo (基于 Epoll 的 Reactor 模型)
* [cite_start]**RPC 通信**: 自定义基于 Protobuf 的二进制序列化协议
* **服务治理**: Zookeeper (C Client)
* [cite_start]**大模型对接**: HTTP/HTTPS (cpp-httplib), nlohmann/json
* **底层组件**: MySQL C API, Linux IPC (管道通信)

## ⚙️ 编译与快速启动

### 1. 环境依赖
请确保本地 Linux 环境已安装以下组件：
* CMake (3.0+)
* MySQL Server & `libmysqlclient-dev`
* Zookeeper Server & Zookeeper C API
* Protobuf Compiler (`protoc`) & `libprotobuf-dev`
* Muduo Network Library

### 2. 编译项目
```bash
# 1. 克隆项目
git clone [https://github.com/YourUsername/MpRPC-frame.git](https://github.com/YourUsername/MpRPC-frame.git)
cd MpRPC-frame

# 2. 运行 CMake 构建
mkdir build && cd build
cmake ..
make -j4
```
###3.运行为服务器集群
```bash
# 终端 A: 确保 Zookeeper 服务已在后台运行 (默认 2181 端口)
# sudo ./zkServer.sh start

# 终端 B: 启动底层物理执行节点 (Provider)
cd bin
./agent_provider -i test.conf

# 终端 C: 启动 AI 智能体调度网关 (Gateway)
cd bin
./agent_gateway -i test.conf





