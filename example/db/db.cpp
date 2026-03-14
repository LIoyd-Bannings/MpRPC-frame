#include "db.h"
#include <muduo/base/Logging.h>
// 数据库配置信息
//static string server = "127.0.0.1";
//static string user = "root";
//static string password = "123456";
//static string dbname = "chat";

MySQL::MySQL()
{

    _conn = mysql_init(nullptr);
}
// 释放数据库连接资源
MySQL::~MySQL()
{
    if (_conn != nullptr)
    {
        mysql_close(_conn);
    }
}

// 连接数据库
bool MySQL::connect(string ip, unsigned short port, string user, string pwd, string dbname)
{
    if(_conn==nullptr)//f防止init失败
    {
        LOG_ERROR << "MySQL init failed!";
        return false;
    }
    MYSQL *p = mysql_real_connect(_conn, ip.c_str(), user.c_str(), pwd.c_str(), dbname.c_str(), port, nullptr, 0);
    if (p != nullptr)
    {
        // C和C++代码默认的编码字符是ASCII，如果不设置 从MYSQL上拉下来的中文显示？？？
        mysql_query(_conn, "set names gbk");
        // 【埋点4】连接成功！这是真正的含金量

        // 把这行日志注释掉！高并发压测时不要打印 info 日志
        //LOG_INFO << "connect mysql success!";
    }
    else
    {
        LOG_INFO << "connect mysql fail!";
         
        LOG_ERROR << "Connect mysql fail! Error: " << mysql_error(_conn);
    }
    return p;
}

// 更新操作
bool MySQL::update(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        // 【修改】加上 mysql_error(_conn)
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":" 
                 << sql << "更新失败！ 错误原因: " << mysql_error(_conn);
        return false;
    }
    return true;
}
// 查询操作
MYSQL_RES *MySQL::query(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        //LOG_INFO << __FILE__ << ": " << __LINE__ << ":" << sql << "查询失败！";
        // 【修改】加上 mysql_error(_conn)
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":" 
                 << sql << "查询失败！ 错误原因: " << mysql_error(_conn);
                 return nullptr;
    }

    return mysql_use_result(_conn);
}
MYSQL *MySQL::getConnection()
{
    return _conn;
}
