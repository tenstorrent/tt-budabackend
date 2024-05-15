// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "llk_assemble_kernels.h"

#include <glog/logging.h>
#include <llk_addresses.h>

#include <cstddef>
#include <experimental/filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#include "stdlib.h"

bool llk::assemble_kernels::run_command(const std::string &cmd) {
    int ret;
    if (getenv("PRINT_CK_COMPILE")) {
        LOG(INFO) << "running: `" + cmd + '`';
        ret = system(cmd.c_str());
    } else {
        std::string redirected_cmd = cmd + " >assemble_ck_command.dump";
        ret = system(redirected_cmd.c_str());
    }
    return (ret == 0);
}

void llk::assemble_kernels::generate_kernel_param_file(
    const std::string &param_filename,
    const std::string &param_output_filename,
    const std::string &outdir,
    const std::map<std::string, std::string> &kernel_params) {
    std::experimental::filesystem::path input_path(param_filename);
    std::experimental::filesystem::path output_path(param_output_filename);
    if (not std::experimental::filesystem::exists(input_path.parent_path())) {
        std::experimental::filesystem::create_directory(input_path.parent_path());
    }
    if (not std::experimental::filesystem::exists(output_path.parent_path())) {
        std::experimental::filesystem::create_directory(output_path.parent_path());
    }
    std::ifstream input_param_file(param_filename);
    std::ofstream output_param_file(param_output_filename);
    std::string line;
    while (std::getline(input_param_file, line)) {
        for (const auto &it : kernel_params) {
            auto &key = it.first;
            auto &value = it.second;
            auto start_pos = line.find(key);
            if (start_pos != std::string::npos) {
                line.replace(line.find(key), key.length(), value);
            }
        }
        output_param_file << line << std::endl;
    }
}

void llk::assemble_kernels::compile_kernels(
    std::string kernel_param_dir,
    std::string output_dir,
    std::string make_src_args,
    std::string make_args,
    std::string test_name,
    std::uint32_t trisc_mailbox_addr,
    int thread_id,
    bool hlkc_test,
    std::string used_kernels,
    bool perf_dump,
    std::string root) {
    std::stringstream make_cmd;
    std::stringstream make_gen_cmd;
    std::stringstream make_src_cmd;
    std::string full_output_dir = std::experimental::filesystem::absolute(output_dir).string();
    std::size_t found = used_kernels.find_first_of(" ");
    while (found != std::string::npos) {
        used_kernels.erase(found, 1);
        found = used_kernels.find_first_of(" ", found + 1);
    }

    // Build ckernels/src
    make_src_cmd << " make " << make_src_args;
    make_src_cmd << " KERNELS='" << used_kernels << '\'';
    make_src_cmd << " KERNEL_PARAM_OVERRIDE_DIR='" << kernel_param_dir << '\'';
    make_src_cmd << " TEST_NAME='" << test_name << '\'';
    make_src_cmd << " OUTPUT_DIR=" << full_output_dir;
    make_src_cmd << " MAILBOX_ADDR=" << trisc_mailbox_addr;
    make_src_cmd << " BUDA_HOME=" << root;

    LOG(INFO) << __FUNCTION__ << " -- Kernel Make command: " << make_src_cmd.str() << "\n";
    if (!llk::assemble_kernels::run_command(make_src_cmd.str())) {
        LOG(FATAL) << "Build ckernels/src failed for a thread " << (std::uint32_t)thread_id << " with CKernels '"
                   << used_kernels << '\'';
    }

    // Detect HLKC generated kernel and dont send the kernel list. inclusion is controlled through defines
    std::string no_kernels = "";
    std::string kernel_type = used_kernels.substr(0, 5);
    bool is_hlkc_kernel = kernel_type == "chlkc";

    make_cmd << " make " << make_args;
    make_cmd << " FIRMWARE_NAME=tensix_thread" << (std::uint32_t)thread_id;
    make_cmd << " KERNELS='" << (is_hlkc_kernel ? no_kernels : used_kernels) << '\'';
    // make_cmd << " KERNELS='" << used_kernels << '\'';
    make_cmd << " LINKER_SCRIPT_NAME=trisc" << (std::uint32_t)thread_id << ".ld";
    make_cmd << " TEST=" << test_name;
    make_cmd << " OUTPUT_DIR=" << full_output_dir;
    make_cmd << " CKERNELS_COMMON_OUT_DIR=" << full_output_dir;
    make_cmd << " COMPILED_KERNELS_DIR=" << full_output_dir;
    make_cmd << " CLEAN_OUTPUT_DIR=0";
    make_cmd << " BUDA_HOME=" << root;

    LOG(INFO) << __FUNCTION__ << " -- Kernel Link command: " << make_cmd.str() << "\n";
    if (!llk::assemble_kernels::run_command(make_cmd.str())) {
        LOG(FATAL) << "Build failed for a thread " << (std::uint32_t)thread_id << " with CKernels '" << used_kernels
                   << '\'';
    }
}

