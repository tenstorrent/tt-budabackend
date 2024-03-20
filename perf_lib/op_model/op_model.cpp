// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include "op_model.hpp"

#include "common/env_lib.hpp"
#include "utils/logger.hpp"

namespace tt {

const std::unordered_map<ARCH, std::uint32_t> OpModel::VERSIONS = {
        {ARCH::GRAYSKULL, 2},
        {ARCH::WORMHOLE_B0, 3},
};

// These won't be needed once we migrate the API to use enums instead of strings for architecture, type, etc.
// We can't use tt_backend_api_types functions for string/enum conversion since netlist lib depends on op model lib.
ARCH get_arch_enum_from_string(const std::string& arch_str) {
    std::string str = arch_str;
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::toupper(c); });
    if (str == "GRAYSKULL") {
        return ARCH::GRAYSKULL;
    } else if (str == "WORMHOLE_B0") {
        return ARCH::WORMHOLE_B0;
    } else {
        return ARCH::Invalid;
    }
}

std::string get_arch_string_from_enum(tt::ARCH arch) {
    switch (arch) {
        case ARCH::GRAYSKULL: return "grayskull"; break;
        case ARCH::WORMHOLE_B0: return "wormhole_b0"; break;
        default: return "Invalid"; break;
    }
}

void validate(const tt_op_model_desc& op_desc, tt::ARCH arch, OpType op_type) {
    log_assert(OpModel::VERSIONS.find(arch) != OpModel::VERSIONS.end(), "No OpModelParams for arch: {}", arch);
    log_assert(op_type != OpType::Unknown, "Op type {} doesn't exist", op_desc.type);
    log_assert(op_desc.data_format != DataFormat::Invalid, "Must have a valid data format");
    log_assert(op_desc.math_fidelity != MathFidelity::Invalid, "Must have a valid math fidelity");
    log_assert(op_desc.t > 0, "Must have valid t");
    log_assert(op_desc.mblock_m > 0, "Must have valid mblock_m");
    log_assert(op_desc.mblock_n > 0, "Must have valid mblock_n");
    log_assert(op_desc.ublock_rt > 0, "Must have valid ublock_rt");
    log_assert(op_desc.ublock_ct > 0, "Must have valid ublock_ct");
    log_assert(op_desc.vector_mode != SfpuVectorMode::Invalid, "Must have valid vector_mode");
    log_assert(op_desc.version > 0, "Versioning starts from 1");
    log_assert(
        op_desc.version <= OpModel::VERSIONS.at(arch),
        "Version {} doesn't exist for arch {}",
        op_desc.version,
        op_desc.arch);
    log_assert(
        arch == ARCH::WORMHOLE_B0 ||
            (op_desc.data_format != DataFormat::Int8 && op_desc.data_format != DataFormat::Int32),
        "Int data formats are supported on B0 only.");
}

OpModel::OpCategory OpModel::get_op_category(OpType op_type) {
    static std::unordered_map<OpType, OpCategory> mapping = {
        {OpType::Nop, OpCategory::Unary},
        {OpType::Abs, OpCategory::Unary},
        {OpType::Cosine, OpCategory::Unary},
        {OpType::Dropout, OpCategory::Unary},
        {OpType::Exp, OpCategory::Unary},
        {OpType::Gelu, OpCategory::Unary},
        {OpType::GeluDerivative, OpCategory::Unary},
        {OpType::Log, OpCategory::Unary},
        {OpType::Lrelu, OpCategory::Unary},
        {OpType::Power, OpCategory::Unary},
        {OpType::Reciprocal, OpCategory::Unary},
        {OpType::Sigmoid, OpCategory::Unary},
        {OpType::Sine, OpCategory::Unary},
        {OpType::Sqrt, OpCategory::Unary},
        {OpType::Square, OpCategory::Unary},
        {OpType::Tanh, OpCategory::Unary},
        {OpType::Add, OpCategory::Binary},
        {OpType::Subtract, OpCategory::Binary},
        {OpType::Multiply, OpCategory::Binary},
        {OpType::Maximum, OpCategory::Binary},
        {OpType::Quantization, OpCategory::Binary},
        {OpType::Dequantization, OpCategory::Binary},
        {OpType::Requantization, OpCategory::Binary},
        {OpType::Depthwise, OpCategory::Depthwise},
        {OpType::Matmul, OpCategory::Matmul},
        {OpType::MatmulSparse, OpCategory::Matmul},
        {OpType::Embedding, OpCategory::Embedding},
        {OpType::EthernetDatacopy, OpCategory::Unary},
        {OpType::Reduce, OpCategory::Unary},
        {OpType::ReduceZ, OpCategory::Unary},
        {OpType::Splice, OpCategory::Nary},
        {OpType::Tilizer, OpCategory::Unary},
        {OpType::Topk, OpCategory::Topk},
        {OpType::FusedOp, OpCategory::FusedOp},
    };

    if (mapping.find(op_type) == mapping.end()) {
        return OpCategory::Unknown;
    }

    return mapping.at(op_type);
}

