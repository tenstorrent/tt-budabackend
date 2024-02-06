// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>

template <class T>
inline void hash_combine(std::size_t& cur_hash, const T& v) {
    std::hash<T> hasher;
    cur_hash ^= hasher(v) + 0x9e3779b9 + (cur_hash << 6) + (cur_hash >> 2);
}