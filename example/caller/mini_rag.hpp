
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
#include "hnswlib/hnswlib.h"
#include <fstream>
using json = nlohmann::json;




struct SearchHit {
    float score;
    std::string content;
};

std::vector<float> GetEmbedding(const std::string& text) {
    // 🌟 绝杀技：thread_local！
    // 让每个线程拥有自己专属的、独立的 Keep-Alive 长连接，彻底免去加锁的性能损耗！
    thread_local static httplib::Client cli("https://api.siliconflow.cn");
    
    // 初始化配置，每个线程只会执行一次
    thread_local static bool is_init = false;
    if (!is_init) {
        cli.set_keep_alive(true); // 开启长连接
        cli.set_read_timeout(10, 0); 
        is_init = true;
    }

    httplib::Headers headers = {
        {"Authorization", "Bearer sk-myjtuvwvluhegiyobsrdfhoekzrmstkatnrfxsvvnwfnguki"},
        {"Content-Type", "application/json"}
    };
    
    json body = {{"model", "BAAI/bge-large-zh-v1.5"}, {"input", text}};
    
    // 无锁调用！因为当前线程用的是专属的 cli 对象，绝对安全！
    auto res = cli.Post("/v1/embeddings", headers, body.dump(), "application/json");
    
    if (res && res->status == 200) {
        auto vec = json::parse(res->body)["data"][0]["embedding"].get<std::vector<float>>();
        return vec;
    } else {
        LOG_ERR("Embedding API 调用失败！状态码: %d", res ? res->status : -1);
    }
    return {};
}
// 作用：对粗排捞出的文本，进行极其深度的二次打分
// ==========================================
std::vector<float> GetRerankScores(const std::string& query, const std::vector<std::string>& texts) {
    if (texts.empty()) return {};

    // 依然使用 thread_local 保证并发安全与长连接复用
    thread_local static httplib::Client cli("https://api.siliconflow.cn");
    thread_local static bool is_init = false;
    if (!is_init) {
        cli.set_keep_alive(true);
        cli.set_read_timeout(10, 0);
        is_init = true;
    }

    httplib::Headers headers = {
        {"Authorization", "Bearer sk-myjtuvwvluhegiyobsrdfhoekzrmstkatnrfxsvvnwfnguki"},
        {"Content-Type", "application/json"}
    };

    // SiliconFlow 的 Reranker 接口参数
    json body = {
        {"model", "BAAI/bge-reranker-v2-m3"}, // BAAI 顶级的重排小模型
        {"query", query},
        {"documents", texts}
    };

    auto res = cli.Post("/v1/rerank", headers, body.dump(), "application/json");

    // 初始化一个全为 0 的分数数组
    std::vector<float> scores(texts.size(), 0.0f);
    
    if (res && res->status == 200) {
        auto results = json::parse(res->body)["results"];
        for (const auto& item : results) {
            int index = item["index"];                 // 原数组的下标
            float score = item["relevance_score"];     // 绝对置信度分数 (0~1)
            if (index >= 0 && index < scores.size()) {
                scores[index] = score;
            }
        }
    } else {
        //LOG_ERR("Rerank API 调用失败！状态码: %d", res ? res->status : -1);
        //  暴力的硬核探针：绕过日志框架，强行打印到屏幕！
        if (res) {
            printf("\033[1;31m[致命网络错误] 医院拒诊了！状态码: %d\n\033[0m", res->status);
            printf("\033[1;33m[服务器原话] %s\n\033[0m", res->body.c_str());
        } else {
            auto err = res.error();
            printf("\033[1;31m[致命网络错误] 根本没走到大门！(比如没网或者证书错误)\n错误详情码: %d\033[0m\n", (int)err);
        }
    }
    
    return scores;
}
// 在 mini_rag.hpp 的 GetEmbedding 下面加一个工具函数
void NormalizeVector(std::vector<float>& v) {
    float sum = 0;
    for (float x : v) sum += x * x;
    float norm = std::sqrt(sum);
    if (norm > 1e-6) { // 防止除以 0
        for (float& x : v) x /= norm;
    }
}

