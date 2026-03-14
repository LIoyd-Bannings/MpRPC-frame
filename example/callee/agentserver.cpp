#include<iostream>
#include<string>
#include"agent.pb.h"
#include"json.hpp"
#include"logger.h"
#include"mprpcapplication.h"
#include"rpcprovider.h"
#include"db.h"
#include <cstdio>
using json=nlohmann::json;

class AgentService:public ai_agent::AgentServiceRpc{
public:
//重写Execute方法
    void Execute(::google::protobuf::RpcController* controller,
                 const ::ai_agent::ToolCallRequest* request,
                 ::ai_agent::ToolCallResponse* response,
                 ::google::protobuf::Closure* done)override
    {
        //拆开信封 获取元数据
        std::string trace_id=request->trace_id();
        std::string tool_name=request->tool_name();
        std::string args_json_str=request->args_json();

        //每一次核心调度
        LOG_INFO("[TraceID: %s] 收到大模型工具调用指令, 目标工具: %s", trace_id.c_str(), tool_name.c_str());

        //核心 JSON解析与动态路由防线
        try{
            //将字符串反序列化为JSON对象
            auto args=json::parse(args_json_str);

            std::string exec_result;//用来存放工具执行的最终结果

            if(tool_name == "SearchDataBase")
            {
                // 1. 从 Agent 传来的 JSON 中提取要执行的真实 SQL 语句
                // 假设大模型生成了: {"sql": "select id, name, state from user limit 3"}
                std::string sql_query=args.value("sql","");
                if(sql_query.empty())
                {
                    exec_result="ERROR: SQL statement is empty.";
                }
                else
                {
                    //启动MYSQL连接
                    MySQL mysql;
                    if(!mysql.connect("127.0.0.1", 3306, "root", "123456", "chat"))
                    {
                        exec_result="ERROR: Failed to connect to local MySQL database.";
                    }else{
                        LOG_INFO("[TraceID: %s] 正在执行大模型派发的 SQL: %s", trace_id.c_str(), sql_query.c_str());
                        
                        //执行真实查询
                        MYSQL_RES* res=mysql.query(sql_query);
                        if(res==nullptr)
                        {
                            exec_result = "ERROR: SQL execution failed or returned no result.";  
                        }else{
                            //4.将MySQL结果集组装回JSON字符串，图给大模型！
                            json result_array=json::array();
                            MYSQL_ROW row;

                            //获取列数 以便通用处理任何大模型发来的查询
                            int num_fields=mysql_num_fields(res);
                            MYSQL_FIELD *fields=mysql_fetch_fields(res);

                            while((row=mysql_fetch_row(res))!=nullptr)
                            {
                                json row_json;
                                for(int i=0;i<num_fields;++i)
                                {
                                    //防御性编程  数据库字段可能为NULL
                                    std::string field_name=fields[i].name;
                                    std::string field_value=row[i]?row[i]:"NULL";
                                    row_json[field_name]=field_value;
                                }
                                result_array.push_back(row_json);
                            }
                            mysql_free_result(res);

                            //把整个结果数组转化为字符串 这就是Agent的Observation
                            exec_result=result_array.dump();
                        }
                    }
                }
            
            
            }   
            else if(tool_name == "ExecutePython")
            {
                //拿到大模型生成的真实的Python代码
                std::string code=args.value("code","print('hello world')");

                //将代码写入本地临时文件
                std::string file_name="/tmp/agent_exec_"+trace_id+".py";
                FILE* fp=fopen(file_name.c_str(),"w");
                if(fp)
                {   
                    fputs(code.c_str(),fp);
                    fclose(fp);
                }

                //核心  使用popen真实调用Python解释器，并且捕获输出
                std::string command="python3 "+file_name;
                FILE* pipe=popen(command.c_str(),"r");
                if(!pipe)
                {
                    exec_result = "ERROR: Failed to open python pipe.";

                }else{
                    char buffer[128];
                    exec_result="";
                    //循环读取Python脚本的stdout标准输出
                    while(fgets(buffer,sizeof(buffer),pipe)!=nullptr)
                    {
                        exec_result+=buffer;
                    }
                    pclose(pipe);
                }

                //清理现场
                remove(file_name.c_str());
                LOG_INFO("[TraceID: %s] Python真实执行完毕, 结果长度: %zu", trace_id.c_str(), exec_result.length());
            }else{
                LOG_ERR("[TraceID: %s] 非法工具调用: %s", trace_id.c_str(), tool_name.c_str());
                response->mutable_result()->set_errcode(-1);
                response->mutable_result()->set_errmsg("Unknown Tool");
                done->Run();
                return;
            }
             response->mutable_result()->set_errcode(0);
             response->mutable_result()->set_errmsg("Execute Success");
             response->set_output(exec_result);  


        } catch(json::parse_error& e)
        {
         // 极其关键的防御：如果大模型吐出的 JSON 格式烂了（少个括号之类），
            // nlohmann 会抛出异常。你不 catch，你的整个调度节点直接 Core Dump 宕机！
            LOG_ERR("[TraceID: %s] JSON 解析引发致命异常: %s, 原始载荷: %s", 
                    trace_id.c_str(), e.what(), args_json_str.c_str());
            
            response->mutable_result()->set_errcode(-2);
            response->mutable_result()->set_errmsg("JSON Parse Error");   
        }
        //4.执行回调 交给底层的Muduo网络层序列化并发送给客户端
        done->Run();
    }
};

//启动节点的主函数
int main(int argc, char**argv)
{
    //框架初始化
    MprpcApplication::Init(argc,argv);
    //把Agent调度服务器发布到RPC节点上！
    RpcProvider provider;
    provider.NotifyService(new AgentService());

    //死守本机端口  等待大模型流量涌入
    provider.Run();
    return 0;
}