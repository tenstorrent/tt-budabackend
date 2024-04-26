// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "common/cache_lib.hpp"

namespace tt {

tt_recency_cache::tt_recency_cache(uint32_t capacity) : capacity(capacity) {
    last_key = -1;
    last_value = -1;
}

void tt_recency_cache::put_lru(key_t key, value_t value, uint32_t offset) {
    cache_type::iterator it = cache.find(key);
    if (it != cache.end()) {
        update_cache(it);
    } else {
        if (cache.size() == capacity) {
            cache.erase(list.back());
            list.pop_back();
        }
        list.push_front(key);
    }
    last_key = key;
    last_value = value;
    cache[key] = {value, list.begin()};
}

void tt_recency_cache::put_mru_bt(key_t key, value_t value, uint32_t offset) {
    cache_type::iterator it = cache.find(key);
    if (it != cache.end()) {
        update_cache(it);
    } else {
        if (cache.size() == capacity) {
            cache.erase(*std::next(list.begin(), offset));
            list.erase(std::next(list.begin(), offset));
            //lru_list.pop_front();
        }
        list.push_front(key);
    }
    last_key = key;
    last_value = value;
    cache[key] = {value, list.begin()};
}
int tt_recency_cache::get(key_t key, bool preload) {
    if(enable_profiler) {
        profiler.num_cache_accesses++;
        if(std::find(profiler.unique_cache_keys.begin(), profiler.unique_cache_keys.end(), key) == profiler.unique_cache_keys.end()) {
            profiler.unique_cache_keys.push_back(key);
            profiler.total_unique_cache_keys++;
        }
        if(preload) {
            profiler.num_preloads++;
        }
    }
    
    if (key == last_key)
        return last_value;

    cache_type::iterator it = cache.find(key);
    if (it != cache.end()) {
        update_cache(it);
        last_key  = key;
        last_value = it->second.first;
        return last_value;
    }
    if(enable_profiler) {
        profiler.cache_misses++;
    }
    return -1;
}

void tt_recency_cache::update_cache(cache_type::iterator it) {
    key_t key = it->first;
    list.erase(it->second.second);
    list.push_front(key);
    it->second.second = list.begin();
}

template <typename K, typename V>
tt_fifo_cache<K,V>::tt_fifo_cache(uint32_t capacity) : capacity(capacity) {
    last_key = -1;
    last_value = -1;
}

template <typename K, typename V>
int tt_fifo_cache<K,V>::get(key_t key) {
    if (key == last_key)
        return last_value;
    if (std::find(cache.begin(), cache.end(), key) != cache.end()) {
        return value_map[key];
    }
    return -1;
}

template <typename K, typename V>
void tt_fifo_cache<K,V>::put(key_t key, value_t value) {
    if (std::find(cache.begin(), cache.end(), key) == cache.end()) {
        if (cache.size() == capacity)
            cache.pop_front();
        cache.push_back(key);
        value_map[key] = value;
    }
    last_key = key;
    last_value = value;
}

template class tt_fifo_cache<std::string, int>;

} // namespace tt