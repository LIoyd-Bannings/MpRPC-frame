# 🚀 Distributed MCP AI Agent System v2.0

**基于 ZeroMQ 异步架构与 Linux 内核级沙盒的分布式智能体基础设施**

本项目是一个专为大语言模型（LLM）设计的分布式工具执行（Tool Calling）基础设施。系统在 V2.0 版本中全面转向基于 **ZeroMQ** 的高并发异步架构，并引入了 **Linux 内核级资源限制（setrlimit）** 与 **AI 自愈闭环（Reviewer 机制）**。

---

## ✨ V2.0 核心技术演进

### ⚡ 1. 极速异步 RPC 底座
* **DEALER/ROUTER 通信模式**：弃用传统 TCP 阻塞调用，采用 ZeroMQ 实现全双工异步链路。
* **Poller 轮询引擎**：网关层设计专用的 IO 多路复用 Poller 线程，通过 Correlation ID 匹配回包。
* **一致性哈希负载均衡**：集成虚拟节点技术的哈希环，确保分布式环境下的任务调度亲和性。

### 🛡️ 2. Linux 内核级沙盒 (OS-level Sandbox)
* **进程级物理隔离**：利用 `fork` + `exec` 模式构建受限的执行子进程。
* **资源配额审计 (setrlimit)**：严格限制 **RLIMIT_CPU** (时间片) 与 **RLIMIT_AS** (内存)。精准拦截 **SIGXCPU** (死循环) 与 **SIGSEGV** (OOM) 信号，确保宿主机系统绝对安全。
* **标准流劫持**：通过 `dup2` 劫持子进程输出流，实现执行结果的透明监控与反哺。

### 🧠 3. 智能决策与逻辑自愈
* **RAG 相似度阈值拦截**：自研余弦相似度算法，对知识库检索进行置信度过滤。高分匹配时强行锁定 RAG 模式，抑制 AI 盲目查库。
* **Reviewer (A2A) 自愈机制**：当底层执行报错时，网关自动唤醒审计模型进行日志分析与纠错指令下发，引导 Coder 模型自动重试。
* **元数据驱动解包**：基于 MySQL C API 操控 Metadata，支持 AI 在无预设 Schema 环境下动态探索数据库。

### ⚙️ 4. 稳健工程实践
* **RAII 智能连接池**：手写单例连接池，利用智能指针结合 Lambda 自定义删除器实现句柄零感知自动回收。
* **全链路染色日志**：基于 ANSI 颜色码实现可视化链路追踪。

---

## 📂 目录结构

```text
MpRPC-frame/
├── bin/                    # 编译产物与提示词配置
├── src/                    # RPC 框架底层源码 (ZMQ封装/线程池/ZK工具)
├── example/                # 业务逻辑
│   ├── callee/             # Provider 实现 (DB/Python工具/沙盒)
│   ├── caller/             # Gateway 核心 ReAct 循环逻辑
│   └── db/                 # RAII 连接池与数据库驱动
└── CMakeLists.txt          # 构建配置
```

## 🚀 快速开始 (Quick Start)

### 1. 环境依赖 (Prerequisites)
* **操作系统**: Linux (内核需支持 `setrlimit` 资源配额机制) 
* **编译器**: C++ 11/14 标准 [cite: 201]
* **核心依赖库**: 
  * `libzmq` & `cppzmq`: 处理高性能异步通信 
  * `zookeeper_mt`: 分布式服务发现与元数据管理 
  * `protobuf`: 跨平台序列化协议 
  * `mysqlclient`: 关系型数据库底层驱动 
  * `openssl`: 提供 HTTPS 安全传输支持 (`ssl`, `crypto`) 

### 2. 构建项目 (Build)
使用 CMake 进行开箱即用的编译构建 
```bash
# 进入工程根目录
mkdir build && cd build
cmake ..
make -j4
```


### 3. 运行指南 (Deployment)
请按以下顺序顺序启动系统组件，以确保分布式链路的完整性：

* **第一步：启动 Zookeeper** 确保服务器 2181 端口可用且服务已就绪。

* **第二步：启动工具执行物理节点 (Provider)** 运行 Provider 节点，通过 `-i` 参数指定对应的配置文件：
    ```bash
    ./agent_provider -i test_8001.conf
    ```
    *(注：可启动多个 Provider 副本，验证一致性哈希负载均衡 )*

* **第三步：启动 AI 智能体网关 (Gateway)** 启动高性能异步网关，监听任务分发请求：
    ```bash
    ./agent_gateway -i gateway.conf
    ```

* **第四步：启动交互式客户端 (Client)** 运行客户端开始与 AI 智能体对话：
    ```bash
    ./chat_client 智能林弟
    ```




