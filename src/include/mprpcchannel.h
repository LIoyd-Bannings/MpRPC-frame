#pragma once
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include "threadpool.h"
#include <memory>
#include <unordered_map>
#include <mutex>
#include "consistent_hash.h"
class MprpcChannel : public google::protobuf::RpcChannel, public std::enable_shared_from_this<MprpcChannel>
{
public:
    MprpcChannel(ThreadPool *pool) : threadpool(pool) {}
    // 所有通过stub代理对象调用的rpc方法  都走到这里了  统一做rpc方法调用的数据序列化和网络发送
    void CallMethod(const google::protobuf::MethodDescriptor *method,
                    google::protobuf::RpcController *controller, const google::protobuf::Message *request,
                    google::protobuf::Message *response, google::protobuf::Closure *done) override;

private:
    ThreadPool *threadpool;
    // 本地路由缓存表
    std::unordered_map<std::string, std::string> route_cache_;
    // 保护路由表的互斥锁
    std::mutex cache_mutex_;

    // ==========================================================
    // 【现在加的】TCP 连接池
    // Key: "IP:Port" (例如 "192.168.1.10:8000")
    // Value: 存放空闲 socket 句柄 (clientfd) 的队列
    // ==========================================================
    std::unordered_map<std::string, std::queue<int>> conn_pool_;
    std::mutex conn_mutex_; // 保护连接池的锁
};