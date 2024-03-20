// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <ostream>
#include <string>

#include "common/base.hpp"
#include "device/tt_cluster_descriptor.h"
#include "model/tensor.hpp"
#include "netlist_basic_info_types.hpp"
#include "netlist_fused_op_info_types.hpp"
#include "netlist_op_info_types.hpp"


struct tt_queue_setting_info {
    string name = "";
    string prolog = "false";
    string epilog = "false";
    string zero = "false";
    string rd_ptr_autoinc = ""; 
    string global_wrptr_autoinc = "";
    string global_rdptr_autoinc = "";  // Defaults are derived in next stage of netlist_parser
    string rd_ptr_local = "";
    string rd_ptr_global = "";
    string wr_ptr_global = "";
    string read_only = "false";
};
bool operator==(tt_queue_setting_info const& lhs, tt_queue_setting_info const& rhs);
bool operator!=(tt_queue_setting_info const& lhs, tt_queue_setting_info const& rhs);
ostream& operator<<(ostream& out, const tt_queue_setting_info& t);
void verify(const tt_queue_setting_info& t);

enum class INSTRUCTION_OPCODE {
    Var,
    StaticVar,
    Param,
    VarInst,
    AllocateQueue,
    DeallocateQueue,
    Loop,
    EndLoop,
    Execute,
    EndProgram,
    Invalid,
};
ostream& operator<<(ostream& os, const INSTRUCTION_OPCODE& t);
INSTRUCTION_OPCODE get_instruction_opcode_enum(const string& t);

enum class VAR_INSTRUCTION_OPCODE {
    Set,
    Add,
    Mul,
    Inc,
    IncWrap,
    Invalid,
};
ostream& operator<<(ostream& os, const VAR_INSTRUCTION_OPCODE& t);
VAR_INSTRUCTION_OPCODE get_var_instruction_opcode_enum(const string& t);

struct tt_instruction_info {
    INSTRUCTION_OPCODE opcode = INSTRUCTION_OPCODE::Invalid;
    string graph_name = "";
    VAR_INSTRUCTION_OPCODE varinst_opcode = VAR_INSTRUCTION_OPCODE::Invalid;
    string loop_count = "";
    std::vector<std::pair<string, int>> vars = {};
    vector<tt_queue_setting_info> queue_settings = {};
    bool is_declaration = false;
};
ostream& operator<<(ostream& out, const tt_instruction_info& t);
void verify(const tt_instruction_info& t);

enum class QUEUE_LOCATION {
    DRAM,
    HOST,
    INVALID,
};
ostream& operator<<(ostream& os, const QUEUE_LOCATION& t);
QUEUE_LOCATION get_queue_location_enum(const string& t);

ostream& operator<<(ostream& os, const IO_LAYOUT& t);
IO_LAYOUT get_queue_layout_enum(const string& t);


ostream& operator<<(ostream& os, const IO_TYPE& t);
IO_TYPE get_io_type_enum(const string& t);
struct tt_queue_info {
    string name = "";
    string input = "";
    std::vector<int> buf_size = {};
    int target_device = -1;
    int entries = -1;
    tt_tm_info input_tm_ops = {};

    tt::tt_grid_shape grid_size = {};

    int input_count = -1;
    tt_dim_info dim = {};

    DataFormat data_format = DataFormat::Invalid;
    TileDim tile_dim = TileDim::Default;
    IO_TYPE type = IO_TYPE::Invalid;
    QUEUE_LOCATION loc = QUEUE_LOCATION::INVALID;
    std::vector<tt_queue_allocation_info> alloc_info;  // Per core of grid_size.r*grid_size.c and row major in mapping
    chip_id_t src_device_id = -1;
    IO_LAYOUT layout = IO_LAYOUT::Tilized; // Default to tilized layout unless specified
    string alias = "";
};
ostream& operator<<(ostream& out, const tt_queue_info& t);
void verify(const tt_queue_info& t);
int get_mblock_size_tiles(const tt_queue_info& queue_info);
int get_entry_size_in_tiles(const tt_queue_info& queue_info);
int get_entry_size_in_bytes(const tt_queue_info& queue_info, const bool include_header_padding);
int get_tensor_size_in_bytes(const tt_queue_info& queue_info, const bool include_header_padding);
int get_tensor_size_in_elements(const tt_queue_info& queue_info);
int get_tile_dim_x(const tt_queue_info& queue_info);
int get_tile_dim_y(const tt_queue_info& queue_info);
tt::tt_dram_io_desc get_tt_dram_io_desc_from_tt_queue_info(const tt_queue_info& queue_info);
tt_queue_info get_tt_queue_info_from_tt_dram_io_desc(const tt::tt_dram_io_desc& queue_desc);
tt_tensor_metadata get_tensor_metadata_for_queue(const tt_dram_io_desc& q_desc);
tt_tensor_metadata get_tensor_metadata_from_tt_queue_info(const tt_queue_info& q_info, const bool batched);
std::uint32_t get_queue_dram_channel(const tt_queue_info& queue_info, int entry_r, int entry_c);

//! Holds the minimum amount of information to specify the fused ops.
struct tt_graph_info {
    string name = "";
    int target_device = -1;
    int input_count = -1;
    std::map<string, tt_op_info> op_map = {};
};
ostream& operator<<(ostream& out, const tt_graph_info& t);
void verify(const tt_graph_info& t);

struct tt_program_info {
    string name = "";
    vector<tt_instruction_info> program_trace = {};
};
ostream& operator<<(ostream& out, const tt_program_info& t);
void verify(const tt_program_info& t);

struct tt_device_info {
    tt::ARCH arch = tt::ARCH::Invalid;
};
ostream& operator<<(ostream& out, const tt_device_info& t);
void verify(const tt_device_info& t);
