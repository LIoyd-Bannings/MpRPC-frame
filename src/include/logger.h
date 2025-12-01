#pragma once
#include"lockqueue.h"
#include<string>
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