#include "ConnectionPool.h"
#include "mprpcapplication.h"
#include <thread>

// 获取单例
ConnectionPool* ConnectionPool::getInstance() {
    static ConnectionPool pool;
    return &pool;
}

// 构造函数
ConnectionPool::ConnectionPool() {
    // 1. 加载配置 (这里可以从你的 mprpcconfig 里读)
    _ip = "127.0.0.1";
    _port = 3306;
    _user = "root";
    _password = "123456";
    _dbname = "chat";
    _initSize = 5;
    _maxSize = 15;
    _maxIdleTime = 60;
    _connectionTimeout = 100;

    // 2. 预创建初始连接
    for (int i = 0; i < _initSize; ++i) {
        MySQL* p = new MySQL();
        if (p->connect(_ip, _port, _user, _password, _dbname)) {
            _connectionQue.push(p);
            _connectionCnt++;
        }
    }

    // 3. 启动后台生产线程
    std::thread produce(std::bind(&ConnectionPool::produceConnectionTask, this));
    produce.detach();

    // 4. 启动后台扫描线程
    std::thread scanner(std::bind(&ConnectionPool::scannerConnectionTask, this));
    scanner.detach();
}

// 生产新连接的线程函数
void ConnectionPool::produceConnectionTask() {
    for (;;) {
        std::unique_lock<std::mutex> lock(_queueMutex);
        while (!_connectionQue.empty()) {
            _cv.wait(lock); // 队列不空，生产线程睡觉
        }

        // 队列空了，且没达到上限，创建新连接
        if (_connectionCnt < _maxSize) {
            MySQL* p = new MySQL();
            if (p->connect(_ip, _port, _user, _password, _dbname)) {
                _connectionQue.push(p);
                _connectionCnt++;
            }
        }
        _cv.notify_all(); // 通知消费者
    }
}

// 获取连接
std::shared_ptr<MySQL> ConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(_queueMutex);
    while (_connectionQue.empty()) {
        if (std::cv_status::timeout == _cv.wait_for(lock, std::chrono::milliseconds(_connectionTimeout))) {
            LOG_ERR("获取连接超时！");
            return nullptr;
        }
    }

    // 1. 取出原始指针
    MySQL* p = _connectionQue.front();
    _connectionQue.pop();
    _cv.notify_all();

    // 2. 🌟 关键魔术：返回一个 shared_ptr，并绑定自定义删除器
    // 当这个 shared_ptr 生命周期结束时，会自动调用 releaseConnection 归还给池子
    return std::shared_ptr<MySQL>(p, [this](MySQL* ptr) {
        this->releaseConnection(ptr); 
    });
}

// 归还连接
void ConnectionPool::releaseConnection(MySQL* p) {
    std::unique_lock<std::mutex> lock(_queueMutex);
    _connectionQue.push(p);
    _cv.notify_all(); // 通知等待连接的消费者
}


void ConnectionPool::scannerConnectionTask() {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(_maxIdleTime));
        // 这里可以加逻辑：如果连接数 > initSize 且空闲太久，就 delete 掉一些
    }
}

ConnectionPool::~ConnectionPool() {
    while (!_connectionQue.empty()) {
        MySQL* p = _connectionQue.front();
        _connectionQue.pop();
        delete p;
    }
}