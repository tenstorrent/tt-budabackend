// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <boost/container_hash/hash_fwd.hpp>

#include <string>
#include <thread>
#include <boost/functional/hash.hpp>

#include "common/model/hlk_desc.hpp"
#include "model/data_format.hpp"
#include "model/model.hpp"
#include "netlist/netlist.hpp"
#include "perf_lib/perf_descriptor.hpp"


enum class SfpuExecutionThread : uint8_t;
namespace tt {

class tt_hlk_args_descriptor;
class netlist_workload_data;

struct CompilationConfig{
    int op_index;
    perf::PerfDesc perf_desc;
    unordered_set<perf::PerfTriscDecoupleMode> perf_trisc_decouplings;
    bool is_perf_dump_en;
    bool untilize_output;
    bool pack_microblocks;
    bool fp32_dest_acc_en;
    uint32_t relu_config;
    SfpuExecutionThread sfpu_execution_thread;
    bool is_fused_op;
    string graph_name; // proxy for epoch
    int loop_count;
    string hlk_file_name;
    string op_type;
    string op_path;
    string device_name;
    shared_ptr<tt_op> op;
    tt_hlk_desc *hlk_desc;
    const tt_op_info* op_info;
    std::array<uint32_t, 3> kernel_delays;
    std::vector<std::array<uint32_t, 2>> op_input_tile_dims;
    std::array<uint32_t, 2> op_output_tile_dims;
    std::array<std::pair<int, bool>, 16> kernel_broadcasts;
    StochRndMode stoch_rnd_mode;

