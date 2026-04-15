#pragma once
#include <list>
#include <unordered_map>
#include <mutex>
#include <memory>

template<typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    // 获取数据：如果存在，提到表头并返回；不存在返回空指针
    V get(const K& key) {
        std::lock_guard<std::mutex> lock(mtx_); // 线程安全保障
        if (map_.find(key) == map_.end()) {
            return nullptr; // 针对你的场景，建议返回 shared_ptr 或空
        }
        // 将节点移动到链表头部（表示最近访问）
        items_.splice(items_.begin(), items_, map_[key]);
        printf("\033[1;32m[LRU Cache]  命中缓存！Key: %s 被提升至活跃链表头部。\033[0m\n", key.c_str());
        return map_[key]->second;
    }

    // 存入数据
    void put(const K& key, V value) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        if (map_.find(key) != map_.end()) {
            // 已存在：更新值并移到头部
            items_.splice(items_.begin(), items_, map_[key]);
            map_[key]->second = value;
            return;
        }

        // 不存在：检查是否满载
        if (items_.size() == capacity_) {
            // 剔除末尾（最久未使用）
            K last_key = items_.back().first;
            printf("\033[1;31m[LRU Cache] 容量达到上限 (%zu)！最久未使用 Key: %s 被淘汰。\033[0m\n", capacity_, last_key.c_str());
            map_.erase(last_key);
            items_.pop_back();
        }

        // 插入新节点到头部
        items_.emplace_front(key, value);
        map_[key] = items_.begin();
    }

private:
    size_t capacity_;
    std::list<std::pair<K, V>> items_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> map_;
    std::mutex mtx_; // 保护内存数据，防止并发写崩溃
};