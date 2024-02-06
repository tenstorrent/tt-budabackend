// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "netlist_basic_info_types.hpp"

#include "netlist_utils.hpp"
#include "common/size_lib.hpp"
#include "model/tensor.hpp"
#include "utils/logger.hpp"

using namespace std;

ostream& operator<<(ostream& os, const tt_dim_info& t) {
    os << "tt_dim_info{";
    os << " .t = " << t.t << ",";
    os << " .ublock_rt = " << t.ublock_rt << ",";
    os << " .ublock_ct = " << t.ublock_ct << ",";
    os << " .mblock_m = " << t.mblock_m << ",";
    os << " .mblock_n = " << t.mblock_n << ",";
    os << " .ublock_order = " << t.ublock_order << ",";
    os << "}";
    return os;
}
bool tt_dim_info::operator>(const tt_dim_info& rhs) const {
    return (mblock_m * mblock_n * ublock_ct * ublock_rt * t) >
           (rhs.mblock_m * rhs.mblock_n * rhs.ublock_ct * rhs.ublock_rt * rhs.t);
}
bool tt_dim_info::operator==(const tt_dim_info& rhs) const {
    return (mblock_m == rhs.mblock_m) and (mblock_n == rhs.mblock_n) and (ublock_ct == rhs.ublock_ct) and
           (ublock_rt == rhs.ublock_rt) and (t == rhs.t) and (ublock_order == rhs.ublock_order);
}
bool tt_dim_info::operator!=(const tt_dim_info& rhs) const { return (not(*this == rhs)); }
void verify(const tt_dim_info& t) {
    log_assert(t.t > 0, "t must be set in tt_dim_info");
    log_assert(t.ublock_ct > 0, "ublock_ct must be set in tt_dim_info");
    log_assert(t.ublock_rt > 0, "ublock_rt must be set in tt_dim_info");
    log_assert(t.mblock_m > 0, "mblock_m must be set in tt_dim_info");
    log_assert(t.mblock_n > 0, "mblock_n must be set in tt_dim_info");
    log_assert(t.ublock_order != Dim::Invalid, "mblock_n must be set in tt_dim_info");
}

bool operator==(tt_queue_allocation_info const& lhs, tt_queue_allocation_info const& rhs) {
    return lhs.channel == rhs.channel && lhs.address == rhs.address;
};

bool operator<(tt_queue_allocation_info const& lhs, tt_queue_allocation_info const& rhs) {
    return lhs.channel < rhs.channel && lhs.address < rhs.address;
};

bool operator>(tt_queue_allocation_info const& lhs, tt_queue_allocation_info const& rhs) {
    return lhs.channel > rhs.channel && lhs.address > rhs.address;
};

bool operator==(queue_buffer_info const& lhs, queue_buffer_info const& rhs) {
    return lhs.alloc_info == rhs.alloc_info && lhs.queue_name == rhs.queue_name;
}

ostream& operator<<(ostream& os, const tt_queue_allocation_info& t) {
    os << "tt_queue_allocation_info{";
    os << " .channel = " << t.channel << ",";
    os << " .address = " << t.address << ",";
    os << "}";
    return os;
}
