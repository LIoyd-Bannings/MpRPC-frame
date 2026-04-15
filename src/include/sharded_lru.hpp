#pragma once
#include <iostream>
#include <unordered_map>
#include <list>
#include <mutex>
#include <vector>
#include <string>
#include "json.hpp" // 必须包含 json 库

using json = nlohmann::json;

// 内部：单个普通的 LRU 缓存分片
class LRUShard {
private:
    size_t capacity_;
    std::list<std::pair<std::string, json>> cache_list_;
    std::unordered_map<std::string, decltype(cache_list_.begin())> cache_map_;
    std::mutex mtx_;

public:
    LRUShard(size_t cap) : capacity_(cap) {}

    json Get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) return json(); // 返回一个 null json 代表没命中

        // 命中缓存，移到链表头部
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
        printf("\033[1;32m[分段 LRU] 命中缓存！Key: %s 被提升至活跃端。\033[0m\n", key.c_str());
        return it->second->second;
    }

    void Put(const std::string& key, const json& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            it->second->second = value;
            cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
            return;
        }

        if (cache_map_.size() >= capacity_) {
            auto last = cache_list_.back();
            cache_map_.erase(last.first);
            cache_list_.pop_back();
        }

        cache_list_.emplace_front(key, value);
        cache_map_[key] = cache_list_.begin();
    }
};

// 外部：工业级分段锁 LRU
class ShardedLRUCache {
private:
    // 🌟 修改 1：不要存对象，存 unique_ptr 智能指针！
    std::vector<std::unique_ptr<LRUShard>> shards_;
    size_t num_shards_;

    size_t GetShardIndex(const std::string& key) {
        return std::hash<std::string>{}(key) % num_shards_;
    }

public:
    ShardedLRUCache(size_t num_shards = 16, size_t shard_capacity = 62) 
        : num_shards_(num_shards) {
        for (size_t i = 0; i < num_shards_; ++i) {
            // 🌟 修改 2：用 make_unique 在堆区创建分片，把指针塞进 vector
            shards_.push_back(std::make_unique<LRUShard>(shard_capacity));
        }
    }

    json get(const std::string& key) {
        // 🌟 修改 3：因为是智能指针，调用方法要从 . 变成 -> 
        return shards_[GetShardIndex(key)]->Get(key);
    }

    void put(const std::string& key, const json& value) {
        // 🌟 修改 4：同理，从 . 变成 ->
        shards_[GetShardIndex(key)]->Put(key, value);
    }
};