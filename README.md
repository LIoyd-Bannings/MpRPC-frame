# 🚀 Distributed MCP AI Agent System
**基于 C++ RPC 框架与 MCP 协议的分布式智能体系统**

本项目是一个从零构建的、具备工业级特性的**分布式 AI 智能体 (AI Agent) 基础设施**。系统底层基于 C++ 自研的非阻塞 RPC 框架（集成 Muduo 网络库与 Zookeeper），顶层接入 DeepSeek 大模型，并严格实现了 **MCP (Model Context Protocol)** 标准。

通过 ReAct 自主思考循环、一致性哈希路由、动态服务发现与安全的沙盒执行环境，本系统能够让 AI 大模型穿透公网，安全、高效地调度本地物理机集群完成复杂的“查库、算法推演、绘图”等多步连环任务。

---

## ✨ 核心架构与功能特性 (Core Features)

### 🧠 1. 高级 AI 智能体网关 (AI Agent Gateway)
* **ReAct 自主思考与调度引擎**：实现了 `Thought -> Action -> Observation` 的闭环。大模型可根据复杂需求，自主决定分步调用多个物理机工具（例如：先查表结构 -> 再查数据 -> 最终丢给 Python 运行算法分析）。
* **滑动窗口安全记忆修剪 (Safe Memory Pruning)**：
  * 针对长文本对话带来的 Token 膨胀及 LLM 的 `400 Invalid Request` (上下文断层) 错误，独创了**成对修剪算法**。
  * 永远锁定 `[0] System Prompt` 与 `[1] User Query`，并在记忆深度超限时，精准剔除最旧的一对 `[Assistant ToolCall] + [Tool Result]`，实现无限续航与上下文绝对安全。
* **外部 Prompt 动态注入 (Decoupled Prompting)**：
  * 摒弃硬编码，系统提示词外置为独立文件（`dba_system_prompt.txt`）。支持启动时热加载，轻松实现 AI 角色（DBA、数据分析师）的无侵入式切换。
  * 植入严苛的工业级**思想钢印**（如强制 `LIMIT 10` 防全表扫描、强制 `matplotlib.use('Agg')` 防 Headless 物理机宕机）。
* **全链路染色日志 (Full-link Colored Observability)**：
  * 基于 ANSI 终端控制码，实现多维度日志隔离：`[GATEWAY]蓝`、`[SYSTEM]青`、`[DEEPSEEK]绿`、`[RPC_NODE]黄`、`[ERROR]红`，复杂的分布式交互流转一目了然。

### ⚡ 2. 高性能分布式 RPC 底座 (High-Performance RPC Foundation)
* **Zookeeper 动态服务发现与治理**：
  * 物理节点（Provider）启动时将自身能力以**临时节点 (`ZOO_EPHEMERAL`)** 挂载至 ZK 注册中心。
  * 网关（Gateway）启动时动态拉取存活节点，并实现**全局工具去重（Capability Deduplication）**，防止多副本部署导致向 LLM 注册冗余 Tool Schema。机器断电可实现秒级剔除。
* **一致性哈希负载均衡 (Consistent Hashing)**：
  * 构建了包含虚拟节点的哈希环。网关根据大模型下发的 JSON 参数 (`args_str`) 进行精确寻址，保证相同任务（如同一张表的查询）优先命中同一物理机，为后续本地缓存提供基础。
* **TCP 长连接池与复用 (Connection Pooling)**：
  * 彻底解决高并发下频繁“三次握手”的开销。RPC 调用完成后，Socket 句柄自动归还至当前进程的连接池 (`conn_pool_`)，下次同目标调用直接复用。
* **高可用熔断与三段式重试 (Retry & Failover)**：
  * 结合 `std::promise/future` 实现优雅的异步转同步等待。对于建连失败、发包失败、读包头包体失败等网络抖动，系统提供最大 3 次平滑重试；单次调用设有 15 秒超时熔断机制。

### 🛠️ 3. MCP 协议与安全沙盒 (MCP Protocol & Sandbox)
* **标准化 MCP 接口整合**：抛弃非标 RPC 定义，全盘采用标准的 `ListTools` 与 `CallTool` 接口，支持能力的动态发现与无缝热插拔。
* **防崩溃 Python 沙盒**：
  * 物理机采用 `popen` 执行大模型动态生成的 Python 代码。
  * **管道重定向黑科技**：使用 `2>&1` 将标准错误 (`stderr`) 强行重定向至标准输出 (`stdout`)。即使 AI 代码缺少库 (`ModuleNotFoundError`) 或语法错误，C++ 也能完美捕获并反哺给大模型，促使其进行**自我纠错 (Self-Correction)**。

---

## 📂 项目目录结构 (Directory Structure)

```text
MpRPC-frame/
├── bin/                    # 编译后的可执行文件与配置文件
│   ├── prompts/            # AI 外部提示词存放目录 (🌟 必须)
│   │   └── agent_system_prompt.txt
│   ├── test.conf           # Zookeeper 与 RPC 端口配置
│   ├── agent_gateway       # AI 网关核心程序
│   └── agent_provider      # 物理机节点 (可启动多个副本)
├── src/                    # RPC 框架底层源码
│   ├── include/            # mprpcchannel, consistent_hash, logger 等
│   └── mprpc/              # 底层网络 IO, 序列化与 Zookeeper 交互实现
├── example/                # 业务层代码
│   ├── agent.proto / mcp.proto # Protobuf 协议定义文件
│   ├── caller/             # agent_gateway.cpp (网关核心逻辑)
│   └── callee/             # agentserver.cpp (提供 DB 与 Python 工具执行)
└── CMakeLists.txt          # 工程构建配置

## 🚀 快速开始 (Build & Run)

### 环境依赖
* **OS**: Linux (Ubuntu/CentOS)
* **C++ Standard**: C++ 11/14
* **Dependencies**: `Muduo` (网络库), `Protobuf`, `Zookeeper` (C client), `MySQL-client`, `OpenSSL` (httplib HTTPS所需)

### 1. 编译项目
```bash
cd MpRPC-frame
mkdir build && cd build
cmake ..
make -j4

### 2. 准备运行环境
请确保你的 bin/prompts/ 目录下存在系统提示词文件：

```bash
mkdir -p bin/prompts
cat << 'EOF' > bin/prompts/agent_system_prompt.txt
你是一个全能的 AI 智能体助手，你可以通过调用各种外部工具来解决用户的问题。
【特殊工具安全限制（极其重要）】：
- 针对数据库：不清楚表结构先 SHOW TABLES/DESC。严禁全表扫描，所有 SELECT 语句必须加 LIMIT 10！
- 针对代码执行：这是无界面的 Linux 服务器，【严禁】调用 plt.show()！如需绘图，必须在代码顶部强制声明：
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
并最终使用 plt.savefig('xxx.png') 保存图片。
EOF


### 3. 启动 (Start Services)

*启动 Zookeeper 服务 (确保 2181 端口开放)

*启动 物理节点 (Provider)（可开多个终端启动多个副本验证一致性哈希）：
```bash
cd bin
./agent_provider -i test_8001.conf
./agent_provider -i test_8002.conf

*启动 AI 网关 (Gateway)：
```bash

cd bin
./agent_gateway -i test_8001.conf