// // ==========================================
// //  核心数学：余弦相似度计算 (Cosine Similarity)
// // ==========================================
// float CosineSimilarity(const std::vector<float>& vec_a, const std::vector<float>& vec_b) {
//     if (vec_a.size() != vec_b.size() || vec_a.empty()) return 0.0f;

//     float dot_product = 0.0f;
//     float norm_a = 0.0f;
//     float norm_b = 0.0f;

//     // 极致的 C++ 循环压榨，一次遍历算出点积和两个模长
//     for (size_t i = 0; i < vec_a.size(); ++i) {
//         dot_product += vec_a[i] * vec_b[i];
//         norm_a += vec_a[i] * vec_a[i];
//         norm_b += vec_b[i] * vec_b[i];
//     }

//     if (norm_a == 0.0f || norm_b == 0.0f) return 0.0f;
    
//     // 返回值越接近 1.0，说明两句话的意思越像
//     return dot_product / (std::sqrt(norm_a) * std::sqrt(norm_b));
// }

// // ==========================================
// // 🗄️ 2. 内存向量数据库 (Micro Vector DB)
// // ==========================================
// struct Document {
//     std::string text;              // 文本切片内容
//     std::vector<float> embedding;  // 对应的浮点数向量
// };

// class MiniVectorDB {
// private:
//     std::vector<Document> docs;

// public:

//     std::vector<SearchHit> SearchWithScores(const std::vector<float>& query_vec, int top_k) {
//         std::vector<SearchHit> hits;
//         if (query_vec.empty() || docs.empty()) return hits;

//         for (const auto& doc : docs) {
//             // 使用 CosineSimilarity 计算当前 query 与库里每个 doc 的相似度
//             float sim = CosineSimilarity(query_vec, doc.embedding);
//             hits.push_back({sim, doc.text});
//         }

//         // 按得分从高到低排序
//         std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
//             return a.score > b.score;
//         });

//         // 截取 top_k
//         if (hits.size() > (size_t)top_k) {
//             hits.resize(top_k);
//         }
//         return hits;
//     }



//     //新增：获取知识库当前存储条数
//     size_t size() const { 
//         return docs.size(); 
//     }
//     // 插入知识
//     void AddDocument(const std::string& text, const std::vector<float>& embedding) {
//         docs.push_back({text, embedding});
//     }

//     // 检索 Top-K 相关的知识
//     std::vector<std::string> Search(const std::vector<float>& query_embedding, int top_k = 1) {
//         if (docs.empty()) return {};

//         // 用一个 pair 数组存 <相似度得分, 文本内容>
//         std::vector<std::pair<float, std::string>> scores;
//         for (const auto& doc : docs) {
//             float score = CosineSimilarity(query_embedding, doc.embedding);
//             scores.push_back({score, doc.text});
//         }

//         // 按照相似度从大到小排序 (降序)
//         std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
//             return a.first > b.first;
//         });

//         // 提取前 K 个最相关的文本
//         std::vector<std::string> results;
//         for (int i = 0; i < std::min(top_k, (int)scores.size()); ++i) {
//             // 打印一下得分，面试时极其硬核的调试日志
//             printf("[向量检索] 命中知识! 相似度得分: %.4f, 内容摘要: %s...\n", 
//                    scores[i].first, scores[i].second.substr(0, 20).c_str());
//             results.push_back(scores[i].second);
//         }
//         return results;
//     }
// };



class MiniVectorDB {
private:
    int dim = 1024;           // 向量维度 (bge-large-zh-v1.5 通常是 1024维)
    int max_elements = 10000; // 你的知识库最大容量 (目前设为1万，足够你测试了)
    
    // HNSW 的核心调优参数
    int M = 16;               // 每个节点的邻居数 (网络连通度)
    int ef_construction = 200;// 建树时的搜索深度 (越大树越精细)
    int ef_search = 50;       // 检索时的搜索深度 (越大越准，但稍慢)

