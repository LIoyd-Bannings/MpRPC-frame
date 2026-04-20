
#include <iostream>
#include <string>
#include <atomic>
#include <mutex>

#include <mysql/mysql.h>
#include <crypt.h>
#include <zmq.hpp>

#include "mprpcapplication.h"
#include "../db/ConnectionPool.h"
#include "logger.h"
#include "GatewayServer.h"

// ==========================================
//  全局监控指标与核心锁 (真正的物理内存分配在这里)
// ==========================================
extern std::atomic<uint64_t> metric_total_requests;
extern std::atomic<uint64_t> metric_model_fallback_total;
extern std::atomic<uint64_t> metric_rag_hit_total;
extern std::atomic<uint64_t> metric_last_llm_latency_ms;
std::mutex http_mtx;
std::mutex zmq_send_mutex;
zmq::context_t g_zmq_context(1);

// 这两个指标是在底层的 MprpcChannel/ThreadPool 里定义的，所以需要 extern 借过来
extern std::atomic<uint64_t> metric_rpc_pending_count;
extern std::atomic<uint64_t> metric_thread_pool_queue_size;

// ==========================================
//  数据库鉴权函数 (供 GatewayPipeline 调用)
// ==========================================
bool VerifyUserFromDB(const std::string& username, const std::string& password) {
    auto sp = ConnectionPool::getInstance()->getConnection();
    if (sp == nullptr) {
        LOG_SYS("获取数据库连接失败");
        return false;
    }

    // 获取底层的裸指针
    MYSQL* conn = sp->getConnection(); 
    if (conn == nullptr) return false;

    // 1. 初始化 stmt 句柄
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (!stmt) {
        LOG_SYS("mysql_stmt_init 失败, 内存不足");
        return false;
    }

    // 2. 准备 SQL 模板 (使用 ? 作为纯数据占位符)
    const char* sql = "SELECT password_hash FROM agent_users WHERE username=?";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql))) {
        LOG_SYS("预编译失败: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    // 3. 绑定输入参数 (Input Binding)
    MYSQL_BIND bind_param[1];
    memset(bind_param, 0, sizeof(bind_param)); // 必须清零，否则会有野指针风险

    bind_param[0].buffer_type = MYSQL_TYPE_STRING;
    bind_param[0].buffer = (char*)username.c_str(); // C API 需要 char*，因为是只读，这里强转是安全的
    bind_param[0].buffer_length = username.length();

    if (mysql_stmt_bind_param(stmt, bind_param)) {
        LOG_SYS("参数绑定失败: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    // 4. 执行预编译语句
    if (mysql_stmt_execute(stmt)) {
        LOG_SYS("执行语句失败: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    // 5. 绑定输出结果 (Output Binding)
    MYSQL_BIND bind_result[1];
    memset(bind_result, 0, sizeof(bind_result));

    char db_hashed_pwd[256]; // 预分配足够大的栈内存承接 hash
    unsigned long length = 0;
    bool is_null = false;

    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = db_hashed_pwd;
    bind_result[0].buffer_length = sizeof(db_hashed_pwd);
    bind_result[0].length = &length;
    bind_result[0].is_null = &is_null;

    if (mysql_stmt_bind_result(stmt, bind_result)) {
        LOG_SYS("结果绑定失败: %s", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return false;
    }

    // 6. 提取数据并验证
    bool auth_success = false;
    // mysql_stmt_fetch 返回 0 表示成功抓取到一行数据
    if (mysql_stmt_fetch(stmt) == 0) { 
        if (!is_null) {
            std::string hash_str(db_hashed_pwd, length);
            // 原生的加盐哈希比对
            char* calc_hash = crypt(password.c_str(), hash_str.c_str());
            if (calc_hash != nullptr && hash_str == calc_hash) {
                auth_success = true;
            } else {
                LOG_SYS("\033[1;33m密码比对失败！\033[0m");
            }
        }
    } else {
        LOG_SYS("\033[1;33m查无此人: %s\033[0m", username.c_str());
    }

    // 7. 打扫战场，严格释放资源
    mysql_stmt_close(stmt);
    return auth_success;
}
// ==========================================
//  终极 Main 函数 (Spring Boot 风格)
// ==========================================
int main(int argc, char **argv)
{
    // 1. 初始化底层 RPC 框架配置
    MprpcApplication::Init(argc, argv);

    // 2. 实例化分布式网关服务器机箱
    GatewayServer server;

    // 3. 一键启动全套生命周期组件
    if (!server.Initialize()) {
        std::cerr << "服务器启动失败，进程终止。" << std::endl;
        return EXIT_FAILURE;
    }

    // 4. 进入高并发核心调度死循环
    server.Run();

    return 0;
}