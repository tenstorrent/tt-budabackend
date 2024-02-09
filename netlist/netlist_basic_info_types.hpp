// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <ostream>
#include <string>

#include "common/base.hpp"

using namespace tt;

struct tt_dim_info {
    int t = -1;
    int ublock_rt = -1;
    int ublock_ct = -1;
    int mblock_m = -1;
    int mblock_n = -1;
    Dim ublock_order = Dim::Invalid;
    bool operator>(const tt_dim_info& rhs) const;
    bool operator==(const tt_dim_info& rhs) const;
    bool operator!=(const tt_dim_info& rhs) const;
};

struct tt_fused_subop_input_info {
    std::string id = "";
    tt_dim_info input_dim;
    bool is_forked;
    int c_bcast_factor = 1;
    int r_bcast_factor = 1;
};

struct tt_fused_subop_info {
    std::string id = "";
    std::vector<tt_fused_subop_input_info> inputs;
    tt_dim_info output_dim;
};

ostream& operator<<(ostream& os, const tt_dim_info& t);
void verify(const tt_dim_info& t);

struct tt_queue_allocation_info {
    std::uint32_t channel = 0;
    std::uint32_t address = 0;
    std::int32_t read_port = -1;
    std::int32_t write_port = -1;
};

bool operator==(tt_queue_allocation_info const& lhs, tt_queue_allocation_info const& rhs);
bool operator<(tt_queue_allocation_info const& lhs, tt_queue_allocation_info const& rhs);
bool operator>(tt_queue_allocation_info const& lhs, tt_queue_allocation_info const& rhs);
struct queue_buffer_info {
    string queue_name = "";
    tt_queue_allocation_info alloc_info;
};
bool operator==(queue_buffer_info const& lhs, queue_buffer_info const& rhs);
namespace std {
template <>
struct hash<tt_queue_allocation_info> {
    std::size_t operator()(tt_queue_allocation_info const& o) const {
        std::size_t seed = 0;
        // boost::hash_combine(seed, std::hash<string>{}(o.queue_name));
        boost::hash_combine(seed, std::hash<uint32_t>{}(o.channel));
        boost::hash_combine(seed, std::hash<uint32_t>{}(o.address));

        return seed;
    }
};
template <>
struct hash<queue_buffer_info> {
    std::size_t operator()(queue_buffer_info const& o) const {
        std::size_t seed = 0;
        // boost::hash_combine(seed, std::hash<string>{}(o.queue_name));
        boost::hash_combine(seed, std::hash<string>{}(o.queue_name));
        boost::hash_combine(seed, std::hash<tt_queue_allocation_info>{}(o.alloc_info));

        return seed;
    }
};
}  // namespace std
ostream& operator<<(ostream& out, const tt_queue_allocation_info& t);
void verify(const tt_queue_allocation_info& t);