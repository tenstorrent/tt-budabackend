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
        {ARCH::WORMHOLE_B0, 2},
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

void validate(const tt_op_model_desc& op_desc, tt::ARCH arch) {
    log_assert(OpModel::VERSIONS.find(arch) != OpModel::VERSIONS.end(), "No OpModelParams for arch: {}", arch);
    log_assert(op_desc.type != "", "Must specify op type");
    log_assert(op_desc.data_format != DataFormat::Invalid, "Must have a valid data format");
    log_assert(op_desc.math_fidelity != MathFidelity::Invalid, "Must have a valid math fidelity");
    log_assert(op_desc.t > 0, "Must have valid t");
    log_assert(op_desc.mblock_m > 0, "Must have valid mblock_m");
    log_assert(op_desc.mblock_n > 0, "Must have valid mblock_n");
    log_assert(op_desc.ublock_rt > 0, "Must have valid ublock_rt");
    log_assert(op_desc.ublock_ct > 0, "Must have valid ublock_ct");
    log_assert(op_desc.vector_mode != SfpuVectorMode::Invalid, "Must have valid vector_mode");
    log_assert(op_desc.version > 0, "Versioning starts from 1");
    log_assert(op_desc.version <= OpModel::VERSIONS.at(arch), "Version {} doesn't exist for arch {}", op_desc.version, op_desc.arch);
}

uint32_t get_math_weight(MathFidelity math_fidelity) {
    static std::unordered_map<MathFidelity, int> math_fidelity_weight = {
        {MathFidelity::LoFi, 1},
        {MathFidelity::HiFi2, 2},
        {MathFidelity::HiFi3, 3},
        {MathFidelity::HiFi4, 4},
    };
    return math_fidelity_weight[math_fidelity];
}

std::unordered_map<std::string, float> get_sparse_matmul_params(const tt_op_model_desc& op_desc) {
    std::unordered_map<std::string, float> p;
    p["reload"] = OpModel::get_op_param(op_desc, "sparse_reload");
    p["reload_config"] = OpModel::get_op_param(op_desc, "sparse_reload_config");
    p["reload_wait_pop"] = OpModel::get_op_param(op_desc, "sparse_reload_wait_pop");
    p["pack_wait_push"] = OpModel::get_op_param(op_desc, "sparse_pack_wait_push");
    p["pack"] = OpModel::get_op_param(op_desc, "sparse_pack");
    p["ublock_decode"] = OpModel::get_op_param(op_desc, "sparse_ublock_decode");
    p["nz_ublock_decode"] = OpModel::get_op_param(op_desc, "sparse_nz_ublock_decode");
    p["nz_tile_decode"] = OpModel::get_op_param(op_desc, "sparse_nz_tile_decode");
    for (auto& kv : p) {
        log_assert(kv.second > 0, "sparse_matmul param {} must be greater than 0", kv.first);
    }
    return p;
}

float get_sparse_matmul_math_fidelity_weight(MathFidelity math_fidelity) {
    switch (math_fidelity) {
        // for math fidelities LoFi and HiFi2 we already have measured math runtime parameter, so 
        // we don't need to scale it
        case MathFidelity::LoFi:
            return 1;
        case MathFidelity::HiFi2:
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
            log_fatal("Invalid math fidelity.");
    }

    return 1;
}

std::string OpModel::get_weight_attr(const tt_op_model_desc& op_desc) {
    if (op_desc.type == "matmul") {
        DataFormat format = op_desc.data_format;
        if (format == DataFormat::Bfp2 || format == DataFormat::Bfp2_b || format == DataFormat::Bfp4 ||
            format == DataFormat::Bfp4_b || format == DataFormat::Bfp8 || format == DataFormat::Bfp8_b) {
            return "bfp";
        }
    } else if (op_type(op_desc) == OpType::Unary) {
        if (op_desc.vector_mode == SfpuVectorMode::R) {
            return "vector_r";
        } else if (op_desc.vector_mode == SfpuVectorMode::C) {
            return "vector_c";
        }
    }
    return "";
}

