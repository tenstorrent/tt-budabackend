// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// llk specific headers
#include "llk_memory.h"
#include "llk_soc_descriptor.h"
#include "llk_xy_pair.h"

namespace llk {
//! assemble_kernels meant to assemble and build kernel memories
namespace assemble_kernels {

bool run_command(const std::string &cmd);
void generate_kernel_param_file(
    const std::string &kernel_name,
    const std::string &generate_kernel_param_file,
    const std::string &outdir,
    const std::map<std::string, std::string> &kernel_params = {});
void compile_kernels(
    std::string kernel_param_dir,
    std::string output_dir,
    std::string make_src_args,
    std::string make_args,
    std::string test_name,
    std::uint32_t trisc_mailbox_addr,
    int thread_id,
    bool hlkc_kernels,
    std::string used_kernels,
    bool perf_dump,
    std::string root);
void build_kernel_memories(
    llk::SocDescriptor soc_descriptor,
    std::string root,
    bool hlkc_kernels,
    std::string test_name,
    std::string test_dir,
    std::unordered_map<llk::xy_pair, std::vector<std::vector<std::string>>> &kernels_used,
    std::unordered_map<llk::xy_pair, std::vector<llk::memory>> &ckernel_memories,
    std::unordered_map<llk::xy_pair, std::string> &fwlogs,
    bool perf_dump);
}  // namespace assemble_kernels
}  // namespace llk
