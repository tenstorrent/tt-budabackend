// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

class tt_queue_ptr
{
    private:
    uint32_t limit;
    uint32_t slots;

    public:
    //! phyiscal storage for ptrs
    //  vector guarantees contiguous memory,
    //  useful for physical alignment with device ptrs
    std::vector<uint32_t> ptrs;

    uint16_t& rd_ptr; // references lower 16 bits of ptrs.at(0)
    uint16_t& wr_ptr; // references lower 16 bits of ptrs.at(1)

    tt_queue_ptr(uint32_t islots);

    void incr_rd(uint32_t amount=1);
    void incr_wr(uint32_t amount=1);

    bool empty_during_batched_read(uint32_t amount);
    bool empty();
    bool full();
    uint32_t occupancy();
    uint32_t free_space();

    //! returns in-range ptr
    uint32_t get_wr_ptr();
    uint32_t get_rd_ptr();

    //! incr and returns pre-incr ptr
    uint32_t incr_get_rd(uint32_t amount=1);
    uint32_t incr_get_wr(uint32_t amount=1);

    uint32_t get_limit();
    tt_queue_ptr &operator = (const tt_queue_ptr &t);
    tt_queue_ptr(const tt_queue_ptr& t);

};