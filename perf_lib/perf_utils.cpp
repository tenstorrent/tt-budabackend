// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <experimental/filesystem>  // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>
#include "perf_utils.hpp"
#include "create_reports.hpp"
#include "perf_base.hpp"
#include "third_party/json/json.hpp"
#include "common/model/tt_core.hpp"
#include "netlist_utils.hpp"
#include "op_model/op_model.hpp"

namespace fs = std::experimental::filesystem; // see comment above

using namespace perf;
namespace postprocess {

// Same functionality as GET_L1_TILE_SIZE in ckernel_defs.h
uint32_t get_tile_size(tt::DataFormat data_format) {
    log_assert(data_format != tt::DataFormat::Invalid, "invalid data format");

    uint32_t tile_size = 0;
    uint32_t header_and_padding_size = 32;

    if (data_format == DataFormat::Float32 or
        data_format == DataFormat::Int32 or
        data_format == DataFormat::RawUInt32) {
        tile_size = 32 * 32 * 4 + 32;
    
    } else if (data_format == DataFormat::Float16 or
            data_format == DataFormat::Float16_b or
            data_format == DataFormat::UInt16 or
            data_format == DataFormat::RawUInt16) {    
        tile_size = 32 * 32 * 2 + 32;
    
    } else if (data_format == DataFormat::Bfp8 or
            data_format == DataFormat::Bfp8_b) {
        tile_size = 32 * 32 * 1 + 32 + 4*16;
    
    } else if (data_format == DataFormat::Bfp4 or
            data_format == DataFormat::Bfp4_b) {
        tile_size = 32 * 32 / 2 + 32 + 4*16;
    
    } else if (data_format == DataFormat::Bfp2 or
            data_format == DataFormat::Bfp2_b) {
        tile_size = 32 * 32 / 4 + 32 + 4*16;
    } else if ((data_format == DataFormat::Lf8) ||
               (data_format == DataFormat::Fp8_e4m3) ||
               (data_format == DataFormat::Int8) ||
               (data_format == DataFormat::UInt8)) {
        tile_size = 32 * 32 + 32;
    
    } else {
        log_fatal("Tile size for data-format does not exist. Please add the tile size to this list");
    }
    return tile_size;
}

string get_perf_lib_directory() {
    string root;
    if (getenv("BUDA_HOME")) {
        root = getenv("BUDA_HOME");
    } else {
        root = "./";
    }
    string perf_lib_dir = root + "perf_lib/";
    return perf_lib_dir;
}

void skip_lines(ifstream &input_file, int max_num_lines) {
    for (int i = 0; i < max_num_lines; i++) {
        string str;
        std::getline(input_file, str);
    }
}

uint read_hex_number(ifstream &input_file) {
    string event_line;
    char bullet;
    uint event_val;
    if (!std::getline(input_file, event_line)) { log_error("Invalid line read from input file"); exit(-1); }
    std::istringstream iss(event_line);
    iss >> bullet >> std::hex >> event_val;
    return event_val;
}

// Extracts the number in "[KEYWORD]: NUMBER"
uint64_t convert_string_line_to_64b_val(const string& input_number_line_str) {
    uint64_t event_val;
    string identifier;
    std::istringstream iss(input_number_line_str);
    iss >> identifier >> event_val;
    return event_val;
}

// Converts "- 0x..." to an unsigned integer.
uint convert_string_line_to_hex(const string& input_number_line_str) {
    char bullet;
    uint event_val;
    std::istringstream iss(input_number_line_str);
    iss >> bullet >> std::hex >> event_val;
    return event_val;
}


unordered_map<string, core_descriptor> populate_cores_to_ops_for_graph(const tt_graph_info &graph_info, const buda_SocDescriptor &soc_descriptor) {
    unordered_map<string, core_descriptor> core_to_op;
    log_debug(tt::LogPerfInfra, "Generating the cores to ops for graph {}", graph_info.name);

    for (auto &op_it: graph_info.op_map) {
        string op_name = op_it.first;
        const tt_op_info &op_info = op_it.second;
        log_assert(op_name == op_info.name, "op_name {} inside the graph_info does not match the op_name {} inside op_info.", op_name, op_info.name);
        if (netlist_utils::is_non_tensix_op(op_info.type)) {
            continue;
        }
        int grid_shape_x = op_info.grid_size_x();
        int grid_shape_y = op_info.grid_size_y();
        int grid_loc_x = op_info.grid_loc_x();
        int grid_loc_y = op_info.grid_loc_y();
        for (int x = 0; x < grid_shape_x; x++) {
            for (int y = 0; y < grid_shape_y; y++) {
                
                int logical_x = grid_loc_x + x;
                int logical_y = grid_loc_y + y;

                int physical_x = soc_descriptor.worker_log_to_routing_x.at(logical_x);
                int physical_y = soc_descriptor.worker_log_to_routing_y.at(logical_y);
                
                tt_xy_pair physical_core(physical_x, physical_y);
                tt_xy_pair logical_core(logical_x, logical_y);

                core_descriptor core_desc(op_name, op_info.type, physical_core, logical_core);
                core_to_op.insert({core_desc.get_core_str(CoordType::Physical), core_desc});
            }
        }
    }
    return core_to_op;
}

json get_core_config_json(const unordered_map<string, core_descriptor> &core_to_op) {
    json all_cores_config;
    for (const auto &core_it: core_to_op) {
        json single_core_config;
        single_core_config["op-name"] = core_it.second.op_name;
        single_core_config["op-type"] = core_it.second.op_type;
        single_core_config["logical-core-id"] = core_it.second.get_core_str(CoordType::Logical, false);
        all_cores_config[core_it.second.get_core_str(CoordType::Physical)] = single_core_config;
    }
    return all_cores_config;
}

std::set<EpochType> get_unique_epoch_types(vector<EpochType> epoch_types_executed) {
    std::set<EpochType> unique_epoch_tpes;
    for (EpochType epoch_type: epoch_types_executed) {
        if (unique_epoch_tpes.find(epoch_type) == unique_epoch_tpes.end()) {
            unique_epoch_tpes.insert(epoch_type);
        }
    }
    return unique_epoch_tpes;
}

pair<uint64_t, uint64_t> get_earliest_start_and_lastest_end(const json& all_events) {
    uint64_t earliest_start = 0;
    uint64_t latest_end = 0;

    for (auto &core_label: all_events.items()) {
        auto core_label_value = core_label.value();
        if (core_label.key() != "per-epoch-events") {
            if (!core_label_value["NCRISC"].contains("epoch")) {
                // cout << "Generating partial perf vcd file." << endl;
                // gen_vcd_from_perf_dump(gstate, ai_clk, epoch_perf_json_files);
                log_assert(false, "NCRISC epoch event does not exist for core {}", core_label.key() );
            }
            uint64_t start = core_label_value["NCRISC"]["epoch"]["start"].at(0).get<uint64_t>();
            uint64_t end = core_label_value["NCRISC"]["epoch"]["end"].at(0).get<uint64_t>();
            if (earliest_start == 0 or start < earliest_start) {
                earliest_start = start;
            }
            if (end > latest_end) {
                latest_end = end;
            }
        }
    }
    return pair<uint64_t, uint64_t>(earliest_start, latest_end);
}

uint32_t run_perf_model(const tt_op_model_desc& op_desc, string perf_output_dir) {
    log_assert(fs::exists(perf_output_dir), "Perf output directory {} does not exist", perf_output_dir);
    // Save the original streambufs of cout and cerr
    std::streambuf* originalCoutBuffer = std::cout.rdbuf();
    std::streambuf* originalCerrBuffer = std::cerr.rdbuf();

    // Create and open a file stream for redirection
    std::ofstream fileStream(perf_output_dir + "/" + perf_model_log);

    // Redirect cout and cerr to the file stream
    std::cout.rdbuf(fileStream.rdbuf());
    std::cerr.rdbuf(fileStream.rdbuf());

    uint32_t model_execution_cycles = 0;
    try {
        model_execution_cycles = OpModel::get_op_cycles(op_desc);
    } catch (std::exception &e) {}

    // Restore the original streambufs
    std::cout.rdbuf(originalCoutBuffer);
    std::cerr.rdbuf(originalCerrBuffer);

    return model_execution_cycles;
}


pair<int, int> get_first_and_last_recorded_input_idx(const core_events& core) {
    int first_input = -1;
    int last_input = -1;
    for (const auto &[input_idx, events] : core.all_outer_loop_events) {
        if (first_input == -1 or input_idx < first_input) {
            first_input = input_idx;
        }
        if (last_input == -1 or input_idx > last_input) {
            last_input = input_idx;
        }
    }
    return {first_input, last_input};
}

}