uint32_t get_math_weight(MathFidelity math_fidelity) {
    static std::unordered_map<MathFidelity, int> math_fidelity_weight = {
        {MathFidelity::LoFi, 1},
        {MathFidelity::HiFi2_A, 2},
        {MathFidelity::HiFi2_B, 2},
        {MathFidelity::HiFi3, 3},
        {MathFidelity::HiFi4, 4},
    };

    if (math_fidelity_weight.find(math_fidelity) == math_fidelity_weight.end()) {
        // fallback in case a new fidelity is added before the map is updated
        return 1;
    }

    return math_fidelity_weight[math_fidelity];
}

float get_sparse_matmul_math_fidelity_weight(MathFidelity math_fidelity) {
    switch (math_fidelity) {
        // for math fidelities LoFi and HiFi2 we already have measured math runtime parameter, so 
        // we don't need to scale it
        case MathFidelity::LoFi:
            return 1;
        case MathFidelity::HiFi2_A:
        case MathFidelity::HiFi2_B:
            return 1;
        // for math fidelity larger than HiFi2, we need to scale math runtime parameter we have for HiFi2; 
        // since math cycles involve non-zero tile decode as well, 
        // math time won't simply increase by 1.5 (for HiFi3) and 2 (for HiFi4);
        // a sample of tests ran with HiFi2, HiFi3 and HiFi4 showed these coefficients scale HiFi3 and HiFi4
        // estimates to be in the same ratio wrt real runtime as HiFi2 estimates are
        case MathFidelity::HiFi3:
            return 1.2;
        case MathFidelity::HiFi4:
            return 1.7;
        default:
            return 1;
    }
}

uint32_t OpModel::get_op_cycles(const tt_op_model_desc& op_desc) {
    tt::ARCH arch = get_arch_enum_from_string(op_desc.arch);
    OpType op_type = get_op_type_from_descriptor(op_desc);
    validate(op_desc, arch, op_type);
    OpCategory op_category = OpModel::get_op_category(op_type);

    auto& op_model = OpModel::get(arch);

    std::uint32_t model_cycles = 0;
    switch (op_category) {
        case OpCategory::Unary: model_cycles = op_model.compute_execution_cycles_unary(op_desc); break;
        case OpCategory::Binary: model_cycles = op_model.compute_execution_cycles_binary(op_desc); break;
        case OpCategory::Nary: model_cycles = op_model.compute_execution_cycles_nary(op_desc); break;
        case OpCategory::Matmul: model_cycles = op_model.compute_execution_cycles_matmul(op_desc); break;
        case OpCategory::Depthwise: model_cycles = op_model.compute_execution_cycles_depthwise(op_desc); break;
        case OpCategory::Unknown: model_cycles = op_model.compute_execution_cycles_other(op_desc); break;
        // for other ops we recognize, but don't have estimates, we default to unary function and default params
        default: model_cycles = op_model.compute_execution_cycles_unary(op_desc); break;
    }
    log_debug(LogModel, "OpModel {}, {}", op_desc.type, model_cycles);
    log_assert(model_cycles != 0, "Execution cycles cannot be zero for arch: {}, op type: {}, not implemented?", op_desc.arch, op_desc.type);

    // Clamp to min/max cycles
    model_cycles = std::max(model_cycles, OpModel::MIN_CYCLES);
    model_cycles = std::min(model_cycles, OpModel::MAX_CYCLES);
    return model_cycles;
}

