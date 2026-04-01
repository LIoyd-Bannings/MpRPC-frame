#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <memory>
#include "db.h"
#include "logger.h"

// 🌟 数据库连接池：单例模式
class ConnectionPool {
public:
    // 获取单例对象
    static ConnectionPool* getInstance();

    // 获取一个可用的连接 (对外接口)
    std::shared_ptr<MySQL> getConnection();

    // 归还连接 (对外接口)
    void releaseConnection(MySQL* conn);

private:
    ConnectionPool(); // 私有构造
    ~ConnectionPool();

    // 运行在独立的线程中，负责生产新连接
    void produceConnectionTask();
    
    // 运行在独立的线程中，负责定时清理空闲过久的连接
    void scannerConnectionTask();

    // 数据库连接配置
    std::string _ip;
    unsigned short _port;
    std::string _user;
    std::string _password;
    std::string _dbname;

    int _initSize;    // 初始连接量
    int _maxSize;     // 最大连接量
    int _maxIdleTime; // 最大空闲时间
    int _connectionTimeout; // 连接超时时间

    std::queue<MySQL*> _connectionQue; // 存储连接的队列
    std::mutex _queueMutex;            // 维护队列的互斥锁
    std::atomic_int _connectionCnt;    // 记录创建的连接总数
    std::condition_variable _cv;       // 等待连接的条件变量
};