void llk::assemble_kernels::build_kernel_memories(
    llk::SocDescriptor soc_descriptor,
    std::string root,
    bool hlkc_test,
    std::string test_name,
    std::string test_dir,
    std::unordered_map<llk::xy_pair, std::vector<std::vector<std::string>>> &kernels_used,
    std::unordered_map<llk::xy_pair, std::vector<llk::memory>> &kernel_memories,
    std::unordered_map<llk::xy_pair, std::string> &fwlogs,
    bool perf_dump) {
    ////////////////////////////////////////////////////////////////////
    //                    ASSEMBLE + LOAD  CKERNELS                   //
    ////////////////////////////////////////////////////////////////////
    std::stringstream arch_name_ss;
    operator<<(arch_name_ss, soc_descriptor.arch);
    std::string arch_name = arch_name_ss.str();
    const std::string make_ckernels_compile_dir = root + "/src/ckernels/" + arch_name + "/src";
    const std::string make_ckernels_link_dir = root + "/src/ckernels/";

    const std::string output_dir = test_dir + "/kernel_builds/";
    if (std::experimental::filesystem::exists(output_dir)) {
        std::experimental::filesystem::remove_all(output_dir);
    }

    std::string make_src_args = "-C ";
    make_src_args += make_ckernels_compile_dir;
    make_src_args += hlkc_test ? " HLKC_KERNELS=1 " : " HLKC_KERNELS=0 ";
    make_src_args += perf_dump ? " PERF_DUMP=1 " : " PERF_DUMP=0 ";
    make_src_args += " LLK_TB_TEST=1 ";
    make_src_args += "CKERNEL_INC_OVERRIDE_DIR=" + root + "/src/ckernels/";
    make_src_args += " -j8 ";
    std::uint32_t trisc_mailbox_addresses[3] = {
        (std::uint32_t)l1_mem::address_map::TRISC_BASE + (std::uint32_t)l1_mem::address_map::TRISC_L1_MAILBOX_OFFSET,
        (std::uint32_t)l1_mem::address_map::TRISC_BASE + (std::uint32_t)soc_descriptor.trisc_sizes[0] +
            (std::uint32_t)l1_mem::address_map::TRISC_L1_MAILBOX_OFFSET,
        (std::uint32_t)l1_mem::address_map::TRISC_BASE + (std::uint32_t)soc_descriptor.trisc_sizes[0] +
            (std::uint32_t)soc_descriptor.trisc_sizes[1] + (std::uint32_t)l1_mem::address_map::TRISC_L1_MAILBOX_OFFSET};

    std::string make_args = "-C ";
    make_args += make_ckernels_link_dir;
    // Add Trisc sizes to Make arg command
    make_args += " TRISC0_SIZE=" + std::to_string(soc_descriptor.trisc_sizes[0]) +
                 " TRISC1_SIZE=" + std::to_string(soc_descriptor.trisc_sizes[1]) +
                 " TRISC2_SIZE=" + std::to_string(soc_descriptor.trisc_sizes[2]);
    make_args += " ARCH_NAME=" + arch_name;
    make_args += " TRISC_BASE=" + std::to_string(l1_mem::address_map::TRISC_BASE);

    std::vector<llk::memory> kernel_blank_memory;
    std::stringstream merged_kernel_blank_fwlog;

    std::vector<std::thread> ths(3);
    for (int thread_id = 0; thread_id < soc_descriptor.trisc_sizes.size(); thread_id++) {
        std::string used_kernels = "cblank";
        std::stringstream ckernels_compile_output_dir;
        ckernels_compile_output_dir << output_dir << "tensix_thread" << (uint)thread_id << "_blank";
        std::vector<std::string> used_kernels_vector;
        std::string blank_kernels_fwlog_string = "";
        ths[thread_id] = std::thread(
            llk::assemble_kernels::compile_kernels,
            test_dir + test_name + "_params",
            ckernels_compile_output_dir.str(),
            make_src_args,
            make_args,
            test_name,
            trisc_mailbox_addresses[thread_id],
            thread_id,
            hlkc_test,
            used_kernels,
            perf_dump,
            root);
    }

    for (int thread_id = 0; thread_id < soc_descriptor.trisc_sizes.size(); thread_id++) {
        ths[thread_id].join();
        std::string used_kernels = "cblank";
        std::stringstream ckernels_compile_output_dir;
        ckernels_compile_output_dir << output_dir << "tensix_thread" << (uint)thread_id << "_blank";
        std::vector<std::string> used_kernels_vector;
        std::string blank_kernels_fwlog_string = "";
        std::stringstream tensix_thread_hex_path;
        tensix_thread_hex_path << ckernels_compile_output_dir.str() << "/tensix_thread" << (std::uint32_t)thread_id
                               << ".hex";
        std::ifstream tensix_thread_hex_istream(tensix_thread_hex_path.str());
        llk::memory ckernel_memory = llk::memory::from_discontiguous_risc_hex(
            tensix_thread_hex_istream, (llk::memory::risc_type_t)((int)llk::memory::TRISC0 + thread_id));
        kernel_blank_memory.push_back(ckernel_memory);
        merged_kernel_blank_fwlog << blank_kernels_fwlog_string;
    }
    if (kernels_used.empty()) {
        throw std::runtime_error("kernels_used is empty");
    }
    for (const auto &it : kernels_used) {
        const auto coord = it.first;
        const auto kernels_used_per_core = it.second;
        if (not soc_descriptor.has(coord)) {
            LOG(WARNING) << "Coord does not exist in SOC -- Ignoring kernels for " << coord.str();
            continue;
        }
        std::vector<llk::memory> kernel_memory_per_core;
        if (kernels_used_per_core.size() > 0) {
            std::stringstream merged_kernel_fwlog;
            for (int thread_id = 0; thread_id < soc_descriptor.trisc_sizes.size(); thread_id++) {
                const auto kernels_used_per_trisc = kernels_used_per_core[thread_id];
                std::stringstream ckernels_compile_output_dir;
                ckernels_compile_output_dir << output_dir << "tensix_thread" << (uint)thread_id;
                std::stringstream tensix_thread_hex_path;
                tensix_thread_hex_path << ckernels_compile_output_dir.str() << "/tensix_thread" << thread_id << ".hex";
                std::string kernels_used_string;
                for (const auto &kernel_name : kernels_used_per_trisc) {
                    kernels_used_string += kernel_name + " ";
                }
                LOG(INFO) << "(" << coord.x << "," << coord.y << ") Thread #" << thread_id
                          << "Kernels: " << kernels_used_string;

                ths[thread_id] = std::thread(
                    llk::assemble_kernels::compile_kernels,
                    test_dir + test_name + "_params",
                    ckernels_compile_output_dir.str(),
                    make_src_args,
                    make_args,
                    test_name,
                    trisc_mailbox_addresses[thread_id],
                    thread_id,
                    hlkc_test,
                    kernels_used_string,
                    perf_dump,
                    root);
            }

            for (int thread_id = 0; thread_id < soc_descriptor.trisc_sizes.size(); thread_id++) {
                ths[thread_id].join();
                const auto kernels_used_per_trisc = kernels_used_per_core[thread_id];
                std::stringstream ckernels_compile_output_dir;
                ckernels_compile_output_dir << output_dir << "tensix_thread" << (uint)thread_id;
                std::stringstream tensix_thread_hex_path;
                tensix_thread_hex_path << ckernels_compile_output_dir.str() << "/tensix_thread" << thread_id << ".hex";

                std::string kernels_fwlog_string;
                std::ifstream tensix_thread_hex_istream(tensix_thread_hex_path.str());

                std::stringstream per_thread_fwlog_path;
                per_thread_fwlog_path << output_dir << "/tensix_thread" << (std::uint32_t)thread_id << "/ckernel.fwlog";
                std::ifstream per_thread_fwlog_istream(per_thread_fwlog_path.str());
                if (not per_thread_fwlog_istream) {
                    throw std::runtime_error("unable to read fwlog " + per_thread_fwlog_path.str());
                }
                std::stringstream fwlog_thread_stringstream;
                fwlog_thread_stringstream << per_thread_fwlog_istream.rdbuf();
                kernels_fwlog_string = fwlog_thread_stringstream.str();

                merged_kernel_fwlog << kernels_fwlog_string;
                llk::memory ckernel_memory = llk::memory::from_discontiguous_risc_hex(
                    tensix_thread_hex_istream, (llk::memory::risc_type_t)((int)llk::memory::TRISC0 + thread_id));
                kernel_memory_per_core.push_back(ckernel_memory);
            }
            if (kernel_memory_per_core.size() == 0) {
                kernel_memories.insert({coord, kernel_blank_memory});
            } else {
                // put per-core kernel_memories into kernel_memories
                kernel_memories.insert({coord, kernel_memory_per_core});
            }
            if (merged_kernel_fwlog.str().size() == 0) {
                fwlogs.insert({coord, merged_kernel_blank_fwlog.str()});
            } else {
                fwlogs.insert({coord, merged_kernel_fwlog.str()});
            }
        } else {
            kernel_memories.insert({coord, kernel_blank_memory});
            fwlogs.insert({coord, merged_kernel_blank_fwlog.str()});
        }
    }
}
