#pragma once
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include "json.hpp"
#include "GatewayPipeline.h"
#include "logger.h"
#include "mini_rag.hpp"
#include "sharded_lru.hpp"
#include "mprpcchannel.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#include"mprpccontroller.h"
using json = nlohmann::json;
#include"../mcp.pb.h"
#include "mprpcclosure.h"
// 声明外部的全局变量（如果在别的文件定义了，这里用 extern 引入）
extern std::mutex http_mtx;
extern std::atomic<uint64_t> metric_rag_hit_total;
extern std::atomic<uint64_t> metric_model_fallback_total;
extern std::atomic<uint64_t> metric_last_llm_latency_ms;
extern std::vector<float> GetEmbedding(const std::string& text);
extern std::vector<float> GetRerankScores(const std::string& query, const std::vector<std::string>& docs);

// ==========================================
// 📦 依赖包裹：避免构造函数参数过长
// ==========================================
struct AgentDependencies {
    MiniVectorDB* rag_db;
    ShardedLRUCache* session_memory;
    std::shared_ptr<httplib::Client> cli;
    std::shared_ptr<httplib::Client> backup_cli;
    httplib::Headers headers;
    httplib::Headers backup_headers;
    json tools;
    std::string system_prompt;
    std::shared_ptr<MprpcChannel> channel;


};

// ==========================================
// 🤖 工业级 Agent 工作流引擎
// ==========================================
class AgentWorkflow {
private:
    // 1. 外部依赖资源
    AgentDependencies deps;
    RequestContext ctx;

    // 2. 🌟 核心状态流转变量（提拔为类成员，大家都能用！）
    std::string final_prompt;         // RAG 组装后的终极提示词
    std::string final_summary;        // LLM 给出的最终回答
    json messages_array;              // 完整的历史记忆（用于存档）
    json current_turn_messages;       // 加上了当前问题和背景知识的临时对话数组
    
    int tool_call_count = 0;
    const int MAX_TOOL_CALLS = 6;
    const int MAX_RETRY = 3;

    //记录触发自愈的次数
    int retry_count = 0;

public:
    // 构造函数：注入上下文和外部依赖
    AgentWorkflow(const RequestContext& context, const AgentDependencies& dependencies) 
        : ctx(context), deps(dependencies) {}

    // 启动工作流
    std::string run() {
        try {
            buildRagContext();
            loadSessionMemory();
            return executeReActLoop();
        } catch (const std::exception& e) {
            return std::string("系统繁忙或崩溃: ") + e.what();
        }
    }

private:
    // ==========================================
    // 模块 1：RAG 检索 (只负责组装 final_prompt)
    // ==========================================
    void buildRagContext() {
        LOG_GW("正在进行 HNSW 向量粗排与 Rerank...");
        std::vector<float> user_query_vec = GetEmbedding(ctx.real_question);
        auto search_hits = deps.rag_db->SearchWithScores(user_query_vec, 15);

        std::vector<std::string> candidate_texts;
        for (const auto& hit : search_hits) candidate_texts.push_back(hit.content);

        std::vector<float> rerank_scores = GetRerankScores(ctx.real_question, candidate_texts);
        for (size_t i = 0; i < search_hits.size(); ++i) search_hits[i].score = rerank_scores[i];

        std::sort(search_hits.begin(), search_hits.end(), [](const SearchHit& a, const SearchHit& b) {
            return a.score > b.score;
        });

        std::string strong_context = "";
        bool has_high_confidence_knowledge = false;

        if (!search_hits.empty()) {
            float max_gap = 0.0f;
            int cliff_index = search_hits.size();
            for (size_t i = 1; i < search_hits.size(); ++i) {
                float current_gap = search_hits[i-1].score - search_hits[i].score;
                if (current_gap > max_gap) { max_gap = current_gap; cliff_index = i; }
            }
            if (max_gap < 0.05f) cliff_index = std::min(3, (int)search_hits.size());

            const size_t MAX_CONTEXT_LENGTH = 1000;
            size_t current_length = 0;
            for (int i = 0; i < cliff_index; ++i) {
                if (search_hits[i].score > 0.45f) {
                    if (current_length + search_hits[i].content.length() > MAX_CONTEXT_LENGTH) break;
                    std::string snippet = "[" + std::to_string(i+1) + "] " + search_hits[i].content + "\n";
                    strong_context += snippet;
                    current_length += snippet.length();
                    has_high_confidence_knowledge = true;
                }
            }
        }

        // 🌟 将结果存入类成员变量，传递给下一个函数
        if (has_high_confidence_knowledge) {
            metric_rag_hit_total.fetch_add(1, std::memory_order_relaxed);
            final_prompt = "【已知事实】：\n" + strong_context + "\n【用户问题】：\n" + ctx.real_question;
        } else {
            final_prompt = "【系统指令】：你是一个 AI 助手...\n【用户问题】：\n" + ctx.real_question;
        }
    }

