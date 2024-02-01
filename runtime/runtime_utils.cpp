// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "runtime_utils.hpp"

#include <regex>
#include <tuple>
#include <unistd.h>

#include "pipegen2.h"
#include "utils/scoped_timer.hpp"
namespace tt {

void pause(const string &msg) {
    do {
        cout << '\n' << msg << " - Press ENTER to continue...";
    } while (cin.get() != '\n');
}

string get_overlay_output_dir(const string& build_dir_path, int temporal_epoch_index) {
    return build_dir_path + "/temporal_epoch_" + std::to_string(temporal_epoch_index) + "/overlay/";
}

void generate_pipegen_spec(const string &netlist, const string &build_dir_path, const int global_epoch_start, const string &soc_descriptor_path, const string &network_desc_path) {
    PROFILE_SCOPE_MS();
    string root = buda_home();
    bool verbose = parse_env("TT_BACKEND_PRINT_PIPEGEN", false);
    stringstream net2pipe_cmd;
    string net2pipe_path = "build/bin/net2pipe ";
    string net2pipe_log = build_dir_path + "/net2pipe.log";
    string net2pipe_err = build_dir_path + "/net2pipe.err";

    net2pipe_cmd << root << net2pipe_path;
    net2pipe_cmd << " " << netlist;
    net2pipe_cmd << " " << build_dir_path;
    net2pipe_cmd << " " << global_epoch_start;
    net2pipe_cmd << " " << soc_descriptor_path;
    net2pipe_cmd << " " << network_desc_path;

    stringstream net2pipe_out;
    net2pipe_out << "Run net2pipe" << endl << net2pipe_cmd.str();
    log_debug(tt::LogRuntime, "{}", net2pipe_out.str());

    if (!fs::exists(build_dir_path)) {
        fs::create_directories(build_dir_path);
    }
    auto result = tt::run_command(net2pipe_cmd.str(), net2pipe_log, net2pipe_err);
    if (!result.success) {
        log_fatal("Running net2pipe command failed: {}, error_message: {}", net2pipe_cmd.str(), result.message);
    }
}

void generate_sdesc_yaml_for_pipegen(std::set<chip_id_t> chips){
    YAML::Node device_descs;
    for(auto it = chips.begin(); it != chips.end(); it++)
        device_descs["chip_descriptors"][std::to_string(*it)] = tt::io::info.output_dir + "/device_descs/" + std::to_string(*it) + ".yaml";
    std::ofstream fout(tt::io::info.output_dir + "/device_descs_for_pipegen.yaml");
    fout << device_descs;
    fout.close();
}


void generate_cluster_desc_yaml(const string &build_dir_path) {
    std::string cmd = buda_home();
    cmd += "device/bin/silicon/wormhole/create-ethernet-map " + build_dir_path + "/cluster_desc.yaml";
    log_info(tt::LogRuntime, "running: '{}'", cmd);
    log_assert(system(cmd.c_str()) == 0, "Unable to generate cluster descriptor file");
    log_debug(tt::LogRuntime, "Generated cluster descriptor file at path={}/cluster_desc.yaml", build_dir_path);
}

void run_pipegen(
    const string &build_dir_path,
    const std::string &graph_name,
    int temporal_epoch,
    const std::vector<chip_id_t> &chip_ids,
    const perf::PerfDesc &perf_desc,
    const string &desc_name,
    const std::unordered_map<chip_id_t, buda_soc_description>& sdesc_per_chip) {
    string root = buda_home();
    string build_graph_dir = get_overlay_output_dir(build_dir_path, temporal_epoch);
    uint32_t perf_dump_info = (perf_desc.perf_dump_level & 0xff) | ((uint(perf_desc.device_perf_mode) & 0xff) << 8);

    const string pipegen_yaml_path = build_graph_dir + "pipegen.yaml";
    const string blob_yaml_path = build_graph_dir + "blob.yaml";

    if (!fs::exists(build_dir_path)) {
        fs::create_directories(build_dir_path);
    }

    // Pipegen2 can be run as a library or as a command line tool. The library is used by default.
    run_pipegen2(desc_name, pipegen_yaml_path, temporal_epoch, blob_yaml_path, perf_dump_info);

    run_blobgen(root, build_graph_dir, build_dir_path, temporal_epoch, chip_ids, sdesc_per_chip);
}

void run_pipegen2(const string &desc_name,
                  const string &pipegen_yaml_path,
                  const int temporal_epoch,
                  const string &blob_yaml_path,
                  const uint32_t perf_dump_info) {

    try {
        pipegen2::Pipegen2 pipegen(desc_name);
        std::unique_ptr<pipegen2::StreamGraphCollection> stream_graphs =
            pipegen.create_stream_graphs(pipegen_yaml_path, temporal_epoch);
        pipegen.output_blob_yaml(stream_graphs.get(), blob_yaml_path, perf_dump_info);
    } catch (const std::exception& e) {
        log_fatal("Running pipegen2 failed: {}", e.what());
    }
}

void run_blobgen(
    const string &root,
    const string &build_graph_dir,
    const string &build_dir_path,
    int temporal_epoch,
    const std::vector<chip_id_t> &chip_ids,
    const std::unordered_map<chip_id_t, buda_soc_description> &sdesc_per_chip) {

    PROFILE_SCOPE_MS();
    stringstream blobgen_cmd;
    blobgen_cmd << "ruby " + root + "/src/overlay/blob_gen.rb";
    blobgen_cmd << " --blob_out_dir " + build_graph_dir;
    blobgen_cmd << " --graph_yaml 1";
    blobgen_cmd << " --graph_input_file " + build_graph_dir + "blob.yaml";
    blobgen_cmd << " --graph_name pipegen_epoch" + to_string(temporal_epoch);
    blobgen_cmd << " --root " + root;
    blobgen_cmd << " --noc_x_size " + to_string(sdesc_per_chip.begin()->second.physical_grid_size.x);
    blobgen_cmd << " --noc_y_size " + to_string(sdesc_per_chip.begin()->second.physical_grid_size.y);
    blobgen_cmd << " --chip " << tt::get_string_lowercase(sdesc_per_chip.begin() -> second.arch);
    blobgen_cmd << " --noc_version " + to_string(sdesc_per_chip.begin() -> second.overlay_version);
    blobgen_cmd << " --tensix_mem_size " + to_string(l1_mem::address_map::MAX_SIZE);
    blobgen_cmd << " --noc_translation_id_enabled " + to_string(sdesc_per_chip.begin() -> second.noc_translation_id_enabled ? 1 : 0);

    if (sdesc_per_chip.begin() -> second.ethernet_cores.size() > 0) {
        blobgen_cmd << " --tensix_mem_size_eth " + to_string(eth_l1_mem::address_map::MAX_SIZE);
        std::string eth_cores_string = "";
        std::vector<tt_xy_pair> eth_cores = {};
        bool inserted = false;
        for(auto& chip : sdesc_per_chip) {
            for(auto& core : chip.second.ethernet_cores) {
                if(std::find(eth_cores.begin(), eth_cores.end(), core) == eth_cores.end()) {
                    eth_cores.push_back(core);
                }
            }
        }

        for (const tt_xy_pair &eth_core : sdesc_per_chip.begin() -> second.ethernet_cores) {
            if (inserted) {
                eth_cores_string += ",";
            }
            eth_cores_string += to_string(eth_core.y) + "-" + to_string(eth_core.x);
            inserted = true;
        }

        blobgen_cmd << " --eth_cores " + eth_cores_string;
        blobgen_cmd << " --blob_section_start " + to_string(l1_mem::address_map::OVERLAY_BLOB_BASE);
        blobgen_cmd << " --blob_section_start_eth " + to_string(eth_l1_mem::address_map::OVERLAY_BLOB_BASE);
        blobgen_cmd << " --data_buffer_space_base_eth " + to_string(eth_l1_mem::address_map::DATA_BUFFER_SPACE_BASE);
    }
    std::stringstream chip_ids_ss;
    std::stringstream logical_size_x_ss;
    std::stringstream logical_size_y_ss;

    chip_ids_ss << chip_ids.at(0);
    logical_size_x_ss << to_string(sdesc_per_chip.at(chip_ids.at(0)).grid_size.x);
    logical_size_y_ss << to_string(sdesc_per_chip.at(chip_ids.at(0)).grid_size.y);
    for (int i = 1; i < chip_ids.size(); i++) {
        chip_ids_ss << "," << chip_ids[i];
        logical_size_x_ss << "," << to_string(sdesc_per_chip.at(chip_ids.at(i)).grid_size.x);
        logical_size_y_ss << "," << to_string(sdesc_per_chip.at(chip_ids.at(i)).grid_size.y);
    }

    blobgen_cmd << " --chip_ids " + chip_ids_ss.str();
    blobgen_cmd << " --noc_x_logical_size " + logical_size_x_ss.str();
    blobgen_cmd << " --noc_y_logical_size " + logical_size_y_ss.str();

    stringstream blobgen_out;
    blobgen_out << "Run blobgen" << endl << blobgen_cmd.str();
    log_debug(tt::LogRuntime, "{}", blobgen_out.str());

    if (!fs::exists(build_dir_path)) {
        fs::create_directories(build_dir_path);
    }

    string blobgen_log = build_graph_dir + "blobgen.log";
    string blobgen_err = build_graph_dir + "blobgen.err";
    // Create fresh log and error files
    // Each thread has its own unique blobgen log file
    if (fs::exists(blobgen_log)) {
        fs::remove(blobgen_log);
    }
    if (fs::exists(blobgen_err)) {
        fs::remove(blobgen_err);
    }
    auto result = tt::run_command(blobgen_cmd.str(), blobgen_log, blobgen_err);
    if (!result.success) {
        log_fatal("Running blob command failed: {}, error_message: {}", blobgen_cmd.str(), result.message);
    }
}

bool using_arm_host() {
#ifdef __ARM_ARCH
    return true;
#else
    return false;
#endif
}
void check_system_params(const string &build_dir_path) {

    // In case some environments need a quick way to skip this check or if host does not have required settings (ARM hosts so far)...
    if (using_arm_host() or parse_env("TT_BACKEND_SKIP_CHECK_SYSTEM_PARAMS", false)) {
        log_warning(tt::LogRuntime,  "Skipping system checks via env-var");
        return;
    }

    string boot_params_file = "/proc/cmdline";
    vector<string> required_settings = {"iommu=off|iommu=pt"};

    if (!fs::exists(build_dir_path)) {
        fs::create_directories(build_dir_path);
    }
    if (tt::run_command("systemd-detect-virt --vm", build_dir_path + "/system_virt_checks.log")) {
        // Issue #440 -- skip checks when inside a VM
        log_info(tt::LogRuntime,  "Skipping system checks for VM");
        return;
    }
    for (const string &setting : required_settings) {
        stringstream check_cmd;
        check_cmd << "grep -E '" + setting << "' " + boot_params_file;
        if (!tt::run_command(check_cmd.str(), build_dir_path + "/system_param_checks.log")) {
            stringstream sys_requirement_error;
            sys_requirement_error << "Error: System requirement check failed, " << setting << " is not found " << check_cmd.str();
            log_fatal("{}", sys_requirement_error.str());
        }
    }
}

const std::vector<std::vector<std::vector<int>>> default_arch_dram_cores(const tt::ARCH &arch) {
    if (arch == tt::ARCH::GRAYSKULL) {
        return {
            {{1,0},},
            {{1,6},},
            {{4,0},},
            {{4,6},},
            {{7,0},},
            {{7,6},},
            {{10,0},},
            {{10,6},}
        };
    } else {
        return {
            { {0,0}, {0,1}, {0,11} },
            { {0,5}, {0,6}, {0,7} },
            { {5,0}, {5,1}, {5,11} },
            { {5,2}, {5,9}, {5,10} },
            { {5,3}, {5,4}, {5,8} },
            { {5,5}, {5,6}, {5,7} }
        };
    }
}

const std::vector<std::vector<int>> default_arch_ethernet_cores(const tt::ARCH &arch) {
    if (arch == tt::ARCH::GRAYSKULL) {
        return {};
    } else {
        return {{9, 0}, {1, 0}, {8, 0}, {2, 0}, {7, 0}, {3, 0}, {6, 0}, {4, 0}, {9, 6}, {1, 6}, {8, 6}, {2, 6}, {7, 6}, {3, 6}, {6, 6}, {4, 6}};
    }
}

void print_grayskull_device_description(ofstream &outfile, std::vector<int> grid) {
    // Number of non-worker rows and cols
    const int special_dram_row = 6;
    const int non_worker_c = 1;
    const int non_worker_r = (grid[0] >= special_dram_row) ? 2 : 1;

    uint32_t num_rows = grid[0] + non_worker_r;
    uint32_t num_cols = grid[1] + non_worker_c;
    outfile << "grid:" << endl;
    outfile << "  x_size: " << num_cols << endl;
    outfile << "  y_size: " << num_rows << endl << endl;

    // No need for these cores in custom device grid
    outfile << "arc:" << endl << "  []" << endl << endl;
    outfile << "pcie:" << endl << "  []" << endl << endl;
    outfile << "eth:" << endl << "  []" << endl << endl;

    // List of available dram cores in full grid
    const vector<vector<vector<int>>> &dram_cores_by_channel = default_arch_dram_cores(tt::ARCH::GRAYSKULL);
    outfile << "dram:" << endl;
    outfile << "  [" << endl;
    bool channel_inserted = false;
    for (int chan = 0; chan < dram_cores_by_channel.size() ; chan++)
    {
        // Insert the dram core if it's within the given grid
        std::vector<std::vector<int>> used_dram_cores = {};
        for (const auto &dram_core : dram_cores_by_channel[chan]) {
            if ((dram_core[0] < num_cols) and (dram_core[1] < num_rows))
            {
                used_dram_cores.push_back(dram_core);
            }
        }

        if (used_dram_cores.size() > 0) {
            if (channel_inserted) {
                outfile << ",";
            }
            outfile << "    [";
            for (const auto &dram_core : used_dram_cores) {
                outfile << dram_core[0] << "-" << dram_core[1] << ",";
            }
            outfile << "]" << endl;
            channel_inserted = true;
        }
    }
    outfile << "  ]" << endl << endl;

    // Insert worker cores that are within the given grid
    outfile << "harvested_workers:" << endl << "  []" << endl << endl;
    outfile << "functional_workers:" << endl;
    outfile << "  [" << endl;

    // Workers start from (1, 1)
    for (int x = 1; x < num_cols; x++)
    {
        outfile << "   ";
        for (int y = 1; y < num_rows; y++)
        {
            if (y == special_dram_row) continue;  // reserved for dram
            outfile << x << "-" << y << ", ";
        }
        outfile << endl;
    }
    outfile << "  ]" << endl << endl;
    outfile << "router_only:" << endl << "  []" << endl << endl;

    // Fill in the rest that are static to our device
    outfile << "worker_l1_size:" << endl;
    outfile << "  " << 1024 * 1024 << endl << endl;
    outfile << "dram_bank_size:" << endl;
    outfile << "  " << 1024 * 1024 * 1024 << endl << endl;
    outfile << "eth_l1_size:" << endl;
    outfile << "  " << 0 << endl << endl;
    outfile << "arch_name: " << "GRAYSKULL" << endl << endl;
    outfile << "features:" << endl;
    outfile << "  unpacker:" << endl;
    outfile << "    version: " << 1 << endl;
    outfile << "    inline_srca_trans_without_srca_trans_instr: False" << endl;
    outfile << "  math:" << endl;
    outfile << "    dst_size_alignment: " << 32 * 1024 << endl;
    outfile << "  packer:" << endl;
    outfile << "    version: " << 1 << endl;
    outfile << "  overlay:" << endl;
    outfile << "    version: " << 1 << endl;
    outfile << endl;
}

void print_wormhole_device_description(ofstream &outfile, std::vector<int> grid) {
    constexpr bool trim_inside_special_dram_col = false;
    // Number of non-worker rows and cols
    int special_dram_col = 5;
    const int special_ethernet_row = 6;
    bool requires_dram_rows_to_right = grid[0] > 1;

    const int non_worker_r_top = 1;
    const int non_worker_c_left = 1;
    const int non_worker_r = non_worker_r_top + ((non_worker_r_top + grid[0] >= special_ethernet_row) ? 1 : 0);
    const int non_worker_c = non_worker_c_left + (((non_worker_c_left + grid[1] >= special_dram_col) || requires_dram_rows_to_right) ? 1 : 0);

    const int special_dram_col_left_offset = trim_inside_special_dram_col ? (non_worker_c_left + grid[1] >= special_dram_col) ? 0 : special_dram_col - grid[1] : 0;

    if (trim_inside_special_dram_col) {
        special_dram_col -= (special_dram_col_left_offset - non_worker_c_left);
    }
    log_assert(special_dram_col_left_offset >= 0, "special_dram_col_left_offset expected to be 0 or +ve");

    uint32_t num_rows = grid[0] + non_worker_r;
    uint32_t num_cols = grid[1] + non_worker_c;

    if (!trim_inside_special_dram_col) {
        if (requires_dram_rows_to_right) {
            num_cols = std::max((uint32_t)6, num_cols);
        }
    }

    outfile << "grid:" << endl;
    outfile << "  x_size: " << num_cols << endl;
    outfile << "  y_size: " << num_rows << endl << endl;

    // No need for these cores in custom device grid
    outfile << "arc:" << endl << "  []" << endl << endl;
    outfile << "pcie:" << endl << "  []" << endl << endl;
    outfile << "eth:" << endl << "  [";
    const vector<vector<int>> &ethernet_cores = default_arch_ethernet_cores(tt::ARCH::WORMHOLE);
    for (int chan = 0; chan < ethernet_cores.size() ; chan++)
    {
        // Insert the dram core if it's within the given grid
        const vector<int> &ethernet_core = ethernet_cores[chan];
        if ((ethernet_core[0] < num_cols) and (ethernet_core[1] < num_rows) && ethernet_core[0] != special_dram_col)
        {
            if (chan != 0) outfile << ", ";
            outfile << ethernet_core[0] << "-" << ethernet_core[1];
        }
    }
    outfile << "]" << endl << endl;

    // List of available dram cores in full grid
    const std::vector<vector<vector<int>>> &dram_cores_by_channel = default_arch_dram_cores(tt::ARCH::WORMHOLE);
    outfile << "dram:" << endl;
    outfile << "  [" << endl;
    bool channel_inserted = false;
    for (int chan = 0; chan < dram_cores_by_channel.size() ; chan++)
    {
        // Insert the dram core if it's within the given grid
        std::vector<std::vector<int>> used_dram_cores = {};
        for (const auto &dram_core : dram_cores_by_channel[chan]) {
            const int dram_core_column = (dram_core[0] >= special_dram_col && trim_inside_special_dram_col) ? dram_core[0] - special_dram_col_left_offset + non_worker_c_left : dram_core[0];
            std::vector<int> shifted_dram_core_location = {dram_core_column, dram_core[1]};
            if ((shifted_dram_core_location[0] < num_cols) and (shifted_dram_core_location[1] < num_rows))
            {
                used_dram_cores.push_back(shifted_dram_core_location);
            }
        }

        if (used_dram_cores.size() > 0) {
            if (channel_inserted) {
                outfile << ",";
            }
            outfile << "    [";
            for (const auto &dram_core : used_dram_cores) {
                outfile << dram_core[0] << "-" << dram_core[1] << ",";
            }
            outfile << "]";
            channel_inserted = true;
        }

    }
    outfile << "  ]" << endl << endl;

    // Insert worker cores that are within the given grid
    outfile << "harvested_workers:" << endl << "  []" << endl << endl;
    outfile << "functional_workers:" << endl;
    outfile << "  [" << endl;

    // Workers start from (1, 1)
    for (int x = 1; x < num_cols; x++)
    {
        if (x == special_dram_col) continue;  // reserved for dram
        outfile << "   ";
        for (int y = 1; y < num_rows; y++)
        {
            if (y == special_ethernet_row) continue;  // reserved for dram
            outfile << x << "-" << y << ", ";
        }
        outfile << endl;
    }
    outfile << "  ]" << endl << endl;
    const std::vector<tt_xy_pair> router_only_cores = {tt_xy_pair(0,2), tt_xy_pair(0,4), tt_xy_pair(0,8), tt_xy_pair(0,9)};
    outfile << "router_only:" << endl << "  [";
    for (const auto &c_xy : router_only_cores) {
        if ((c_xy.x < num_cols) and (c_xy.y < num_rows))
        {
            outfile << std::to_string(c_xy.x) << "-" << std::to_string(c_xy.y) << ",";
        }
    }
    outfile << "]" << endl << endl;

    // Fill in the rest that are static to our device
    outfile << "worker_l1_size:" << endl;
    outfile << "  " << 1499136 << endl << endl;
    outfile << "dram_bank_size:" << endl;
    outfile << "  " << 2147483648 << endl << endl;
    outfile << "eth_l1_size:" << endl;
    outfile << "  " << 262144 << endl << endl;
    outfile << "arch_name: " << "WORMHOLE" << endl << endl;
    outfile << "features:" << endl;
    outfile << "  unpacker:" << endl;
    outfile << "    version: " << 2 << endl;
    outfile << "    inline_srca_trans_without_srca_trans_instr: False" << endl;
    outfile << "  math:" << endl;
    outfile << "    dst_size_alignment: " << 262144 << endl;
    outfile << "  packer:" << endl;
    outfile << "    version: " << 2 << endl;
    outfile << "  overlay:" << endl;
    outfile << "    version: " << 2 << endl;
    outfile << endl;
}

void print_device_description(const tt::ARCH &arch, ofstream &outfile, const std::vector<int> &grid) {
    if (arch == tt::ARCH::GRAYSKULL) {
        print_grayskull_device_description(outfile, grid);
    } else {
        print_wormhole_device_description(outfile, grid);
    }
}

void print_device_description(ofstream &outfile, const std::vector<int> &grid, const buda_soc_description &soc_descriptor, bool noc_translation_en) {
    // Number of non-worker rows and cols

    uint32_t num_rows = grid[0];// + non_worker_r;
    uint32_t num_cols = grid[1];// + non_worker_c;
    outfile << "grid:" << endl;
    outfile << "  x_size: " << num_cols << endl;
    outfile << "  y_size: " << num_rows << endl << endl;

    if(soc_descriptor.physical_grid_size.x != num_cols || soc_descriptor.physical_grid_size.y != num_rows) {
        outfile << "physical:" << endl;
        outfile << "  x_size: " << std::min((uint32_t)soc_descriptor.physical_grid_size.x, num_cols) << endl;
        outfile << "  y_size: " << std::min((uint32_t)soc_descriptor.physical_grid_size.y, num_rows) << endl << endl;
    }

    outfile << "arc:" << endl;
    outfile <<  "  [" << endl;

    const vector<tt_xy_pair> &arc_cores = soc_descriptor.arc_cores;
    for (const tt_xy_pair &arc : arc_cores) {
        if (arc.x < num_cols && arc.y < num_rows) {
            outfile << arc.x << "-" << arc.y << ", ";
        }
    }
    outfile << endl;
    outfile << "  ]" << endl << endl;

    outfile << "pcie:" << endl;
    outfile <<  "  [" << endl;

    const vector<tt_xy_pair> &pcie_cores = soc_descriptor.pcie_cores;
    for (const tt_xy_pair &pcie : pcie_cores) {
        if (pcie.x < num_cols && pcie.y < num_rows) {
            outfile << pcie.x << "-" << pcie.y << ", ";
        }
    }
    outfile << endl;
    outfile << "  ]" << endl << endl;

    // List of available dram cores in full grid
    const vector<vector<tt_xy_pair>> &dram_cores_by_channel = soc_descriptor.dram_cores;
    outfile << "dram:" << endl;
    outfile << "  [" << endl;

    for (int chan = 0; chan < dram_cores_by_channel.size() ; chan++)
    {
        // Insert the dram core if it's within the given grid
        std::vector<std::string> inserted = {};
        for (const tt_xy_pair &dram_core : dram_cores_by_channel[chan]) {
            if ((dram_core.x < num_cols) and (dram_core.y < num_rows))
            {
                inserted.push_back(std::to_string(dram_core.x) + "-" + std::to_string(dram_core.y));
            }
        }
        if(inserted.size()){
            outfile << "[";

            for (int i = 0; i < inserted.size(); i++) {
                outfile << inserted[i] << ", ";
            }

            outfile << "]," << endl;
        }
    }
    outfile << endl << "]" << endl << endl;

    outfile << "eth:" << endl << "  [" << endl;
    bool inserted_eth = false;
    for (const tt_xy_pair &ethernet_core : soc_descriptor.ethernet_cores)
    {
        // Insert the eth core if it's within the given grid
        if (noc_translation_en || (ethernet_core.x < num_cols && ethernet_core.y < num_rows))
        {
            if (inserted_eth) {
                outfile << ", ";
            }
            outfile << ethernet_core.x << "-" << ethernet_core.y;
            inserted_eth = true;
        }
    }
    outfile << endl << "]" << endl << endl;
    // Insert worker cores that are within the given grid
    outfile << "harvested_workers:" << endl;
    outfile <<  "  [" << endl;

    const vector<tt_xy_pair> &tensix_cores_harvested = soc_descriptor.harvested_workers;
    for (const tt_xy_pair &worker : tensix_cores_harvested) {
        if (worker.x < num_cols && worker.y < num_rows) {
            outfile << worker.x << "-" << worker.y << ", ";
        }
    }
    outfile << endl;
    outfile << "  ]" << endl << endl;

    outfile << "functional_workers:" << endl;
    outfile << "  [" << endl;
    const vector<tt_xy_pair> &tensix_cores = soc_descriptor.workers;
    for (const tt_xy_pair &worker : tensix_cores) {

        if (noc_translation_en || (worker.x < num_cols && worker.y < num_rows)) {
            outfile << worker.x << "-" << worker.y << ", ";
        }
    }
    outfile << endl;
    outfile << "  ]" << endl << endl;

    outfile << "router_only:" << endl << "  []" << endl << endl;

    // Fill in the rest that are static to our device
    outfile << "worker_l1_size:" << endl;
    outfile << "  " << soc_descriptor.worker_l1_size << endl << endl;
    outfile << "dram_bank_size:" << endl;
    outfile << "  " << soc_descriptor.dram_bank_size << endl << endl;
    outfile << "eth_l1_size:" << endl;
    outfile << "  " << soc_descriptor.eth_l1_size << endl << endl;
    outfile << "arch_name: " << tt::get_string_lowercase(soc_descriptor.arch) << endl << endl;
    outfile << "features:" << endl;
    outfile << "  noc:" << endl;
    outfile << "    translation_id_enabled: True" << endl;
    outfile << "  unpacker:" << endl;
    outfile << "    version: " << soc_descriptor.unpacker_version << endl;
    outfile << "    inline_srca_trans_without_srca_trans_instr: False" << endl;
    outfile << "  math:" << endl;
    outfile << "    dst_size_alignment: " << soc_descriptor.dst_size_alignment << endl;
    outfile << "  packer:" << endl;
    outfile << "    version: " << soc_descriptor.packer_version << endl;
    outfile << "  overlay:" << endl;
    outfile << "    version: " << soc_descriptor.overlay_version << endl;
    outfile << endl;
}

std::unique_ptr<buda_soc_description> get_default_soc_desc(const tt::ARCH &arch) {
    std::string default_soc_desc_name;

    switch (arch) {
        case tt::ARCH::WORMHOLE:
            default_soc_desc_name = "device/wormhole_8x10.yaml"; break;
        case tt::ARCH::WORMHOLE_B0:
            default_soc_desc_name = "device/wormhole_b0_8x10.yaml"; break;
        case tt::ARCH::GRAYSKULL:
            default_soc_desc_name = "device/grayskull_10x12.yaml"; break;
        default:
            log_fatal("Invalid arch name");
    }

    return load_soc_descriptor_from_yaml(default_soc_desc_name);
}

void generate_soc_description(buda_soc_description *default_full_soc_desc_ptr, const std::string &file_path, const std::vector<int> &grid, bool noc_translation_en) {
    ofstream f(file_path);

    log_assert(default_full_soc_desc_ptr != NULL, "Expected soc desc to be populated");

    log_debug(tt::LogRuntime, "Generating {} for {}x{} grid", file_path, grid[0], grid[1]);
    print_device_description(f, grid, *default_full_soc_desc_ptr, noc_translation_en);
    f.close();
}

YAML::Node get_runtime_data(const string &runtime_data_path) {
    try {
        return YAML::LoadFile(runtime_data_path);
    } catch (const std::exception &e) {
        return YAML::Node{};
    }
}
std::string get_soc_desc_path(chip_id_t chip, bool runtime_descriptor) {
    string file_to_use;
    if(runtime_descriptor && fs::exists(tt::io::info.output_dir + "/device_desc_runtime/" + std::to_string(chip) + ".yaml")) {
        file_to_use = tt::io::info.output_dir + "/device_desc_runtime/" + std::to_string(chip) + ".yaml";
    }
    else if(fs::exists(tt::io::info.output_dir + "/device_descs/")) {
        file_to_use = tt::io::info.output_dir + "/device_descs/" + to_string(chip) + ".yaml";
    }
    else {
        file_to_use = tt::io::info.output_dir + "/device_desc.yaml";
    }
    return file_to_use;
}

void cleanup_stale_soc_descriptors() {
    std::vector<std::string> files_to_delete  = {"/device_desc.yaml", "/device_desc_runtime/", "/device_descs/", "/device_descs_for_pipegen.yaml"};

    for(auto& file : files_to_delete) {
        if(fs::exists(tt::io::info.output_dir + file)) {
            fs::remove_all(tt::io::info.output_dir + file);
        }
    }
}
std::pair<std::string, std::string> get_git_info() {
    string root = buda_home();
    std::string branch_name;
    std::string commit_hash;

    std::ifstream infile(root + "/build/.gitinfo");
    if (infile.fail()) {
        branch_name = "cannot-find-branch";
        commit_hash = "cannot-find-commit";
    }
    infile >> branch_name;
    infile >> commit_hash;
    return {branch_name, commit_hash};
}

void generate_soc_descriptors_for_compile(const tt_runtime_config& config, bool noc_trans_en, const std::string& default_sdesc_path, chip_id_t chip, int harvesting_mask, const tt::ARCH& arch_name, const std::string& runtime_path, const std::string& overlay_path) {
    // Generate Harvested/NOC Translated Descriptors for compile only mode
    if(!config.valid_device_desc()) {
        auto default_sdesc = tt_SocDescriptor(default_sdesc_path); // This comes from config if valid sdesc was passed in. Otherwise its the default desc picked up by runtime.
        std::string soc_desc_path = runtime_path + std::to_string(chip) + ".yaml";
        tt_SiliconDevice::harvest_rows_in_soc_descriptor(arch_name, default_sdesc, harvesting_mask);
        auto temp_buda_sdesc = buda_soc_description(default_sdesc);
        generate_soc_description(&temp_buda_sdesc, soc_desc_path, {static_cast<int>(default_sdesc.grid_size.y), static_cast<int>(default_sdesc.grid_size.x)});
    }
    else {
        // Done for WH only, since we need to differentiate between runtime and overlay descriptors.
        // Use runtime descriptor that was passed in (we assume that harvesting corresponding to the mask has already been performed here if required)
        std::string soc_desc_path = runtime_path + std::to_string(chip) + ".yaml";
        fs::copy(config.soc_descriptor_path, soc_desc_path);
    }
    if(noc_trans_en) {
        // Generate overlay descriptors using translated coordinates.
        std::string overlay_soc_desc_path = overlay_path + std::to_string(chip) + ".yaml";
        auto overlay_soc_desc = tt_SocDescriptor(runtime_path + std::to_string(chip) + ".yaml");
        auto translation_table = tt_SiliconDevice::create_harvested_coord_translation(arch_name, false);

        for(auto& worker : overlay_soc_desc.workers) {
            worker = translation_table.at(worker);
        }
        for(auto& eth_core : overlay_soc_desc.ethernet_cores) {
            eth_core = translation_table.at(eth_core);
        }
        overlay_soc_desc.physical_grid_size = overlay_soc_desc.grid_size;
        overlay_soc_desc.grid_size = tt_xy_pair(32, 32);
        auto temp_buda_overlay_sdesc = buda_soc_description(overlay_soc_desc);
        generate_soc_description(&temp_buda_overlay_sdesc, overlay_soc_desc_path, {static_cast<int>((temp_buda_overlay_sdesc.grid_size).y), static_cast<int>((temp_buda_overlay_sdesc.grid_size).x)}, true);
    }
}

void translate_workers_and_eth(tt_cluster* cluster, const std::set<chip_id_t>& target_devices, const std::string& soc_descriptor_path, const std::string& output_dir) {
    for(auto it = target_devices.begin(); it != target_devices.end(); it++) {
        std::unique_ptr<buda_SocDescriptor> translated_soc_descriptor = load_soc_descriptor_from_yaml(soc_descriptor_path);
        for(auto& worker : translated_soc_descriptor -> workers) {
            cluster -> translate_to_noc_table_coords(*it, worker.y, worker.x);
        }
        for(auto& eth_core : translated_soc_descriptor -> ethernet_cores) {
            cluster -> translate_to_noc_table_coords(*it, eth_core.y, eth_core.x);
        }
        translated_soc_descriptor -> physical_grid_size = translated_soc_descriptor -> grid_size;
        translated_soc_descriptor -> grid_size = tt_xy_pair(32, 32); // Hardcoding this to the translated grid size for WH

        stringstream desc_name;
        stringstream runtime_desc_name;
        desc_name << output_dir << "/device_descs/" << *it << ".yaml";
        generate_soc_description(translated_soc_descriptor.get(), desc_name.str(), {static_cast<int>((translated_soc_descriptor -> grid_size).y), static_cast<int>((translated_soc_descriptor -> grid_size).x)}, true);
        // runtime_desc_name << output_dir << "/device_desc_runtime/" << *it << ".yaml";
        // fs::copy(soc_descriptor_path, runtime_desc_name.str());
    }
}

void remove_worker_row_from_descriptor(buda_soc_description* full_soc_descriptor, const vector<int>& row_coordinates_to_remove) {
    vector<tt_xy_pair> workers_to_keep;
    for(auto worker = (full_soc_descriptor -> workers).begin(); worker != (full_soc_descriptor -> workers).end(); worker++){
        if(find(row_coordinates_to_remove.begin(), row_coordinates_to_remove.end(), (*worker).y) == row_coordinates_to_remove.end()){
            workers_to_keep.push_back(*worker);
        }
        else{
            (full_soc_descriptor -> harvested_workers).push_back(*worker);
        }
    }
    full_soc_descriptor -> workers = workers_to_keep;
    (full_soc_descriptor -> worker_grid_size).y -= row_coordinates_to_remove.size();
}

vector<int> extract_rows_to_remove(const tt::ARCH &arch, const int worker_grid_rows, const int harvested_rows) {
    // Check if harvesting config is legal for GS and WH
    log_assert(!((harvested_rows & 1) || (harvested_rows & 64) || (harvested_rows & 0xFFFFF000)), "For grayskull and wormhole, only rows 1-5 and 7-11 can be harvested");
    vector<int> row_coordinates_to_remove;
    int row_coordinate = 0;
    int tmp = harvested_rows;
    while (tmp) {
        if (tmp & 1)
            row_coordinates_to_remove.push_back(row_coordinate);

        tmp = tmp >> 1;
        row_coordinate++;
    }
    if (arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
        // For Wormhole, we always remove the last few rows in the SOC descriptor in case of harvesting
        for (int i = 0; i < row_coordinates_to_remove.size(); i++) {
            row_coordinates_to_remove[i] = worker_grid_rows - i;
        }
    }
    return row_coordinates_to_remove;
}

void harvest_rows_in_soc_descriptor(tt::ARCH arch, tt::TargetDevice target_type, chip_id_t chip, uint32_t harvested_rows, const std::string& soc_descriptor_path, const std::string& output_dir) {
    log_assert(!((harvested_rows & 1) || (harvested_rows & 64) || (harvested_rows & 0xFFFFF000)), "For Grayskull and Wormhole, only rows 1-5 and 7-11 can be harvested");
    int row_coordinate = 0;
    std::unique_ptr<buda_SocDescriptor> full_soc_descriptor;
    std::unique_ptr<buda_SocDescriptor> soc_descriptor_for_runtime;
    if(arch == tt::ARCH::GRAYSKULL){
        full_soc_descriptor = load_soc_descriptor_from_yaml(soc_descriptor_path);
        soc_descriptor_for_runtime = load_soc_descriptor_from_yaml(soc_descriptor_path);
    }
    else if(arch == tt::ARCH::WORMHOLE_B0 || arch == tt::ARCH::WORMHOLE) {
        full_soc_descriptor = load_soc_descriptor_from_yaml(get_soc_description_file(arch, target_type, output_dir, true));
        soc_descriptor_for_runtime = load_soc_descriptor_from_yaml(soc_descriptor_path);
    }
    else {
        log_assert(false, "Can only support harvesting for Grayskull and Wormhole!");
    }

    uint32_t max_row = (*std::max_element((soc_descriptor_for_runtime -> workers).begin(), (soc_descriptor_for_runtime -> workers).end(), [] (const auto& a, const auto& b) { return a.y < b.y; })).y;
    vector<int> row_coordinates_to_remove = extract_rows_to_remove(arch, max_row, harvested_rows);

    if(arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
        // Harvested SOC descriptor for runtime is different than the one passed to overlay.
        // Generate this here and store it separately.
        if(!fs::exists(output_dir + "/device_desc_runtime/")) fs::create_directory(output_dir + "/device_desc_runtime/");

        stringstream runtime_desc_name;
        runtime_desc_name << output_dir << "/device_desc_runtime/" << chip << ".yaml";

        remove_worker_row_from_descriptor(soc_descriptor_for_runtime.get(), row_coordinates_to_remove);
        // Write runtime SOC desc to file
        generate_soc_description(soc_descriptor_for_runtime.get(), runtime_desc_name.str(), {static_cast<int>((soc_descriptor_for_runtime->grid_size).y), static_cast<int>((soc_descriptor_for_runtime->grid_size).x)});

        // Used to create SOC descriptors for overlay
        std::uint32_t max_row_to_remove = (*std::max_element((full_soc_descriptor -> workers).begin(), (full_soc_descriptor -> workers).end(), [] (const auto& a, const auto& b) { return a.y < b.y; })).y;
        for(int i = 0; i < row_coordinates_to_remove.size(); i++){
            row_coordinates_to_remove[i] = max_row_to_remove - i;
        }
    }
    remove_worker_row_from_descriptor(full_soc_descriptor.get(), row_coordinates_to_remove);

    if(!fs::exists(output_dir + "/device_descs/")) fs::create_directory(output_dir + "/device_descs/");
    stringstream desc_name;
    desc_name << output_dir << "/device_descs/" << chip << ".yaml";
    generate_soc_description(full_soc_descriptor.get(), desc_name.str(), {static_cast<int>((full_soc_descriptor->grid_size).y), static_cast<int>((full_soc_descriptor->grid_size).x)});
    log_debug(tt::LogRuntime, "Worker Grid Size after Harvesting: <r,c> = <{},{}>", (full_soc_descriptor -> worker_grid_size).y , (full_soc_descriptor -> worker_grid_size).x);
}

std::vector<int> extract_harvest_info_for_simulation(std::string harvest_info){
    size_t start;
    size_t end = 0;
    std::vector<int> out;
    if(!harvest_info.size()) return out;

    while((start = harvest_info.find_first_not_of(",", end)) != std::string::npos){
        end = harvest_info.find(",", start);
        out.push_back(stoi(harvest_info.substr(start, end - start)));
    }
    return out;
}
std::unordered_map<chip_id_t, uint32_t> populate_harvesting_masks_from_env_var(const std::set<chip_id_t>& devices, bool allow_num_masks_less_than_num_devices) {
    log_assert(std::getenv("TT_BACKEND_HARVESTED_ROWS") != nullptr, "TT_BACKEND_HARVESTED_ROWS should be set for {} to be called", __FUNCTION__);
    log_warning(tt::LogRuntime, "TT_BACKEND_HARVESTED_ROWS will be deprecated. For end to end runs please use the harvested_rows field in backend config");
    std::vector<int> harvesting_info = extract_harvest_info_for_simulation(std::getenv("TT_BACKEND_HARVESTED_ROWS"));
    if(allow_num_masks_less_than_num_devices and harvesting_info.size()) {
        while(harvesting_info.size() != devices.size()) {
            harvesting_info.push_back(harvesting_info.at(harvesting_info.size() - 1));
        }
    }
    log_assert(devices.size() == harvesting_info.size(), 
                "Number of entries in the comma seperated harvesting config should match the number of devices in the netlist. Num Devices: {} Num Entries: {}",
                devices.size(), harvesting_info.size());
    int idx = 0;
    std::unordered_map<chip_id_t, uint32_t> harvesting_mask_per_device = {};
    for (const auto& device : devices) {
        harvesting_mask_per_device.insert({device, harvesting_info.at(idx)});
        idx++;
    }
    return harvesting_mask_per_device;
}

std::unordered_map<chip_id_t, uint32_t> parse_harvested_rows_from_config(const tt_runtime_config& config, const std::set<chip_id_t>& devices) {
    if(tt::parse_env("TT_BACKEND_HARVESTED_ROWS", false)) {
        // Legacy flow: Harvesting masks passed in through envvar
        log_assert(!config.harvested_rows.size(), 
                    "Cannot specify the harvesting configuration through both the TT_BACKEND_HARVESTED_ROWS env-var and the tt_backend_config object.");
        log_debug(tt::LogRuntime, "Setting harvesting mask from env-var");
        return populate_harvesting_masks_from_env_var(devices, false);
    }
    // New flow: Harvesting masks passed in through backend_config.
    // If config has populated harvested rows for a chip, parse those. For all unpopulated chips, assume harvesting mask is 0.
    log_debug(tt::LogRuntime, "Setting harvesting mask from config");
    std::unordered_map<chip_id_t, std::vector<uint32_t>> harvested_rows = {};
    for(const auto& chip : devices) {
        if(config.harvested_rows.find(chip) != config.harvested_rows.end()) {
            harvested_rows.insert({chip, config.harvested_rows.at(chip)});
        } else {
            harvested_rows.insert({chip, {}});
        }
    }
    return tt_SiliconDevice::get_harvesting_masks_from_harvested_rows(harvested_rows);
}

bool using_sw_harvesting(const tt_runtime_config& config) {
    return config.harvested_rows.size() or std::getenv("TT_BACKEND_HARVESTED_ROWS");
}

vector<uint32_t> pad_entry_to_padding_blob(const YAML::Node& pad_entry) {
    const uint32_t size_tiles = pad_entry["size_tiles"].as<uint32_t>();
    const DataFormat data_format = static_cast<DataFormat>(pad_entry["data_format"].as<uint32_t>());
    const float padding_value = pad_entry["padding_value"].as<float>();
    vector<uint32_t> padding_entry_blob = {};
    auto padding_tile = tt_tile(data_format);
    padding_tile.set_number(padding_value);
    padding_tile.pack_data();
    for(int i = 0; i < size_tiles; i++) {
        padding_entry_blob.insert(padding_entry_blob.end(), padding_tile.packed_data.begin(), padding_tile.packed_data.end());
    }
    return padding_entry_blob;
}

void populate_result_from_string(tt_compile_result *r, const std::string &e, const std::unordered_map<uint32_t, std::unordered_map<chip_id_t, std::string>>& global_epoch_device_to_graph) {
    tt_compile_result &result = *r;
    std::smatch match;
    std::regex regex;

    // Set failure status if exception exists
    if (e == "") {
        result.success = true;
        return;
    } else {
        result.success = false;
        result.failure_message = e;
    }

    // Parse the failure type specific info
    if (e.find("net2pipe command failed") != std::string::npos) {
        result.failure_type = COMPILE_FAILURE::Net2Pipe;
    } else if (e.find("pipegen command failed") != std::string::npos) {
        result.failure_type = COMPILE_FAILURE::Net2Pipe;
    } else if (e.find("blob command failed") != std::string::npos) {
        result.failure_type = COMPILE_FAILURE::BlobGen;
        // Overlay Blob Size Failure
        if (e.find("The overlay blob") != std::string::npos) {
            result.failure_type = COMPILE_FAILURE::OverlaySize;
            regex = std::regex("we tried to allocate (\\d+)");
            // Parse the extra size required
            if (std::regex_search(e, match, regex) && match.size() > 1) {
                uint32_t size = std::stoi(match[1].str());
                uint32_t delta = size - l1_mem::address_map::OVERLAY_BLOB_SIZE;
                uint32_t alignment = 4096;
                result.extra_size_bytes = (delta - 1) - ((delta - 1) % alignment) + alignment;
            }
            // Parse the compilation target chip, epoch, core, and graph name
            regex = std::regex("\\(e:(\\d+),c:(\\d+),y:(\\d+),x:(\\d+)\\)");
            if (std::regex_search(e, match, regex) && match.size() == 5) {
                uint32_t temporal_epoch = std::stoi(match.str(1));
                result.temporal_epoch_id = temporal_epoch;
                uint32_t device_id = std::stoi(match.str(2));
                result.device_id = device_id;
                auto sdesc = load_soc_descriptor_from_yaml(get_soc_desc_path(result.device_id));
                uint32_t y = std::stoi(match.str(3));
                uint32_t x = std::stoi(match.str(4));
                tt_xy_pair logical_core = sdesc->get_worker_core(tt_xy_pair{x, y});
                result.logical_core_y = logical_core.y;
                result.logical_core_x = logical_core.x;
                if (global_epoch_device_to_graph.find(temporal_epoch) == global_epoch_device_to_graph.end() ||
                    global_epoch_device_to_graph.at(temporal_epoch).find(device_id) == global_epoch_device_to_graph.at(temporal_epoch).end()) {
                        result.graph_name = "undefined";
                    }
                else {
                    result.graph_name = global_epoch_device_to_graph.at(temporal_epoch).at(device_id);
                }
            }
        }
    }

    // Parse the failure command
    regex = std::regex("failed: ([^,]*)\\s*,");
    if (std::regex_search(e, match, regex) && match.size() > 1) {
        result.failure_target = match[1].str();
    }
}

void populate_compile_stats_from_logs(tt_compile_result *result, const std::string& output_dir, uint num_temporal_epochs, uint global_epoch_start_id) {
    const std::regex overlay_blob_size_re("^Info: Overlay Blob size for \\(e:(\\d+),c:(\\d+),y:(\\d+),x:(\\d+)\\) is (\\d+)$");
    std::smatch matches;
    for (uint temporal_epoch_id = 0; temporal_epoch_id < num_temporal_epochs; temporal_epoch_id++) {
        uint global_epoch_id = global_epoch_start_id + temporal_epoch_id;
        std::string overlay_output_dir = tt::get_overlay_output_dir(output_dir, global_epoch_id);
        std::string blobgen_log_path = overlay_output_dir + "blobgen.log";
        if (fs::exists(blobgen_log_path)) {
            std::ifstream blobgen_log_file(blobgen_log_path);
            log_assert(blobgen_log_file.is_open(), "{} - Cannot open {}. errno: {}", __FUNCTION__, blobgen_log_path, std::strerror(errno));
            // Parse file for blob usage results here. Place the results in r -> blob_usage_per_epoch_per_core.
            std::string line, chip_core_xy;
            uint blob_size;
            while (std::getline(blobgen_log_file, line)) {
                if (regex_match(line, matches, overlay_blob_size_re)) {
                    uint blobgen_epoch_id = stoi(matches[1].str());
                    log_assert(blobgen_epoch_id == global_epoch_id, "Unexpected epoch {} in blobgen.log that doesn't match epoch of overlay output dir {}", blobgen_epoch_id, overlay_output_dir);
                    chip_core_xy = matches[2].str() + "-" + matches[4].str() + "-" + matches[3].str();
                    blob_size = stoi(matches[5]);
                    result->blob_usage_per_epoch_per_core[blobgen_epoch_id][chip_core_xy] = blob_size;
                }
            }
        }
    }
}

std::vector<std::string> retrieve_compile_stats_info(tt_compile_result* result, const std::unordered_map<uint32_t, std::unordered_map<chip_id_t, std::string>>& global_epoch_device_to_graph) {
    std::vector<std::string> all_compile_stats_info;
    // retrieve blob usage info
    std::string device_core_xy, core_x, core_y, graph_name, info_str;
    chip_id_t device_id;
    uint global_epoch_id, blob_size;
    std::smatch chip_core_match;
    bool found_chip_core_match;
    const std::regex chip_core_re("^(\\d+)-(\\d+)-(\\d+)$");
    for (const auto& epoch_it : result->blob_usage_per_epoch_per_core) {
        global_epoch_id = epoch_it.first;
        for (const auto& core_it : epoch_it.second) {
            device_core_xy = core_it.first;
            found_chip_core_match = regex_match(device_core_xy, chip_core_match, chip_core_re);
            log_assert(found_chip_core_match, "device, core-xy does not follow correct format");
            device_id = stoi(chip_core_match[1].str());
            core_x = chip_core_match[2].str();
            core_y = chip_core_match[3].str();
            blob_size = core_it.second;
            if (global_epoch_device_to_graph.find(global_epoch_id) == global_epoch_device_to_graph.end() ||
                global_epoch_device_to_graph.at(global_epoch_id).find(device_id) == global_epoch_device_to_graph.at(global_epoch_id).end()) {
                    log_warning(tt::LogCompileTrisc, "temporal epoch {} or device {} from blobgen.log doesn't exist in [epoch, device] to graph map.", global_epoch_id, device_id);
                    graph_name = "undefined";
            }
            else {
                graph_name = global_epoch_device_to_graph.at(global_epoch_id).at(device_id);
            }
            info_str = "Overlay blob size " + std::to_string(blob_size) + " bytes";
            info_str += ", at temporal epoch " + std::to_string(global_epoch_id);
            info_str += ", on device " + std::to_string(device_id);
            info_str += ", core x-y=" + core_x + "-" + core_y;
            info_str += ", running graph " + graph_name;

            all_compile_stats_info.push_back(info_str);
        }
    }
    return all_compile_stats_info;
}

const perf::PerfOverlayDecoupleMode get_overlay_decouplings_for_op(
    const tt_op_info& op_info, const perf::PerfDesc& perf_desc) {
    const string& op_name = op_info.name;
    perf::PerfOverlayDecoupleMode overlay_decoupling;
    if (perf_desc.overlay_decouplings.find(op_name) != perf_desc.overlay_decouplings.end()) {
        overlay_decoupling = perf_desc.overlay_decouplings.at(op_name);
    } else if (perf_desc.overlay_decouplings.find("*") != perf_desc.overlay_decouplings.end()) {
        overlay_decoupling = perf_desc.overlay_decouplings.at("*");
    }
    if (op_info.type == "matmul" && op_info.attributes.identity) {
        if (overlay_decoupling.all_inputs) {
            overlay_decoupling.all_inputs = 0;
            overlay_decoupling.input_operand_decouple = {1, 3};
        } else if (overlay_decoupling.input_operand_decouple.size() > 0) {
            overlay_decoupling.input_operand_decouple.erase(0);
            overlay_decoupling.input_operand_decouple.erase(2);
        }
    }
    return overlay_decoupling;
}

const unordered_set<perf::PerfTriscDecoupleMode> get_trisc_decouplings_for_op(
    const string& op_name, const perf::PerfDesc& perf_desc) {
    if (perf_desc.device_perf_mode == perf::PerfDumpMode::Disable) {
        return {};
    }
    if (perf_desc.trisc_decouplings.find("*") != perf_desc.trisc_decouplings.end()) {
        return perf_desc.trisc_decouplings.at("*");
    } else if (perf_desc.trisc_decouplings.find(op_name) != perf_desc.trisc_decouplings.end()) {
        return perf_desc.trisc_decouplings.at(op_name);
    } else {
        return {};
    }
}

const std::pair<bool, bool> check_perf_trisc_decouple_exist(const string& op_name, const perf::PerfDesc& perf_desc) {
    const unordered_set<perf::PerfTriscDecoupleMode> all_decoupled_res =
        get_trisc_decouplings_for_op(op_name, perf_desc);
    bool unp_math_decouple = all_decoupled_res.find(perf::PerfTriscDecoupleMode::UnpMath) != all_decoupled_res.end();
    bool math_pack_decouple = all_decoupled_res.find(perf::PerfTriscDecoupleMode::MathPack) != all_decoupled_res.end();
    return {unp_math_decouple, math_pack_decouple};
}

const uint16_t get_input_output_overlay_decouple_mask(
    const tt_op_info& op_info, const perf::PerfDesc& perf_desc) {

    if (perf_desc.overlay_decouplings.empty()) {
        return 0;
    }

    auto overlay_decouple = get_overlay_decouplings_for_op(op_info, perf_desc);
    const bool& input_overlay_decouple =
        overlay_decouple.all_inputs || !overlay_decouple.input_operand_decouple.empty();
    const bool& output_overlay_decouple = overlay_decouple.output;

    const std::pair<bool, bool>& trisc_decouple = check_perf_trisc_decouple_exist(op_info.name, perf_desc);
    const bool& decouple_unp_math = trisc_decouple.first;
    const bool& decouple_math_pack = trisc_decouple.second;

    if (decouple_unp_math || decouple_math_pack) {
        log_assert(
            !(input_overlay_decouple || output_overlay_decouple),
            "Trisc and overlay decouplings cannot be both enabled for the same op");
    }
    if (input_overlay_decouple || output_overlay_decouple) {
        log_assert(
            !(decouple_unp_math || decouple_math_pack),
            "Trisc and overlay decouplings cannot be both enabled for the same op");
    }
    uint8_t overlay_input_decouple_mask = 0;
    if (input_overlay_decouple) {
        if (op_info.type == "matmul" && op_info.attributes.identity) {
            log_assert(
                !overlay_decouple.all_inputs,
                "For sparse matmuls overlay decoupling cannot be enabled for all input operands");
            if (overlay_decouple.input_operand_decouple.size() > 0) {
                log_assert(
                    overlay_decouple.input_operand_decouple.find(0) == overlay_decouple.input_operand_decouple.end() &&
                        overlay_decouple.input_operand_decouple.find(2) ==
                            overlay_decouple.input_operand_decouple.end(),
                    "For sparse matmuls, input overlay decoupling can only be enabled for input-operand 1 or 3");
            }
        }
        if (overlay_decouple.all_inputs) {
            overlay_input_decouple_mask = 0xff;
        } else {
            for (const uint& idx : overlay_decouple.input_operand_decouple) {
                log_assert(idx < 8, "Invalid input operand index for overlay decoupling {}", idx);
                overlay_input_decouple_mask |= (0b1 << idx);
            }
        }
    }
    uint8_t overlay_output_decouple_mask = 0;
    if (output_overlay_decouple) {
        overlay_output_decouple_mask = 0xff;
    }
    log_debug(
        tt::LogCompileTrisc,
        "Overlay Perf Decouplings -- OP={}, OVERLAY_INPUT_DECOUPLE={}, OVERLAY_OUTPUT_DECOUPLE={}, "
        "overlay_input_decouple_mask = {}, overlay_output_decouple_mask = {}",
        op_info.name,
        std::to_string(input_overlay_decouple),
        std::to_string(output_overlay_decouple),
        std::to_string(overlay_input_decouple_mask),
        std::to_string(overlay_output_decouple_mask));

    return (uint16_t(overlay_output_decouple_mask) << 8) | (overlay_input_decouple_mask & 0xff);
}

const std::unordered_map<tt_xy_pair, uint16_t> get_input_output_overlay_decouple_mask_graph(const tt_graph_info &graph_info, const perf::PerfDesc &perf_desc, const buda_soc_description &sdesc) {
    std::unordered_map<tt_xy_pair, uint16_t> all_masks;
    if (!perf_desc.overlay_decouplings.empty()) {
        for (const auto &op_it: graph_info.op_map) {
            string op_name = op_it.first;
            const tt_op_info &op_info = op_it.second;

            int x_start = op_info.grid_loc_x();
            int y_start = op_info.grid_loc_y();
            int x_size = op_info.grid_size_x();
            int y_size = op_info.grid_size_y();
            for (int x = x_start; x < x_start + x_size; x++) {
                for (int y = y_start; y < y_start + y_size; y++) {
                    int physical_core_x = sdesc.worker_log_to_routing_x.at(x);
                    int physical_core_y = sdesc.worker_log_to_routing_y.at(y);

                    tt_xy_pair core_coord(physical_core_x, physical_core_y);
                    uint16_t mask = get_input_output_overlay_decouple_mask(op_info, perf_desc);
                    log_assert(all_masks.find(core_coord) == all_masks.end(), "Overlay decouple mask for core {} op {} graph {} already exists", core_coord.str(), op_name, graph_info.name);
                    all_masks.insert({core_coord, mask});
                }
            }
        }
        log_info(tt::LogPerfInfra, "Overlay decoupling is enabled for graph {}", graph_info.name);
        std::stringstream decoupling_str;
        for (const auto &decouple_it: all_masks) {
            decoupling_str << decouple_it.first.str() << ": Input_mask: 0x" << std::hex << (decouple_it.second & 0xff);
            decoupling_str << ", Output_mask: 0x" << std::hex << ((decouple_it.second >> 8) & 0xff);
            decoupling_str << " - ";
        }
        log_info(tt::LogPerfInfra, "Overlay decoupling masks for graph {} = {}", graph_info.name, decoupling_str.str());
    }
    return all_masks;
}

string get_dim_str(const Dim &dim) {
    if (dim == Dim::None) {
        return "none";
    } else if (dim == Dim::R) {
        return "r";
    } else if (dim == Dim::C) {
        return "c";
    } else if (dim == Dim::Z) {
        return "z";
    } else if (dim == Dim::RC) {
        return "rc";
    } else if (dim == Dim::ZR) {
        return "zr";
    } else if (dim == Dim::Invalid) {
        return "invalid";
    } else {
        log_fatal("Invalid Dim type {}", uint(dim));
        return ""; // Just to avoid compile errors
    }
}

tt_op_model_desc get_perf_model_desc(const tt_op_info &op_info) {
    tt_op_model_desc model_desc;
    string op_type = op_info.type;
    if (op_type == "datacopy") {
        op_type = "nop";
    }
    model_desc.type = op_type;
    model_desc.arch = get_string_lowercase(op_info.arch_name);
    model_desc.data_format = op_info.output_data_format;
    model_desc.math_fidelity = op_info.math_fidelity;

    model_desc.t = op_info.output_dim.t;
    model_desc.mblock_m = op_info.output_dim.mblock_m;
    model_desc.mblock_n = op_info.output_dim.mblock_n;
    model_desc.ublock_rt = op_info.output_dim.ublock_rt;
    model_desc.ublock_ct = op_info.output_dim.ublock_ct;

    model_desc.reduce_z = op_info.attributes.z;

    model_desc.mblock_k = op_info.attributes.m_k;
    model_desc.ublock_kt = op_info.attributes.u_kt;

    model_desc.sparse_indices = 0;

    model_desc.approx_mode = op_info.attributes.approximate_mode;
    model_desc.op_attr = get_dim_str(op_info.attributes.reduce_dim);
    return model_desc;
}

std::string get_tt_runtime_hostname() {
    const size_t MAX_HOSTNAME_LENGTH = 256;
    char hostname[MAX_HOSTNAME_LENGTH];
    const std::string hostname_str = (gethostname(hostname, MAX_HOSTNAME_LENGTH) == 0) ? hostname : "unknown hostname";
    return hostname_str;
}

}

std::ostream &operator<<(std::ostream &os, std::pair<std::string, std::set<std::string>> const &pair) {
    os << pair.first << " -> ";
    for (auto &item : pair.second) os << item << ", ";
    return os;
}

std::ostream &operator<<(std::ostream &os, std::set<int> const &set) {
    os << "{";
    std::set<int>::iterator it;
    for (it = set.begin(); it != set.end(); ++it) {
        if (it != set.begin()) os << ", ";
        os << (*it);
    }
    os << "}";
    return os;
}

std::ostream &operator<<(std::ostream &os, std::set<string> const &set) {
    os << "{";
    std::set<string>::iterator it;
    for (it = set.begin(); it != set.end(); ++it) {
        if (it != set.begin()) os << ", ";
        os << (*it);
    }
    os << "}";
    return os;
}

std::ostream &operator<<(std::ostream &os, std::set<tt_xy_pair> const &set) {
    os << "{";
    std::set<tt_xy_pair>::iterator it;
    for (it = set.begin(); it != set.end(); ++it) {
        if (it != set.begin()) os << ", ";
        os << (*it).x << "-" << (*it).y;
    }
    os << "}";
    return os;
}