    // 余弦相似度通常用内积空间 (InnerProductSpace) 配合归一化，或者 L2 空间。
    
    // hnswlib::L2Space* space;
    hnswlib::InnerProductSpace* space;
    hnswlib::HierarchicalNSW<float>* alg_hnsw;
    std::unordered_map<size_t, std::string> id_to_text; // 记录 ID 到原始文本的映射
    size_t current_id = 0;

public:
    MiniVectorDB() {
        space = new hnswlib::InnerProductSpace(dim);
        // 瞬间在内存中开辟出一棵 HNSW 多层高速导航树！
        alg_hnsw = new hnswlib::HierarchicalNSW<float>(space, max_elements, M, ef_construction);
    }

    ~MiniVectorDB() {
        delete alg_hnsw;
        delete space;
    }

    // 获取当前知识条数
    size_t size() const {
        return current_id;
    }

// 1. 插入知识：先归一化，再建树
void AddDocument(const std::string& text, std::vector<float> vec) { // 注意：这里改用值传递，因为我们要修改它
    if (vec.size() != dim) {
        LOG_ERR("向量维度不匹配！");
        return;
    }
    
    //  核心操作：在塞进 HNSW 之前，先归一化
    NormalizeVector(vec); 
    
    alg_hnsw->addPoint(vec.data(), current_id); 
    id_to_text[current_id] = text;              
    current_id++;
}

   // 2. 检索知识：先归一化提问向量，再进行搜索
std::vector<SearchHit> SearchWithScores(std::vector<float> query_vec, int top_k) {
    std::vector<SearchHit> hits;
    if (query_vec.empty() || current_id == 0) return hits;

    // 🌟 核心操作：提问向量也要归一化，这样“内积”才等于“余弦相似度”
    NormalizeVector(query_vec);

    auto result = alg_hnsw->searchKnn(query_vec.data(), top_k);
    
    while (!result.empty()) {
        auto& top = result.top();
        // 因为归一化了，且使用的是 InnerProductSpace
        // hnswlib 返回的是 1.0 - 内积，所以伪得分 = 1.0 - (1.0 - 内积) = 内积 = 余弦相似度
        float cosine_score = 1.0f - top.first; 
        
        printf("\033[1;35m[RAG 探针] 命中: 余弦得分=%.4f, 文本=%.40s...\033[0m\n", 
               cosine_score, id_to_text[top.second].c_str());

        hits.push_back({cosine_score, id_to_text[top.second]});
        result.pop();
    }
    // ... 排序逻辑保持不变 ...
    std::sort(hits.begin(), hits.end(), [](const SearchHit& a, const SearchHit& b) {
            return a.score > b.score;
        });
    return hits;
}


// 1. 将 HNSW 树和文本映射保存到本地磁盘
    void Save(const std::string& index_file, const std::string& map_file) {
        // 保存 HNSW 树的二进制拓扑结构
        alg_hnsw->saveIndex(index_file);

        // 保存 ID 到文本的映射 (转成 JSON 存进本地文件)
        json j_map;
        for (const auto& kv : id_to_text) {
            j_map[std::to_string(kv.first)] = kv.second;
        }
        std::ofstream o(map_file);
        o << j_map.dump();
        LOG_SYS(" RAG 索引已持久化至磁盘: %s", index_file.c_str());
    }

    //  2. 从本地磁盘秒级加载
    bool Load(const std::string& index_file, const std::string& map_file) {
        std::ifstream f_index(index_file);
        std::ifstream f_map(map_file);
        if (!f_index.is_open() || !f_map.is_open()) {
            return false; // 文件不存在，说明是第一次运行
        }

        // 瞬间加载百万级向量树！
        alg_hnsw->loadIndex(index_file, space);

        // 恢复文本映射
        json j_map;
        f_map >> j_map;
        for (auto& item : j_map.items()) {
            id_to_text[std::stoull(item.key())] = item.value().get<std::string>();
        }
        current_id = id_to_text.size();
        return true;
    }
};




#endif