OpModel::OpType OpModel::op_type(const tt_op_model_desc& op_desc) {
    static std::unordered_set<std::string> binary_ops = {
        "add",
        "subtract",
        "multiply",
        "quantization",
        "dequantization",
        "requantization",
        "maximum"
    };
    static std::unordered_set<std::string> nary_ops = {"splice"};
    static std::unordered_set<std::string> unary_ops = {
        "abs",
        "cosine",
        "datacopy",
        "dropout",
        "ethernet_datacopy",
        "exp",
        "gelu",
        "gelu_derivative",
        "log",
        "lrelu",
        "nop", 
        "power",
        "reciprocal",
        "reduce",
        "sigmoid",
        "sine",
        "sqrt",
        "square",
        "tanh",
        "tilizer",
    };

    if (unary_ops.find(op_desc.type) != unary_ops.end()) {
        return OpModel::OpType::Unary;
    } else if (binary_ops.find(op_desc.type) != binary_ops.end()) {
        return OpModel::OpType::Binary;
    } else if (nary_ops.find(op_desc.type) != nary_ops.end()) {
        return OpModel::OpType::Nary;
    } else if (op_desc.type == "matmul") {
        return OpModel::OpType::Matmul;
    } else if (op_desc.type == "depthwise") {
        return OpModel::OpType::Depthwise;
    } else if (op_desc.type == "embedding") {
        return OpModel::OpType::Embedding;
    } else if (op_desc.type == "fused_op") {
        return OpModel::OpType::FusedOp;
    } else if (op_desc.type == "topk") {
        return OpModel::OpType::Topk;
    } else {
        return OpModel::OpType::Unknown;
    }
}