std::map<tt::ARCH, OpModel>& OpModel::get_instances() {
    static std::map<tt::ARCH, OpModel> instances;
    return instances;
}

OpModel& OpModel::get(const ARCH arch) {
    auto& instances = get_instances();
    auto match = instances.find(arch);
    if (match == instances.end()) {
        match = instances.emplace(arch, OpModel(arch)).first;
    }
    return match->second;
}

float OpModel::get_op_param(const tt_op_model_desc& op_desc, const std::string& param_name) {
    tt::ARCH arch = get_arch_enum_from_string(op_desc.arch);
    OpType op_type = get_op_type_from_descriptor(op_desc);
    validate(op_desc, arch, op_type);
    auto& op_model = OpModel::get(arch);
    return op_model.m_model_params->get_param(op_desc, param_name);
}

OpModel::OpModel(const tt::ARCH arch) {
    if (VERSIONS.find(arch) == VERSIONS.end()) {
        log_fatal("No OpModelParams for arch: {}", arch);
    }
    std::string params_path = buda_home() + "perf_lib/op_model/params/" + get_arch_string_from_enum(arch);
    const std::uint32_t total_versions = VERSIONS.at(arch);

    m_model_params = std::make_unique<OpModelParams>(params_path, total_versions);
    if (!m_model_params) {
        log_fatal("Failed to load OpModelParams from {}", params_path);
    }

    if (arch == tt::ARCH::WORMHOLE_B0) {
        std::string sparse_params_path_lofi = buda_home() + "perf_lib/op_model/params/wormhole_b0_sparse_v2_formula_params/params_lofi.yaml";
        std::string sparse_params_path_hifi2 = buda_home() + "perf_lib/op_model/params/wormhole_b0_sparse_v2_formula_params/params_hifi2.yaml";

        sparse_matmul_params_by_fidelity.emplace(MathFidelity::LoFi, SparseMatmulParams(sparse_params_path_lofi));
        sparse_matmul_params_by_fidelity.emplace(MathFidelity::HiFi2, SparseMatmulParams(sparse_params_path_hifi2));
    }
}

uint32_t OpModel::compute_execution_cycles_unary(const tt_op_model_desc& op_desc) {
    uint32_t num_ublocks = op_desc.mblock_m * op_desc.mblock_n;
    uint32_t num_tiles = num_ublocks * op_desc.ublock_rt * op_desc.ublock_ct;
    std::string op_type = op_desc.type;

    const auto& params = m_model_params->get_params(op_desc);

    float w1 = params.at("ublock_weight");
    float w2 = params.at("tile_weight");
    float w3 = 0;
    uint32_t z = 1;

    if (op_desc.reduce_z > 1 && op_type == "reduce" && op_desc.op_attr == "z") {
        z = op_desc.reduce_z;
        w3 = params.at("z_weight");
    }

    float cycle_count = op_desc.t * z * (num_ublocks * w1 + num_tiles * w2 + w3);
    return static_cast<uint32_t>(cycle_count);
}

uint32_t OpModel::compute_execution_cycles_binary(const tt_op_model_desc& op_desc) {
    uint32_t num_ublocks = op_desc.mblock_m * op_desc.mblock_n;
    uint32_t num_tiles = num_ublocks * op_desc.ublock_rt * op_desc.ublock_ct;
    const auto& params = m_model_params->get_params(op_desc);

    uint32_t df_weight = 1;
    // maximum is done on SFPU, so math thread is the bottleneck
    if (op_desc.type != "maximum") {
        if (op_desc.data_format == DataFormat::Float16 or op_desc.data_format == DataFormat::Float16_b) {
            df_weight = 2;
        } else if (op_desc.data_format == DataFormat::Float32 or op_desc.data_format == DataFormat::Tf32) {
            df_weight = 4;
        }
    }

    float cycle_count = op_desc.t * (
        num_ublocks * params.at("ublock_weight") +
        num_tiles * params.at("tile_weight") * df_weight
    );
    
    return static_cast<uint32_t>(cycle_count);
}

uint32_t OpModel::compute_execution_cycles_depthwise(const tt_op_model_desc& op_desc) {
    return compute_execution_cycles_dense_matmulV2(op_desc);
}

