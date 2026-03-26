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

using json = nlohmann::json;

// =========================================================================
//  业务隔离区：每一个工具都是一个极其纯粹的 C++ 函数，互不干扰
// =========================================================================

// 工具 1：查数据库
std::string DoSearchDataBase(const std::string& args_json_str) {
    LOG_INFO("开始执行工具: SearchDataBase, 参数: %s", args_json_str.c_str());
    try {
        auto args = json::parse(args_json_str);
        std::string sql_query = args.value("sql", "");
        if (sql_query.empty()) return "ERROR: SQL statement is empty.";

        MySQL mysql;
        if (!mysql.connect("127.0.0.1", 3306, "root", "123456", "chat")) {
            return "ERROR: Failed to connect to local MySQL database.";
        }

        MYSQL_RES* res = mysql.query(sql_query);
        if (res == nullptr) return "ERROR: SQL execution failed or returned no result.";

        json result_array = json::array();
        MYSQL_ROW row;
        int num_fields = mysql_num_fields(res);
        MYSQL_FIELD* fields = mysql_fetch_fields(res);

        while ((row = mysql_fetch_row(res)) != nullptr) {
            json row_json;
            for (int i = 0; i < num_fields; ++i) {
                std::string field_name = fields[i].name;
                std::string field_value = row[i] ? row[i] : "NULL";
                row_json[field_name] = field_value;
            }
            result_array.push_back(row_json);
        }
        mysql_free_result(res);
        return result_array.dump();
    } catch (json::parse_error& e) {
        return std::string("JSON Parse Error: ") + e.what();
    }
}

// 工具 2：执行 Python 物理沙盒
std::string DoExecutePython(const std::string& args_json_str) {
    LOG_INFO("开始执行工具: ExecutePython, 参数: %s", args_json_str.c_str());
    try {
        auto args = json::parse(args_json_str);
        std::string code = args.value("code", "print('hello world')");

        // 用时间戳替代之前的 trace_id，保证文件名唯一
        std::string file_name = "/tmp/agent_exec_" + std::to_string(time(nullptr)) + ".py";
        FILE* fp = fopen(file_name.c_str(), "w");
        if (fp) {
            fputs(code.c_str(), fp);
            fclose(fp);
        }

        std::string command = "python3 " + file_name+" 2>&1";
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) return "ERROR: Failed to open python pipe.";

        char buffer[128];
        std::string exec_result = "";
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            exec_result += buffer;
        }
        pclose(pipe);
        remove(file_name.c_str());
        return exec_result;
    } catch (json::parse_error& e) {
        return std::string("JSON Parse Error: ") + e.what();
    }
}


// =========================================================================
// 🚀 节点启动区：在这里像搭积木一样，把能力注册到 MCP 框架里！
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

    std::cout << "🚀 MCP Server (智能体物理节点) 组装完毕，正在等待网关调度...\n";
    provider.Run();

    return 0;
}