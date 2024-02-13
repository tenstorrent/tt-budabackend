// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "tt_backend_api_types.hpp"

namespace tt {

enum class OpType {
    Nop,
    Abs,
    Cosine,
    Dropout,
    Exp,
    Gelu,
    GeluDerivative,
    Log,
    Lrelu,
    Power,
    Reciprocal,
    Sigmoid,
    Sine,
    Sqrt,
    Square,
    Tanh,
    Add,
    Subtract,
    Multiply,
    Maximum,
    Quantization,
    Dequantization,
    Requantization,
    Depthwise,
    Matmul,
    MatmulSparse,
    Embedding,
    EthernetDatacopy,
    Reduce,
    ReduceZ,
    Splice,
    Tilizer,
    Topk,
    FusedOp,
    Unknown,
};
OpType get_op_type_from_string(const std::string& op_type_str);
OpType get_op_type_from_descriptor(const tt_op_model_desc& op_model_desc);


// ParamMap reflects param structure per op from the params yaml files.
// More info in perf_lib/op_model/params/wormhole_b0/params_v<n>.yaml.
using OpParamMap = std::vector<std::pair<std::string, std::unordered_map<std::string, float>>>;

// There is a MatchingAttributeFunc for each attribute value used to match op model
// to the correct set of params. For example, if the params set key is approx/vector_c,
// we need to define two functions named approx and vector_c that would return true if 
// op model matches the attribute value combination.
typedef bool (*MatchingAttributeFunc)(const tt_op_model_desc& op_model_desc);

bool df_int8(const tt_op_model_desc& op_model_desc);

bool df_int32(const tt_op_model_desc& op_model_desc);

bool df_bfp(const tt_op_model_desc& op_model_desc);

bool approx(const tt_op_model_desc& op_model_desc);

bool vector_r(const tt_op_model_desc& op_model_desc);

bool vector_c(const tt_op_model_desc& op_model_desc);

bool l1_acc_en(const tt_op_model_desc& op_model_desc);

bool dim_c(const tt_op_model_desc& op_model_desc);

class OpModelParams {
   public:
    std::unordered_map<std::string, float> get_params(const tt_op_model_desc& op_model_desc) const;
    float get_param(const tt_op_model_desc& op_model_desc, const std::string& param_name) const;

    explicit OpModelParams(const std::string &params_dir_path, const std::uint32_t total_versions);

   private:
    void register_attribute_matching_functions();
    void load_params(const std::string &params_dir_path, const std::uint32_t total_versions);
    void validate_attribute_key(const std::string& attribute_key) const;

    // Each op has its own map of params - ParamOp.
    // Each update of params of one or multiple ops triggers a new version of all op params.
    // This is needed to decouple op params updates from PyBuda updates of BudaBackend.
    //
    // m_params is a vector of op params - each element keeps op params for version i+1, where i is the index of the
    // vector. Versioning starts from 1, and no version can be skipped. Each version is loaded from a yaml file that is
    // located in params_dir_path passed to the constructor.
    std::vector<std::unordered_map<OpType, OpParamMap>> m_params;
    std::unordered_map<std::string, MatchingAttributeFunc> m_attribute_matching_functions;
};

}  // namespace tt
