// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "llk_device.h"

#include <glog/logging.h>
#include <stdlib.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <thread>

#include "experimental/filesystem"

// llk specific headers
#include "llk_addresses.h"
#include "llk_assemble_kernels.h"
#include "llk_tensor_ops.h"

// Device headers
#include "device.h"
#include "sim_interactive.h"

namespace CA = CommandAssembler;

using namespace llk::assemble_kernels;

string llk::to_string(llk::ThreadType thread) {
    if (thread == ThreadType::Unpack) {
        return "ThreadType::Unpack";
    } else if (thread == ThreadType::Math) {
        return "ThreadType::Math";
    } else if (thread == ThreadType::Pack) {
        return "ThreadType::Pack";
    } else if (thread == ThreadType::Ncrisc) {
        return "ThreadType::Ncrisc";
    } else if (thread == ThreadType::Firmware) {
        return "ThreadType::Firmware";
    } else {
        assert(0 && "Invalid Threadtype");
    }
}

string llk::get_overlay_version(const ARCH arch_name) {
    if (arch_name == ARCH::GRAYSKULL) {
        return "1";
    } else if ((arch_name == ARCH::WORMHOLE) || (arch_name == ARCH::WORMHOLE_B0)) {
        return "2";
    } else {
        throw std::runtime_error("Invalid ARCH passed in");
    }
}

void llk::Device::generate_overlay_blob(
    std::string root, std::string output_root, std::map<std::string, std::string> blob_args) {
    std::stringstream gen_blob_cmd;
    gen_blob_cmd << "ruby " << root << "/tb/llk_tb/overlay/blob_gen.rb ";
    gen_blob_cmd << "--blob_out_dir " << output_root << "/overlay/out  ";
    gen_blob_cmd << "--root " + root;
    for (auto const &[key, val] : blob_args) {
        gen_blob_cmd << " --" << key << " " << val;
    }
    const std::string arch = get_arch_string(soc_descriptor.arch);
    const std::string overlay_version = get_overlay_version(soc_descriptor.arch);

    gen_blob_cmd << " --chip " << arch << " ";
    gen_blob_cmd << " --noc_version " << overlay_version << " ";
    LOG(INFO) << " *** Generate blob command: " << gen_blob_cmd.str() << "\n";
    if (system(gen_blob_cmd.str().c_str()) != 0) {
        LOG(FATAL) << __FUNCTION__ << "::failed";
    }
}
void llk::Device::init(std::string root, std::string device_descriptor_filename) {
    VLOG(3) << __FUNCTION__ << "::Running ";
    const auto device_descriptor_dir = root + "/tb/llk_tb/device_descriptor/";
    VLOG(4) << __FUNCTION__ << "::load_soc_descriptor_from_yaml ";
    soc_descriptor = llk::load_soc_descriptor_from_yaml(device_descriptor_dir + device_descriptor_filename);
}