uint32_t OpModel::get_op_cycles(const tt_op_model_desc& op_desc) {
    tt::ARCH arch = get_arch_enum_from_string(op_desc.arch);
    validate(op_desc, arch);
    auto& op_model = OpModel::get(arch);
    OpType op_type = OpModel::op_type(op_desc);

    std::uint32_t model_cycles = 0;
    switch (op_type) {
        case OpType::Unary: model_cycles = op_model.compute_execution_cycles_unary(op_desc); break;
        case OpType::Binary: model_cycles = op_model.compute_execution_cycles_binary(op_desc); break;
        case OpType::Nary: model_cycles = op_model.compute_execution_cycles_nary(op_desc); break;
        case OpType::Matmul: model_cycles = op_model.compute_execution_cycles_matmul(op_desc); break;
        case OpType::Depthwise: model_cycles = op_model.compute_execution_cycles_depthwise(op_desc); break;
        case OpType::Unknown: model_cycles = op_model.compute_execution_cycles_other(op_desc); break;
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
    validate(op_desc, arch);

    std::string op_name = op_desc.type;
    if (op_name == "reduce") {
        log_assert(op_desc.op_attr != "", "Reduce dim must be set in op_attr");
        op_name += "-" + op_desc.op_attr;
    }
    if (op_name == "matmul") {
        if (op_desc.l1_accumulate) {
            op_name += "_l1_acc";
        }
    }
    if (op_type(op_desc) == OpType::Unary) {
        if (op_desc.approx_mode) {
            op_name += "_approx";
        }
    }

    std::string weight_attr = get_weight_attr(op_desc);
    OpModel& model = get(arch);
    return model.get_param(op_name, op_desc.version, param_name, weight_attr);
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

float OpModel::get_param(
    const std::string& op_name,
    const std::uint32_t version,
    const std::string& param_name,
    const std::string& param_attr) {
    return m_model_params->get_param(op_name, version, param_name, param_attr);
}

uint32_t OpModel::compute_execution_cycles_unary(const tt_op_model_desc& op_desc) {
    uint32_t num_ublocks = op_desc.mblock_m * op_desc.mblock_n;
    uint32_t num_tiles = num_ublocks * op_desc.ublock_rt * op_desc.ublock_ct;
    std::string op_type = op_desc.type;
    float w1 = OpModel::get_op_param(op_desc, "ublock_weight");
    float w2 = OpModel::get_op_param(op_desc, "tile_weight");
    float w3 = 0;
    uint32_t z = 1;

    if (op_desc.reduce_z > 1 && op_type == "reduce" && op_desc.op_attr == "z") {
        z = op_desc.reduce_z;
        w3 = OpModel::get_op_param(op_desc, "z_weight");
    }

    float cycle_count = op_desc.t * z * (num_ublocks * w1 + num_tiles * w2 + w3);
    return static_cast<uint32_t>(cycle_count);
}

uint32_t OpModel::compute_execution_cycles_binary(const tt_op_model_desc& op_desc) {
    uint32_t num_ublocks = op_desc.mblock_m * op_desc.mblock_n;
    uint32_t num_tiles = num_ublocks * op_desc.ublock_rt * op_desc.ublock_ct;

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
        num_ublocks * OpModel::get_op_param(op_desc, "ublock_weight") +
        num_tiles * OpModel::get_op_param(op_desc, "tile_weight") * df_weight
    );
    
    return static_cast<uint32_t>(cycle_count);
}

uint32_t OpModel::compute_execution_cycles_depthwise(const tt_op_model_desc& op_desc) {
    return compute_execution_cycles_dense_matmulV2(op_desc);
}

uint32_t OpModel::compute_execution_cycles_nary(const tt_op_model_desc& op_desc) {
    uint32_t num_ublocks = op_desc.mblock_m * op_desc.mblock_n;
    uint32_t num_tiles = num_ublocks * op_desc.ublock_rt * op_desc.ublock_ct;
    float cycle_count = op_desc.t * (
        num_ublocks * OpModel::get_op_param(op_desc, "ublock_weight") +
        num_tiles * OpModel::get_op_param(op_desc, "tile_weight")
    );
    return static_cast<uint32_t>(cycle_count);
}

uint32_t OpModel::compute_execution_cycles_matmul(const tt_op_model_desc& op_desc) {
    if (op_desc.sparse_indices > 0) {
        if (op_desc.arch == "wormhole_b0" && op_desc.sparse_nz_strips != -1 && op_desc.sparse_nz_ublocks != -1) {
            // If nz_strip_count and nz_ublock_count are populated, use v2 formula
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
    uint32_t num_ublocks = op_desc.mblock_m * op_desc.mblock_n;
    uint32_t m_k_executions = op_desc.mblock_k - 1;
    uint32_t mblock_executions = op_desc.mblock_k * num_ublocks;
    uint32_t ublock_executions = mblock_executions * op_desc.ublock_kt * op_desc.ublock_rt * op_desc.ublock_ct;
    float math_weight = get_math_weight(op_desc.math_fidelity);
    float m_k_weight = OpModel::get_op_param(op_desc, "m_k_weight");
    float m_weight = OpModel::get_op_param(op_desc, "ublock_weight");
    float u_weight = OpModel::get_op_param(op_desc, "tile_weight");
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

    uint32_t num_ublocks = op_desc.mblock_k * op_desc.mblock_m * op_desc.mblock_n;

    // calculate math tiles scaled by fidelity phases
    uint32_t math_fidelity_phases = get_math_weight(op_desc.math_fidelity);
    uint32_t math_tiles = num_ublocks * op_desc.ublock_kt * op_desc.ublock_rt * op_desc.ublock_ct;
    uint32_t math_tiles_scaled_by_fidelity = math_tiles * math_fidelity_phases;

    // calculate math tiles on the outer m,k,n,r,t dimensions (skip all inner k, and first outer k)
    uint32_t math_dest_spill_ublocks = (op_desc.mblock_k - 1) * op_desc.mblock_m * op_desc.mblock_n;
    uint32_t math_dest_spill_tiles = math_dest_spill_ublocks * op_desc.ublock_rt * op_desc.ublock_ct;

    float math_w1 = OpModel::get_op_param(op_desc, "math_tiles_weight");
    float math_w2 = OpModel::get_op_param(op_desc, "math_dest_spill_weight");

    // unpack_tiles_per_ublock: models MM op0 reuse across tiles in each op1 horizontal strip
    uint32_t unpack_tiles_per_ub = op_desc.ublock_kt * op_desc.ublock_rt * (1 + op_desc.ublock_ct);
    uint32_t unpack_total_tiles = unpack_tiles_per_ub * num_ublocks;

    // unpack_tile_weight: models unpack at full rate then at reduced rate due to fidelity phase backpressure from math
    float unpack_w1 = OpModel::get_op_param(op_desc, "unpack_ublocks_weight");
    float unpack_w2 = OpModel::get_op_param(op_desc, "unpack_tiles_weight");

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
    // Shorter names to make the formulas more readable
    uint32_t t = op_desc.t, ublock_rt = op_desc.ublock_rt, ublock_ct = op_desc.ublock_ct,
             mblock_m = op_desc.mblock_m, mblock_k = op_desc.mblock_k, mblock_n = op_desc.mblock_n;

    // Compute sparsity parameters
    uint32_t num_sparse_strips = t * mblock_k;
    uint32_t num_sparse_ublocks = num_sparse_strips * mblock_m;
    uint32_t num_sparse_nz_tiles = op_desc.sparse_indices;
    uint32_t num_sparse_nz_ublocks = num_sparse_nz_tiles / (t * ublock_rt * ublock_ct);
    num_sparse_ublocks = std::max(num_sparse_ublocks, num_sparse_nz_ublocks);  // avoid underflow in the subtraction below

    float tile_weight = OpModel::get_op_param(op_desc, "tile_weight");
    float math_weight = get_math_weight(op_desc.math_fidelity);
    float math_cycles = num_sparse_nz_tiles * mblock_n * ublock_ct * tile_weight * math_weight;
    float reload_cycles, pack_cycles, encode_decode_cycles;
    auto c = get_sparse_matmul_params(op_desc);

    // Compute the number of cycles for each phase of the sparse matmul 
    reload_cycles = (num_sparse_ublocks * c["reload_wait_pop"] + num_sparse_nz_ublocks * (c["reload_config"] + c["reload"] * ublock_ct * ublock_rt)) * mblock_n;
    if (mblock_k > 1)
        reload_cycles += mblock_m * t * (c["reload_config"] + c["reload"] * ublock_ct * ublock_rt) * mblock_n;
    pack_cycles = c["pack_wait_push"] * (num_sparse_ublocks - num_sparse_nz_ublocks) * mblock_n + c["pack"] * 2 * t * mblock_n * mblock_m * ublock_ct * ublock_rt;
    encode_decode_cycles = num_sparse_nz_tiles * c["nz_tile_decode"] * (2 + mblock_n) + num_sparse_ublocks * c["ublock_decode"] + num_sparse_nz_ublocks * c["nz_ublock_decode"];

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
            log_assert(
                op_desc.data_format == DataFormat::Bfp8 || op_desc.data_format == DataFormat::Bfp8_b,
                "Math fidelity is LoFi. V2 formula expects activations on the sparse matmul to have Bfp8 a/b format.");
            return sparse_matmul_params_by_fidelity.at(op_desc.math_fidelity)
                .get_params(op_desc.ublock_rt, op_desc.ublock_ct, op_desc.ublock_kt);
        case MathFidelity::HiFi2:
        case MathFidelity::HiFi3:
        case MathFidelity::HiFi4:
            log_assert(
                op_desc.data_format == DataFormat::Float16 || op_desc.data_format == DataFormat::Float16_b,
                "Math fidelity is HiFi. V2 formula expects activations on the sparse matmul to have Float16 a/b format");
            // HiFi3 and HiFi4 also use HiFi2 params, just with a scaling coefficient
            return sparse_matmul_params_by_fidelity.at(MathFidelity::HiFi2)
                .get_params(op_desc.ublock_rt, op_desc.ublock_ct, op_desc.ublock_kt);
        default: log_fatal("Invalid math fidelity.");
    }

    return {};
}

uint32_t OpModel::compute_execution_cycles_sparse_matmulV2(const tt_op_model_desc& op_desc) {
    log_assert(op_desc.mblock_k > 0 && op_desc.ublock_kt > 0, "Must have valid mblock_k and ublock_kt.");
    log_assert(
        op_desc.sparse_nz_strips > 0 && op_desc.sparse_nz_ublocks > 0 && op_desc.sparse_indices > 0,
        "Must have at least one non-zero strip, ublock and tile");

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
    uint32_t nz_strips_decode = OpModel::get_op_param(op_desc, "v2_sparse_nz_strip_decode");
    uint32_t nz_ublocks_decode = OpModel::get_op_param(op_desc, "v2_sparse_nz_ublock_decode");
    uint32_t sparse_ublocks_decode = OpModel::get_op_param(op_desc, "v2_sparse_ublock_decode");

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
