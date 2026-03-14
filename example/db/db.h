#ifndef DB_H
#define DB_H

#include<mysql/mysql.h>
#include<string>
#include<time.h>
#include <chrono>
using namespace std;

//数据库操作类
class MySQL
{
public:
    //初始化数据库连接
    MySQL();
    //释放数据库连接资源
    ~MySQL();

    //拒绝浅拷贝
    MySQL(const MySQL&) = delete;
    MySQL& operator=(const MySQL&) = delete;
    //连接数据库
    bool connect(string ip, unsigned short port, string user, string pwd, string dbname);

    //更新操作
    bool update(string sql);
    //查询操作
    MYSQL_RES* query(string sql);

    //获取链接
    MYSQL* getConnection();

    // 【新增】刷新空闲时间点
    void refreshAliveTime() { _alivetime = chrono::steady_clock::now(); }
    // 【新增】获取存活时长
    long long getAliveTime() {
        return chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - _alivetime).count();
    }


private:
MYSQL *_conn;
chrono::steady_clock::time_point _alivetime; // 【新增】记录进入空闲状态的时间
};

#endif