void llk::Device::start(
    std::string root,
    std::string output_root,
    std::vector<std::string> plusargs,
    std::vector<std::string> dump_cores,
    YAML::Node &testdef_yaml,
    std::string overlay_graph) {
    VLOG(3) << __FUNCTION__ << "::Running ";
    const auto firmware_dir = root + "/build/src/firmware/riscv/targets/brisc/out";
    const auto ncrisc_fw_dir = root + "/build/src/firmware/riscv/targets/ncrisc/out";
    const auto overlay_dir = output_root + "/overlay/out/";

    std::optional<std::string> vcd_suffix;
    if (dump_cores.size() > 0) {
        vcd_suffix = "core_dump.vcd";
    }

    std::vector<std::string> vcd_cores;

    // TODO: For now create a temporary stuff from CA and populate from descriptor before passing back to versim-core
    // interface. mainly bypasses arch_configs etc from llir.  We can populate soc directly
    CA::Soc ca_soc_manager(soc_descriptor.grid_size);
    llk::translate_soc_descriptor_to_ca_soc(ca_soc_manager, soc_descriptor);
    // TODO: End

    VLOG(4) << __FUNCTION__ << "::turn_on_device ";
    const auto soc_grid_size = soc_descriptor.grid_size;
    bool no_checkers = true;
    std::vector<std::uint32_t> trisc_sizes = {
        l1_mem::address_map::TRISC0_SIZE, l1_mem::address_map::TRISC1_SIZE, l1_mem::address_map::TRISC2_SIZE};
    std::unique_ptr<versim::VersimSimulator> versim_unique = versim::turn_on_device(
        soc_grid_size,
        ca_soc_manager,
        plusargs,
        vcd_suffix,
        dump_cores,
        no_checkers,
        l1_mem::address_map::TRISC_BASE,
        trisc_sizes);
    versim = versim_unique.release();

    VLOG(4) << __FUNCTION__ << "::write_info_to_tvm_db ";
    versim::write_info_to_tvm_db(l1_mem::address_map::TRISC_BASE, trisc_sizes);

    VLOG(4) << __FUNCTION__ << "::write_testdef_yaml ";
    versim::write_testdef_yaml(testdef_yaml);

    versim::build_and_connect_tvm_phase();

    versim->spin_threads(ca_soc_manager, false);
    versim::assert_reset(*versim);

    // Briscv firmware -- 0x0
    VLOG(4) << __FUNCTION__ << "::load_firmware - brisc_fw_dir: " << firmware_dir;
    ifstream firmware_hex_istream(firmware_dir + "/brisc.hex");
    auto firmware_memory = llk::memory::from_discontiguous_risc_hex(firmware_hex_istream, llk::memory::BRISC);
    for (const auto &it : soc_descriptor.cores) {
        auto core_coord = it.first;
        auto core_descriptor = it.second;
        if (core_descriptor.type == llk::CoreType::WORKER) {
            nuapi::device::write_memory_to_core(*versim, core_coord, firmware_memory);
            VLOG(4) << __FUNCTION__ << "::load_firmware - brisc fw loaded";
        }
    }

    VLOG(4) << __FUNCTION__ << "::load_ncrisc - ncrisc_fw_dir: " << ncrisc_fw_dir;
    ifstream ncrisc_firmware_hex_istream(ncrisc_fw_dir + "/ncrisc.hex");
    auto ncrisc_firmware_memory =
        llk::memory::from_discontiguous_risc_hex(ncrisc_firmware_hex_istream, llk::memory::NCRISC);
    VLOG(4) << __FUNCTION__ << "::load_ncrisc - ncrisc fw loaded";
    for (const auto &it : soc_descriptor.cores) {
        auto core_coord = it.first;
        auto core_descriptor = it.second;
        if (core_descriptor.type == llk::CoreType::WORKER) {
            nuapi::device::write_memory_to_core(*versim, core_coord, ncrisc_firmware_memory);
        }
    }

    VLOG(4) << __FUNCTION__ << "::load_overlay - overlay_graph = " << overlay_graph;
    if (!overlay_graph.empty()) {
        for (const auto &it : soc_descriptor.cores) {
            auto core_coord = it.first;
            auto core_descriptor = it.second;
            const auto coord_str = "_0_" + std::to_string(core_coord.x) + "_" + std::to_string(core_coord.y);
            const auto overlay_blob = overlay_graph + coord_str + ".hex";
            VLOG(4) << __FUNCTION__ << "::load_overlay - blob = " << overlay_blob;
            ifstream blob_hex_istream(overlay_dir + overlay_blob);
            auto blob_memory = llk::memory::from_discontiguous_hex(blob_hex_istream);
            if (core_descriptor.type == llk::CoreType::WORKER) {
                nuapi::device::write_memory_to_core(*versim, core_coord, blob_memory);
            }
        }
    }

    versim::handle_resetting_triscs(*versim);

    VLOG(3) << __FUNCTION__ << "::Done ";
}

// TODO: Refactor out the trisc memory assembling code to helper function
void llk::Device::setup_kernels(
    std::string root,
    std::unordered_map<llk::xy_pair, std::vector<std::vector<std::string>>> &kernels_used,
    std::string test_name,
    std::string test_dir,
    bool hlkc_test,
    bool perf_dump) {
    VLOG(3) << __FUNCTION__ << "::Running ";

    std::unordered_map<llk::xy_pair, std::vector<llk::memory>> kernel_memories;
    std::unordered_map<llk::xy_pair, std::string> fwlogs;
    std::unordered_map<CA::xy_pair, std::string> fwlogs_CA;  // TODO Remove this in future...
    llk::assemble_kernels::build_kernel_memories(
        soc_descriptor, root, hlkc_test, test_name, test_dir, kernels_used, kernel_memories, fwlogs, perf_dump);

    for (const auto &it : kernel_memories) {
        auto coord = it.first;
        auto kernel_memories_per_core = it.second;
        if (not soc_descriptor.has(coord)) {
            LOG(WARNING) << "Coord does not exist in SOC -- Not loading kernels for " << coord.str();
            continue;
        }
        for (const auto &kernel_memory : kernel_memories_per_core) {
            nuapi::device::write_memory_to_core(*versim, coord, kernel_memory);
        }
    }

    ////////////////////////////////////////////////////////////////////
    //                    ASSEMBLE + LOAD FW/CK LOGS                  //
    ////////////////////////////////////////////////////////////////////
    // TODO:  Casts coord to CA for versim connection.  Need to remove

    for (const auto &it : fwlogs) {
        fwlogs_CA.insert({CA::xy_pair(it.first.x, it.first.y), it.second});
    }
    versim::load_fwlogs(*versim, "", fwlogs_CA);  // TODO: Need to add global fw logs file to the empty string

    VLOG(3) << __FUNCTION__ << "::Done ";
}