uint32_t OpModel::compute_execution_cycles_nary(const tt_op_model_desc& op_desc) {
    uint32_t num_ublocks = op_desc.mblock_m * op_desc.mblock_n;
    uint32_t num_tiles = num_ublocks * op_desc.ublock_rt * op_desc.ublock_ct;
    const auto& params = m_model_params->get_params(op_desc);

    float cycle_count = op_desc.t * (
        num_ublocks * params.at("ublock_weight") +
        num_tiles * params.at("tile_weight")
    );
    return static_cast<uint32_t>(cycle_count);
}

bool supports_v2_sparse_formula(const tt_op_model_desc& op_desc) {
    if (op_desc.arch != "wormhole_b0" || op_desc.sparse_nz_strips == -1 || op_desc.sparse_nz_ublocks == -1) {
        return false;
    }

    bool valid_hifi_combination =
        (op_desc.data_format == DataFormat::Float16 || op_desc.data_format == DataFormat::Float16_b) &&
        (op_desc.math_fidelity == MathFidelity::HiFi2_A || op_desc.math_fidelity == MathFidelity::HiFi2_B ||
         op_desc.math_fidelity == MathFidelity::HiFi3 || op_desc.math_fidelity == MathFidelity::HiFi4);
    bool valid_lofi_combination =
        (op_desc.data_format == DataFormat::Bfp8 || op_desc.data_format == DataFormat::Bfp8_b) &&
        op_desc.math_fidelity == MathFidelity::LoFi;
    return valid_hifi_combination || valid_lofi_combination;
}

uint32_t OpModel::compute_execution_cycles_matmul(const tt_op_model_desc& op_desc) {
    if (op_desc.sparse_indices > 0) {
        if (supports_v2_sparse_formula(op_desc)) {
            return this->compute_execution_cycles_sparse_matmulV2(op_desc);
        } else {
            return this->compute_execution_cycles_sparse_matmul(op_desc);
        }
    } else if (op_desc.arch == "grayskull") {
        return this->compute_execution_cycles_dense_matmul(op_desc);
    }
    return this->compute_execution_cycles_dense_matmulV2(op_desc);
}

uint32_t OpModel::compute_execution_cycles_dense_matmul(const tt_op_model_desc& op_desc) {
    log_assert(op_desc.mblock_k > 0, "Must have valid mblock_k");
    log_assert(op_desc.ublock_kt > 0, "Must have valid ublock_kt");

    const auto& params = m_model_params->get_params(op_desc);

    uint32_t num_ublocks = op_desc.mblock_m * op_desc.mblock_n;
    uint32_t m_k_executions = op_desc.mblock_k - 1;
    uint32_t mblock_executions = op_desc.mblock_k * num_ublocks;
    uint32_t ublock_executions = mblock_executions * op_desc.ublock_kt * op_desc.ublock_rt * op_desc.ublock_ct;

    float math_weight = get_math_weight(op_desc.math_fidelity);
    float m_k_weight = params.at("m_k_weight");
    float m_weight = params.at("ublock_weight");
    float u_weight = params.at("tile_weight");
    float cycle_count = op_desc.t * (
        m_k_executions * m_k_weight +
        mblock_executions * m_weight +
        ublock_executions * u_weight * math_weight
    );
    return static_cast<uint32_t>(cycle_count);
}

