# Distributed-System-Lab: 高性能 RPC 框架与 Raft 一致性协议

本项目是一个专注于分布式系统基础设施实现的实验室。目前包含了自研的 **Mprpc (C++)** 远程过程调用框架以及基于 **Go** 实现的 **MIT 6.824 Raft** 共识算法模块。

---

## 🛠 核心模块一：Mprpc 框架 (C++)

这是一个基于 Muduo 网络库和 Protobuf 序列化协议开发的高性能 RPC 框架，支持服务注册与发现。

### 1. 技术架构
- [cite_start]**网络底座**：基于 **Muduo** 库实现的非阻塞 I/O 和 Reactor 模型，能够高效处理大规模并发连接 [cite: 1097-1100, 1630]。
- [cite_start]**服务治理**：集成 **Zookeeper** 作为注册中心，利用 **临时节点 (EPHEMERAL)** 实现服务自动上线与失效剔除 [cite: 1636, 1653]。
- [cite_start]**序列化方案**：使用 **Google Protobuf** 负责数据的序列化与反序列化，通过自定义协议头解决 TCP 粘包问题 [cite: 1348, 1583-1588]。

### 2. 关键组件
- [cite_start]**异步日志系统**：采用生产者-消费者模型，结合 `LockQueue` 与后台守护线程，实现非阻塞式日志记录 [cite: 948-960, 1245]。
- [cite_start]**高并发线程池**：通过自研 `ThreadPool` 将 RPC 业务逻辑调用异步化，保护 I/O 线程不被阻塞 [cite: 1143, 1741]。
- [cite_start]**服务注册接口**：提供简洁的 `NotifyService` 接口，支持快速发布 RPC 方法 [cite: 1111, 1601]。

---

## 🚀 核心模块二：Raft 共识协议 (Go - MIT 6.824)

基于 MIT 6.824 实验实现的强一致性共识算法，确保了分布式环境下数据的一致性与高可用性。

### 1. 已实现特性
- [cite_start]**领导者选举 (Lab 2A)**：实现了基于随机超时时间的选举机制，包含心跳维持与角色平滑切换 [cite: 51-53, 565-583]。
- [cite_start]**日志同步 (Lab 2B)**：通过一致性检查确保 Leader 日志强同步至半数以上节点，并由 `applier` 异步提交至状态机 [cite: 307-311, 622-648]。
- [cite_start]**持久化恢复 (Lab 2C)**：实现了对 `CurrentTerm`、`VotedFor` 和日志条目的编码存储，支持节点宕机后的状态恢复 [cite: 102, 115-117, 125]。

### 2. 核心优化
- [cite_start]**快速回退 (Fast Rollback)**：在日志冲突时通过返回 `ConflictTerm` 和 `ConflictIndex` 快速定位，大幅降低 RPC 同步开销 [cite: 199-201, 521-542]。
- [cite_start]**高效唤醒机制**：使用 `sync.Cond` 条件变量优化日志应用逻辑，避免无效的 CPU 轮询 [cite: 85, 626-628]。

---

## 🌟 未来演进计划 (Roadmap)

项目下一步的核心目标是将上述两个模块深度融合，构建一个完整的分布式 C++ 存储系统：

1. **Raft 算法 C++ 移植**：
   - 将 Go 版本的 Raft 逻辑迁移至 C++ 环境。
   - 使用本项目中的 **Mprpc 框架** 作为 Raft 节点间的底层通信组件。
2. **分布式 KV 存储引擎**：
   - 在 C++ Raft 层之上构建键值存储服务（支持 Put/Get/Delete）。
   - 实现 Read-Index 优化以提供强一致性读能力。
3. **性能监控与调优**：
   - 集成日志分析与性能剖析工具（如 perf/gdb），针对高并发场景下的 RPC 延迟进行调优。

---

## 📦 如何运行测试

### Mprpc (C++)
```bash
# 构建项目
mkdir build && cd build
cmake ..
make

# 启动服务
./bin/provider -i config.conf
./bin/consumer -i config.conf
