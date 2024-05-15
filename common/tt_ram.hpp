// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "tt_io.hpp"

namespace tt {
////////////////
// tt_ram - Generic model of a random access memory in DRAM
////////////////

class tt_ram : public tt_io<std::shared_ptr<tt_tensor>> {
   public:
    typedef tt_io::tt_io_entry ram_entry;

    tt_ram(){};
    tt_ram(int entries, tt_queue_info q_info, string name) {
        log_assert(entries > 0, "Expected a positive number of entries when initializing ran");
        this->entries = entries;
        this->name = name;
        this->memory = std::vector<ram_entry>(entries);
        this->global_wrptr_autoinc = 0;
        this->rd_ptr_autoinc = 0;
        this->global_rdptr_autoinc = 0;
        this->type = tt::IO_TYPE::RandomAccess;
        for (auto &slot : this->memory) {
            slot = std::make_shared<tt_tensor>(get_tensor_metadata_from_tt_queue_info(q_info, false));
        }
    };

    void push(ram_entry in);
    ram_entry get();
    void pop();
    void pop_n(int n);
    void set_zero(const bool &zero);
    void clear();

    //! Meant for traversal of structure without popping these should not be implemented for rams and errors on calls.
    void set_local_rd_ptr(int ptr);
    int get_local_rd_ptr();
    void reset_local_rd_ptr();
    void set_local_wr_ptr(int ptr);
    int get_local_wr_ptr();
    void reset_local_wr_ptr();
    void backdoor_wrap_global_wr_ptr();

    //! Only global calls exist for rams
    void set_global_rd_ptr(int ptr);
    int get_global_rd_ptr();
    void set_global_wr_ptr(int ptr);
    int get_global_wr_ptr();

    ~tt_ram();

   private:
    void update_from_base(int ptr);
    void update_to_base(int ptr);
};
}  // namespace tt