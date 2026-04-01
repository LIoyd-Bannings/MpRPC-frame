
#ifndef MINI_RAG_HPP
#define MINI_RAG_HPP

#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include "httplib.h"
#include "json.hpp"
#include"logger.h"
using json = nlohmann::json;

struct SearchHit {
    float score;
    std::string content;
};

std::vector<float> GetEmbedding(const std::string& text) {
    httplib::Client cli("https://api.siliconflow.cn");
    httplib::Headers headers = {
        {"Authorization", "Bearer sk-myjtuvwvluhegiyobsrdfhoekzrmstkatnrfxsvvnwfnguki"},
        {"Content-Type", "application/json"}
    };
    json body = {{"model", "BAAI/bge-large-zh-v1.5"}, {"input", text}};
    auto res = cli.Post("/v1/embeddings", headers, body.dump(), "application/json");
    if (res && res->status == 200) {
        return json::parse(res->body)["data"][0]["embedding"].get<std::vector<float>>();
    }else{
        LOG_ERR("Embedding API 调用失败！状态码: %d, 原因: %s", 
        res ? res->status : -1, res ? res->body.c_str() : "网络超时");
    }
    return {};
}

// ==========================================
//  核心数学：余弦相似度计算 (Cosine Similarity)
// ==========================================
float CosineSimilarity(const std::vector<float>& vec_a, const std::vector<float>& vec_b) {
    if (vec_a.size() != vec_b.size() || vec_a.empty()) return 0.0f;

    float dot_product = 0.0f;
    float norm_a = 0.0f;
    float norm_b = 0.0f;

    // 极致的 C++ 循环压榨，一次遍历算出点积和两个模长
    for (size_t i = 0; i < vec_a.size(); ++i) {
        dot_product += vec_a[i] * vec_b[i];
        norm_a += vec_a[i] * vec_a[i];
        norm_b += vec_b[i] * vec_b[i];
    }

    if (norm_a == 0.0f || norm_b == 0.0f) return 0.0f;
    
    // 返回值越接近 1.0，说明两句话的意思越像
    return dot_product / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

// ==========================================
// 🗄️ 2. 内存向量数据库 (Micro Vector DB)
// ==========================================
struct Document {
    std::string text;              // 文本切片内容
    std::vector<float> embedding;  // 对应的浮点数向量
};

class MiniVectorDB {
private:
    std::vector<Document> docs;

public:

    std::vector<SearchHit> SearchWithScores(const std::vector<float>& query_vec, int top_k) {
        std::vector<SearchHit> hits;
        if (query_vec.empty() || docs.empty()) return hits;

        for (const auto& doc : docs) {
            // 使用 CosineSimilarity 计算当前 query 与库里每个 doc 的相似度
            float sim = CosineSimilarity(query_vec, doc.embedding);
            hits.push_back({sim, doc.text});
        }

        // 按得分从高到低排序
        std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
            return a.score > b.score;
        });

        // 截取 top_k
        if (hits.size() > (size_t)top_k) {
            hits.resize(top_k);
        }
        return hits;
    }



    //新增：获取知识库当前存储条数
    size_t size() const { 
        return docs.size(); 
    }
    // 插入知识
    void AddDocument(const std::string& text, const std::vector<float>& embedding) {
        docs.push_back({text, embedding});
    }

    // 检索 Top-K 相关的知识
    std::vector<std::string> Search(const std::vector<float>& query_embedding, int top_k = 1) {
        if (docs.empty()) return {};

        // 用一个 pair 数组存 <相似度得分, 文本内容>
        std::vector<std::pair<float, std::string>> scores;
        for (const auto& doc : docs) {
            float score = CosineSimilarity(query_embedding, doc.embedding);
            scores.push_back({score, doc.text});
        }

        // 按照相似度从大到小排序 (降序)
        std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

        // 提取前 K 个最相关的文本
        std::vector<std::string> results;
        for (int i = 0; i < std::min(top_k, (int)scores.size()); ++i) {
            // 打印一下得分，面试时极其硬核的调试日志
            printf("[向量检索] 命中知识! 相似度得分: %.4f, 内容摘要: %s...\n", 
                   scores[i].first, scores[i].second.substr(0, 20).c_str());
            results.push_back(scores[i].second);
        }
        return results;
    }
};


#endif