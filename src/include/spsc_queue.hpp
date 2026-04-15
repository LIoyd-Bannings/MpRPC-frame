#pragma once
#include <atomic>
#include <vector>
#include <cstddef>
#include <memory>

// 单生产者单消费者 (SPSC) 无锁环形队列
template <typename T>
class SPSCQueue {
private:
    std::vector<T> buffer_;
    const size_t capacity_;

    // 核心心法：强制缓存行对齐（Cache Line Alignment）
    // 防止 Head 和 Tail 落在同一个 CPU 缓存行内，引发致命的“伪共享（False Sharing）”
    alignas(64) std::atomic<size_t> head_{0}; // 消费者独占读指针
    alignas(64) std::atomic<size_t> tail_{0}; // 生产者独占写指针

public:
    // 初始化时必须多分配一个空间，用于区分“队列满”和“队列空”
    explicit SPSCQueue(size_t capacity) 
        : buffer_(capacity + 1), capacity_(capacity + 1) {}

    // 生产者调用：Push 数据
    bool Push(T data) {
        // memory_order_relaxed: 我只关心当前的写指针位置，不需要与其他操作同步
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % capacity_;

        // memory_order_acquire: 确保读到最新的 head_ 值。如果 next_tail 追上了 head_，说明队列满了。
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue Full, 写入失败
        }

        // 写入数据
        buffer_[current_tail] = std::move(data);

        // memory_order_release: 极其关键！保证在更新 tail_ 之前，上一行的“写入数据”操作必须已经写进物理内存，绝对不能被 CPU 指令重排到后面去！
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // 消费者调用：Pop 数据
    bool Pop(T& result) {
        size_t current_head = head_.load(std::memory_order_relaxed);

        // memory_order_acquire: 获取最新的 tail_ 值。如果 head_ 和 tail_ 重合，说明队列是空的。
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue Empty, 读取失败
        }

        // 提取数据
        result = std::move(buffer_[current_head]);

        // memory_order_release: 保证数据被读走之后，再更新 head_ 指针给生产者腾出空间。
        head_.store((current_head + 1) % capacity_, std::memory_order_release);
        return true;
    }
};