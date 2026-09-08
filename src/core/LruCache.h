// Iris Core —— 容量受限 LRU 缓存
// Put/Get 触碰热度，超容量驱逐最久未用条目；Contains 仅观测不触碰。
// 线程不安全（调用方保证并发安全）。
#pragma once

#include <cstddef>
#include <list>
#include <optional>
#include <unordered_map>
#include <utility>

namespace iris {

template <typename K, typename V>
class LruCache {
public:
    explicit LruCache(size_t capacity) : capacity_(capacity) {}

    // 插入/更新并触碰；超出容量驱逐最久未用条目
    void Put(const K& key, V value) {
        if (capacity_ == 0) return;
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(value);
            items_.splice(items_.begin(), items_, it->second);  // 移到最新端
            return;
        }
        if (index_.size() >= capacity_) {
            index_.erase(items_.back().first);  // 驱逐最久未用
            items_.pop_back();
        }
        items_.emplace_front(key, std::move(value));
        index_.emplace(key, items_.begin());
    }

    // 命中返回值副本并触碰；未命中返回 nullopt
    std::optional<V> Get(const K& key) {
        auto it = index_.find(key);
        if (it == index_.end()) return std::nullopt;
        items_.splice(items_.begin(), items_, it->second);
        return it->second->second;
    }

    // 是否在缓存（不触碰热度，仅供观测/测试）
    bool Contains(const K& key) const { return index_.count(key) != 0; }

    size_t Size() const { return index_.size(); }

private:
    size_t capacity_;
    std::list<std::pair<K, V>> items_;  // front=最新，back=最久未用
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> index_;
};

} // namespace iris
