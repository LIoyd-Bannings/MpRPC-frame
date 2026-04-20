#pragma once
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <iostream>
#include <string>
#include <thread>
#include <memory>
#include <unordered_set>
#include <zmq.hpp>
#include "json.hpp"

// 引入你的各个组件
#include "mprpcapplication.h"
#include "mprpcchannel.h"
#include "threadpool.h"
#include "zookeeperutil.h"
#include "logger.h"
#include "mini_rag.hpp"
#include "sharded_lru.hpp"
#include "RedisUtil.h"
#include "GatewayPipeline.h"
#include "AgentWorkflow.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include "../src/mcp.pb.h"
#include "mprpccontroller.h"
#include "mprpcclosure.h"

using json = nlohmann::json;

// 引入外部的全局指标变量
extern std::atomic<uint64_t> metric_total_requests;
extern std::atomic<uint64_t> metric_model_fallback_total;
extern std::atomic<uint64_t> metric_rag_hit_total;
extern std::atomic<uint64_t> metric_last_llm_latency_ms;
extern std::atomic<uint64_t> metric_rpc_pending_count;
extern std::atomic<uint64_t> metric_thread_pool_queue_size;
extern std::unordered_map<std::string, std::shared_ptr<zmq::socket_t>> zq_dealer_pool_;
extern std::mutex dealer_mutex_;
extern std::mutex http_mtx;
extern std::mutex zmq_send_mutex;
extern std::vector<float> GetEmbedding(const std::string& text);

// ==========================================
// 🏭 工业级网关应用容器 (Application Server)
// ==========================================
class GatewayServer {
private:
    // --- 核心生命周期组件 (原 main 里的局部变量) ---
    ThreadPool pool;
    std::shared_ptr<MprpcChannel> channel;
    GatewayPipeline pipeline;
    MiniVectorDB rag_db;
    ShardedLRUCache session_memory; 
    
    json tools;
    std::string system_prompt;
    
    zmq::context_t context;
    zmq::socket_t server;
    
    std::shared_ptr<httplib::Client> cli;
    std::shared_ptr<httplib::Client> backup_cli;
    httplib::Headers headers;
    httplib::Headers backup_headers;

public:
    // 构造函数：初始化线程池、LRU缓存和 ZMQ Server
    GatewayServer() 
        : pool(16), 
          session_memory(16, 62), 
          context(1), 
          server(context, zmq::socket_type::router) 
    {
        channel = std::make_shared<MprpcChannel>(&pool);
    }

    // 🌟 暴露给 main 的总初始化开关
    bool Initialize() {
        LOG_SYS("=========================================");
        LOG_SYS("   🚀 正在启动分布式 Agent 网关引擎...");
        LOG_SYS("=========================================");

        if (!InitRedis()) return false;
        InitMetrics();
        InitZmqPoller();
        InitMcpTools();
        InitRAG();
        if (!InitLlmClients()) return false;
        
        server.bind("tcp://*:5556");
        LOG_SYS("✅ 网关初始化全盘完成，正在监听 5556 端口...");
        return true;
    }

    // 🌟 暴露给 main 的主干流转引擎
    void Run() {
        while (true) {
            zmq::message_t identity, empty, request;
            server.recv(identity);
            server.recv(empty);
            server.recv(request);

            std::string user_input = request.to_string();
            std::string client_id = identity.to_string();

            // 1. 安检流水线
            RequestContext ctx(client_id, user_input);
            pipeline.process(ctx);

            if (ctx.is_handled) {
                std::lock_guard<std::mutex> lock(zmq_send_mutex);
                server.send(identity, zmq::send_flags::sndmore);
                server.send(zmq::message_t(0), zmq::send_flags::sndmore);
                server.send(zmq::buffer(ctx.response_msg), zmq::send_flags::none);
                continue;
            }

            LOG_GW("安检通过！监听到合法请求 [ID: %s]，异步处理中...", client_id.c_str());
            metric_total_requests.fetch_add(1, std::memory_order_relaxed);

            // 2. 扔进线程池运行 Workflow
            pool.addTask([this, identity = std::move(identity), ctx]() mutable {
                
                // 组装依赖包裹 (直接使用 this 指针获取类成员)
                AgentDependencies deps;
                deps.rag_db = &this->rag_db;
                deps.session_memory = &this->session_memory;
                deps.cli = this->cli;
                deps.backup_cli = this->backup_cli;
                deps.headers = this->headers;
                deps.backup_headers = this->backup_headers;
                deps.tools = this->tools;
                deps.system_prompt = this->system_prompt;
                deps.channel = this->channel;

                // 点火运行
                AgentWorkflow workflow(ctx, deps);
                std::string final_answer = workflow.run();

                // 结果回传
                std::lock_guard<std::mutex> lock(zmq_send_mutex);
                this->server.send(identity, zmq::send_flags::sndmore);
                this->server.send(zmq::message_t(0), zmq::send_flags::sndmore);
                this->server.send(zmq::buffer(final_answer), zmq::send_flags::none);
            });
        }
    }

private:
    // ==========================================
    // ⚙️ 下面全是私有的初始化子模块 (从你原来的代码搬过来的)
    // ==========================================
    bool InitRedis() {
        if (!RedisUtil::getInstance()->connect("127.0.0.1", 6379)) {
            LOG_SYS("致命错误：Redis 启动失败！");
            return false;
        }
        return true;
    }