    // ==========================================
    // 模块 2：加载记忆 (负责初始化 current_turn_messages)
    // ==========================================
    void loadSessionMemory() {
        messages_array = deps.session_memory->get(ctx.client_id);
        if (messages_array.is_null()) {
            messages_array = json::array();
            messages_array.push_back({{"role", "system"}, {"content", deps.system_prompt}});
        }
        
        // 复制一份用于当前回合的推理
        current_turn_messages = messages_array; 
        current_turn_messages.push_back({{"role", "user"}, {"content", final_prompt}});
    }

    // ==========================================
    // 模块 3：核心循环
    // ==========================================
    std::string executeReActLoop() {
        while (tool_call_count < MAX_TOOL_CALLS) {
            json llm_response = callLLMAPI();
            
            if (llm_response.is_null()) return "大模型 API 调用彻底失败。";

            auto message = llm_response["choices"][0]["message"];
            current_turn_messages.push_back(message); 

            if (message.contains("tool_calls")) {
                tool_call_count++;
                auto tool_call = message["tool_calls"][0];
                
                std::string rpc_res = invokeRpcTool(tool_call["function"]["name"], tool_call["function"]["arguments"]);
                
                current_turn_messages.push_back({
                    {"role", "tool"},
                    {"tool_call_id", tool_call["id"]},
                    {"content", rpc_res}
                });
            } else {
                final_summary = message["content"].get<std::string>();
                saveSessionMemory();
                return final_summary;
            }
        }
        return "思考超时：强制终止。";
    }

    // ==========================================
    // 模块 4：纯净的 API 请求 (包含熔断)
    // ==========================================
    json callLLMAPI() {
        json request_body = {
            {"model", "deepseek-chat"},
            {"messages", current_turn_messages},
            {"tools", deps.tools},
            {"tool_choice", "auto"}
        };

        if (tool_call_count >= MAX_TOOL_CALLS - 1) {
            request_body.erase("tools");
            request_body.erase("tool_choice");
        }

        httplib::Result res;
        auto start = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(http_mtx);    
            res = deps.cli->Post("/chat/completions", deps.headers, request_body.dump(), "application/json");
        }
        metric_last_llm_latency_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(), std::memory_order_relaxed);

        if (!res || res->status != 200) {
            metric_model_fallback_total.fetch_add(1, std::memory_order_relaxed);
            request_body["model"] = "moonshot-v1-32k";
            std::lock_guard<std::mutex> lock(http_mtx);
            res = deps.backup_cli->Post("/v1/chat/completions", deps.backup_headers, request_body.dump(), "application/json");
        }

        if (res && res->status == 200) {
            return json::parse(res->body);
        }
        return json(); // 返回空 JSON 表示失败
    }

