// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_queue_ptr.hpp"

#include <cstdio>

tt_queue_ptr::tt_queue_ptr(uint32_t islots) :
    ptrs({0, 0}), rd_ptr(*reinterpret_cast<uint16_t*>(&ptrs[0])), wr_ptr(*reinterpret_cast<uint16_t*>(&ptrs[1])), slots(islots), limit(2 * islots) {}

void tt_queue_ptr::incr_rd(uint32_t amount){
    rd_ptr = (rd_ptr + amount) % limit;
}

void tt_queue_ptr::incr_wr(uint32_t amount){
    wr_ptr = (wr_ptr + amount) % limit;
}

bool tt_queue_ptr::empty_during_batched_read(uint32_t amount){
    if(wr_ptr > rd_ptr) return rd_ptr + amount > wr_ptr;
    else return rd_ptr + amount > limit + wr_ptr;
}
uint32_t tt_queue_ptr::get_wr_ptr() {
    uint32_t tmp = wr_ptr;
    // Wrap the returned pointer into actual buffer range
    if (tmp >= slots)
        tmp = tmp - slots;
    return tmp;
}

uint32_t tt_queue_ptr::get_rd_ptr() {
    uint32_t tmp = rd_ptr;
    // Wrap the returned pointer into actual buffer range
    if (rd_ptr >= slots)
        return tmp - slots;
    return tmp;
}

uint32_t tt_queue_ptr::incr_get_rd(uint32_t amount) {
    uint32_t tmp = get_rd_ptr();
    incr_rd(amount);
    return tmp;
};

uint32_t tt_queue_ptr::incr_get_wr(uint32_t amount) {
    uint32_t tmp = get_wr_ptr();
    incr_wr(amount);
    return tmp;
};

uint32_t tt_queue_ptr::occupancy() {
    if (wr_ptr >= rd_ptr) {
        return wr_ptr - rd_ptr;
    } else {
        return limit + wr_ptr - rd_ptr;
    }
}

uint32_t tt_queue_ptr::free_space() {
    return slots - occupancy();
}

bool tt_queue_ptr::full() {
    bool full = false;
    uint32_t reduced_by_slots;
    if (rd_ptr != wr_ptr) {
        if (wr_ptr > rd_ptr) {
            reduced_by_slots = wr_ptr - slots;
            full = (reduced_by_slots == rd_ptr);
        } else {
            reduced_by_slots = rd_ptr - slots;
            full = (reduced_by_slots == wr_ptr);
        }
    }
    return full;
}

bool tt_queue_ptr::empty() {
    bool empty = (rd_ptr == wr_ptr);
    return empty;
}

uint32_t tt_queue_ptr::get_limit() {
    return limit;
}

tt_queue_ptr &tt_queue_ptr::operator = (const tt_queue_ptr &t) {
    ptrs = t.ptrs;
    rd_ptr = *reinterpret_cast<uint16_t*>(&ptrs[0]);
    wr_ptr = *reinterpret_cast<uint16_t*>(&ptrs[1]);
    slots = t.slots;
    limit = t.limit;
    return *this;
}


// Copy constructor needed for pushing to vectors, maps etc.
tt_queue_ptr::tt_queue_ptr(const tt_queue_ptr& t): ptrs(t.ptrs), rd_ptr(*reinterpret_cast<uint16_t*>(&ptrs[0])), wr_ptr(*reinterpret_cast<uint16_t*>(&ptrs[1])), slots(t.slots), limit(t.limit) {}
