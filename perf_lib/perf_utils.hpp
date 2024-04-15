// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <fstream>
#include <string>
#include <unordered_map>
#include "perf_base.hpp"
#include "common/base.hpp"
#include "common/buda_soc_descriptor.h"
#include "netlist/netlist_info_types.hpp"

namespace perf {
    struct core_descriptor;
}

namespace postprocess {

inline string epoch_type_str(tt::EpochType epoch_type) {
    switch (epoch_type) {
        case tt::EpochType::Forward: return "Forward";
        case tt::EpochType::Backward: return "Backward";
        case tt::EpochType::Optimizer: return "Optimizer";
        case tt::EpochType::Control: return "Control";
        default: log_assert(false ,  "unrecognized enum type for EpochType");
    }
    return "Unknown";
}

uint32_t get_tile_size(tt::DataFormat data_format);
std::string get_perf_lib_directory();
void skip_lines(std::ifstream &input_file, int max_num_lines);
uint read_hex_number(std::ifstream &input_file);
uint convert_string_line_to_hex(const std::string& input_number_line_str);
std::unordered_map<std::string, perf::core_descriptor> populate_cores_to_ops_for_graph(const tt_graph_info &graph_info, const buda_SocDescriptor &soc_descriptor);
json get_core_config_json(const std::unordered_map<std::string, perf::core_descriptor> &core_to_op);
uint32_t run_perf_model(const tt::tt_op_model_desc &op_desc, string perf_output_dir);
pair<int, int> get_first_and_last_recorded_input_idx(const perf::core_events &core);

}
