#pragma once
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include "threadpool.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include "consistent_hash.h"

// 增加异步上下文结构
struct AsyncRpcContext
{
    google::protobuf::Message *response;// 存放返回结果的内存地址
    google::protobuf::Closure *done;// 业务层的收尾回调（闭包）
    // 增加一个时间戳，后续可以做超时清理
    std::chrono::steady_clock::time_point start_time;// 用于超时监控
};

class MprpcChannel : public google::protobuf::RpcChannel, public std::enable_shared_from_this<MprpcChannel>
{
public:
    MprpcChannel(ThreadPool *pool) : threadpool(pool) {}
    // 所有通过stub代理对象调用的rpc方法  都走到这里了  统一做rpc方法调用的数据序列化和网络发送
    void CallMethod(const google::protobuf::MethodDescriptor *method,
                    google::protobuf::RpcController *controller, const google::protobuf::Message *request,
                    google::protobuf::Message *response, google::protobuf::Closure *done) override;

    // 新增：由 Poller 线程调用的分发函数
    static void HandleRpcResponse(const std::string &id, const std::string &data,ThreadPool* pool);

private:
    ThreadPool *threadpool;
    // 本地路由缓存表
    std::unordered_map<std::string, std::string> route_cache_;
    // 保护路由表的互斥锁
    std::mutex cache_mutex_;

    // 核心：Correlation ID -> 闭包上下文
    static std::unordered_map<std::string, AsyncRpcContext> pending_map_;
    static std::mutex map_mutex_;
    static std::atomic<uint64_t> uuid_gen_;

    // ==========================================
    // [新增]清道夫线程相关声明
    // ==========================================
    static std::thread scavenger_thread_;
    static bool is_scavenger_started_;
    static std::mutex init_mutex_;
    static void ScavengerTask(); // 定时巡逻任务
};