// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "netlist/netlist_program.hpp"

namespace tt::golden {
//! Meant to save the snapshot of queue settings
struct golden_queue_setting {
    std::string name = "";
    int prolog = 0;
    int epilog = 0;
    int zero = 0;
    int read_only = 0;
    int rd_ptr_autoinc = -1;
    int global_wrptr_autoinc = -1;
    int global_rdptr_autoinc = -1;
    int rd_ptr_global = -1;
    int wr_ptr_global = -1;
    int rd_ptr_local = -1;
    tt_queue_setting_info saved_q_info = {};
};
bool operator!=(const golden_queue_setting &lhs, const golden_queue_setting &rhs);
bool operator==(const golden_queue_setting &lhs, const golden_queue_setting &rhs);
ostream& operator<<(ostream& out, const golden_queue_setting& t);

golden_queue_setting get_queue_setting_snapshot(
    netlist_program &program, const tt_queue_setting_info &queue_setting_info, const tt_queue_info &queue_info);
}  // namespace tt::golden
