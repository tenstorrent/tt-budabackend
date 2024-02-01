// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "netlist/netlist_info_types.hpp"
#include "model/tensor.hpp"

namespace tt {
////////////////
// tt_io
////////////////

//! Meant to be a base virtual class for io types.
//!   each implementation needs to implement and check as necessary
template <class Entry>
class tt_io {
   public:
    typedef Entry tt_io_entry;

    string name = "";
    int entries = 0;
    bool prolog = 0;
    bool epilog = 0;
    bool zero = 0;
    bool read_only = 0;
    int global_wrptr_autoinc = 0;
    int rd_ptr_autoinc = 0;
    int global_rdptr_autoinc = 0;
    tt::IO_TYPE type = tt::IO_TYPE::Invalid;

    std::shared_ptr<tt_io> base_io;  // This is only specified if we alias

    tt_io(){};
    tt_io(int entries, tt_queue_info q_info, string name) : entries(entries), name(name) {
        log_assert(entries > 0, "expected a non-zero number of entries when initializing queue");
        memory = std::vector<tt_io_entry>(entries);

        for (auto &slot : this->memory) {
            slot = tt_io_entry();
        }
    };

    virtual void push(tt_io_entry in) = 0;
    virtual tt_io_entry get() = 0;
    virtual void pop() = 0;
    virtual void pop_n(int n) = 0;
    virtual void clear() = 0;

    virtual void set_local_rd_ptr(int ptr) = 0;
    virtual int get_local_rd_ptr() = 0;
    virtual void reset_local_rd_ptr() = 0;
    virtual void set_local_wr_ptr(int ptr) = 0;
    virtual int get_local_wr_ptr() = 0;
    virtual void reset_local_wr_ptr() = 0;
    virtual void set_global_rd_ptr(int ptr) = 0;
    virtual int get_global_rd_ptr() = 0;
    virtual void set_global_wr_ptr(int ptr) = 0;
    virtual void backdoor_wrap_global_wr_ptr() = 0;
    virtual int get_global_wr_ptr() = 0;

    virtual void set_zero(const bool &zero) = 0;
    virtual ~tt_io(){};

   protected:
    std::vector<tt_io_entry> memory = {};
    int local_rd_ptr = 0;         //! Local pointers are meant for device to increment and keep track of reads/writes.
    int local_wr_ptr = 0;         //! Local pointers are meant for device to increment and keep track of reads/writes.
    int local_rd_ptr_base = 0;    //! Local pointers are meant for device to increment and keep track of reads/writes.
    int local_wr_ptr_base = 0;    //! Local pointers are meant for device to increment and keep track of reads/writes.
    int global_rd_ptr = 0;        //! Global pointers are meant for host to device signaling/communication.
    int global_wr_ptr = 0;        //! Global pointers are meant for host to device signaling/communication.
    int num_current_entries = 0;  //! Meant to keep track of how many entries are in the struct.
};

}  // namespace tt