uint32_t OpModel::compute_execution_cycles_dense_matmulV2(const tt_op_model_desc& op_desc) {
    log_assert(op_desc.mblock_k > 0, "Must have valid mblock_k");
    log_assert(op_desc.ublock_kt > 0, "Must have valid ublock_kt");

    const auto& params = m_model_params->get_params(op_desc);

    uint32_t num_ublocks = op_desc.mblock_k * op_desc.mblock_m * op_desc.mblock_n;

    // calculate math tiles scaled by fidelity phases
    uint32_t math_fidelity_phases = get_math_weight(op_desc.math_fidelity);
    uint32_t math_tiles = num_ublocks * op_desc.ublock_kt * op_desc.ublock_rt * op_desc.ublock_ct;
    uint32_t math_tiles_scaled_by_fidelity = math_tiles * math_fidelity_phases;

    // calculate math tiles on the outer m,k,n,r,t dimensions (skip all inner k, and first outer k)
    uint32_t math_dest_spill_ublocks = (op_desc.mblock_k - 1) * op_desc.mblock_m * op_desc.mblock_n;
    uint32_t math_dest_spill_tiles = math_dest_spill_ublocks * op_desc.ublock_rt * op_desc.ublock_ct;
    
    float math_w1 = params.at("math_tiles_weight");
    float math_w2 = params.at("math_dest_spill_weight");

    // unpack_tiles_per_ublock: models MM op0 reuse across tiles in each op1 horizontal strip
    uint32_t unpack_tiles_per_ub = op_desc.ublock_kt * op_desc.ublock_rt * (1 + op_desc.ublock_ct);
    uint32_t unpack_total_tiles = unpack_tiles_per_ub * num_ublocks;

    // unpack_tile_weight: models unpack at full rate then at reduced rate due to fidelity phase backpressure from math
    float unpack_w1 = params.at("unpack_ublocks_weight");
    float unpack_w2 = params.at("unpack_tiles_weight");

    // V2 model combines math and unpack cycles into a single cycle count where
    // - math models a fidelity phases component and a dest spill component
    // - unpack models an ublocks config component and an unpack tiles component
    float cycle_count = op_desc.t * (math_tiles_scaled_by_fidelity * math_w1 + math_dest_spill_tiles * math_w2 +
                                     num_ublocks * unpack_w1 + unpack_total_tiles * unpack_w2);
    return static_cast<uint32_t>(cycle_count);
}

uint32_t OpModel::compute_execution_cycles_sparse_matmul(const tt_op_model_desc& op_desc) {
    log_assert(op_desc.mblock_k > 0, "Must have valid mblock_k");
    log_assert(op_desc.sparse_indices > 0, "Must have valid sparse_indices");

    const auto& params = m_model_params->get_params(op_desc);

    // Shorter names to make the formulas more readable
    uint32_t t = op_desc.t, ublock_rt = op_desc.ublock_rt, ublock_ct = op_desc.ublock_ct,
             mblock_m = op_desc.mblock_m, mblock_k = op_desc.mblock_k, mblock_n = op_desc.mblock_n;

    // Compute sparsity parameters
    uint32_t num_sparse_strips = t * mblock_k;
    uint32_t num_sparse_ublocks = num_sparse_strips * mblock_m;
    uint32_t num_sparse_nz_tiles = op_desc.sparse_indices;
    uint32_t num_sparse_nz_ublocks = num_sparse_nz_tiles / (t * ublock_rt * ublock_ct);
    num_sparse_ublocks = std::max(num_sparse_ublocks, num_sparse_nz_ublocks);  // avoid underflow in the subtraction below

    float tile_weight = params.at("tile_weight");
    float math_weight = get_math_weight(op_desc.math_fidelity);
    float math_cycles = num_sparse_nz_tiles * mblock_n * ublock_ct * tile_weight * math_weight;
    float reload_cycles, pack_cycles, encode_decode_cycles;

    // Compute the number of cycles for each phase of the sparse matmul
    reload_cycles =
        (num_sparse_ublocks * params.at("reload_wait_pop") +
         num_sparse_nz_ublocks * (params.at("reload_config") + params.at("reload") * ublock_ct * ublock_rt)) *
        mblock_n;

    if (mblock_k > 1) {
        reload_cycles += mblock_m * t * (params.at("reload_config") + params.at("reload") * ublock_ct * ublock_rt) * mblock_n;
    }

    pack_cycles = params.at("pack_wait_push") * (num_sparse_ublocks - num_sparse_nz_ublocks) * mblock_n +
                  params.at("pack") * 2 * t * mblock_n * mblock_m * ublock_ct * ublock_rt;

    encode_decode_cycles = num_sparse_nz_tiles * params.at("nz_tile_decode") * (2 + mblock_n) +
                           num_sparse_ublocks * params.at("ublock_decode") +
                           num_sparse_nz_ublocks * params.at("nz_ublock_decode");

    float cycle_count = math_cycles + reload_cycles + pack_cycles + encode_decode_cycles;
    log_trace(
        LogModel,
        "Sparse matmul: math_cycles = {}, reload_cycles = {}, pack_cycles = {}, encode_decode_cycles = {}, total = {}",
        math_cycles,
        reload_cycles,
        pack_cycles,
        encode_decode_cycles,
        cycle_count);
    return static_cast<uint32_t>(cycle_count);
}

