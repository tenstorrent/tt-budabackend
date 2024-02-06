// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>
#include <string>

#include "op_params.hpp"
#include "sparse_matmul_params.hpp"
#include "tt_backend_api_types.hpp"

namespace tt {

class OpModel {
   public:
    enum class OpType {
        Unary,
        Binary,
        Nary,
        Matmul,
        Depthwise,
        Embedding,
        FusedOp,
        Topk,
        Unknown,
    };
    static constexpr std::uint32_t MIN_CYCLES = 0;
    static constexpr std::uint32_t MAX_CYCLES = (1 << 30);
    static const std::unordered_map<ARCH, std::uint32_t> VERSIONS;

    static OpType op_type(const tt_op_model_desc& op_desc);

    static std::map<ARCH, OpModel>& get_instances();

    static OpModel& get(const ARCH arch);

    static uint32_t get_op_cycles(const tt_op_model_desc& op_desc);

    static float get_op_param(const tt_op_model_desc& op_desc, const std::string& param_name);

    static std::string get_weight_attr(const tt_op_model_desc& op_desc);

   private:
    std::unique_ptr<OpModelParams> m_model_params;
    // This map holds sparse matmul params that depend on multiple factors (fidelity, data format, ublock dimensions);
    // They are used only in V2 formula and they are kept separate from other op params for simplicity
    std::unordered_map<MathFidelity, SparseMatmulParams> sparse_matmul_params_by_fidelity;

    explicit OpModel(const ARCH arch);
    float get_param(
        const std::string& op_name,
        const std::uint32_t version,
        const std::string& param_name,
        const std::string& param_attr = "");

    std::unordered_map<std::string, std::uint32_t> get_sparse_matmul_paramsV2(const tt_op_model_desc& op_desc);

    uint32_t compute_execution_cycles_unary(const tt_op_model_desc& op_desc);
    uint32_t compute_execution_cycles_binary(const tt_op_model_desc& op_desc);
    uint32_t compute_execution_cycles_nary(const tt_op_model_desc& op_desc);
    uint32_t compute_execution_cycles_depthwise(const tt_op_model_desc& op_desc);
    uint32_t compute_execution_cycles_matmul(const tt_op_model_desc& op_desc);
    uint32_t compute_execution_cycles_dense_matmul(const tt_op_model_desc& op_desc);
    uint32_t compute_execution_cycles_dense_matmulV2(const tt_op_model_desc& op_desc);
    uint32_t compute_execution_cycles_sparse_matmul(const tt_op_model_desc& op_desc);
    uint32_t compute_execution_cycles_sparse_matmulV2(const tt_op_model_desc& op_desc);
    uint32_t compute_execution_cycles_other(const tt_op_model_desc& op_desc);

};

}  // namespace tt
