// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "golden_queue_setting.hpp"

#include "netlist/netlist_info_types.hpp"
namespace tt::golden {
golden_queue_setting get_queue_setting_snapshot(
    netlist_program &program, const tt_queue_setting_info &queue_setting_info, const tt_queue_info &queue_info) {
    auto prolog_variable = program.get_variable(queue_setting_info.prolog);
    auto epilog_variable = program.get_variable(queue_setting_info.epilog);
    if ((prolog_variable.type == VARIABLE_TYPE::PARAM) or (epilog_variable.type == VARIABLE_TYPE::PARAM)) {
        log_fatal(
            "queue={} cannot have prologue or epilogue queue_settings which are runtime parameters controlled",
            queue_info.name);
    }
    return golden_queue_setting(
        {.name = queue_info.name,
         .prolog = prolog_variable.value,
         .epilog = epilog_variable.value,
         .zero = program.get_variable(queue_setting_info.zero).value,
         .read_only = program.get_variable(queue_setting_info.read_only).value,
         .rd_ptr_autoinc = program.get_variable(queue_setting_info.rd_ptr_autoinc).value,
         .global_wrptr_autoinc = program.get_variable(queue_setting_info.global_wrptr_autoinc).value,
         .global_rdptr_autoinc = program.get_variable(queue_setting_info.global_rdptr_autoinc).value,
         .rd_ptr_global = program.get_variable(queue_setting_info.rd_ptr_global).value,
         .wr_ptr_global = (queue_info.type == IO_TYPE::RandomAccess)
                              ? program.get_variable(queue_setting_info.wr_ptr_global).value
                              : 0,
         .rd_ptr_local =
             (queue_info.type == IO_TYPE::Queue) ? program.get_variable(queue_setting_info.rd_ptr_local).value : 0,
         .saved_q_info = queue_setting_info});
}

bool operator!=(const golden_queue_setting &lhs, const golden_queue_setting &rhs) { return !(lhs == rhs); }

bool operator==(const golden_queue_setting &lhs, const golden_queue_setting &rhs) {
    return (lhs.name.compare(rhs.name) == 0) && (lhs.prolog == rhs.prolog) && (lhs.epilog == rhs.epilog) &&
           (lhs.zero == rhs.zero) && (lhs.read_only == rhs.read_only) && (lhs.rd_ptr_autoinc == rhs.rd_ptr_autoinc) &&
           (lhs.global_wrptr_autoinc == rhs.global_wrptr_autoinc) && (lhs.global_rdptr_autoinc == rhs.global_rdptr_autoinc) &&
           (lhs.rd_ptr_global == rhs.rd_ptr_global) && (lhs.wr_ptr_global == rhs.wr_ptr_global) &&
           (lhs.rd_ptr_local == rhs.rd_ptr_local) && (lhs.saved_q_info == rhs.saved_q_info);
}
ostream& operator<<(ostream& os, const golden_queue_setting& t) {
    os << "golden_queue_setting{";
    os << " .name = " << t.name << ",";
    os << " .prolog = " << t.prolog << ",";
    os << " .epilog = " << t.epilog << ",";
    os << " .zero = " << t.zero << ",";
    os << " .rd_ptr_autoinc = " << t.rd_ptr_autoinc << ",";
    os << " .global_wrptr_autoinc = " << t.global_wrptr_autoinc << ",";
    os << " .global_rdptr_autoinc = " << t.global_rdptr_autoinc << ",";
    os << " .rd_ptr_local = " << t.rd_ptr_local << ",";
    os << " .rd_ptr_global = " << t.rd_ptr_global << ",";
    os << " .wr_ptr_global = " << t.wr_ptr_global << ",";
    os << " .read_only = " << t.read_only;
    os << "}";
    return os;
}
}  // namespace tt::golden
