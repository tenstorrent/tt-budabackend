// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "common/base.hpp"
#include "hlks/inc/hlk_api.h"
#include "model/op.hpp"
#include "netlist_info_types.hpp"
#include "netlist_op_info_types.hpp"
#include "utils/logger.hpp"

using namespace tt;

ostream& operator<<(ostream& os, const Dim& t);

namespace netlist_utils {
Dim get_dim_enum(const string& t);
string get_string(Dim dim);
ReluMode get_relu_mode_enum(const string& t);
string get_relu_mode_string(ReluMode relu_mode);
//! get Variable number from variable netlist string
int get_variable_num(string input);
bool is_number(const std::string& s);
//! get DataFormat from the netlist string
DataFormat get_data_format(string format_string);
//! get MathFidelity from the netlist string
MathFidelity get_fidelity(string fidelity_string);
//! get StochRndMode from the netlist string
StochRndMode get_stoch_rnd_mode(string srnd_mode_string);
//! BinaryOp
BinaryOp get_valid_binary_op(string binary_op);
BinaryOp get_binary_op(string binary_op);
bool is_valid_binary_op(string binary_op);
bool is_valid_binary_quantisation_op(string binary_op);
string binary_op_to_string(BinaryOp binary_op);
TopKSort get_topk_sort_enum(string sort);
//! UnaryOp -- Different from SfpuOp (elementwise Unary), this can shuffle data around, but single input
UnaryOp get_valid_unary_op(string unary_op);
UnaryOp get_unary_op(string unary_op);
bool is_valid_unary_op(string unary_op);
//! NaryOp
NaryOp get_valid_nary_op(string nary_op);
NaryOp get_nary_op(string nary_op);
bool is_valid_nary_op(string nary_op);
SpliceMode get_splice_mode(string splice_mode);
SpliceMode get_valid_splice_mode(string splice_mode);
string get_splice_mode_string(SpliceMode splice_mode);
//! SfpuOp
SfpuOp get_valid_sfpu_op(string sfpu_op);
SfpuOp get_sfpu_op(string sfpu_op);
bool is_valid_sfpu_op(string sfpu_op);
SfpuExecutionThread get_sfpu_execution_thread(const string& sfpu_execution_thread);
string sfpu_op_to_string(SfpuOp sfpu_op);
//! Matmul op
bool is_valid_matmul_op(string matmul_op);
//! Reduce op
ReduceFunc get_reduce_func(string reduce_func);
ReduceFunc get_valid_reduce_func(string reduce_func);
string get_reduce_func_string(ReduceFunc reduce_func);
bool is_valid_splice_op(string splice_op);
bool is_valid_reduce_op(string reduce_op);
//! Fused op
bool is_valid_fused_op(string matmul_op);
//! Embedding op
EmbeddingOp get_valid_embedding_op(string embedding_op);
EmbeddingOp get_embedding_op(string embedding_op);
bool is_valid_embedding_op(string embedding_op);
//! Tilizer op
TilizerOp get_valid_tilizer_op(string tilizer_op);
TilizerOp get_tilizer_op(string tilizer_op);
bool is_valid_tilizer_op(string tilizer_op);
//! Depthwise op
DepthwiseOp get_depthwise_op(string depthwise_op);
DepthwiseOp get_valid_depthwise_op(string depthwise_op);
bool is_valid_depthwise_op(string depthwise_op);
//! Depthwise op
TopKOp get_topk_op(string topk_op);
TopKOp get_valid_topk_op(string topk_op);
bool is_valid_topk_op(string topk_op);
//! TmOp
TmOp get_valid_tm_op(string tm_op);
TmOp get_tm_op(string tm_op);
bool is_valid_ethernet_op(string op_type);
bool is_non_tensix_op(string op_type);

//! Data conversion
std::uint32_t conv_float_to_u16a(float in_f);
std::uint32_t conv_float_to_u16b(float in_f);

bool is_bfp2_format(DataFormat df);
bool is_bfp4_format(DataFormat df);
bool is_bfp8_format(DataFormat df);
bool is_bfp_format(DataFormat df);
bool is_exp_a_format(DataFormat df);
bool is_exp_b_format(DataFormat df);
uint32_t get_bit_width(uint32_t val);
float get_data_format_size(DataFormat df);
bool is_int_format(DataFormat df);

// allocates memory for tt_op and tt_core
std::shared_ptr<tt_op> create_op(
    tt_op_info* op_info_ptr, const unordered_map<string, tt_fused_op_info>& fused_ops_map = {});

// Query functions for features
bool is_queueless_multichip_supported(const tt::ARCH& device);

// Helper Functions for fused op
std::unordered_map<std::string, tt_scheduled_op_info> get_map_of_consumer_ops_per_input(
    const tt_op_info& base_op_info, const unordered_map<string, tt_fused_op_info>& fused_ops_map);
std::unordered_map<std::string, bool> get_map_of_tile_broadcasting_per_input(const tt_fused_op_info& fused_op_info);
void update_fields_for_op_info_from_scheduled_op_info(
    const tt_op_info& base_op_info,
    const tt_scheduled_op_info& scheduled_op,
    const tt_fused_op_info& fused_op_info,
    tt_op_info& new_op_info);
std::vector<tt_op_info> get_list_of_tt_op_info_from_fused_op(
    const tt_op_info& base_op_info, const unordered_map<string, tt_fused_op_info>& fused_ops_map);

std::unordered_map<std::string, tt_fused_subop_info> parse_fused_op_info(const tt_fused_op_info& t);

//Helper functions for setting correct configs
TileDimReconfigMode get_tile_dim_reconfig_mode(const tt_op_info& op_info);

}  // namespace netlist_utils