std::unordered_map<std::string, uint32_t> OpModel::get_sparse_matmul_paramsV2(const tt_op_model_desc& op_desc) {
    switch (op_desc.math_fidelity) {
        case MathFidelity::LoFi:
            return sparse_matmul_params_by_fidelity.at(op_desc.math_fidelity)
                .get_params(op_desc.ublock_rt, op_desc.ublock_ct, op_desc.ublock_kt);
        case MathFidelity::HiFi2_A:
        case MathFidelity::HiFi2_B:
        case MathFidelity::HiFi3:
        case MathFidelity::HiFi4:
            // HiFi3 and HiFi4 also use HiFi2 params, just with a scaling coefficient
            return sparse_matmul_params_by_fidelity.at(MathFidelity::HiFi2)
                .get_params(op_desc.ublock_rt, op_desc.ublock_ct, op_desc.ublock_kt);
        default: return {};
    }
}

uint32_t OpModel::compute_execution_cycles_sparse_matmulV2(const tt_op_model_desc& op_desc) {
    log_assert(op_desc.mblock_k > 0 && op_desc.ublock_kt > 0, "Must have valid mblock_k and ublock_kt.");
    log_assert(
        op_desc.sparse_nz_strips > 0 && op_desc.sparse_nz_ublocks > 0 && op_desc.sparse_indices > 0,
        "Must have at least one non-zero strip, ublock and tile");

    const auto& params = m_model_params->get_params(op_desc);

    // Shorter names to make the formulas more readable
    uint32_t t = op_desc.t;
    uint32_t ublock_rt = op_desc.ublock_rt;
    uint32_t ublock_ct = op_desc.ublock_ct;
    uint32_t mblock_m = op_desc.mblock_m;
    uint32_t mblock_k = op_desc.mblock_k;
    uint32_t mblock_n = op_desc.mblock_n;
    uint32_t nz_strips = op_desc.sparse_nz_strips;
    uint32_t nz_ublocks = op_desc.sparse_nz_ublocks;
    uint32_t nz_tiles = op_desc.sparse_indices;

    uint32_t sparse_ublocks = nz_strips * mblock_m;
    uint32_t nz_strips_decode = params.at("v2_nz_strip_decode");
    uint32_t nz_ublocks_decode = params.at("v2_nz_ublock_decode");
    uint32_t sparse_ublocks_decode = params.at("v2_ublock_decode");

    // Retrieve cycles for different stages in sparse matmul given the op parameters
    const auto& sparse_params = get_sparse_matmul_paramsV2(op_desc);

    // We split sparse matmul into 4 stages: decode, math, reload and pack
    uint32_t decode_cycles =
        sparse_ublocks * sparse_ublocks_decode + nz_ublocks * nz_ublocks_decode + nz_strips * nz_strips_decode;

    uint32_t math_cycles_with_tile_decode =
        (uint32_t)(nz_tiles * mblock_n * ublock_ct * sparse_params.at("math_cycles_with_tile_decode") *
                   get_sparse_matmul_math_fidelity_weight(op_desc.math_fidelity));

    uint32_t reload_cycles = 0;
    if (mblock_k > 1) {
        reload_cycles = nz_ublocks * mblock_n * ublock_ct * ublock_rt * sparse_params.at("reload_cycles");
    }

    uint32_t pack_cycles = t * mblock_n * mblock_m * ublock_ct * ublock_rt * sparse_params.at("pack_cycles");

    uint32_t cycle_count = decode_cycles + math_cycles_with_tile_decode + reload_cycles + pack_cycles;

    log_trace(
        tt::LogModel,
        "Sparse matmul v2: decode_cycles = {}, math_cycles_with_tile_decode = {}, reload_cycles = {}, pack_cycles = {}"
        ", total = {}",
        decode_cycles,
        math_cycles_with_tile_decode,
        reload_cycles,
        pack_cycles,
        cycle_count);

    return cycle_count;
}

uint32_t OpModel::compute_execution_cycles_other(const tt_op_model_desc& op_desc) {
    return 0;
}

}  // namespace tt
