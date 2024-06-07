// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <functional>
#include <vector>

#include "common/base.hpp"
#include "common/env_lib.hpp"
#include "device/device_api.h"
#include "runtime_io.hpp"
#include "perf_lib/perf_descriptor.hpp"
#include "runtime_types.hpp"
#include "yaml-cpp/yaml.h"

// IO
using std::ifstream;
using std::ofstream;
using std::stringstream;
using std::cout;

using buda_soc_description = buda_SocDescriptor;

// Forward declare pipegen2 types.
namespace pipegen2 {

class BasePipegen2CompileException;
class BasePipegen2IOException;
struct L1BufferAllocationInfo;
class StreamGraphCollection;

}

namespace perf {
    class MemoryProfiler;
}

namespace tt {

void pause(const string &msg="");
void run_net2pipe(const string &netlist, const string &build_dir_path, const int global_epoch_start,
                  const string &soc_descriptor_path, const string &network_desc_path,
                  tt_overlay_compile_result& overlay_compile_result);
std::unique_ptr<pipegen2::StreamGraphCollection> run_pipegen2(const string &desc_name,
                                                              const string &pipegen_yaml_path,
                                                              const std::string &graph_name,
                                                              const int temporal_epoch,
                                                              const string &blob_yaml_path,
                                                              const uint32_t perf_dump_info,
                                                              const std::unordered_map<chip_id_t, buda_soc_description> &sdesc_per_chip,
                                                              tt_compile_result_per_epoch &compile_result,
                                                              perf::MemoryProfiler* memory_profiler);
void run_blobgen2(const string &desc_name,
                  std::unique_ptr<pipegen2::StreamGraphCollection> stream_graphs,
                  const uint32_t perf_dump_info,
                  const int temporal_epoch,
                  const string &blob_out_dir,
                  tt_compile_result_per_epoch &compile_result,
                  const std::unordered_map<uint32_t, std::unordered_map<chip_id_t, std::string>>& global_epoch_device_to_graph);
void run_pipegen_and_blobgen(const string &build_dir_path, const std::string &graph_name, int temporal_epoch,
                             const std::vector<chip_id_t> &chip_ids, const perf::PerfDesc &perf_desc,
                             const string &desc_name,
                             const std::unordered_map<chip_id_t, buda_soc_description> &sdesc_per_chip,
                             tt_compile_result_per_epoch &compile_result, 
                             perf::MemoryProfiler* memory_profiler,
                             const std::unordered_map<uint32_t, std::unordered_map<chip_id_t, std::string>>& global_epoch_device_to_graph);
void handle_pipegen2_compile_exception(const pipegen2::BasePipegen2CompileException &ex,
                                       const std::string &graph_name,
                                       const int temporal_epoch,
                                       const std::unordered_map<chip_id_t, buda_soc_description> &sdesc_per_chip,
                                       tt_compile_result_per_epoch &compile_result);
tt_cxy_pair get_pipegen2_exception_logical_core(const pipegen2::BasePipegen2CompileException &ex,
                                                const std::unordered_map<chip_id_t, buda_soc_description> &sdesc_per_chip);
void handle_pipegen2_io_exception(const pipegen2::BasePipegen2IOException &ex,
                                  const std::string &graph_name,
                                  const int temporal_epoch,
                                  tt_compile_result_per_epoch &compile_result);
void handle_pipegen2_internal_error(const std::exception &ex,
                                    const std::string &graph_name,
                                    const int temporal_epoch,
                                    tt_compile_result_per_epoch &compile_result);
void populate_common_pipegen_error_info(const std::string &graph_name,
                                        const int temporal_epoch,
                                        tt_compile_result_per_epoch &compile_result);
void profile_pipegen2_data_buffers(const unordered_map<tt_cxy_pair, vector<pipegen2::L1BufferAllocationInfo>> &all_worker_l1_allocations, perf::MemoryProfiler* memory_profiler, int temporal_epoch_id);                       
bool using_arm_host();
void check_system_params(const string &build_dir_path);
std::string get_soc_desc_path(chip_id_t chip, bool runtime_descriptor = false);
void cleanup_stale_soc_descriptors();
std::pair<std::string, std::string> get_git_info();
YAML::Node get_runtime_data(const string &runtime_data_path);

const std::vector<std::vector<std::vector<int>>> default_arch_dram_cores(const tt::ARCH &arch);
const std::vector<std::vector<int>> default_arch_ethernet_cores(const tt::ARCH &arch);

void generate_soc_description(buda_soc_description *default_full_soc_desc_ptr, const std::string &file_path,
                              const std::vector<int> &grid, bool noc_translation_en = false);
void generate_cluster_desc_yaml(const string &build_dir_path);
void print_grayskull_device_description(ofstream &outfile, std::vector<int> grid);
void print_wormhole_device_description(ofstream &outfile, std::vector<int> grid);
void print_device_description(const tt::ARCH &arch, ofstream &outfile, const std::vector<int> &grid);
void print_device_description(ofstream &outfile, const std::vector<int> &grid,
                              const buda_soc_description &soc_descriptor, bool noc_translation_en = false);
std::unique_ptr<buda_soc_description> get_default_soc_desc(const tt::ARCH &arch);
string get_overlay_output_dir(const string& build_dir_path, int temporal_epoch_index);
void generate_sdesc_yaml_for_overlay_compile(std::set<chip_id_t> chips, tt::ARCH arch_name, bool noc_trans_en);
void translate_workers_and_eth(tt_cluster* cluster, const chip_id_t target_device, const std::string& soc_descriptor_path, const std::string& output_dir);
void generate_soc_descriptors_for_compile(const tt_runtime_config& config, bool noc_trans_en, const std::string& default_sdesc_path, chip_id_t chip, int harvesting_mask, const tt::ARCH& arch_name, const std::string& runtime_path, const std::string& overlay_path);
void remove_worker_row_from_descriptor(buda_soc_description* full_soc_descriptor, const vector<int>& row_coordinates_to_remove);
void harvest_rows_in_soc_descriptor(tt::ARCH arch, tt::TargetDevice target_type, chip_id_t chip, uint32_t harvested_rows, const std::string& soc_descriptor_path, const std::string& output_dir);
vector<int> extract_rows_to_remove(const tt::ARCH &arch, const int worker_grid_rows, const int harvested_rows);
// Runtime: Get harvesting info from config object (new) or env-var (legacy - to be phased out)
std::unordered_map<chip_id_t, uint32_t> parse_harvested_rows_from_config(const tt_runtime_config& config, const std::set<chip_id_t>& devices);
// Compile: Get harvesting infro from env-var (legacy - to be phased out)
std::unordered_map<chip_id_t, uint32_t> populate_harvesting_masks_from_env_var(const std::set<chip_id_t>& devices, bool allow_num_masks_less_than_num_devices);
bool using_sw_harvesting(const tt_runtime_config& config);
vector<uint32_t> pad_entry_to_padding_blob(const YAML::Node& pad_entry);

void match_failure_target(std::string& failure_target, const std::string& e);
void populate_compile_result_from_string_net2pipe(tt_overlay_compile_result *result, const std::string& exception_string);
void populate_compile_result_from_string_blobgen(tt_compile_result_per_epoch *result, const std::string& exception_string, const std::unordered_map<uint32_t, std::unordered_map<chip_id_t, std::string>>& global_epoch_device_to_graph);

const perf::PerfOverlayDecoupleMode get_overlay_decouplings_for_op(const tt_op_info& op_info, const perf::PerfDesc& perf_desc);
const uint16_t get_input_output_overlay_decouple_mask(const tt_op_info& op_info, const perf::PerfDesc& perf_desc);
const unordered_set<perf::PerfTriscDecoupleMode> get_trisc_decouplings_for_op(const string& op_name, const perf::PerfDesc& perf_desc);
const std::pair<bool, bool> check_perf_trisc_decouple_exist(const string& op_name, const perf::PerfDesc& perf_desc);
const std::unordered_map<tt_xy_pair, uint16_t> get_input_output_overlay_decouple_mask_graph(const tt_graph_info &graph_info, const perf::PerfDesc &perf_desc, const buda_soc_description &sdesc);
tt_op_model_desc get_perf_model_desc(const tt_op_info &op_info);
void run_netlist_analyzer(tt::ARCH arch, const string &netlist_path, const string &cluster_desc_path, const string &test_output_dir);

std::string get_tt_runtime_hostname();

std::chrono::seconds get_api_timeout(const tt_timeout_api_type &api_type);

bool is_valid_logical_location(const tt_xy_pair& logical_location);

const std::string overlay_blobs_dir = "blobs";

}

std::ostream &operator<<(std::ostream &os, std::pair<std::string, std::set<std::string>> const &pair);
std::ostream &operator<<(std::ostream &os, std::set<int> const &set);
std::ostream &operator<<(std::ostream &os, std::set<string> const &set);
std::ostream &operator<<(std::ostream &os, std::set<tt_xy_pair> const &set);
