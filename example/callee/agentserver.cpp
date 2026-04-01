#include <iostream>
#include <string>
#include <functional>
#include "json.hpp"
#include "logger.h"
#include "mprpcapplication.h"
#include "rpcprovider.h"
#include "db.h"
#include <cstdio>
// 引入 MCP 标准服务框架
#include "mcp_service_impl.h" 
#include <unistd.h>      // 提供 fork, exec, dup2
#include <sys/types.h>   // 提供 pid_t 类型
#include <sys/wait.h>    // 提供 waitpid 宏和状态检查
#include <sys/resource.h>// 提供大杀器：setrlimit
#include <fcntl.h>       // 提供文件控制 O_RDWR, O_CREAT
#include <fstream>
#include <sstream>
#include"ConnectionPool.h"
#include<ctime>
using json = nlohmann::json;

// =========================================================================
//  业务隔离区：每一个工具都是一个极其纯粹的 C++ 函数，互不干扰
// =========================================================================

// 工具 1：查数据库
std::string DoSearchDataBase(const std::string& args_json_str) {
   LOG_INFO(">>> [物理节点] RPC 调用: %s", args_json_str.c_str());

    try {
        auto args = json::parse(args_json_str);
        std::string sql_query = args.value("sql", "");
        if (sql_query.empty()) return "ERROR: SQL statement is empty.";

        // 1. 🌟 从连接池借一个“火热”的连接
        auto  sp= ConnectionPool::getInstance()->getConnection();
        
        if (sp== nullptr) return "ERROR: 物理节点连接池耗尽，无法获取连接。";

        // 2. 执行查询
        MYSQL_RES* res = sp->query(sql_query);
        if (res == nullptr) {
            std::string err_msg = mysql_error(sp->getConnection());
            
            return "ERROR: SQL 执行失败. 详情: " + err_msg;
        }

        // 3.元数据解包逻辑
        json result_array = json::array();
        int num_fields = mysql_num_fields(res);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            json row_json;
            for (int i = 0; i < num_fields; ++i) {
                std::string col_name = fields[i].name;
                std::string col_value = row[i] ? row[i] : "NULL";
                row_json[col_name] = col_value;
            }
            result_array.push_back(row_json);
        }


            mysql_free_result(res);
        if (result_array.empty()) return "Done. (Empty Set)";
        return result_array.dump();

    } catch (std::exception& e) {
        return std::string("Runtime Error: ") + e.what();
    }
    
}

