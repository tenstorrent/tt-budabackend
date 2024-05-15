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
// tt_queue - Generic model of a FIFO access memory in DRAM
////////////////

class tt_queue : public tt_io<std::shared_ptr<tt_tensor>> {
   public:
    typedef tt_io::tt_io_entry queue_entry;

    tt_queue(){};
    tt_queue(int entries, tt_queue_info q_info, string name) {
        log_assert(entries > 0, "Expected a positive number of entries when initializing queue");
        this->entries = entries;
        this->name = name;
        this->memory = std::vector<queue_entry>(entries);
        this->rd_ptr_autoinc = 1;        // Default rd_ptr_auto_inc to 1 since we want to increment on default
        this->global_rdptr_autoinc = 0;  // Default rd_ptr_auto_inc to 1 since we want to increment on default
        this->type = tt::IO_TYPE::Queue;
        for (auto &slot : this->memory) {
            slot = std::make_shared<tt_tensor>(get_tensor_metadata_from_tt_queue_info(q_info, false));
        }
    };

    void push(queue_entry in);
    void pop();
    void pop_n(int n);
    queue_entry get();

    void set_zero(const bool &zero);
    void clear();

    //! Meant for traversal of structure without popping the queue
    void set_local_rd_ptr(int ptr);
    int get_local_rd_ptr();
    void reset_local_rd_ptr();
    void backdoor_wrap_global_wr_ptr();

    //! Following APIs don't exist for queues. Will error
    void set_local_wr_ptr(int ptr);
    int get_local_wr_ptr();
    void reset_local_wr_ptr();
    void set_global_wr_ptr(int ptr);
    int get_global_wr_ptr();

    //! Only global read calls
    void set_global_rd_ptr(int ptr);
    int get_global_rd_ptr();

    ~tt_queue();

   private:
    bool full() const;
    bool empty() const;
    void update_from_base(int ptr);
    void update_to_base(int ptr);
};
}  // namespace tt