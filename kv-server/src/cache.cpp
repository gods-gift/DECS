// src/cache.cpp
#include "cache.h"

#include <cassert>
#include <list>
#include <string>
#include <unordered_map>

struct LRUCache::Impl {
    struct Node {
        std::string key;
        std::string value;
    };

    explicit Impl(size_t cap)
        : capacity(cap == 0 ? 1 : cap) {} // avoid zero capacity pathological case

    // Doubly-linked list (front = most recently used)
    std::list<Node> lru;
    // Map key -> iterator into list
    std::unordered_map<std::string, std::list<Node>::iterator> index;
    size_t capacity;

    inline void touch(std::list<Node>::iterator it) {
        // Move the accessed node to the front (MRU)
        lru.splice(lru.begin(), lru, it);
    }
};

LRUCache::LRUCache(size_t cap)
: pImpl_(new Impl(cap)) {}

LRUCache::~LRUCache() = default;

bool LRUCache::get(const std::string& key, std::string& value_out) {
    auto it = pImpl_->index.find(key);
    if (it == pImpl_->index.end()) return false;

    auto list_it = it->second;
    value_out = list_it->value;
    pImpl_->touch(list_it);
    return true;
}

void LRUCache::put(const std::string& key, const std::string& value) {
    auto it = pImpl_->index.find(key);
    if (it != pImpl_->index.end()) {
        // Update existing & move to front
        auto list_it = it->second;
        list_it->value = value;
        pImpl_->touch(list_it);
        return;
    }

    // Insert new at front
    pImpl_->lru.push_front(Impl::Node{key, value});
    pImpl_->index[key] = pImpl_->lru.begin();

    // Evict if over capacity
    if (pImpl_->index.size() > pImpl_->capacity) {
        auto& tail = pImpl_->lru.back();
        pImpl_->index.erase(tail.key);
        pImpl_->lru.pop_back();
    }
}

bool LRUCache::erase(const std::string& key) {
    auto it = pImpl_->index.find(key);
    if (it == pImpl_->index.end()) return false;
    pImpl_->lru.erase(it->second);
    pImpl_->index.erase(it);
    return true;
}

size_t LRUCache::size() const {
    return pImpl_->index.size();
}