// 工具 2：执行 Python 物理沙盒
std::string DoExecutePython(const std::string& args_json_str) {
   LOG_INFO("开始执行工具: ExecutePython, 参数: %s", args_json_str.c_str());
    try {
        auto args = json::parse(args_json_str);
        std::string code = args.value("code", "print('hello world')");

        // 1. 生成唯一的文件名，防止并发冲突
        std::string timestamp = std::to_string(time(nullptr));
        std::string file_name = "/tmp/agent_exec_" + timestamp + ".py";
        std::string out_file = "/tmp/agent_out_" + timestamp + ".txt";

        FILE* fp = fopen(file_name.c_str(), "w");
        if (fp) {
            fputs(code.c_str(), fp);
            fclose(fp);
        }

        std::string exec_result = "";
        
        // =====================================================================
        //  [核心防线] Linux OS 物理沙盒执行器
        // =====================================================================
        pid_t pid = fork(); // 影分身：克隆子进程

        if (pid < 0) {
            return "ERROR: 沙盒创建失败 (fork error)";
        } 
        else if (pid == 0) 
        {
            // ---------------------------------------------------------
            //  [子进程/囚犯]：戴上资源枷锁，准备接管 Python
            // ---------------------------------------------------------
            
            // 枷锁 1：CPU 时间硬限制 (最多执行 2 秒，防死循环炸弹)
            struct rlimit cpu_limit;
            cpu_limit.rlim_cur = 2; // 软限制
            cpu_limit.rlim_max = 2; // 硬限制
            setrlimit(RLIMIT_CPU, &cpu_limit);

            // 枷锁 2：虚拟内存硬限制 (最多分配 100MB，防爆内存 OOM)
            struct rlimit mem_limit;
            mem_limit.rlim_cur = 100 * 1024 * 1024; 
            mem_limit.rlim_max = 100 * 1024 * 1024;
            setrlimit(RLIMIT_AS, &mem_limit);

            // 狸猫换太子：把 Python 的标准输出和报错，强行重定向到 out_file 里
            int fd = open(out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd != -1) {
                dup2(fd, STDOUT_FILENO); // 劫持标准输出
                dup2(fd, STDERR_FILENO); // 劫持标准错误
                close(fd);
            }

            // 夺舍：让 Python 解释器接管这个受限的进程壳子
            execlp("python3", "python3", file_name.c_str(), nullptr);
            
            // 如果 execlp 连 Python 环境都找不到，立刻自尽
            exit(EXIT_FAILURE); 
        } 
        else 
        {
            // ---------------------------------------------------------
            //  [父进程/典狱长]：安全监控与收尸
            // ---------------------------------------------------------
            int status;
            waitpid(pid, &status, 0); // 阻塞等待囚犯执行完毕，或者被系统击毙

            if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                if (exit_code != 0) {
                    exec_result += "[沙盒提示] 脚本异常退出，退出码: " + std::to_string(exit_code) + "\n";
                }
            } 
            else if (WIFSIGNALED(status)) 
            {
                // 核心高光：成功拦截到 Linux 内核发射的击毙信号！
                int term_signal = WTERMSIG(status);
                if (term_signal == SIGXCPU || term_signal == SIGKILL) {
                    LOG_INFO("\033[1;31m[沙盒拦截] 成功防御大模型 CPU 死循环\033[0m");
                    // 必须清理文件后直接 return 报错给大模型
                    remove(file_name.c_str());
                    remove(out_file.c_str());
                    return "ERROR: 物理节点熔断！代码触碰 CPU 运行时间红线 (死循环)。";
                } else if (term_signal == SIGSEGV) {
                    LOG_INFO("\033[1;31m[沙盒拦截] 成功防御大模型内存溢出\033[0m");
                    remove(file_name.c_str());
                    remove(out_file.c_str());
                    return "ERROR: 物理节点熔断！代码内存分配超限 (OOM)。";
                } else {
                    remove(file_name.c_str());
                    remove(out_file.c_str());
                    return "ERROR: 进程被系统异常信号终止: " + std::to_string(term_signal);
                }
            }

            // 提取战利品：从文件中读出 Python 的执行结果
            std::ifstream ifs(out_file);
            if (ifs.is_open()) {
                std::stringstream buffer;
                buffer<<ifs.rdbuf();
                exec_result += buffer.str();
            }
            if (exec_result.empty()) exec_result = "执行成功，无控制台输出。";

            // 打扫战场：销毁证据
            remove(file_name.c_str());
            remove(out_file.c_str());
            
            return exec_result;
        }
    } catch (json::parse_error& e) {
        return std::string("JSON Parse Error: ") + e.what();
    }
}


// =========================================================================
// 节点启动区：在这里像搭积木一样，把能力注册到 MCP 框架里
// =========================================================================
int main(int argc, char** argv) {
    MprpcApplication::Init(argc, argv);
    RpcProvider provider;

    // 1. 实例化咱们的 MCP 标准服务
    McpServiceImpl* mcp_server = new McpServiceImpl();

    // 2. 🌟 极其震撼的动态注入：这台机器接什么活，在这里说了算！
    mcp_server->RegisterTool(
        "SearchDataBase",
        "你是高级数据库管理员。你可以执行任何合法的 MySQL 查询语句。如果你不知道当前数据库中有哪些表，请必须先生成并执行 'SHOW TABLES;' 进行探索；如果你不知道某张表的结构，请先生成并执行 'DESC 表名;'。只有在你完全清楚表结构后，再去执行最终的业务查询！绝对不要瞎猜表名和字段！",
        R"({"type":"object","properties":{"sql":{"type":"string","description":"要执行的真实 MySQL 查询语句"}}})",
        std::bind(&DoSearchDataBase, std::placeholders::_1)
    );

    mcp_server->RegisterTool(
        "ExecutePython",
        "用于在本地物理机沙盒中执行 Python 3 代码。当用户需要进行复杂的数学计算、算法推演时，必须调用此工具。",
        R"({"type":"object","properties":{"code":{"type":"string","description":"要执行的真实 Python 脚本代码"}}})",
        std::bind(&DoExecutePython, std::placeholders::_1)
    );

    // 3. 将 MCP 服务发布到 Zookeeper
    provider.NotifyService(mcp_server);

    std::cout << " MCP Server (智能体物理节点) 组装完毕，正在等待网关调度...\n";
    provider.Run();

    return 0;
}