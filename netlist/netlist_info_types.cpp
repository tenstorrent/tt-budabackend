// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "netlist_info_types.hpp"

#include "netlist_utils.hpp"
#include "common/size_lib.hpp"
#include "utils/logger.hpp"
using namespace std;


bool operator==(tt_queue_setting_info const& lhs, tt_queue_setting_info const& rhs) {
    return (lhs.name.compare(rhs.name) == 0) && (lhs.prolog.compare(rhs.prolog) == 0) &&
           (lhs.epilog.compare(rhs.epilog) == 0) && (lhs.zero.compare(rhs.zero) == 0) &&
           (lhs.rd_ptr_autoinc.compare(rhs.rd_ptr_autoinc) == 0) &&
           (lhs.global_wrptr_autoinc.compare(rhs.global_wrptr_autoinc) == 0) &&
           (lhs.global_rdptr_autoinc.compare(rhs.global_rdptr_autoinc) == 0) &&
           (lhs.rd_ptr_local.compare(rhs.rd_ptr_local) == 0) && (lhs.rd_ptr_global.compare(rhs.rd_ptr_global) == 0) &&
           (lhs.wr_ptr_global.compare(rhs.wr_ptr_global) == 0) && (lhs.read_only.compare(rhs.read_only) == 0);
}
bool operator!=(tt_queue_setting_info const& lhs, tt_queue_setting_info const& rhs) { return !(lhs == rhs); }
ostream& operator<<(ostream& os, const tt_queue_setting_info& t) {
    os << "tt_queue_setting_info{";
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
void verify(const tt_queue_setting_info& t) {
    if (t.rd_ptr_global == "")
        log_fatal("Invalid tt_queue_setting_info::rd_ptr_global parsed from netlist");
}

INSTRUCTION_OPCODE get_instruction_opcode_enum(const string& t) {
    if (t == "var") {
        return INSTRUCTION_OPCODE::Var;
    } else if (t == "staticvar") {
        return INSTRUCTION_OPCODE::StaticVar;
    } else if (t == "param") {
        return INSTRUCTION_OPCODE::Param;
    } else if (t == "varinst") {
        return INSTRUCTION_OPCODE::VarInst;
    } else if (t == "allocate_queue") {
        return INSTRUCTION_OPCODE::AllocateQueue;
    } else if (t == "deallocate_queue") {
        return INSTRUCTION_OPCODE::DeallocateQueue;
    } else if (t == "execute") {
        return INSTRUCTION_OPCODE::Execute;
    } else if (t == "loop") {
        return INSTRUCTION_OPCODE::Loop;
    } else if (t == "endloop") {
        return INSTRUCTION_OPCODE::EndLoop;
    } else if (t == "endprogram") {
        return INSTRUCTION_OPCODE::EndProgram;
    } else {
        return INSTRUCTION_OPCODE::Invalid;
    }
}
ostream& operator<<(ostream& os, const INSTRUCTION_OPCODE& t) {
    switch (t) {
        case INSTRUCTION_OPCODE::Var: os << "INSTRUCTION_OPCODE::Var"; break;
        case INSTRUCTION_OPCODE::StaticVar: os << "INSTRUCTION_OPCODE::StaticVar"; break;
        case INSTRUCTION_OPCODE::Param: os << "INSTRUCTION_OPCODE::Param"; break;
        case INSTRUCTION_OPCODE::VarInst: os << "INSTRUCTION_OPCODE::VarInst"; break;
        case INSTRUCTION_OPCODE::Loop: os << "INSTRUCTION_OPCODE::Loop"; break;
        case INSTRUCTION_OPCODE::EndLoop: os << "INSTRUCTION_OPCODE::EndLoop"; break;
        case INSTRUCTION_OPCODE::Execute: os << "INSTRUCTION_OPCODE::Execute"; break;
        case INSTRUCTION_OPCODE::EndProgram: os << "INSTRUCTION_OPCODE::EndProgram"; break;
        case INSTRUCTION_OPCODE::Invalid: os << "INSTRUCTION_OPCODE::Invalid"; break;
        case INSTRUCTION_OPCODE::AllocateQueue : os << "INSTRUCTION_OPCODE::AllocateQueue"; break;
        case INSTRUCTION_OPCODE::DeallocateQueue : os << "INSTRUCTION_OPCODE::DeallocateQueue"; break;
        default: break;
    }
    return os;
}

VAR_INSTRUCTION_OPCODE get_var_instruction_opcode_enum(const string& t) {
    if (t == "set") {
        return VAR_INSTRUCTION_OPCODE::Set;
    } else if (t == "add") {
        return VAR_INSTRUCTION_OPCODE::Add;
    } else if (t == "mul") {
        return VAR_INSTRUCTION_OPCODE::Mul;
    } else if (t == "inc") {
        return VAR_INSTRUCTION_OPCODE::Inc;
    } else if (t == "incwrap") {
        return VAR_INSTRUCTION_OPCODE::IncWrap;
    } else {
        return VAR_INSTRUCTION_OPCODE::Invalid;
    }
}
ostream& operator<<(ostream& os, const VAR_INSTRUCTION_OPCODE& t) {
    switch (t) {
        case VAR_INSTRUCTION_OPCODE::Set: os << "VAR_INSTRUCTION_OPCODE::Set"; break;
        case VAR_INSTRUCTION_OPCODE::Add: os << "VAR_INSTRUCTION_OPCODE::Add"; break;
        case VAR_INSTRUCTION_OPCODE::Mul: os << "VAR_INSTRUCTION_OPCODE::Mul"; break;
        case VAR_INSTRUCTION_OPCODE::Inc: os << "VAR_INSTRUCTION_OPCODE::Inc"; break;
        case VAR_INSTRUCTION_OPCODE::IncWrap: os << "VAR_INSTRUCTION_OPCODE::IncWrap"; break;
        case VAR_INSTRUCTION_OPCODE::Invalid: os << "VAR_INSTRUCTION_OPCODE::Invalid"; break;
        default: break;
    }
    return os;
}
ostream& operator<<(ostream& os, const tt_instruction_info& t) {
    os << "tt_instruction_info{";
    os << " .opcode = " << t.opcode << ",";
    os << " .graph_name = " << t.graph_name << ",";
    os << " .varinst_opcode = " << t.varinst_opcode << ",";
    os << " .loop_count = " << t.loop_count << ",";
    if (not t.vars.empty()) {
        os << ", .vars = {";
        for (const auto& var_it : t.vars) {
            os << var_it.first << "=" << var_it.second << ",";
        }
        os << "  }";
    }
    if (not t.queue_settings.empty()) {
        os << ", .queue_settings = {";
        for (const tt_queue_setting_info setting : t.queue_settings) {
            os << setting;
        }
        os << "  }";
    }
    os << "}";
    return os;
}
void verify(const tt_instruction_info& t) {
    if (t.opcode == INSTRUCTION_OPCODE::Invalid)
        log_fatal("Invalid tt_instruction_info::opcode parsed from netlist");
    if ((t.opcode == INSTRUCTION_OPCODE::Loop) && (t.loop_count == ""))
        log_fatal("Invalid tt_instruction_info::loop_count_var parsed from netlist");
    if ((t.opcode == INSTRUCTION_OPCODE::VarInst) && (t.varinst_opcode == VAR_INSTRUCTION_OPCODE::Invalid))
        log_fatal("Invalid tt_instruction_info::varinst_opcode parsed from netlist");
    if (not t.queue_settings.empty()) {
        for (const tt_queue_setting_info setting : t.queue_settings) {
            verify(setting);
        }
    }
}
ostream& operator<<(ostream& os, const tt_queue_info& t) {
    os << "tt_queue_info{";
    os << " .name = " << t.name << ",";
    os << " .input = " << t.input << ",";
    os << " .target_device = " << t.target_device << ",";
    os << " .entries = " << t.entries << ",";
    os << " .grid_size = " << t.grid_size << ",";
    os << " .dim = " << t.dim << ",";
    os << " .input_count = " << t.input_count << ",";
    os << " .data_format = " << t.data_format << ",";
    os << " .type = " << t.type << ",";
    os << " .loc = " << t.loc << ",";
    os << " .alloc_info = " << t.alloc_info << ",";
    os << " .layout = " << t.layout << ",";
    os << " .alias = " << t.alias << ",";
    os << "}";
    return os;
}
void verify(const tt_queue_info& t) {
    if (t.target_device < 0)
        log_fatal("Invalid tt_queue_info::target_device parsed from netlist");
    if (t.entries < 0)
        log_fatal("Invalid tt_queue_info::entries parsed from netlist");
    if (t.grid_size.r < 0)
        log_fatal("Invalid tt_queue_info::grid_size.r parsed from netlist");
    if (t.grid_size.c < 0)
        log_fatal("Invalid tt_queue_info::grid_size.c parsed from netlist");
    verify (t.dim);
    if (t.data_format == DataFormat::Invalid)
        log_fatal("Invalid tt_queue_info::data_format parsed from netlist");
    if (t.type == IO_TYPE::Invalid)
        log_fatal("Invalid tt_queue_info::type parsed from netlist");
    if (t.layout == IO_LAYOUT::Invalid)
        log_fatal("Invalid tt_queue_info::layout parsed from netlist");
    log_assert(
        (t.grid_size.r > 0) and
        (t.grid_size.c > 0),
        "queue={} has invalid grid_size=[{},{}]",
        t.name,
        t.grid_size.r,
        t.grid_size.c
    );
    if ((t.alloc_info.size() != (uint32_t)(t.grid_size.c * t.grid_size.r)))
        log_fatal(
            "Invalid tt_queue_info:: number of allocation info must match number of cores parsed from netlist when in "
            "dram mode");
}
int get_tile_dim_x(const tt_queue_info& queue_info) {
    return tile_dim_to_array(queue_info.tile_dim)[1];
}
int get_tile_dim_y(const tt_queue_info& queue_info) {
    return tile_dim_to_array(queue_info.tile_dim)[0];
}

int get_mblock_size_tiles(const tt_queue_info& queue_info) {
    return tt::size::get_mblock_size_in_tiles(
        queue_info.dim.ublock_ct, queue_info.dim.ublock_rt, queue_info.dim.mblock_m, queue_info.dim.mblock_n);
}
int get_entry_size_in_tiles(const tt_queue_info& queue_info) {
    return tt::size::get_entry_size_in_tiles(
        queue_info.dim.ublock_ct, queue_info.dim.ublock_rt, queue_info.dim.mblock_m, queue_info.dim.mblock_n, queue_info.dim.t);
}
int get_entry_size_in_bytes(const tt_queue_info& queue_info, const bool include_header_padding) {
    return tt::size::get_entry_size_in_bytes(
        queue_info.data_format,
        include_header_padding,
        queue_info.dim.ublock_ct,
        queue_info.dim.ublock_rt,
        queue_info.dim.mblock_m,
        queue_info.dim.mblock_n,
        queue_info.dim.t,
        get_tile_dim_y(queue_info),
        get_tile_dim_x(queue_info));
}
int get_tensor_size_in_elements(const tt_queue_info& queue_info) {
    return tt::size::get_entry_size_in_elements(queue_info.dim.ublock_ct, queue_info.dim.ublock_rt, queue_info.dim.mblock_m, queue_info.dim.mblock_n, queue_info.dim.t, 
               get_tile_dim_y(queue_info), get_tile_dim_x(queue_info)) * queue_info.grid_size.r * queue_info.grid_size.c;
}

int get_tensor_size_in_bytes(const tt_queue_info& queue_info, const bool include_header_padding) {
    return tt::size::get_entry_size_in_bytes(
               queue_info.data_format,
               include_header_padding,
               queue_info.dim.ublock_ct,
               queue_info.dim.ublock_rt,
               queue_info.dim.mblock_m,
               queue_info.dim.mblock_n,
               queue_info.dim.t,
               get_tile_dim_y(queue_info),
               get_tile_dim_x(queue_info)) *
           queue_info.grid_size.r * queue_info.grid_size.c;
}

tt::tt_dram_io_desc get_tt_dram_io_desc_from_tt_queue_info(const tt_queue_info& queue_info) {
    return {
        .queue_name = queue_info.name,
        .bufq_grid_dim_r = static_cast<unsigned int>(queue_info.grid_size.r),
        .bufq_grid_dim_c = static_cast<unsigned int>(queue_info.grid_size.c),
        .bufq_num_slots = static_cast<unsigned int>(queue_info.entries),
        .ublock_rt = static_cast<unsigned int>(queue_info.dim.ublock_rt),
        .ublock_ct = static_cast<unsigned int>(queue_info.dim.ublock_ct),
        .mblock_m = static_cast<unsigned int>(queue_info.dim.mblock_m),
        .mblock_n = static_cast<unsigned int>(queue_info.dim.mblock_n),
        .tile_height = static_cast<unsigned int>(get_tile_dim_y(queue_info)),
        .tile_width = static_cast<unsigned int>(get_tile_dim_x(queue_info)),
        .t = static_cast<unsigned int>(queue_info.dim.t),
        .input_count = static_cast<unsigned int>(queue_info.input_count),
        .bufq_target_format = queue_info.data_format,
        .bufq_start_addr_channel = {},
        .bufq_mapping = {},
        .bufq_entry_size =
            static_cast<unsigned int>(get_entry_size_in_bytes(queue_info, false)),  // size bytes for untilized data
        .io_type = queue_info.type,
        .layout = queue_info.layout,
    };
}

tt_queue_info get_tt_queue_info_from_tt_dram_io_desc(const tt::tt_dram_io_desc& queue_desc) {
    return {
        .name = queue_desc.queue_name,
        .entries = static_cast<int>(queue_desc.bufq_num_slots),
        .grid_size= {
            .r = static_cast<uint32_t>(queue_desc.bufq_grid_dim_r),
            .c = static_cast<uint32_t>(queue_desc.bufq_grid_dim_c),
        },
        .input_count = static_cast<int>(queue_desc.input_count),
        .dim = {
            .t = static_cast<int>(queue_desc.t),
            .ublock_rt = static_cast<int>(queue_desc.ublock_rt),
            .ublock_ct = static_cast<int>(queue_desc.ublock_ct),
            .mblock_m = static_cast<int>(queue_desc.mblock_m),
            .mblock_n = static_cast<int>(queue_desc.mblock_n),
        },
        .data_format = queue_desc.bufq_target_format,
        .tile_dim = get_tile_dim_from_array({static_cast<int>(queue_desc.tile_height), static_cast<int>(queue_desc.tile_width)}),
        .type = queue_desc.io_type,
        .src_device_id = -1,
        .layout = queue_desc.layout,
    };
}

tt_tensor_metadata get_tensor_metadata_from_tt_queue_info(const tt_queue_info& q_info, const bool batched) {
    tt_tensor_metadata md;
    md.shape.rt = q_info.dim.mblock_m * q_info.dim.ublock_rt * q_info.grid_size.r;
    md.shape.ct = q_info.dim.mblock_n * q_info.dim.ublock_ct * q_info.grid_size.c;
    md.shape.z = q_info.dim.t;
    md.shape.w = batched ? q_info.input_count : 1;
    md.data_format = q_info.data_format;
    md.shape.tile_height = get_tile_dim_y(q_info);
    md.shape.tile_width = get_tile_dim_x(q_info);
    log_trace(
        tt::LogNetlist,
        "Queue Info -- mblock_m={}, mblock_n={}, ublock_rt={}, ublock_ct={}, grid_size.r={}, grid_size.c={}, "
        "tile_height={}, tile_width={}",
        q_info.dim.mblock_m,
        q_info.dim.mblock_n,
        q_info.dim.ublock_rt,
        q_info.dim.ublock_ct,
        q_info.grid_size.r,
        q_info.grid_size.c,
        get_tile_dim_y(q_info),
        get_tile_dim_x(q_info));
    log_trace(
        tt::LogNetlist,
        "Translating to Metadata for queue {} (rt={}, ct={}, z={}, w={}, tile_height={}, tile_width={})",
        q_info.name,
        md.shape.rt,
        md.shape.ct,
        md.shape.z,
        md.shape.w,
        md.shape.tile_height,
        md.shape.tile_width);
    return md;
}

tt_tensor_metadata get_tensor_metadata_for_queue(const tt_dram_io_desc& q_desc) {
    tt_tensor_metadata md = get_tensor_metadata_from_tt_queue_info(get_tt_queue_info_from_tt_dram_io_desc(q_desc), false);
    md.shape.z = md.shape.z / (q_desc.hstack_factor * q_desc.vstack_factor);
    md.shape.rt = md.shape.rt * q_desc.vstack_factor;
    md.shape.ct = md.shape.ct * q_desc.hstack_factor;
    return md;
}

std::uint32_t get_queue_dram_channel(const tt_queue_info& queue_info, int entry_r, int entry_c) {
    log_assert(queue_info.loc == QUEUE_LOCATION::DRAM, "Expected queue to be in DRAM.");
    const auto alloc_info_index = entry_r * queue_info.grid_size.c + entry_c;
    const tt_queue_allocation_info& alloc_info = queue_info.alloc_info.at(alloc_info_index);
    auto dram_channel = alloc_info.channel;
    return dram_channel;
}

IO_TYPE get_io_type_enum(const string& t) {
    if (t == "queue") {
        return IO_TYPE::Queue;
    } else if (t == "ram") {
        return IO_TYPE::RandomAccess;
    } else {
        return IO_TYPE::Invalid;
    }
}
ostream& operator<<(ostream& os, const IO_TYPE& t) {
    switch (t) {
        case IO_TYPE::Queue: os << "IO_TYPE::Queue"; break;
        case IO_TYPE::RandomAccess: os << "IO_TYPE::RandomAccess"; break;
        case IO_TYPE::Invalid: os << "IO_TYPE::Invalid"; break;
        default: break;
    }
    return os;
}

QUEUE_LOCATION get_queue_location_enum(const string& t) {
    if (t == "host") {
        return QUEUE_LOCATION::HOST;
    } else if (t == "dram") {
        return QUEUE_LOCATION::DRAM;
    } else {
        return QUEUE_LOCATION::INVALID;
    }
}
ostream& operator<<(ostream& os, const QUEUE_LOCATION& t) {
    switch (t) {
        case QUEUE_LOCATION::DRAM: os << "QUEUE_LOCATION::DRAM"; break;
        case QUEUE_LOCATION::HOST: os << "QUEUE_LOCATION::HOST"; break;
        case QUEUE_LOCATION::INVALID: os << "QUEUE_LOCATION::INVALID"; break;
        default: break;
    }
    return os;
}
ostream& operator<<(ostream& os, const IO_LAYOUT& t) {
    switch (t) {
        case IO_LAYOUT::Tilized: os << "IO_LAYOUT::Tilized"; break;
        case IO_LAYOUT::Flat: os << "IO_LAYOUT::Flat"; break;
        case IO_LAYOUT::Invalid: os << "IO_LAYOUT::Invalid"; break;
        default: break;
    }
    return os;
}
IO_LAYOUT get_queue_layout_enum(const string& t) {
    if (t == "tilized") {
        return IO_LAYOUT::Tilized;
    } else if (t == "flat") {
        return IO_LAYOUT::Flat;
    } else {
        return IO_LAYOUT::Invalid;
    }
}
ostream& operator<<(ostream& os, const tt_graph_info& t) {
    os << "tt_graph_info{";
    os << " .name = " << t.name << ",";
    os << " .target_device = " << t.target_device << ",";
    for (auto it : t.op_map) {
        os << it.second;
    }
    os << "}";
    return os;
}
void verify(const tt_graph_info& t) {
    if (t.target_device < 0)
        log_fatal("Invalid tt_graph_info::target_device parsed from netlist");
    if (t.input_count < 1)
        log_fatal("Invalid tt_graph_info::input_count parsed from netlist");
    for (auto it : t.op_map) {
        verify(it.second);
    }
}

ostream& operator<<(ostream& os, const tt_program_info& t) {
    os << "tt_program_info{";
    os << " .name = " << t.name << ",";
    for (auto it : t.program_trace) {
        os << it << endl;
    }
    os << "}";
    return os;
}
void verify(const tt_program_info& t) {
    if (t.program_trace.empty())
        log_fatal("Invalid tt_program_info::program_trace parsed from netlist");
    for (auto it : t.program_trace) {
        verify(it);
    }
}

ostream& operator<<(ostream& os, const tt_device_info& t) {
    os << "tt_device_info{";
    os << " .arch = " << get_string(t.arch);
    os << "}";
    return os;
}
void verify(const tt_device_info& t) {
    if (t.arch == tt::ARCH::Invalid)
        log_fatal("Invalid tt_device_info::device parsed from netlist");
}
