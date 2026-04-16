#include<atomic>
// 1. 【业务吞吐量】总请求计数 (Counter)
// 学习点：观察 QPS (每秒请求数)，验证 ZMQ 异步非阻塞的承压能力
 std::atomic<uint64_t> metric_total_requests{0};

// 2. 【核心高可用】LLM 熔断降级计数 (Counter)
// 学习点：当 DeepSeek 报错触发 Kimi 补位时，该值会跳变，证明你的容灾逻辑生效了 
 std::atomic<uint64_t> metric_model_fallback_total{0};

// 3. 【AI 质量指标】RAG 知识库有效命中数 (Counter)
// 学习点：对比总请求数，观察你的知识库覆盖率。如果命中数极低，说明知识库需要扩充或切片算法需优化 
 std::atomic<uint64_t> metric_rag_hit_total{0};

// 4. 【系统瓶颈指标】LLM 平均推理耗时 (Gauge)
// 学习点：大模型响应非常慢。通过这个值你可以观察网络波动或模型提供商的负载情况 
 std::atomic<uint64_t> metric_last_llm_latency_ms{0};

// 5. 【RPC 链路状态】当前在途请求数 (Gauge)
// 学习点：反映 pending_map_ 的实时大小。如果该值持续升高不回落，说明物理节点卡死或发生了内存泄漏
 std::atomic<uint64_t> metric_rpc_pending_count{0};

// 6. 【底层并发压力】线程池任务队列长度 (Gauge)
// 学习点：如果队列堆积，说明 16 个线程已经满载，你需要扩容或优化业务逻辑
std::atomic<uint64_t> metric_thread_pool_queue_size{0};
