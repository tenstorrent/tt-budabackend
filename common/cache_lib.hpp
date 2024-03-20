// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "utils/logger.hpp"
#include "common/env_lib.hpp"

#include <algorithm>
#include <deque>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <functional>
namespace tt {

class cache_profiler {
    public:
        uint32_t cache_misses = 0;
        uint32_t num_cache_accesses = 0;
        uint32_t total_unique_cache_keys = 0;
        uint32_t num_preloads = 0;
        std::vector<std::string> unique_cache_keys = {};
};

// Cache replacement policy - FIFO
//
template <typename K, typename V>
class tt_fifo_cache {
   private:
    using key_t = K;
    using value_t = V;

    uint32_t capacity;
    std::deque<key_t> cache;
    std::unordered_map<key_t, value_t> value_map;

    // shortcut for last entry lookup,
    // due to loops, a program tends to reuse the last entry
    key_t last_key;
    value_t last_value;

   public:
    tt_fifo_cache(uint32_t capacity);
    int get(key_t key);
    void put(key_t key, value_t value);
};

// Cache replacement policy - Least recently used
//

class tt_recency_cache {
    protected:
     using key_t = std::string;
     using value_t = int;
     using value_entry = std::pair<value_t, std::list<key_t>::iterator>;
     using cache_type = std::unordered_map<key_t, value_entry>;
    
     cache_type cache;
     std::list<key_t> list;
    
     // shortcut for last entry lookup,
     // due to loops, a program tends to reuse the last entry
     key_t last_key;
     value_t last_value;

     void update_cache(cache_type:: iterator it);
     bool enable_profiler = tt::parse_env("TT_BACKEND_BINARY_CACHE_PROFILER_EN", false);

     public:
     uint32_t capacity;
     tt_recency_cache(uint32_t capacity);
     int get(key_t key, bool preload = false);
     void put_lru(key_t key, value_t value, uint32_t offset);
     void put_mru_bt(key_t key, value_t value, uint32_t offset);
     cache_profiler profiler;

};

// Cache replacement policy - User initiated clear & destroy
//
template <typename T>
class tt_object_cache {
    static std::mutex _mutex;
    static bool disable_cache;

    // Allow multi-threaded set & get to cached objects, must guard accesses
    template <typename Func>
    static inline auto _safe(Func func) {
        std::lock_guard<std::mutex> lock(_mutex);
        return func();
    }

   public:
    static void set(std::string tag, T* thing, bool override = false);
    static T* get(std::string tag);
    static bool exists(std::string tag);

    // clears mappings only
    static void clear();
    static void clear(std::string tag);

    // clears mappings and destroys objects
    static void destroy();
    static void destroy(std::string tag);

   private:
    static std::unordered_map<std::string, T*> s_map;
};

template <typename T>
std::mutex tt_object_cache<T>::_mutex;

template <typename T>
bool tt_object_cache<T>::disable_cache = std::getenv("TT_BACKEND_DISABLE_OBJECT_CACHE") ? true : false;

template <typename T>
void tt_object_cache<T>::set(std::string tag, T* thing, bool override) {
    _safe([&] {
        if (disable_cache) return;
        if (!override && (s_map.find(tag) != s_map.end())) {
            std::string obj_type = typeid(T).name();
            log_fatal("Object of type '{}' with tag '{}' already in object cache, try override flag instead", obj_type, tag);
        }
        s_map[tag] = thing;
        // should we destroy the object being overwritten, ie. delete s_map[tag]
    });
}

template <typename T>
T* tt_object_cache<T>::get(std::string tag) {
    return _safe([&] {
        auto iter = s_map.find(tag);
        if (iter == s_map.end()) {
            std::string obj_type = typeid(T).name();
            log_assert(iter != s_map.end(), "Object of type '{} with tag '{}' not found in object cache!", obj_type, tag);
        }
        return iter->second;
    });
}

template <typename T>
bool tt_object_cache<T>::exists(std::string tag) {
    return _safe([&] {
        auto iter = s_map.find(tag);
        if (iter == s_map.end())
            return false;
        return true;
    });
}

template <typename T>
void tt_object_cache<T>::clear() {
    return _safe([&] { s_map.clear(); });
}

template <typename T>
void tt_object_cache<T>::clear(std::string tag) {
    return _safe([&] { s_map.erase(tag); });
}

template <typename T>
void tt_object_cache<T>::destroy() {
    return _safe([&] {
        for (auto& pair : s_map) {
            if (pair.second)
                delete pair.second;
        }
        s_map.clear();
    });
}

template <typename T>
void tt_object_cache<T>::destroy(std::string tag) {
    return _safe([&] {
        auto iter = s_map.find(tag);
        if (iter != s_map.end()) {
            if (s_map[tag])
                delete s_map[tag];
        }
        s_map.erase(tag);
    });
}

template <typename T>
std::unordered_map<std::string, T*> tt_object_cache<T>::s_map;

} // namespace tt