    friend ostream& operator<<(ostream& os, struct CompilationConfig const & obj) {

        string input_tile_dims_str = "[ ";
        for (int i = 0; i < obj.op_input_tile_dims.size(); i++) {
            string in_tile_dims = "[" + to_string(obj.op_input_tile_dims[i][0]) + "," + to_string(obj.op_input_tile_dims[i][1]) + "]";
            input_tile_dims_str += in_tile_dims;
        }
        input_tile_dims_str += " ]";

        return os
            << "(op_index: "
            << obj.op_index
            << ", is_perf_dump_en: "
            << obj.is_perf_dump_en
            << ", untilize_output: "
            << obj.untilize_output
            << ", pack_microblocks: "
            << obj.pack_microblocks
            << ", fp32_dest_acc_en: "
            << obj.fp32_dest_acc_en
            << ", relu_config: "
            << obj.relu_config
            << ", sfpu_execution_thread: "
            << static_cast<uint8_t>(obj.sfpu_execution_thread)
            << ", is_fused_op: "
            << obj.is_fused_op
            << ", stoch_rnd_mode: "
            << static_cast<uint8_t>(obj.stoch_rnd_mode)
            << ", graph_name: "
            << obj.graph_name
            << ", loop_count: "
            << obj.loop_count
            << ", hlk_file_name: "
            << obj.hlk_file_name
            << ", op_type: "
            << obj.op_type
            << ", op_path: "
            << obj.op_path
            << ", input tile_dims: "
            << input_tile_dims_str
            << ", output tile dims: [ "
            << obj.op->output_tile_dims[0]
            << ", "
            << obj.op->output_tile_dims[1]
            <<"]"
            << ", device: "
            << obj.device_name
            << ")" << endl;
    }
};

using UniqueCompileConfigMap = unordered_map<struct CompilationConfig, vector<struct CompilationConfig>, hash<CompilationConfig> >;

void generate_all_fw(
    const netlist_workload_data& workload,
    string device_name,
    perf::PerfDesc &perf_desc,
    bool compile_for_perf_only,
    string build_dir_path = "build");

void generate_trisc_fw(
    const netlist_workload_data& workload,
    string device_name,
    perf::PerfDesc &perf_desc,
    bool compile_for_perf_only,
    string build_dir_path = "build",
    int num_threads = 1);
void generate_ncrisc_fw(const perf::PerfDesc& perf_desc, const string& arch_name, const std::string &out_dir_path);
void generate_brisc_fw(const perf::PerfDesc &perf_desc, const string& arch_name, const std::string &out_dir_path);
void generate_ncrisc_fw(const perf::PerfDesc &perf_desc, const string& arch_name, const std::string &out_dir_path);
void generate_erisc_fw(const perf::PerfDesc &perf_desc, const std::string &arch_name, const std::string &out_dir_path);
void generate_tile_dim_descriptors(const tt_op* op, const string& out_dir_path);
void generate_data_format_descriptors(tt_op* op, const string& out_dir_path, const std::string &arch_name);
void generate_perf_events_target_inputs(tt_op* op, int epoch, const string& out_dir_path, const perf::PerfDesc& perf_desc);
void generate_loop_count_file(tt_op* op, int epoch, const string& out_dir_path);
void generate_op_info_file(const std::map<string, tt_op_info> &op_map, const string& out_dir_path);
void generate_math_approx_mode_descriptor(tt_op* op, string out_dir_path);
void generate_math_fidelity_descriptor(tt_op* op, string out_dir_path);
void generate_constexpr_hlk_args(const tt_op& op, string out_dir_path);
void generate_struct_init_file(const tt_op& op, const string& out_dir_path, const tt_hlk_args_desc& hlk_args_descriptor);
int get_num_inputs_for_epoch(int epoch);
std::string get_trisc_compile_cmd(const std::string& root, const std::string& device_name, const perf::PerfDesc &perf_desc, const std::string& chlkc_src_dir, int thread_id);
std::string get_trisc_link_cmd(const std::string& root, const std::string& device_name, const std::string& chlkc_src_dir, int thread_id);
void compile_ckernels_for_trisc(const std::string& chlkc_src_dir, int thread_id, const std::string& compile_cmd, const std::string& link_cmd);
void compile_ckernels_for_all_triscs(string device_name, string root, string chlkc_src_dir, const perf::PerfDesc &perf_desc, const string &graph_name);
void generate_perf_resource_decouple_config(const tt_op_info& op_info, string out_dir_path, const perf::PerfDesc& perf_desc, const netlist_workload_data &workload, const string& graph_name);
void generate_kernel_slowdown_config(const tt_op_info& op_info, string out_dir_path, const perf::PerfDesc& perf_desc);
void generate_compile_time_constants_file(
    const tt_op* op,
    const string& op_path,
    bool is_fp32_dest_acc_en,
    bool is_perf_dump_en,
    bool is_untilize_output_en,
    bool is_pack_microblocks_en,
    bool is_pack_l1_acc_en,
    bool is_unpack_math_decoupled_en,
    bool is_math_pack_decoupled_en,
    uint32_t relu_config,
    uint8_t sfpu_execution_thread,
    std::array<uint32_t, 3> kernel_delays,
    StochRndMode stoch_rnd_mode,
    bool is_tilize_input_en);

const std::pair<bool, bool> check_perf_trisc_decouple_exist(
    const string &op_name,
    const perf::PerfDesc &perf_desc);
const unordered_set<perf::PerfTriscDecoupleMode> get_trisc_decouplings_for_op(
    const string &op_name,
    const perf::PerfDesc &perf_desc);
const perf::PerfOverlayDecoupleMode get_overlay_decouplings_for_op(
        const tt_op_info& op_info,
        const perf::PerfDesc& perf_desc);

bool try_load_fw_bin(const string& risc_name, const string& load_bin_dir, const string& root, const string& fw_out_dir);
void try_dump_fw_bin(const string& risc_name, const string& dump_bin_dir, const string& fw_out_dir);
}
namespace std {
    template<>
    struct hash<tt::CompilationConfig> {
        inline size_t operator()(const tt::CompilationConfig& obj) const {

            size_t hash_value = 0;

            // Not included in hash, includes epoch information
            // obj.op_index
            // obj.graph_name
            // obj.op_path
            boost::hash_combine(hash_value, hash<string>{}(obj.hlk_file_name));
            boost::hash_combine(hash_value, hash<string>{}(obj.op_type));
            boost::hash_combine(hash_value, hash<string>{}(obj.device_name));
            // Workaround for bool being set different values (not 1 or 0) wrongly affecting hash value
            boost::hash_combine(hash_value, hash<string>{}(obj.untilize_output ? "true" : "false"));
            boost::hash_combine(hash_value, hash<string>{}(obj.pack_microblocks ? "true" : "false"));
            boost::hash_combine(hash_value, hash<string>{}(obj.fp32_dest_acc_en ? "true" : "false"));
            boost::hash_combine(hash_value, hash<int>{}(obj.relu_config));
            boost::hash_combine(hash_value, hash<uint8_t>{}(static_cast<uint8_t>(obj.sfpu_execution_thread)));
            boost::hash_combine(hash_value, hash<string>{}(obj.is_fused_op ? "true" : "false"));
            boost::hash_combine(hash_value, hash<string>{}(obj.is_perf_dump_en ? "true" : "false"));
            boost::hash_combine(hash_value, hash<int>{}(obj.loop_count));
            boost::hash_combine(hash_value, hash<perf::PerfDesc>{}.hash_for_unique_compile(obj.perf_desc));
            boost::hash_combine(hash_value, hash<std::unordered_set<perf::PerfTriscDecoupleMode>>{}(obj.perf_trisc_decouplings));
            boost::hash_combine(hash_value, hash<tt::tt_hlk_desc>{}(*obj.hlk_desc));
            boost::hash_combine(hash_value, hash<uint32_t>{}(obj.kernel_delays.at(0)));
            boost::hash_combine(hash_value, hash<uint32_t>{}(obj.kernel_delays.at(1)));
            boost::hash_combine(hash_value, hash<uint32_t>{}(obj.kernel_delays.at(2)));
            for (int i = 0; i < obj.op_input_tile_dims.size(); i++) {
                boost::hash_combine(hash_value, hash<uint32_t>{}(obj.op_input_tile_dims[i][0]));
                boost::hash_combine(hash_value, hash<uint32_t>{}(obj.op_input_tile_dims[i][1]));
            }

            boost::hash_combine(hash_value, hash<uint32_t>{}(obj.op_output_tile_dims[0]));
            boost::hash_combine(hash_value, hash<uint32_t>{}(obj.op_output_tile_dims[1]));
            boost::hash_combine(hash_value, hash<uint8_t>{}(static_cast<uint8_t>(obj.stoch_rnd_mode)));

            for (const auto& bcast : obj.kernel_broadcasts) {
                boost::hash_combine(hash_value, hash<uint32_t>{}(bcast.first));
                boost::hash_combine(hash_value, hash<bool>{}(bcast.second));
            }

            return hash_value;
        }
    };

    template<>
    struct equal_to<tt::CompilationConfig> {
        inline bool operator()(const tt::CompilationConfig& lhs, const tt::CompilationConfig& rhs) const {
            return true;
        }
    };
}