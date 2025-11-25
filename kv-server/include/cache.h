#pragma once
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <cstddef>
#include <atomic>

/** Thread-safe LRU cache for string key/value pairs. */
class LRUCache {
public:
    explicit LRUCache(std::size_t capacity);

    bool get(const std::string& key, std::string& value_out);
    void put(const std::string& key, const std::string& value);
    void erase(const std::string& key);
    std::size_t size() const;
    std::size_t capacity() const { return capacity_; }

    // stats (approximate, thread-safe via atomics)
    std::size_t hits() const;
    std::size_t misses() const;
    void        reset_stats();

private:
    using ListIt = std::list<std::pair<std::string, std::string>>::iterator;

    std::size_t capacity_;
    mutable std::mutex mu_;
    std::list<std::pair<std::string, std::string>> lru_list_;
    std::unordered_map<std::string, ListIt> map_;

    std::atomic<std::size_t> hits_{0};
    std::atomic<std::size_t> misses_{0};

    void touch(ListIt it);
};
