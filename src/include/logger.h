#pragma once
#include"lockqueue.h"
#include<string>
// ==========================================
// 🌈 ANSI 颜色转义码 (工业级标准)
// ==========================================
#define ANSI_RES   "\033[0m"       // 重置颜色
#define ANSI_RED   "\033[1;31m"    // 红色：错误/崩溃
#define ANSI_GRE   "\033[1;32m"    // 绿色：大模型回复/成功
#define ANSI_YEL   "\033[1;33m"    // 黄色：RPC 调用/物理执行
#define ANSI_BLU   "\033[1;34m"    // 蓝色：网关逻辑/系统状态
#define ANSI_PUR   "\033[1;35m"    // 紫色：外部 Prompt/策略加载
#define ANSI_CYA   "\033[1;36m"    // 青色：Zookeeper/服务发现

// 封装更高级的“带色日志”宏
#define LOG_GW(format, ...) printf(ANSI_BLU "[GATEWAY] " format ANSI_RES "\n", ##__VA_ARGS__)
#define LOG_LLM(format, ...) printf(ANSI_GRE "[DEEPSEEK] " format ANSI_RES "\n", ##__VA_ARGS__)
#define LOG_RPC(format, ...) printf(ANSI_YEL "[RPC_NODE] " format ANSI_RES "\n", ##__VA_ARGS__)
#define LOG_SYS(format, ...) printf(ANSI_CYA "[SYSTEM] " format ANSI_RES "\n", ##__VA_ARGS__)
enum Loglevel
{
    INFO,//普通的日志信息
    ERROR,//错误信息
};

//mprpc框架提供的日志系统

class Logger
{
public:
    //获取日志的单例
    static Logger& getInstance();


    //设置日志级别
    void setLogLevel(Loglevel level);
    //写日志
    void Log(std::string msg);

private:
    int m_loglevel;//记录日志级别
    LockQueue<std::string> m_lckQue;//日志缓冲队列
    Logger();
    Logger(const Logger&)=delete;
    Logger(Logger&&)=delete;
};

//定义宏
#define LOG_INFO(logmsgformat,...)\
do \
{  \
    Logger &logger=Logger::getInstance();\
    logger.setLogLevel(INFO);\
    char c[1024]={0};\
    snprintf(c,1024,logmsgformat,##__VA_ARGS__);\
    logger.Log(c);\
} while (0)


//定义宏
#define LOG_ERR(logmsgformat,...)\
do \
{  \
    Logger &logger=Logger::getInstance();\
    logger.setLogLevel(ERROR);\
    char c[1024]={0};\
    snprintf(c,1024,logmsgformat,##__VA_ARGS__);\
    logger.Log(c);\
} while (0)