bool llk::Device::run() {
    VLOG(3) << __FUNCTION__ << "::Running ";

    // Run Versim main_loop
    versim::startup_versim_main_loop(*versim);

    VLOG(3) << __FUNCTION__ << "::Done ";
    return true;
}

void llk::Device::prepare_tensor_for_streaming(
    llk::Tensor &tensor, std::string output_directory_path, llk::xy_pair num_blocks) {
    VLOG(3) << __FUNCTION__ << "::Running ";
    bool aligned_32B = true;  // soc_descriptor.cores[target].type == llk::CoreType::DRAM;
    // Convert Tensor to packed form + assemble tiles into memory
    llk::Tensor packed_tensor;
    llk::tensor_ops::pack(packed_tensor, tensor);
    std::vector<llk::memory> packed_tensor_header_memories;
    std::vector<llk::memory> packed_tensor_data_memories;
    // Don't need address for streaming at this stage, since we are just construction the raw hex to push to IO folder
    packed_tensor.assemble_tiles_to_memories(
        packed_tensor_header_memories, packed_tensor_data_memories, 0, aligned_32B, num_blocks);
    // Pack the memories into a work folder.

    std::experimental::filesystem::path header_path(output_directory_path + tensor.name + "/header_memories/");
    std::experimental::filesystem::path data_path(output_directory_path + tensor.name + "/data_memories/");
    if (not std::experimental::filesystem::exists(header_path)) {
        std::experimental::filesystem::create_directories(header_path);
    }
    if (not std::experimental::filesystem::exists(data_path)) {
        std::experimental::filesystem::create_directories(data_path);
    }
    for (int tile_id = 0; tile_id < tensor.dims.num_tiles(); tile_id++) {
        std::ostringstream filename_ss;
        filename_ss << std::setfill('0') << std::setw(8) << std::hex << tile_id << ".hex";
        std::experimental::filesystem::path output_header_file_path = header_path / filename_ss.str();
        std::experimental::filesystem::path output_data_file_path = data_path / filename_ss.str();

        ofstream output_header_hex_stream;
        output_header_hex_stream.open(output_header_file_path, std::fstream::out);
        packed_tensor_header_memories[tile_id].write_hex(output_header_hex_stream, false);
        output_header_hex_stream.close();

        ofstream output_data_hex_stream;
        output_data_hex_stream.open(output_data_file_path.c_str(), std::fstream::out);
        packed_tensor_data_memories[tile_id].write_hex(output_data_hex_stream, false);
        output_data_hex_stream.close();
    }
    VLOG(3) << __FUNCTION__ << "::Done ";
}

void llk::Device::write_tensor(
    llk::Tensor &tensor, llk::xy_pair target, std::int32_t address, llk::xy_pair num_blocks, bool untilize) {
    VLOG(3) << __FUNCTION__ << "::Running ";
    bool aligned_32B = true;  // soc_descriptor.cores[target].type == llk::CoreType::DRAM;
    if (untilize) {
        tensor.untilize_layout();
    }
    // Convert Tensor to packed form + assemble tiles into memory
    llk::Tensor packed_tensor;
    llk::tensor_ops::pack(packed_tensor, tensor, untilize);
    llk::memory packed_tensor_memory;
    packed_tensor.assemble_tiles_to_memory(packed_tensor_memory, address, aligned_32B, num_blocks);
    nuapi::device::write_memory_to_core(*versim, target, packed_tensor_memory);
    VLOG(3) << __FUNCTION__ << "::Done ";
}

