// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tt_queue.hpp"

#include "utils/logger.hpp"

namespace tt {
////////////////
// tt_queue
////////////////

void tt_queue::push(tt_queue::queue_entry in) {
    log_assert(!full(), "Writing to a full Queue: {}", name);
    if (global_wr_ptr == (2 * entries)) {
        global_wr_ptr = 0;
    }
    if (global_rd_ptr == (2 * entries)) {
        global_rd_ptr = 0;
    }
    log_debug(tt::LogBackend, "Pushing entry {} to {}", global_wr_ptr, name);
    memory.at(global_wr_ptr % entries) = in;
    update_to_base(global_wr_ptr % entries);
    global_wr_ptr++;
    num_current_entries++;
}

void tt_queue::pop() {
    log_assert(!empty(), "Reading an empty Queue: {}", name);
    log_debug(tt::LogBackend, "Popping entry {} from {}", global_rd_ptr, name);
    if (global_rd_ptr == (2 * entries)) {
        global_rd_ptr = 0;
    }
    global_rd_ptr++;

    if (global_rd_ptr == (2 * entries) and (global_wr_ptr == (2 * entries))) {
        global_wr_ptr = 0;
    }
    num_current_entries--;
}

void tt_queue::pop_n(int n) {
    for (int i = 0; i < n; i++) {
        pop();
    }
}
tt_queue::queue_entry tt_queue::get() {
    log_debug(tt::LogBackend, "Get entry {} to {}", local_rd_ptr, name);
    log_assert(!empty(), "Trying to get entry from  an empty Queue: {}", name);
    if (local_rd_ptr >= (2 * entries)) {
        local_rd_ptr = 0;
    }
    update_from_base(local_rd_ptr % entries);
    auto result = memory.at(local_rd_ptr % entries);
    if (!prolog && !epilog)
        local_rd_ptr += rd_ptr_autoinc;
    return result;
}
//! If this is an aliased IO, then we need to update the current_slot from the base IO
void tt_queue::update_from_base(int ptr) {
    if (base_io) {
        log_fatal("tt_queue={} is aliased with base_io={} -- aliased queues not currently support ",
            this->name,
            base_io->name);
    }
}
//! If this is an aliased IO, then we need to update the current_slot to the base IO
void tt_queue::update_to_base(int ptr) {
    if (base_io) {
        log_fatal("tt_queue={} is aliased with base_io={} -- aliased queues not currently support ",
            this->name,
            base_io->name);
    }
}

void tt_queue::set_zero(const bool &zero) {
    this->zero = zero;
    if (this->zero) {
        for (unsigned int idx = 0; idx < this->memory.size(); idx++) {
            if (this->memory.at(idx) != nullptr) {
                log_debug(tt::LogBackend, "Zeroing idx {} in {}", idx, name);
                this->memory.at(idx)->set_number(0.0f);
            }
        }
    }
}

bool tt_queue::full() const {
    bool result = false;
    if ((global_rd_ptr == 0) && (global_wr_ptr == (2 * entries))) {
        result = true;
    } else if (global_rd_ptr == (global_wr_ptr + 1)) {
        result = true;
    }
    log_debug(
        tt::LogBackend,
        "Queue {}: full()={} -- global_wr_ptr={} global_rd_ptr={} entries={}",
        name,
        result,
        global_wr_ptr,
        global_rd_ptr,
        entries);
    return result;
}
bool tt_queue::empty() const {
    bool result = false;
    if (global_rd_ptr == global_wr_ptr) {
        result = true;
    }
    log_debug(
        tt::LogBackend,
        "Queue {}: empty()={} -- global_wr_ptr={} global_rd_ptr={} entries={}",
        name,
        result,
        global_wr_ptr,
        global_rd_ptr,
        entries);
    return result;
}

void tt_queue::set_local_rd_ptr(int ptr) {
    if ((ptr < 0)) {
        log_fatal(
            "tt_queue::set_local_rd_ptr out of bounds for queue={}, ptr={} has to be within bounds[{}, {}] ",
            name,
            ptr,
            0,
            entries - 1);
    }
    local_rd_ptr = ptr % (2 * entries);
    local_rd_ptr_base = ptr % (2 * entries);
}
void tt_queue::reset_local_rd_ptr() { local_rd_ptr = local_rd_ptr_base; }
int tt_queue::get_local_rd_ptr() { return local_rd_ptr % (2 * entries); }

void tt_queue::set_global_rd_ptr(int ptr) {
    if ((ptr < 0)) {
        log_fatal(
            "tt_queue::set_global_rd_ptr out of bounds for queue={}, ptr={} has to be within bounds[{}, {}] ",
            name,
            ptr,
            0,
            entries - 1);
    }
    global_rd_ptr = ptr % (2 * entries);
}
int tt_queue::get_global_rd_ptr() { return global_rd_ptr % (2 * entries); }
int tt_queue::get_global_wr_ptr() { return global_wr_ptr; }

void tt_queue::backdoor_wrap_global_wr_ptr() { global_wr_ptr = global_rd_ptr; }
void tt_queue::clear() {
    local_rd_ptr = 0;
    local_wr_ptr = 0;
    global_rd_ptr = 0;
    global_wr_ptr = 0;
    local_rd_ptr_base = 0;
    local_wr_ptr_base = 0;
    num_current_entries = 0;
}
tt_queue::~tt_queue() {}

void tt_queue::set_global_wr_ptr(int ptr) {
    log_fatal("tt_queue does not support global write pointer functions for queue={}", name);
}
void tt_queue::set_local_wr_ptr(int ptr) {
    log_fatal("tt_queue does not support local write pointer functions for queue={}", name);
}
int tt_queue::get_local_wr_ptr() {
    log_fatal("tt_queue does not support local write pointer functions for queue={}", name);
    return 0;
}
void tt_queue::reset_local_wr_ptr() {
    log_fatal("tt_queue does not support local write pointer functions for queue={}", name);
}
}  // namespace tt