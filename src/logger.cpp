#include "logger.h"
#include <thread>
#include <iostream>
#include <chrono>

// === 引入底层硬件指令以支持混合退避机制 ===
#if defined(__x86_64__) || defined(_M_X64)
    #include <immintrin.h> // 提供 _mm_pause()
#endif

// 获取日志的单例
Logger &Logger::getInstance()
{
    static Logger logger;
    return logger;
}

// =========================================================================
// 1. 构造函数：初始化无锁队列容量，并启动后台守护线程
// =========================================================================
Logger::Logger() : m_lckQue(100000) // 必须在初始化列表中指定无锁环形队列的容量（10万条）
{
    // 启动专门的后台写日志线程（消费者）
    std::thread writeLogTask([&]()
    {
        int spin_count = 0; // 记录队列为空时的连续空转次数

        for (;;)
        {
            std::string msg;

            // =============================================================
            // 2. 非阻塞 Pop 获取数据
            // =============================================================
            if (m_lckQue.Pop(msg)) 
            {
                //  拿到数据了，清零空转计数器
                spin_count = 0; 

                // 获取当天的日期，准备写入文件
                time_t now = time(nullptr);
                tm *nowtm = localtime(&now);

                char file_name[128];
                sprintf(file_name, "%d-%d-%d-log.txt", nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday);
                
                FILE *pf = fopen(file_name, "a+");
                if (pf == nullptr)
                {
                    // 【优雅降级】：千万不能用 exit(EXIT_FAILURE)！
                    // 日志文件打开失败不应该导致整个 AI 网关崩溃，跳过这次写入即可。
                    std::cerr << "[SYSTEM ERROR] logger file: " << file_name << " open error!" << std::endl;
                    continue; 
                }

                // 拼装时间前缀
                char time_buf[128] = {0};
                sprintf(time_buf, "%d:%d:%d=>[%s] ",
                        nowtm->tm_hour, 
                        nowtm->tm_min, 
                        nowtm->tm_sec,
                        (m_loglevel == INFO ? "info" : "error"));
                
                msg.insert(0, time_buf);
                msg.append("\n");

                fputs(msg.c_str(), pf);
                fclose(pf);
            }
            else 
            {
                // =============================================================
                // 3. 混合指数退避策略 (Hybrid Progressive Backoff) - 核心含金量！
                // =============================================================
                //  队列为空，如果不加限制，死循环会瞬间把当前 CPU 核心打到 100%。
                
                if (spin_count < 100) 
                {
                    // 【第一级：CPU 指令级退避】 (纳秒级延迟)
                    // 极短时间内没有数据，告诉 CPU 流水线这是个自旋锁，降低功耗和分支预测惩罚，不交出线程控制权。
                    #if defined(__x86_64__) || defined(_M_X64)
                        _mm_pause(); 
                    #endif
                } 
                else if (spin_count < 1000) 
                {
                    // 【第二级：系统调度级退避】 (微秒级延迟)
                    // 等了一阵还没数据，主动让出当前 CPU 时间片，让同核心的其他线程（比如网关业务线程）先跑。
                    std::this_thread::yield(); 
                } 
                else 
                {
                    // 【第三级：彻底休眠退避】 (毫秒级延迟)
                    // 彻底空闲（比如深夜无人访问），挂起当前线程 1 毫秒，进入真正的低功耗模式。
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                
                spin_count++; // 增加空转计数
            }
        }
    });

    // 设置分离线程，让其在后台默默运行
    writeLogTask.detach();
}

// 设置日志级别
void Logger::setLogLevel(Loglevel level)
{
    m_loglevel = level;
}

// =========================================================================
// 4. 写日志 (生产者)：极速无锁压入
// =========================================================================
void Logger::Log(std::string msg)
{
    // 无锁 Push，不再需要抢占 std::mutex，耗时从微秒级降至纳秒级。
    if (!m_lckQue.Push(msg)) 
    {
        // 【熔断保护】：如果 10 万的容量瞬间被打满（通常是因为磁盘 IO 极其卡顿），
        // 宁可丢弃日志，也绝对不能阻塞主网关处理大模型的请求！
        std::cerr << "\033[1;31m[CRITICAL WARNING] Log Queue is FULL! Dropping log.\033[0m\n";
    }
}