void llk::Device::read_tensor_for_streaming(llk::Tensor &tensor, std::string directory_path) {
    VLOG(3) << __FUNCTION__ << "::Running ";

    std::experimental::filesystem::path header_path(directory_path + tensor.name + "/header_memories/");
    std::experimental::filesystem::path data_path(directory_path + tensor.name + "/data_memories/");
    if (not std::experimental::filesystem::exists(header_path)) {
        std::experimental::filesystem::create_directories(header_path);
    }
    if (not std::experimental::filesystem::exists(data_path)) {
        std::experimental::filesystem::create_directories(data_path);
    }
    size_t size_in_words = tensor.num_bytes_when_assembled(false) / 4;
    std::vector<uint32_t> assembled_data;
    for (int tile_id = 0; tile_id < tensor.dims.num_tiles(); tile_id++) {
        std::ostringstream filename_ss;
        filename_ss << std::setfill('0') << std::setw(8) << std::hex << tile_id << ".hex";
        std::experimental::filesystem::path header_file_path = header_path / filename_ss.str();
        std::experimental::filesystem::path data_file_path = data_path / filename_ss.str();

        if (not std::experimental::filesystem::exists(header_file_path)) {
            std::cout << header_file_path.c_str();
            throw std::runtime_error("header_file_path doesn't exist");
        }
        if (not std::experimental::filesystem::exists(data_file_path)) {
            std::cout << data_file_path.c_str();
            throw std::runtime_error("data_file_path doesn't exist");
        }
        std::ifstream header_hex_stream(header_file_path);
        header_hex_stream >> hex;
        std::vector<uint32_t> header =
            std::vector<uint32_t>(istream_iterator<uint32_t>(header_hex_stream), istream_iterator<uint32_t>());
        assembled_data.insert(assembled_data.end(), header.begin(), header.end());
        header_hex_stream.close();
        std::ifstream data_hex_stream(data_file_path);
        data_hex_stream >> hex;
        std::vector<uint32_t> data =
            std::vector<uint32_t>(istream_iterator<uint32_t>(data_hex_stream), istream_iterator<uint32_t>());
        assembled_data.insert(assembled_data.end(), data.begin(), data.end());
        data_hex_stream.close();
    }
    tensor.import_from_mem_vector(tensor.dims, assembled_data, false);
    VLOG(3) << __FUNCTION__ << "::Done ";
}

void llk::Device::read_tensor(llk::Tensor &tensor, llk::xy_pair target, std::int32_t address, bool untilized) {
    VLOG(3) << __FUNCTION__ << "::Running ";
    size_t size_in_words = tensor.num_bytes_when_assembled(untilized) / 4;
    std::vector<uint32_t> read_vector = nuapi::device::read_memory_from_core(*versim, target, address, size_in_words);
    tensor.import_from_mem_vector(tensor.dims, read_vector, untilized);
    VLOG(3) << __FUNCTION__ << "::Done ";
}

void llk::Device::write_vector(std::vector<uint32_t> &mem_vector, llk::xy_pair target, std::int32_t address) {
    VLOG(3) << __FUNCTION__ << "::Running ";

    // Convert Tensor to packed form + assemble tiles into memory
    llk::memory packed_tensor_memory(address, mem_vector);
    nuapi::device::write_memory_to_core(*versim, target, packed_tensor_memory);
    VLOG(3) << __FUNCTION__ << "::Done ";
}

void llk::Device::read_vector(
    std::vector<uint32_t> &mem_vector, llk::xy_pair target, std::int32_t address, std::int32_t size_in_bytes) {
    VLOG(3) << __FUNCTION__ << "::Running ";

    size_t size_in_words = size_in_bytes / 4;
    mem_vector = nuapi::device::read_memory_from_core(*versim, target, address, size_in_words);

    VLOG(3) << __FUNCTION__ << "::Done ";
}

void llk::Device::write_register(uint32_t &value, llk::xy_pair target, std::int32_t address) {
    VLOG(3) << __FUNCTION__ << "::Running ";
    versim::write_register_to_core(*versim, target, address, value);
    VLOG(3) << __FUNCTION__ << "::Done ";
}

void llk::Device::read_register(uint32_t &value, llk::xy_pair target, std::int32_t address) {
    VLOG(3) << __FUNCTION__ << "::Running ";
    value = versim::read_register_from_core(*versim, target, address);
    VLOG(3) << __FUNCTION__ << "::Done ";
}

