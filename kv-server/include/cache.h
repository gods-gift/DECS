// include/cache.h
#pragma once

#include <cstddef>
#include <memory>
#include <string>

/**
 * @brief Simple O(1) average-time LRU cache for string key/value pairs.
 *
 * Thread-safety: NOT thread-safe. Wrap with your own mutex if accessed
 * from multiple threads (the server already does this).
 *
 * Semantics:
 *  - get():   returns true on hit and writes value_out; moves entry to MRU.
 *  - put():   inserts or updates key; new/updated entry becomes MRU.
 *  - erase(): removes key if present; returns true if erased.
 *  - size():  number of items currently stored (<= capacity).
 */
class LRUCache {
public:
    /// Construct with a maximum capacity (must be >= 1; 0 is coerced to 1).
    explicit LRUCache(std::size_t capacity);
    ~LRUCache();

    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;

    LRUCache(LRUCache&&) noexcept = default;
    LRUCache& operator=(LRUCache&&) noexcept = default;

    /// Look up key; on hit writes value_out and returns true. Moves entry to MRU.
    bool get(const std::string& key, std::string& value_out);

    /// Insert or update a key/value; moves entry to MRU. May evict LRU on overflow.
    void put(const std::string& key, const std::string& value);

    /// Remove a key if present. Returns true if an item was erased.
    bool erase(const std::string& key);

    /// Current number of items in the cache.
    std::size_t size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};
