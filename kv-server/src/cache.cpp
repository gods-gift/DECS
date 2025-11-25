#include "cache.h"

LRUCache::LRUCache(std::size_t capacity)
    : capacity_(capacity)
{
}

bool LRUCache::get(const std::string& key, std::string& value_out) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    touch(it->second);
    value_out = it->second->second;
    hits_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void LRUCache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it != map_.end()) {
        it->second->second = value;
        touch(it->second);
        return;
    }
    lru_list_.emplace_front(key, value);
    map_[key] = lru_list_.begin();

    if (map_.size() > capacity_) {
        auto last = lru_list_.end();
        --last;
        map_.erase(last->first);
        lru_list_.pop_back();
    }
}

void LRUCache::erase(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(key);
    if (it == map_.end()) return;
    lru_list_.erase(it->second);
    map_.erase(it);
}

std::size_t LRUCache::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return map_.size();
}

std::size_t LRUCache::hits() const {
    return hits_.load(std::memory_order_relaxed);
}

std::size_t LRUCache::misses() const {
    return misses_.load(std::memory_order_relaxed);
}

void LRUCache::reset_stats() {
    hits_.store(0, std::memory_order_relaxed);
    misses_.store(0, std::memory_order_relaxed);
}

void LRUCache::touch(ListIt it) {
    lru_list_.splice(lru_list_.begin(), lru_list_, it);
}
