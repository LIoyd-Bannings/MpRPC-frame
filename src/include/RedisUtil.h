#pragma once
#include <hiredis/hiredis.h>
#include <string>
#include <iostream>
#include <mutex>

// ==========================================
// 工业级 Redis 工具类 (单例模式)
// ==========================================
class RedisUtil {
public:
    // 获取全局唯一的实例
    static RedisUtil* getInstance() {
        static RedisUtil instance;
        return &instance;
    }

    // 连接 Redis
    bool connect(const std::string& ip, int port) {
        context = redisConnect(ip.c_str(), port);
        if (context == nullptr || context->err) {
            if (context) {
                std::cerr << "[Redis Error] 连接失败: " << context->errstr << std::endl;
            } else {
                std::cerr << "[Redis Error] 无法分配 Redis 上下文！" << std::endl;
            }
            return false;
        }
        std::cout << "\033[1;35m[System] 高速缓存 Redis 挂载成功！(Port: " << port << ")\033[0m" << std::endl;
        return true;
    }

    // 断开连接
    ~RedisUtil() {
        if (context != nullptr) {
            redisFree(context);
        }
    }

    // 核心 1：将字符串存入 Redis，并设置过期时间 (用于 JWT 黑名单)
    bool setEx(const std::string& key, const std::string& value, int timeout_seconds) {
        if (!context) return false;
        std::lock_guard<std::mutex> lock(mtx); // 保证多线程网关下的并发安全
        
        // 执行 Redis 命令：SETEX 键 时间 值
        redisReply* reply = (redisReply*)redisCommand(context, "SETEX %s %d %s", key.c_str(), timeout_seconds, value.c_str());
        if (reply == nullptr) return false;
        
        bool success = (reply->type != REDIS_REPLY_ERROR);
        freeReplyObject(reply); // C 语言的库，一定要记得手动释放内存！
        return success;
    }

    // 核心 2：查询某个键是否存在
    bool exists(const std::string& key) {
        if (!context) return false;
        std::lock_guard<std::mutex> lock(mtx);
        
        redisReply* reply = (redisReply*)redisCommand(context, "GET %s", key.c_str());
        if (reply == nullptr) return false;

        // 如果返回的是字符串，说明查到了数据
        bool is_exist = (reply->type == REDIS_REPLY_STRING); 
        freeReplyObject(reply);
        return is_exist;
    }

private:
    RedisUtil() : context(nullptr) {} // 构造函数私有化
    redisContext* context;
    std::mutex mtx; // 互斥锁，防止高并发下多个线程把 Redis 连接打满
};