// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_ram.hpp"

#include "utils/logger.hpp"

namespace tt {
////////////////
// tt_ram
////////////////

void tt_ram::push(tt_ram::ram_entry in) {
    log_assert(global_wr_ptr < entries, "Writing to ram out of bounds, global_wr_ptr= {}", global_wr_ptr);
    log_assert(global_wr_ptr >= 0, "Writing to ram out of bounds, global_wr_ptr= {}", global_wr_ptr);
    log_debug(
        tt::LogBackend,
        "Pushing entry {} to {} -- global_wr_ptr={}, global_rd_ptr={}, entries={}",
        global_wr_ptr,
        name,
        global_wr_ptr,
        global_rd_ptr,
        entries);
    memory.at(global_wr_ptr) = in;
    update_to_base(global_wr_ptr);
    global_wr_ptr = (global_wr_ptr + global_wrptr_autoinc) % entries;
}

tt_ram::ram_entry tt_ram::get() {
    log_assert(global_rd_ptr < entries, "Reading to ram out of bounds, global_rd_ptr= {}", global_rd_ptr);
    log_assert(global_rd_ptr >= 0, "Reading to ram out of bounds, global_rd_ptr= {}", global_rd_ptr);
    log_debug(
        tt::LogBackend,
        "Get entry {} to {} -- global_wr_ptr={}, global_rd_ptr={}, entries={}",
        global_rd_ptr,
        name,
        global_wr_ptr,
        global_rd_ptr,
        entries);
    update_from_base(global_rd_ptr);
    auto result = memory.at(global_rd_ptr);
    if (!prolog && !epilog) {
        global_rd_ptr = (global_rd_ptr + global_rdptr_autoinc) % entries;
    }
    return result;
}

//! If this is an aliased IO, then we need to update the current_slot from the base IO
void tt_ram::update_from_base(int ptr) {
    if (base_io) {
        if (base_io->type == tt::IO_TYPE::Queue) {
            log_fatal("tt_ram={} is aliased with tt_queue::base_io={} -- aliased queues not currently support ",
                this->name,
                base_io->name);
        }
        int num_entries = base_io->entries / this->entries;
        log_assert(
            (base_io->entries % this->entries) == 0,
            "base_io={} entries={} must evenly divide by this io={} entries={}",
            base_io->name,
            base_io->entries,
            this->name,
            this->entries);
        int current_pointer = ptr * num_entries;
        std::vector<tt_io_entry> base_entries(num_entries);
        // Get all the entries from base IO
        if (base_io->type == tt::IO_TYPE::RandomAccess) {
            int initial_base_rd_ptr = base_io->get_global_rd_ptr();
            for (int offset = 0; offset < num_entries; offset++) {
                int rd_ptr = current_pointer + offset;
                base_io->set_global_rd_ptr(rd_ptr);
                base_entries.at(offset) = base_io->get();
            }
            base_io->set_global_rd_ptr(initial_base_rd_ptr);
        }
        // Update the current slot
        tt_dims offset = {0, 0, 0, 0};
        tt_io_entry current_entry = memory.at(ptr);
        for (const auto& base_entry : base_entries) {
            current_entry->fill_with_data(*base_entry, offset);
            offset.z += base_entry->getz();
        }
    }
}
//! If this is an aliased IO, then we need to update the current_slot to the base IO
void tt_ram::update_to_base(int ptr) {
    if (base_io) {
        if (base_io->type == tt::IO_TYPE::Queue) {
            log_fatal("tt_ram={} is aliased with tt_queue::base_io={} -- aliased queues not currently support ",
                this->name,
                base_io->name);
        }
        int num_entries = base_io->entries / this->entries;
        log_assert(
            (base_io->entries % this->entries) == 0,
            "base_io={} entries={} must evenly divide by this io={} entries={}",
            base_io->name,
            base_io->entries,
            this->name,
            this->entries);
        int current_pointer = ptr * num_entries;
        std::vector<tt_io_entry> base_entries(num_entries);
        if (base_io->type == tt::IO_TYPE::RandomAccess) {
            int initial_base_rd_ptr = base_io->get_global_rd_ptr();
            for (int offset = 0; offset < num_entries; offset++) {
                int rd_ptr = current_pointer + offset;
                base_io->set_global_rd_ptr(rd_ptr);
                base_entries.at(offset) = base_io->get();
            }
            base_io->set_global_rd_ptr(initial_base_rd_ptr);
        }
        tt_io_entry current_entry = memory.at(ptr);
        tt_dims start_bounds = {0, 0, 0, 0};
        for (const auto& base_entry : base_entries) {
            tt_dims end_bounds = {
                .rt = start_bounds.rt + base_entry->metadata.shape.rt,
                .ct = start_bounds.ct + base_entry->metadata.shape.ct,
                .z = start_bounds.z + base_entry->metadata.shape.z,
                .w = start_bounds.w + base_entry->metadata.shape.w};
            *base_entry = current_entry->extract_range_to_tensor(start_bounds, end_bounds, false);
            start_bounds.z += base_entry->getz();
        }
    }
}

void tt_ram::set_zero(const bool& zero) {
    this->zero = zero;
    if (this->zero) {
        for (unsigned int idx = 0; idx < this->memory.size(); idx++) {
            if (this->memory.at(idx) != nullptr) {
                log_debug(tt::LogBackend, "Zeroing idx {} in {}", idx, name);
                this->memory.at(idx)->set_number(0.0f);
                update_to_base(idx);
            }
        }
    }
}

void tt_ram::pop() { log_fatal("tt_ram does not support popping for ram={}", name); }
void tt_ram::pop_n(int n) { log_fatal("tt_ram does not support popping for ram={}", name); }

void tt_ram::set_global_rd_ptr(int ptr) {
    if ((ptr >= entries) or (ptr < 0)) {
        log_fatal(
            "tt_ram::set_global_rd_ptr out of bounds for ram={}, ptr={} has to be within bounds[{}, {}] ",
            name,
            ptr,
            0,
            entries - 1);
    }
    global_rd_ptr = ptr;
}
int tt_ram::get_global_rd_ptr() { return global_rd_ptr; }
void tt_ram::set_global_wr_ptr(int ptr) {
    if ((ptr >= entries) or (ptr < 0)) {
        log_fatal(
            "tt_ram::set_global_wr_ptr out of bounds for ram={}, ptr={} has to be within bounds[{}, {}] ",
            name,
            ptr,
            0,
            entries - 1);
    }
    global_wr_ptr = ptr;
}
int tt_ram::get_global_wr_ptr() { return global_wr_ptr; }

void tt_ram::clear() {
    local_rd_ptr = 0;  // unused in tt_ram
    local_wr_ptr = 0;  // unused in tt_ram
    global_rd_ptr = 0;
    global_wr_ptr = 0;
    local_rd_ptr_base = 0;    // unused in tt_ram
    local_wr_ptr_base = 0;    // unused in tt_ram
    num_current_entries = 0;  // unused in tt_ram
}

tt_ram::~tt_ram() {}

void tt_ram::set_local_rd_ptr(int ptr) {
    log_fatal("tt_ram does not support local pointer functions for ram={}", name);
}
int tt_ram::get_local_rd_ptr() {
    log_fatal("tt_ram does not support local pointer functions for ram={}", name);
    return 0;
}
void tt_ram::reset_local_rd_ptr() {
    log_fatal("tt_ram does not support local pointer functions for ram={}", name);
}
void tt_ram::set_local_wr_ptr(int ptr) {
    log_fatal("tt_ram does not support local pointer functions for ram={}", name);
}
int tt_ram::get_local_wr_ptr() {
    log_fatal("tt_ram does not support local pointer functions for ram={}", name);
    return 0;
}
void tt_ram::reset_local_wr_ptr() {
    log_fatal("tt_ram does not support local pointer functions for ram={}", name);
}
void tt_ram::backdoor_wrap_global_wr_ptr() {
    log_fatal("tt_ram does not support backdoor_wrap_global_wr_ptr functions for ram={}", name);
}
}  // namespace tt