// ==========================================
    // 模块 5：RPC 调用与 Reviewer 自愈机制
    // ==========================================
    std::string invokeRpcTool(const std::string& name, const std::string& args) {
        LOG_GW("正在通过 ZeroMQ 投递 RPC 任务至物理节点: %s", name.c_str());

        // 1. 准备 RPC 请求体
        auto rpc_controller = std::make_shared<MprpcController>();
        auto mcp_request = std::make_shared<mcp::CallToolRequest>();
        auto mcp_response = std::make_shared<mcp::CallToolResponse>();

        mcp_request->set_name(name);
        mcp_request->set_arguments(args);

        // 2. 异步转同步 (Promise/Future)
        auto prom = std::make_shared<std::promise<void>>();
        std::future<void> fut = prom->get_future();

        // 🌟 核心变化 1：使用依赖注入的 channel
        mcp::McpToolService_Stub  mcp_stub(deps.channel.get());
        mcp_stub.CallTool(rpc_controller.get(), mcp_request.get(), mcp_response.get(), 
                          new MprpcClosure([prom](){ prom->set_value(); }));

        // 3. 阻塞等待沙盒回信 (超时 10 秒)
        std::future_status status = fut.wait_for(std::chrono::seconds(10));
        std::string rpc_result_str;

        if (status == std::future_status::timeout) {
            rpc_result_str = "错误：物理节点执行超时，沙盒无响应。";
        } else if (rpc_controller->Failed()) {
            rpc_result_str = "错误：RPC 崩溃 (" + rpc_controller->ErrorText() + ")";
        } else {
            rpc_result_str = mcp_response->content();
        }

        // ==========================================
        // 🚨 [A2A Reviewer 自愈机制]
        // ==========================================
        // 如果返回结果包含错误特征，且还没达到最大重试次数
        if ((rpc_result_str.find("ERROR") != std::string::npos || 
             rpc_result_str.find("错误") != std::string::npos ||
             rpc_result_str.find("Exception") != std::string::npos) && 
             retry_count < MAX_RETRY) {
            
            retry_count++;
            LOG_GW(ANSI_YEL "检测到沙盒执行异常！(重试: %d/%d) 正在唤醒 Reviewer 专家模型分析..." ANSI_RES, retry_count, MAX_RETRY);

            // 构造给 Reviewer 看的分析 Prompt
            json reviewer_messages = {
                {{"role", "system"}, {"content", "你是一个资深代码审计专家。请分析以下报错日志和参数，指出根本原因，并给前一个大模型提出具体的修改意见。"}},
                {{"role", "user"}, {"content", "尝试执行的参数:\n" + args + "\n\n执行报错内容:\n" + rpc_result_str}}
            };

            json request_body = {
                {"model", "deepseek-chat"},
                {"messages", reviewer_messages}
            };

            httplib::Result rev_res;
            {
                // 🌟 核心变化 2：使用依赖注入的 cli、headers 和外部的全局 http_mtx 锁
                std::lock_guard<std::mutex> lock(http_mtx);
                rev_res = deps.cli->Post("/chat/completions", deps.headers, request_body.dump(), "application/json");
            }

            // 解析专家的意见
            if (rev_res && rev_res->status == 200) {
                try {
                    std::string advice = json::parse(rev_res->body)["choices"][0]["message"]["content"];
                    LOG_SYS("Reviewer 专家诊断意见: \n%s", advice.c_str());
                    
                    // 🌟 核心变化 3：篡改结果，把专家的意见拼进去，喂给外层的 LLM 让它重写！
                    rpc_result_str = "【自动纠错系统】检测到代码执行失败。\n[内核原始报错]:\n" + rpc_result_str +
                                     "\n\n[Reviewer 专家诊断建议]:\n" + advice + 
                                     "\n\n【系统强制指令】：请务必基于上述专家建议，反思之前的错误，重新生成参数并调用工具！";
                } catch (...) {
                    LOG_SYS("Reviewer 模型返回格式异常，跳过深度诊断。");
                }
            } else {
                LOG_SYS("Reviewer 唤醒失败，回退为普通报错反馈。");
            }
        }

        return rpc_result_str;
    }

    // ==========================================
    // 模块 6：记忆存档
    // ==========================================
    void saveSessionMemory() {
        messages_array.push_back({{"role", "user"}, {"content", ctx.real_question}}); 
        messages_array.push_back({{"role", "assistant"}, {"content", final_summary}});

        if (messages_array.size() > 20) {
            messages_array.erase(messages_array.begin() + 1, messages_array.begin() + 3);
        }
        deps.session_memory->put(ctx.client_id, messages_array);
    }
};