    void InitMetrics() {
        std::thread metrics_thread([](){
            httplib::Server svr;
            svr.Get("/metrics", [](const httplib::Request&, httplib::Response& res) {
                std::stringstream ss;
                ss << "gateway_rpc_pending " << metric_rpc_pending_count.load() << "\n"
                   << "gateway_thread_queue " << metric_thread_pool_queue_size.load() << "\n"
                   << "gateway_requests_total " << metric_total_requests.load() << "\n"
                   << "gateway_llm_latency_ms " << metric_last_llm_latency_ms.load() << "\n"
                   << "gateway_model_fallback_total " << metric_model_fallback_total.load() << "\n"
                   << "gateway_rag_hit_total " << metric_rag_hit_total.load() << "\n";
                res.set_content(ss.str(), "text/plain");
            });
            svr.listen("0.0.0.0", 8081);
        });
        metrics_thread.detach();
    }

    void InitZmqPoller() {
        std::thread poller_thread([this]() {
            while (true) {
                std::vector<std::shared_ptr<zmq::socket_t>> current_sockets;
                {
                    std::lock_guard<std::mutex> lock(dealer_mutex_);
                    for (auto& pair : zq_dealer_pool_) current_sockets.push_back(pair.second);
                }
                bool got_message = false;
                for (auto& dealer : current_sockets) {
                    zmq::message_t req_id_msg;
                    if (dealer->recv(req_id_msg, zmq::recv_flags::dontwait)) {
                        zmq::message_t data_msg;
                        dealer->recv(data_msg, zmq::recv_flags::none); 
                        MprpcChannel::HandleRpcResponse(req_id_msg.to_string(), data_msg.to_string(), &this->pool);
                        got_message = true;
                    }
                }
                if (!got_message) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } 
        });
        poller_thread.detach();
    }

    void InitMcpTools() {
        ZkClient zkCli;
        zkCli.Start();
        std::vector<std::string> nodes = zkCli.GetChildren("/McpToolService/ListTools");
        std::unordered_set<std::string> unique_tool_names;

        for (const auto &ip_port : nodes) {
            auto lt_controller = std::make_shared<MprpcController>();
            auto lt_request = std::make_shared<mcp::ListToolsRequest>();
            auto lt_response = std::make_shared<mcp::ListToolsResponse>();
            auto prom = std::make_shared<std::promise<void>>();
            std::future<void> fut = prom->get_future();

            mcp::McpToolService_Stub mcp_stub(channel.get());
            mcp_stub.ListTools(lt_controller.get(), lt_request.get(), lt_response.get(),
                               new MprpcClosure([prom](){ prom->set_value(); }));

            if (fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready && !lt_controller->Failed()) {
                for (int i = 0; i < lt_response->tools_size(); ++i) {
                    const auto &tool = lt_response->tools(i);
                    if (unique_tool_names.insert(tool.name()).second) {
                        json tool_json;
                        tool_json["type"] = "function";
                        tool_json["function"]["name"] = tool.name();
                        tool_json["function"]["description"] = tool.description();
                        tool_json["function"]["parameters"] = json::parse(tool.input_schema());
                        tools.push_back(tool_json);
                    }
                }
            }
        }
        LOG_GW("MCP 同步完成！AI 脑内工具总数: %zu", tools.size());
    }

    void InitRAG() {
        std::ifstream file("prompts/agent_system_prompt.txt");
        if (file.is_open()) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            system_prompt = buffer.str();
            if (!system_prompt.empty() && system_prompt.back() == '\n') system_prompt.pop_back();
        } else {
            system_prompt = "你是一个有用的助手。";
        }

        if (rag_db.Load("knowledge.bin", "knowledge_map.json")) {
            LOG_GW(ANSI_PUR "从磁盘秒级加载 HNSW 索引成功！" ANSI_RES);
        } else {
            LOG_GW(ANSI_YEL "开始首次全量 API 灌库..." ANSI_RES);
            std::ifstream kg_file("knowledge.txt");
            if (kg_file.is_open()) {
                std::string line;
                while (std::getline(kg_file, line)) {
                    if (line.empty()) continue;
                    std::vector<float> vec = GetEmbedding(line);
                    if (!vec.empty()) rag_db.AddDocument(line, vec);
                }
                rag_db.Save("knowledge.bin", "knowledge_map.json");
            }
        }
    }

    bool InitLlmClients() {
        MprpcConfig& config = MprpcApplication::GetInstance().GetConfig();
        std::string ds_key = config.Load("deepseek_key");
        std::string kimi_key = config.Load("kimi_key");

        if (ds_key == " " || kimi_key == " ") {
            LOG_SYS(ANSI_RED "警告：未检测到 API Key！" ANSI_RES);
        }

        cli = std::make_shared<httplib::Client>("https://api.deepseek.com");
        backup_cli = std::make_shared<httplib::Client>("https://api.moonshot.cn");

        cli->set_keep_alive(true); backup_cli->set_keep_alive(true);
        cli->set_read_timeout(30, 0); backup_cli->set_read_timeout(30, 0);

        headers = {{"Authorization", "Bearer " + ds_key}, {"Content-Type", "application/json"}};
        backup_headers = {{"Authorization", "Bearer " + kimi_key}, {"Content-Type", "application/json"}};
        return true;
    }
};