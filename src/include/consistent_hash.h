#pragma once

#include <map>
#include <string>
#include <mutex>
// 删掉了 <shared_mutex>
#include <functional>

// 一致性哈希负载均衡器 (C++11)
class ConsistentHash {
public:
    ConsistentHash(int virtual_node_num = 100) : virtual_node_num_(virtual_node_num) {}

    void AddNode(const std::string& physical_node) {
        // C++11 标准互斥锁
        std::lock_guard<std::mutex> lock(hash_ring_mutex_);
        
        for (int i = 0; i < virtual_node_num_; ++i) {
            std::string virtual_node_name = physical_node + "#" + std::to_string(i);
            uint32_t hash_val = getHash(virtual_node_name);
            hash_ring_[hash_val] = physical_node;
        }
    }

    void RemoveNode(const std::string& physical_node) {
        // C++11 标准互斥锁
        std::lock_guard<std::mutex> lock(hash_ring_mutex_);
        
        for (int i = 0; i < virtual_node_num_; ++i) {
            std::string virtual_node_name = physical_node + "#" + std::to_string(i);
            uint32_t hash_val = getHash(virtual_node_name);
            hash_ring_.erase(hash_val);
        }
    }

    std::string GetNode(const std::string& request_data) {
        // C++11 标准互斥锁 (读的时候也加同一把锁)
        std::lock_guard<std::mutex> lock(hash_ring_mutex_);
        
        if (hash_ring_.empty()) {
            return "";
        }

        uint32_t hash_val = getHash(request_data);
        auto it = hash_ring_.upper_bound(hash_val);

        if (it == hash_ring_.end()) {
            it = hash_ring_.begin();
        }

        return it->second;
    }

private:
    std::map<uint32_t, std::string> hash_ring_;
    
    
    std::mutex hash_ring_mutex_;
    
    int virtual_node_num_;

    uint32_t getHash(const std::string& key) {
        std::hash<std::string> hasher;
        return static_cast<uint32_t>(hasher(key)); 
    }
};