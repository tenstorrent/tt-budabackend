// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "op_params.hpp"
#include "model/utils.hpp"
#include "utils/logger.hpp"

namespace tt {

OpType get_op_type_from_string(const std::string& op_type_str) {
    static std::unordered_map<std::string, OpType> mapping = {
        {"nop", OpType::Nop},
        {"abs", OpType::Abs},
        {"cosine", OpType::Cosine},
        {"dropout", OpType::Dropout},
        {"exp", OpType::Exp},
        {"gelu", OpType::Gelu},
        {"gelu_derivative", OpType::GeluDerivative},
        {"log", OpType::Log},
        {"lrelu", OpType::Lrelu},
        {"power", OpType::Power},
        {"reciprocal", OpType::Reciprocal},
        {"sigmoid", OpType::Sigmoid},
        {"sine", OpType::Sine},
        {"sqrt", OpType::Sqrt},
        {"square", OpType::Square},
        {"tanh", OpType::Tanh},
        {"add", OpType::Add},
        {"subtract", OpType::Subtract},
        {"multiply", OpType::Multiply},
        {"maximum", OpType::Maximum},
        {"quantization", OpType::Quantization},
        {"dequantization", OpType::Dequantization},
        {"requantization", OpType::Requantization},
        {"depthwise", OpType::Depthwise},
        {"matmul", OpType::Matmul},
        {"matmul_sparse", OpType::MatmulSparse},
        {"embedding", OpType::Embedding},
        {"ethernet_datacopy", OpType::EthernetDatacopy},
        {"reduce", OpType::Reduce},
        {"reduce_z", OpType::ReduceZ},
        {"splice", OpType::Splice},
        {"tilizer", OpType::Tilizer},
        {"topk", OpType::Topk},
        {"fused_op", OpType::FusedOp},
    };

    if (mapping.find(op_type_str) == mapping.end()) {
        return OpType::Unknown;
    }

    return mapping.at(op_type_str);
}

OpType get_op_type_from_descriptor(const tt_op_model_desc& op_model_desc) {
    OpType base_op_type = get_op_type_from_string(op_model_desc.type);

    // Sparse matmul and reduce z actually use different kernels
    // and different formulas so we refer to them as separate op types
    if (base_op_type == OpType::Matmul && op_model_desc.sparse_indices > 0) {
        return OpType::MatmulSparse;
    }

    if (base_op_type == OpType::Reduce && op_model_desc.op_attr == "z") {
        return OpType::ReduceZ;
    }

    return base_op_type;
}

bool base(const tt_op_model_desc& op_model_desc) {
    return true;
}

bool df_int8(const tt_op_model_desc& op_model_desc) {
    return op_model_desc.data_format == DataFormat::Int8;
}

bool df_int32(const tt_op_model_desc& op_model_desc) {
    return op_model_desc.data_format == DataFormat::Int32;
}

bool df_bfp(const tt_op_model_desc& op_model_desc) {
    return op_model_desc.data_format == DataFormat::Bfp2 || op_model_desc.data_format == DataFormat::Bfp2_b ||
           op_model_desc.data_format == DataFormat::Bfp4 || op_model_desc.data_format == DataFormat::Bfp4_b ||
           op_model_desc.data_format == DataFormat::Bfp8 || op_model_desc.data_format == DataFormat::Bfp8_b;
}

bool approx(const tt_op_model_desc& op_model_desc) {
    return op_model_desc.approx_mode;
}

bool vector_r(const tt_op_model_desc& op_model_desc) {
    return op_model_desc.vector_mode == SfpuVectorMode::R;
}

bool vector_c(const tt_op_model_desc& op_model_desc) {
    return op_model_desc.vector_mode == SfpuVectorMode::C;
}

bool l1_acc_en(const tt_op_model_desc& op_model_desc) {
    return op_model_desc.l1_accumulate;
}

bool dim_c(const tt_op_model_desc& op_model_desc) {
    return op_model_desc.op_attr == "c";
}

void OpModelParams::register_attribute_matching_functions() {
    m_attribute_matching_functions["base"] = base;
    m_attribute_matching_functions["df_int8"] = df_int8;
    m_attribute_matching_functions["df_int32"] = df_int32;
    m_attribute_matching_functions["df_bfp"] = df_bfp;
    m_attribute_matching_functions["approx"] = approx;
    m_attribute_matching_functions["vector_r"] = vector_r;
    m_attribute_matching_functions["vector_c"] = vector_c;
    m_attribute_matching_functions["l1_acc_en"] = l1_acc_en;
    m_attribute_matching_functions["dim_c"] = dim_c;
}

void OpModelParams::validate_attribute_key(const std::string& attribute_key) const {
    std::vector<std::string> attribute_functions;
    tt::args::split_string_into_vector(attribute_functions, attribute_key, "/");

    for (const auto& attribute_function : attribute_functions) {
        if (m_attribute_matching_functions.find(attribute_function) == m_attribute_matching_functions.end()) {
            log_fatal("Unknown attribute function {} in params file.", attribute_function);
        }
    }
}

void OpModelParams::load_params(const std::string& params_dir_path, const std::uint32_t total_versions) {
    if (!std::filesystem::is_directory(params_dir_path)) {
        log_fatal("Op params directory {} doesn't exist!", params_dir_path);
    }

    // Each version of params is stored in a separate file named params_v{version}.yaml
    for (std::uint32_t version = 1; version <= total_versions; version++) {
        std::string params_file_name = params_dir_path + "/params_v" + std::to_string(version) + ".yaml";
        std::ifstream params_file(params_file_name);
        if (!params_file.is_open()) {
            log_fatal("Failed to open op params file {} for version {}.", params_file_name, version);
        }
        const std::uint32_t version_index = version - 1;

        m_params.push_back(std::unordered_map<OpType, OpParamMap>());

        try {
            YAML::Node yaml_data = YAML::Load(params_file);

            for (const auto& entry : yaml_data) {
                const std::string& op_type_str = entry.first.as<std::string>();
                const YAML::Node& params_by_attributes = entry.second;

                OpType op_type = get_op_type_from_string(op_type_str);
                log_assert(
                    op_type != OpType::Unknown, "Unknown op type {} in params file {}", params_file_name, op_type_str);

                m_params[version_index].insert({op_type, OpParamMap()});
                OpParamMap& param_map = m_params[version_index].at(op_type);

                for (const auto& param_entry : params_by_attributes) {
                    const std::string& attribute_key = param_entry.first.as<std::string>();
                    // make sure we have the needed attribute matching functions defined and registered
                    validate_attribute_key(attribute_key);
                    const YAML::Node& params = param_entry.second;

                    param_map.push_back({attribute_key, std::unordered_map<std::string, float>()});

                    for (const auto& param : params) {
                        const std::string& param_name = param.first.as<std::string>();
                        const float param_value = param.second.as<float>();
                        param_map.back().second[param_name] = param_value;
                    }
                }
            }
        } catch (const YAML::Exception& e) {
            std::ostringstream error_stream;
            error_stream << "Error while parsing YAML: " << e.what();
            log_fatal("{}", error_stream.str());
        }
    }
}

OpModelParams::OpModelParams(const std::string& params_dir_path, const std::uint32_t total_versions) {
    register_attribute_matching_functions();
    load_params(params_dir_path, total_versions);
}

std::unordered_map<std::string, float> OpModelParams::get_params(const tt_op_model_desc& op_model_desc) const {
    if (op_model_desc.version == 0 || op_model_desc.version > m_params.size()) {
        log_warning(LogModel, "Param version invalid. Valid versions: 1 <= version <= {}.", m_params.size());
        return {};
    }

    const int version_index = op_model_desc.version - 1;
    const auto& params = m_params[version_index];
    OpType type = get_op_type_from_descriptor(op_model_desc);

    if (params.find(type) == params.end()) {
        log_warning(tt::LogModel, "OpType '{}' not found in params. Defaulting to nop params.", op_model_desc.type);
        type = OpType::Nop;
    }

    const auto& op_params = params.at(type);

    // traverse params sets for an op in reverse order since more specialized params sets should be at the end;
    // check out perf_lib/op_model/params/wormhole_b0/params_v1.yaml for an example
    for (int i = op_params.size() - 1; i >= 0; i--) {
        const auto& param_entry = op_params[i];
        const std::string& attribute_key = param_entry.first;
        std::vector<std::string> attribute_functions;
        tt::args::split_string_into_vector(attribute_functions, attribute_key, "/");

        bool match = true;
        for (const auto& attribute_function : attribute_functions) {
            if (!m_attribute_matching_functions.at(attribute_function)(op_model_desc)) {
                match = false;
                break;
            }
        }

        if (match) {
            return param_entry.second;
        }
    }

    return {};
}

float OpModelParams::get_param(const tt_op_model_desc& op_model_desc, const std::string& param_name) const {
    const auto& params = get_params(op_model_desc);

    if (params.find(param_name) == params.end()) {
        log_warning(tt::LogModel, "Param '{}' not found in params. Defaulting to 0.", param_name);
        return 0;
    }

    return params.at(param_name);
}

}  // namespace tt
