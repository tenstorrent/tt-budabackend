// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>

namespace tt {

// Cycles per kernel stage - math, reload, pack
using SparseMatmulStageCycles = std::unordered_map<std::string, std::uint32_t>;

// ublock_rt -> ublock_ct -> ublock_kt -> stage cycles
// Check out params/wormhole_b0_sparse_params_<fidelity>.yaml files to see the exact params the map will load
using SparseParamsByDimensions = std::unordered_map < std::uint32_t, std::unordered_map < std::uint32_t, std::unordered_map < std::uint32_t, SparseMatmulStageCycles >>>;

class SparseMatmulParams {
   public:
    SparseMatmulParams(const std::string &yaml_file_path);

    const std::unordered_map<std::string, std::uint32_t> &get_params(
        std::uint32_t ublock_rt, std::uint32_t ublock_ct, std::uint32_t ublock_kt);

   private:
    SparseParamsByDimensions params_by_dimensions;
    std::string name;
};

}  // namespace tt
