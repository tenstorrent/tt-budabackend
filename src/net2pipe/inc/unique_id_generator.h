// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "assert.hpp"
#include "net2pipe_common.h"

namespace n2p {
constexpr std::uint64_t START_UNIQUE_ID = 100000000000ULL;
constexpr std::uint64_t UNIQUE_ID_ALIGN = 1000000000;
static_assert(
    UNIQUE_ID_ALIGN < static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
        UNIQUE_ID_ALIGN < static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()),
    "UNIQUE_ID_ALIGN must fit inside int32_t until size_tiles type containers (e.g. in size lib and buffer info) can "
    "be updated to use larger types");
// Subtracting UNIQUE_ID_ALIGN from max mod UNIQUE_ID_ALIGN incase that max is "in the middle" of the range of scatter
// offsets, in which case it wouldn't actually be a valid base ID
constexpr std::uint64_t MAX_BASE_UNIQUE_ID = std::numeric_limits<std::uint64_t>::max() -
                                             (std::numeric_limits<std::uint64_t>::max() % UNIQUE_ID_ALIGN) -
                                             UNIQUE_ID_ALIGN;

static_assert(MAX_BASE_UNIQUE_ID % UNIQUE_ID_ALIGN == 0, "MAX_BASE_UNIQUE_ID computed incorrectly");
class UniqueIdGenerator {
   public:
    UniqueIdGenerator() = default;
    std::uint64_t get_next_unique_id(int align, int id_block = 1) {
        std::scoped_lock lock(this->get_next_id_mutex);
        return get_next_unique_id_no_lock(align, id_block);
    }

    std::uint64_t get_next_unique_id_no_lock(int align, int id_block = 1) {
        log_assert(id_block <= n2p::UNIQUE_ID_ALIGN, "id_block must be <= UNIQUE_ID_ALIGN");

        if ((this->next_unique_id % align) != 0) {
            next_unique_id = ((this->next_unique_id / align) + 1) * align;
        }
        std::uint64_t result = this->next_unique_id;
        this->next_unique_id += id_block;
        if (this->next_unique_id >= n2p::MAX_BASE_UNIQUE_ID) {
            ERROR("Net2Pipe ran out of unique IDs to assign!");
        }

        return result;
    }

    private:
    // Declare the copy constructor and copy assignment operator as private
    UniqueIdGenerator(const UniqueIdGenerator& other);
    UniqueIdGenerator& operator=(const UniqueIdGenerator& other);

    std::mutex get_next_id_mutex;
    std::uint64_t next_unique_id = START_UNIQUE_ID;
};

class DeterministicKeyMap {
   public:
    DeterministicKeyMap() = default;
    std::uint64_t get_deterministic_key(std::uint64_t key) const {
        std::uint64_t index = key % n2p::UNIQUE_ID_ALIGN;
        std::uint64_t map_key = key - index;
        return deterministic_key_map.at(map_key) + index;
    }

    void insert(UniqueIdGenerator& id_gen, std::uint64_t key) {
        std::uint64_t index = key % n2p::UNIQUE_ID_ALIGN;
        std::uint64_t map_key = key - index;
        auto it = deterministic_key_map.find(index);

        if (it == deterministic_key_map.end()) {
            deterministic_key_map[map_key] = id_gen.get_next_unique_id_no_lock(n2p::UNIQUE_ID_ALIGN);;
        }
    }

   private:
    // Declare the copy constructor and copy assignment operator as private
    DeterministicKeyMap(const UniqueIdGenerator& other);
    DeterministicKeyMap& operator=(const UniqueIdGenerator& other);
    std::unordered_map<std::uint64_t, std::uint64_t> deterministic_key_map;
};
}  // namespace n2p