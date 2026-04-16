# 🚀 Distributed MCP AI Agent System v2.0

**基于 ZeroMQ 异步架构、Docker沙盒与全链路监控的分布式智能体基础设施**

本项目是一个专为大语言模型（LLM）设计的分布式工具执行（Tool Calling）基础设施。在 V2.0 版本中，系统不仅全面转向基于 **ZeroMQ** 的高并发异步架构，引入了 **Docker 容器化安全沙盒 (Containerized Sandbox)**，更在并发吞吐、知识检索精确度与系统可观测性上实现了工业级的大幅跃升。

---

## ✨ V2.0 核心架构大跃进

### ⚡ 1. 极速异步 RPC 底座 & 并发调优
* **DEALER/ROUTER 全双工通信**：弃用传统 TCP 阻塞调用，底层基于 ZeroMQ 封装异步链路。网关层设计专用的 IO 多路复用 Poller 守护线程（扫地僧模式），通过 Correlation ID 精准匹配回包。
* **一致性哈希负载均衡**：集成虚拟节点技术的哈希环，确保分布式环境下的任务调度亲和性。
* **破除全局锁 (Sharded LRU Cache)**：废弃传统的单互斥锁会话记忆结构，重构为 **16 槽位分片锁 (ShardedLRUCache)** 架构。大幅降低高并发洪峰下的线程锁竞争，吞吐量成倍提升。

### 🛡️ 2. Docker 容器化安全沙盒 (Containerized Sandbox)
* **容器级强隔离**：通过 `docker run` 构建完全独立的运行时环境，实现 AI 编写的代码与宿主机系统的内核级解耦，防御恶意代码对宿主机的渗透。
* **物理断网与资源配额**：
    * **断网隔离 (`--network none`)**：物理切断容器网络，彻底封死任何反弹 Shell 或敏感数据外传的路径。
    * **硬性资源限额 (`-m 64m` / `--cpus 0.5`)**：在容器层面强行限制内存与 CPU 消耗，免疫 OOM 攻击与死循环导致的宿主机 CPU 耗尽。
* **阅后即焚与超时清理**：
    * **生命周期自动回收 (`--rm`)**：任务执行结束即刻销毁容器，不留任何持久化痕迹。
    * **双重超时保护 (`timeout 5s`)**：在宿主机与容器内双重计时，强制终结超时任务，确保物理节点始终具备极高的响应可用性。

### 🧠 3. 智能决策与 RAG 检索革命
* **工业级 RAG 流水线**：彻底摒弃单一余弦相似度，构建了 **HNSW 向量粗排 + 交叉编码器 (Reranker) 深度精排** 的双阶段检索架构。
* **Max-Gap 动态截断算法**：首创最大裂谷无参截断策略，结合 Token 预算池双重保护，动态摒弃低质量知识噪声，抑制 AI 产生幻觉。
* **A2A (Agent-to-Agent) Reviewer 自愈网络**：当物理沙盒执行崩溃（如语法错误、超时）时，网关自动唤醒独立的审计模型进行日志分析与纠错指令下发，引导 Coder 模型实现异常自愈。

### 📈 4. 工业级可观测性 (Observability)
* **全链路指标大屏**：系统内嵌轻量级 HTTP Exporter，全面打通 **Prometheus + Grafana** 监控体系。
* **黄金信号监控**：实时采集并落盘 6 大核心维度指标：
  * `Traffic`: 网关实时 QPS (`requests_total`)
  * `Latency`: 大模型 API 推理耗时基线 (`llm_latency_ms`)
  * `Saturation`: RPC 在途积压 (`rpc_pending`) 与线程池负载 (`thread_queue`)
  * `Errors/Fallback`: 主模型降级熔断触发频率 (`model_fallback_total`)
  * `Business`: 高质量 RAG 命中统计 (`rag_hit_total`)

---

## 📂 目录结构

```text
MpRPC-frame/
├── bin/                    # 编译产物、提示词配置与运行时日志
├── monitoring/             # 📈 Prometheus & Grafana 监控部署配置
├── src/                    # RPC 框架底层源码 (ZMQ封装/线程池/ZkUtil/Metrics导出)
├── example/                # 业务逻辑
│   ├── callee/             # Provider 物理节点 (DB操作/Python沙盒/MCP工具注册)
│   ├── caller/             # Gateway 异步网关 (ReAct循环/RAG流水线/自愈网络)
│   └── db/                 # RAII 智能连接池与 MySQL 驱动
└── CMakeLists.txt          # CMake 构建配置
```


## 🚀 快速开始 (Quick Start)

### 1. 环境依赖 (Prerequisites)
* **操作系统**: Linux (内核需支持 `setrlimit` 与 `Signals` 机制)
* **编译器**: 支持 C++ 11/14 标准
* **核心依赖库**: 
  * `libzmq` & `cppzmq`: 处理高性能异步通信 
  * `zookeeper_mt`: 分布式服务发现与全网工具 (MCP) 同步
  * `protobuf`: 跨平台数据序列化 
  * `httplib` & `json`: 大模型 API 交互与监控指标导出
  * `mysqlclient` & `openssl`: 数据持久化与安全传输 

### 2. 编译构建 (Build)
使用 CMake 进行开箱即用的编译构建：
```bash
# 进入工程根目录
mkdir build && cd build
cmake ..
make -j4
```

### 3. 运行部署 (Deployment)
为保证分布式组件握手成功，请按以下拓扑顺序启动集群：

* **Step 1**: 基础设施启动
确保 Zookeeper (2181) 已就绪。若需查看大屏，可在此阶段通过 Docker-compose 启动 Prometheus (9091) 与 Grafana (3000)。

* **Step 2**: 启动物理节点 (Provider)
运行计算节点，通过 -i 参数指定对应的配置文件（支持多副本验证一致性哈希负载均衡）：
```bash
./agent_provider -i test_8001.conf
```

* **Step 3**: 启动 AI 智能体网关 (Gateway)
启动高性能网关，自动向 ZK 拉取全网工具并开启 8081 监控暴露端口：
```bash
./agent_gateway -i gateway.conf
```

* **Step 4**: 启动交互式客户端 (Client)
运行客户端开始与 AI 智能体对话：
```bash
./chat_client
```