// Meant to breakout running functions for simulator
bool llk::Device::stop() {
    VLOG(3) << __FUNCTION__ << "::Running ";
    versim::turn_off_device(*versim);
    versim->shutdown();
    // Force free of all versim cores
    for (auto x = 0; x < versim->grid_size.x; x++) {
        for (auto y = 0; y < versim->grid_size.y; y++) {
            delete versim->core_grid.at(x).at(y);
        }
    }
    VLOG(3) << __FUNCTION__ << "::Done ";
    return true;
}
bool llk::Device::wait_completion(int timeout, std::vector<ThreadType> threads_to_check) {
    VLOG(3) << __FUNCTION__ << "::Running ";

    const std::uint32_t NUM_RISCS = 5;

    std::uint32_t risc_mailbox_addresses[NUM_RISCS] = {
        (std::uint32_t)l1_mem::address_map::TRISC_BASE + (std::uint32_t)l1_mem::address_map::TRISC_L1_MAILBOX_OFFSET,
        (std::uint32_t)l1_mem::address_map::TRISC_BASE + (std::uint32_t)soc_descriptor.trisc_sizes[0] +
            (std::uint32_t)l1_mem::address_map::TRISC_L1_MAILBOX_OFFSET,
        (std::uint32_t)l1_mem::address_map::TRISC_BASE + (std::uint32_t)soc_descriptor.trisc_sizes[0] +
            (std::uint32_t)soc_descriptor.trisc_sizes[1] + (std::uint32_t)l1_mem::address_map::TRISC_L1_MAILBOX_OFFSET,
        (std::uint32_t)l1_mem::address_map::NCRISC_FIRMWARE_BASE +
            (std::uint32_t)l1_mem::address_map::NRISC_L1_MAILBOX_OFFSET,
        (std::uint32_t)l1_mem::address_map::FIRMWARE_BASE +
            (std::uint32_t)l1_mem::address_map::BRISC_L1_MAILBOX_OFFSET};
    int prev_sim_time = get_sim_time(*versim);

    for (auto thread : threads_to_check) {
        std::cout << "Waiting for: " << to_string(thread) << endl;
    }
    for (const auto &it : soc_descriptor.cores) {
        auto core_coord = it.first;
        auto core_descriptor = it.second;
        if (core_descriptor.type == llk::CoreType::WORKER) {
            bool exit;
            do {
                uint32_t risc_mailbox_val[NUM_RISCS];
                exit = true;
                for (auto thread : threads_to_check) {
                    int thread_id = static_cast<int>(thread);
                    std::vector<uint32_t> read_mem =
                        nuapi::device::read_memory_from_core(*versim, core_coord, risc_mailbox_addresses[thread_id], 1);
                    risc_mailbox_val[thread_id] = read_mem[0];
                    exit = exit && (risc_mailbox_val[thread_id] == 0x1);
                }
                int sim_time = get_sim_time(*versim);
                if (sim_time > timeout) {
                    cout << "[TIMEOUT] @" << sim_time << " cycles elapsed -- specified timeout of " << timeout
                         << " cycles" << endl;
                    exit = true;
                }
            } while (!exit);
        }
    }

    VLOG(3) << __FUNCTION__ << "::Done ";
    return true;
}
bool llk::Device::test_write_read(llk::xy_pair target) {
    std::vector<uint32_t> test_vector1(30, 0xDEADBEEF);
    std::vector<uint32_t> test_vector2(30, 0xDEAD0000);
    this->write_vector(test_vector1, target, 512 * 1024);
    this->write_vector(test_vector2, target, 512 * 1024 + 120);
    std::vector<uint32_t> read_data;
    this->read_vector(read_data, target, 512 * 1024, 240);
    bool result = true;
    for (int i = 0; i < test_vector1.size(); i++) {
        if (test_vector1[i] != read_data[i]) {
            result = false;
            LOG(ERROR) << __FUNCTION__ << "Mismatch in " << i << "th index -- expected: 0x" << std::hex
                       << test_vector1[i] << " got: 0x" << read_data[i];
        }
    }
    for (int i = 0; i < test_vector2.size(); i++) {
        if (test_vector2[i] != read_data[i + test_vector1.size()]) {
            result = false;
            LOG(ERROR) << __FUNCTION__ << "Mismatch in " << i << "th index -- expected: 0x" << std::hex
                       << test_vector2[i] << " got: 0x" << read_data[i + test_vector1.size()];
        }
    }
    return result;
}
