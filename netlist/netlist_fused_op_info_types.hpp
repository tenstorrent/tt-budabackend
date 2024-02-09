// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <ostream>
#include <string>

#include "common/base.hpp"
#include "netlist_basic_info_types.hpp"
#include "tt_backend_api_types.hpp"

using namespace tt;
using tt_tm_info = std::map<int, std::vector<std::pair<std::string, std::vector<int>>>>;
struct tt_scheduled_op_info {
    string name;
    string type;
    std::vector<string> input_names = {};
    string output = {};
    // input key: vector of tm with it's paired vector of args.
    tt_tm_info input_tm_ops = {};
    // START: Specific Attribute Overrides
    // MATMUL
    int m_k = -1;
    int u_kt = -1;
    // UNARY
    Dim vector_mode = Dim::RC;
    bool approximate_mode = false;
    // Reduce related attributes
    Dim reduce_dim = Dim::Invalid;
    int z = 0;  // Reduce z-dim
    ReduceFunc reduce_type = ReduceFunc::Invalid;
    // Relu attributes
    bool relu_en = false;
    float relu_threshold = 0.0f;
    ReluMode relu_mode = ReluMode::None;
    // Dropout related attributes
    float probability = 0.0f;
    float scale = 0.0f;
    int seed = 0;
    // Power related attributes
    int exponent = 0;
    // LRelu related attributes
    float slope = 0.01; // Default for pytorch
    // Quantization ops
    float zero_point = 0.0f;
    // END: Specific Attribute Overrides
    // Output data format for packer  relu config
    DataFormat output_data_format = DataFormat::Invalid;
    // True if accumulation is fp32 and input is of exp type A
    // Enables workaround for bbe bug #1372
    bool cast_dest_fp32_to_fp16_a = false;
    // Output dimensions for net2pipe and buffer calculations
    tt_dim_info output_dim;
    // Pop information -- Signals when to pop the input used, can only be used for intermediate inputs, will pop every
    // iteration
    std::set<string> inputs_to_pop = {};
    // Pop_last information -- Signals when to pop the input used, can only be used for intermediate inputs, will only
    // pop on last iteration
    std::set<string> inputs_to_pop_last = {};
};
ostream& operator<<(ostream& os, const tt_scheduled_op_info& t);

struct tt_schedule_info {
    std::vector<tt_scheduled_op_info> scheduled_ops = {};
};
ostream& operator<<(ostream& os, const tt_schedule_info& t);

struct tt_fused_op_info {
    string name = "";
    int num_inputs = -1;
    int num_intermediates = -1;
    std::string custom_kernel_path = "";
    std::unordered_map<std::string, std::string> input_names_to_rel_names_map = {};
    std::unordered_map<std::string, int> forked_input_names = {};
    std::vector<tt_schedule_info> schedules = {};

    // True if all inputs and intermed formats are the same on fused op
    // In this case we can skip issuing reconfigure commands for the unpacker.
    bool inputs_and_intermed_formats_match = false;

    // True if output and intermed formats are the same on fused op 
    // In this case we can skip issuing reconfigure commands for the packer.
    bool output_and_intermed_format_match = false;

    std::vector<DataFormat> input_data_formats;
    DataFormat accumulation_data_format = DataFormat::Invalid;
    DataFormat intermed_data_format = DataFormat::Invalid;
    DataFormat output_data_format = DataFormat::Invalid;

    DataFormat get_input_data_format(string input_name) const;

    //! Derived and copied from the base op that references this... should not be assigned in fused_op
    //vector<bool> single_tile = {}; // tenstorrent/budabackend#863
    vector<std::pair<int, bool>> kernel_broadcast = {}; // tenstorrent/budabackend#863, also 2182
};

void verify(const tt_scheduled_op_info& t, tt::ARCH arch, const tt_fused_op_info& fused_op_info, bool last_op_in_schedule);
void verify(const tt_schedule_info& t, tt::ARCH arch, const tt_fused_op_info& fused_op_info);

ostream& operator<<(ostream& os, const tt_fused_op_info& t);
void verify(const tt_fused_op_info& t, tt::ARCH arch);
