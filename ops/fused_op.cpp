// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "fused_op.hpp"

#include "common/model/tt_core.hpp"
#include "hlks/inc/fused_op_stream.h"
#include "tt_backend_api_types.hpp"

string tt_fused_op::get_hlks_file_name(string id) { return "fused_op_" + id + "_stream.cpp"; }

#include "netlist/netlist_utils.hpp"
#include "ops/eltwise_binary_bare.hpp"
#include "ops/eltwise_unary_sfpu_bare.hpp"
#include "ops/mm_bare.hpp"
#include "ops/reduce_bare.hpp"
#include "ops/unary_bare.hpp"
#include "ops/tm_bare.hpp"
#include "common/tensor_lib.hpp"

tt_fused_op::~tt_fused_op() { }
void tt_fused_op::set_hlk_args_t(
    std::uint32_t block_tile_dim,
    std::uint32_t block_cnt,
    std::uint32_t batch_cnt,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    std::uint32_t gradient_op,
    std::vector<std::pair<int, bool>> kernel_broadcast) {

    log_assert(kernel_broadcast.size() <= FUSED_OP_MAX_NUM_INPUTS,
            "Passed kernel broadcast size {} is greater then the maximum number of supported inputs {}",
            kernel_broadcast.size(), FUSED_OP_MAX_NUM_INPUTS);


    tt::tt_hlk_args_desc hlk_args_descriptor;
    hlk_args_descriptor.push_scalar_value("block_tile_dim", num_tiles_per_m_sub_block * num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("block_cnt", num_m_sub_blocks * num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
    hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("gradient_op", 0); // unused, set to zero
    hlk_args_descriptor.push_scalar_value("transpose", 0); // unused, set to zero
    cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
}

tt_fused_op::tt_fused_op(
    string name,
    string type,
    tt_grid_shape grid_shape,
    tt_grid_shape grid_loc,
    bool grid_transpose,
    bool approximation_mode,
    std::uint32_t block_tile_dim,
    std::uint32_t block_cnt,
    std::uint32_t batch_cnt,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    std::uint32_t gradient_op,
    bool untilize_output,
    MathFidelity math_fidelity,
    bool fp32_dest_acc_en,
    string id,
    std::vector<DataFormat> input_data_formats,
    DataFormat out_data_format,
    DataFormat intermed_data_format,
    std::vector<std::pair<int, bool>> kernel_broadcast,
    const tt_fused_op_info& fused_op_info,
    const std::vector<tt_op_info>& scheduled_op_infos,
    const std::vector<std::vector<int>>& input_tile_dims,
    const std::vector<int>& output_tile_dims,
    const StochRndMode stoch_rnd_mode
    ) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose),
    m_fused_op_info(fused_op_info),
    m_scheduled_op_infos(scheduled_op_infos) {
    set_hlk_args_t(
        block_tile_dim,
        block_cnt,
        batch_cnt,
        num_m_sub_blocks,
        num_n_sub_blocks,
        num_tiles_per_m_sub_block,
        num_tiles_per_n_sub_block,
        gradient_op,
        kernel_broadcast);

    input_cnt = input_data_formats.size();
    output_cnt = 1;
    intermed_cnt = 1;

    set_hlk_cpp_file_name_all_cores("hlks/" + get_hlks_file_name(id));

    for (uint8_t i = 0; i < input_cnt; i++) {
        set_hlk_operand_dataformat_all_cores((HlkOperand)((std::uint8_t)HlkOperand::in0 + i), input_data_formats.at(i));
    }
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);

    for (uint8_t i = 0; i < fused_op_info.num_intermediates; i++) {
        set_hlk_operand_dataformat_all_cores(
            (HlkOperand)((std::uint8_t)HlkOperand::intermed0 + i), intermed_data_format);
    }

    bool int_fpu_en = netlist_utils::is_int_format(out_data_format) || netlist_utils::is_int_format(intermed_data_format);
    for (const auto& input_data_format : input_data_formats) {
        int_fpu_en |= netlist_utils::is_int_format(input_data_format);
    }

    set_hlk_math_fidelity_all_cores(math_fidelity);

    set_hlk_math_approx_mode_all_cores(approximation_mode);

    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;

    // Fused op handles relu in the per op level in net2hlk
    this->relu_en = false;
    this->relu_threshold = 0.0f;
    this->relu_mode = ReluMode::None;
    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};
    this->int_fpu_en = int_fpu_en;
    this->stoch_rnd_mode = stoch_rnd_mode;
    this->kernel_broadcasts = kernel_broadcast;
    for (int i = this->kernel_broadcasts.size(); i < FUSED_OP_MAX_NUM_INPUTS; i++) {
        this->kernel_broadcasts.emplace_back(0, false);
    }

    for (int i = 0; i < input_tile_dims.size(); i++) {
        this->input_tile_dims.push_back({(uint32_t)input_tile_dims[i][0], (uint32_t)input_tile_dims[i][1]});
    }
}

std::string tt_fused_op::type_to_str() {
    std::string ret = "tt_fused_op";
    return ret;
}

void tt_fused_op::model(vector<tt_tensor*>& inputs, tt_tensor* out) {
    std::vector<tt_tensor> intermed_buffers(m_fused_op_info.num_intermediates);
    std::vector<tt_op_info> intermed_buffers_producer_op_infos(m_fused_op_info.num_intermediates);
    std::vector<tt_tensor> dest_buffers(1);

    std::vector<int> default_tile_dims = {32, 32};
    for (const auto& scheduled_op : m_scheduled_op_infos) {
        try {
            log_debug(tt::LogOp, "Processing Op={} in fused_op_id={}", scheduled_op.name, m_fused_op_info.name);
            // Process Output Tensor -- Can be intermediate or the final output.
            string output_string = scheduled_op.attributes.fused_op_output;
            tt_tensor* current_output_tensor;
            int output_intermed_index = -1;
            bool is_output_dest = false;
            DataFormat output_data_format = DataFormat::Invalid;
            if (output_string.find("intermed") == 0) {
                // Using intermediate buffer as output.
                output_intermed_index = stoi(output_string.substr(output_string.find_first_of("0123456789")));
                log_assert(
                    output_intermed_index < m_fused_op_info.num_intermediates,
                    "output={} in scheduled_op={} specified is outside the number of intermediates expected for this "
                    "fusion op id={}",
                    output_string,
                    scheduled_op.name,
                    m_fused_op_info.name);
                current_output_tensor = &intermed_buffers.at(output_intermed_index);
                output_data_format = scheduled_op.intermed_data_format;
            } else if (output_string == "dest") {
                current_output_tensor = &dest_buffers.at(0);
                is_output_dest = true;
                output_data_format = scheduled_op.dest_accumulate_data_format;
            } else {
                current_output_tensor = out;
                output_data_format = scheduled_op.output_data_format;
            }
            // Process Inputs
            bool is_output_also_input = false;
            bool dest_buffer_used = false;
            std::vector<tt_tensor*> input_tensors = {};
            int index = 0;
            for (const auto& input_name : scheduled_op.input_names) {
                if (input_name.find("input") == 0) {
                    int input_index = stoi(input_name.substr(input_name.find_first_of("0123456789")));
                    log_assert(
                        input_index < inputs.size(),
                        "input_name={} in scheduled_op={} specified is outside the number of inputs={} expected for "
                        "this "
                        "fusion op id={}",
                        input_name,
                        scheduled_op.name,
                        inputs.size(),
                        m_fused_op_info.name);
                    input_tensors.push_back(inputs.at(input_index));
                } else if (input_name.find("intermed") == 0) {
                    int intermed_index = -1;
                    intermed_index = stoi(input_name.substr(input_name.find_first_of("0123456789")));
                    if (not is_output_also_input) {
                        is_output_also_input =
                            (output_intermed_index > -1) and (intermed_index == output_intermed_index);
                    }
                    log_assert(
                        intermed_index < m_fused_op_info.num_intermediates,
                        "intermed={} in scheduled_op={} outside the number of intermediates expected for this "
                        "fusion_op_id={}",
                        input_name,
                        scheduled_op.name,
                        m_fused_op_info.name);
                    log_assert(
                        !(intermed_buffers.at(intermed_index).is_shape_only() or
                          (intermed_buffers_producer_op_infos.at(intermed_index).name == "")),
                        "intermediate buffer={} in scheduled_op={} fused_op_id={} is used as input before being used "
                        "as an output, or it was popped erroneously",
                        input_name,
                        scheduled_op.name,
                        m_fused_op_info.name);
                    if (index == 0 and netlist_utils::is_valid_matmul_op(scheduled_op.type)) {
                        int bcast_factor = 1;
                        if (scheduled_op.input_tm_ops.find(0) != scheduled_op.input_tm_ops.end())
                        {
                            for (auto tm: scheduled_op.input_tm_ops.at(0)) {
                                if (tm.first == "c_broadcast") {
                                    bcast_factor = tm.second.at(0);
                                    break;
                                }
                            }
                        }
                        log_assert(
                            (scheduled_op.attributes.m_k
                            * scheduled_op.attributes.u_kt 
                            ==
                            intermed_buffers_producer_op_infos.at(intermed_index).output_dim.mblock_n
                            * intermed_buffers_producer_op_infos.at(intermed_index).output_dim.ublock_ct
                            * bcast_factor),
                            "intermed={} in scheduled_op={} fused_op_id={} for {} needs (m_k={} * u_kt={}) == "
                            "(in0.output_dim.mblock_n={} * in0.output_dim.ublock_ct={} * bcast_factor={})",
                            input_name,
                            scheduled_op.name,
                            m_fused_op_info.name,
                            scheduled_op.type,
                            scheduled_op.attributes.m_k,
                            scheduled_op.attributes.u_kt,
                            intermed_buffers_producer_op_infos.at(intermed_index).output_dim.mblock_n,
                            intermed_buffers_producer_op_infos.at(intermed_index).output_dim.ublock_ct,
                            bcast_factor
                            );
                    } else if (index == 0 and netlist_utils::is_valid_reduce_op(scheduled_op.type)) {
                        const auto& input_dim = intermed_buffers_producer_op_infos.at(intermed_index).output_dim;
                        if (scheduled_op.attributes.reduce_dim == Dim::R) {
                            log_assert(
                                scheduled_op.attributes.m_k == input_dim.mblock_m and scheduled_op.attributes.u_kt == input_dim.ublock_rt,
                                "intermed={} in scheduled_op={} fused_op_id={} for {} needs in0.output_dim.mblock_m={} == "
                                "m_k={} and in0.output_dim.ublock_rt={} == u_kt={}",
                                input_name,
                                scheduled_op.name,
                                m_fused_op_info.name,
                                scheduled_op.type,
                                input_dim.mblock_m,
                                scheduled_op.attributes.m_k,
                                input_dim.ublock_rt,
                                scheduled_op.attributes.u_kt
                            );
                        } else if (scheduled_op.attributes.reduce_dim == Dim::C) {
                            log_assert(
                                scheduled_op.attributes.m_k == input_dim.mblock_n and scheduled_op.attributes.u_kt == input_dim.ublock_ct,
                                "intermed={} in scheduled_op={} fused_op_id={} for {} needs in0.output_dim.mblock_n={} == "
                                "m_k={} and in0.output_dim.ublock_ct={} == u_kt={}",
                                input_name,
                                scheduled_op.name,
                                m_fused_op_info.name,
                                scheduled_op.type,
                                input_dim.mblock_n,
                                scheduled_op.attributes.m_k,
                                input_dim.ublock_ct,
                                scheduled_op.attributes.u_kt
                            );
                        }
                    } else if (index == 1 and netlist_utils::is_valid_matmul_op(scheduled_op.type)) {
                        log_assert(
                            (scheduled_op.attributes.m_k ==
                             intermed_buffers_producer_op_infos.at(intermed_index).output_dim.mblock_m) and
                                (scheduled_op.attributes.u_kt ==
                                 intermed_buffers_producer_op_infos.at(intermed_index).output_dim.ublock_rt),
                            "intermed={} in scheduled_op={} fused_op_id={} for matmul needs in1.output_dim.mblock_m={} "
                            "== m_k={} and in1.output_dim.ublock_rt={} == u_kt={}",
                            input_name,
                            scheduled_op.name,
                            m_fused_op_info.name,
                            intermed_buffers_producer_op_infos.at(intermed_index).output_dim.mblock_m,
                            scheduled_op.attributes.m_k,
                            intermed_buffers_producer_op_infos.at(intermed_index).output_dim.ublock_rt,
                            scheduled_op.attributes.u_kt);
                    }
                    input_tensors.push_back(&intermed_buffers.at(intermed_index));
                } else if (input_name == "dest") {
                    dest_buffer_used = true;
                    if (not is_output_also_input) {
                        is_output_also_input = is_output_dest;
                    }
                    log_assert(
                        ((index == 1) and (netlist_utils::is_valid_matmul_op(scheduled_op.type) or netlist_utils::is_valid_binary_op(scheduled_op.type))) or
                            ((index == 0) and (netlist_utils::is_valid_binary_op(scheduled_op.type) or
                                               netlist_utils::is_valid_sfpu_op(scheduled_op.type) or (netlist_utils::get_unary_op(scheduled_op.type) == UnaryOp::Datacopy))),
                        "dest buffer={} in scheduled_op={} fused_op_id={} can only have dest buffer used as input0 of "
                        "binary/unary op or input1 of binary/matmul op",
                        input_name,
                        scheduled_op.name,
                        m_fused_op_info.name);
                    log_assert(
                        !dest_buffers.at(0).is_shape_only(),
                        "dest buffer={} in scheduled_op={} fused_op_id={} is used as input before being used "
                        "as an output, or it was popped erroneously",
                        input_name,
                        scheduled_op.name,
                        m_fused_op_info.name);
                    input_tensors.push_back(&dest_buffers.at(0));
                } else {
                    log_fatal("Can only have input/intermediates/dest as inputs to ops within a fused_op");
                }
                index++;
            }
            // We are not using dest_buffer as an input, so we should pop it, since every op will "overwrite dest"
            if (not dest_buffer_used) {
                dest_buffers.at(0).tile_tensor.clear();
            }

            // If we are reading and writing to same intermediate buffer, it must also be explicitly popped, check for
            // this, otherwise, we must always we writing to a popped intermediate buffer
            std::set<string> set_inputs_to_pop = {};
            std::set_union(
                scheduled_op.attributes.fused_op_inputs_to_pop.begin(),
                scheduled_op.attributes.fused_op_inputs_to_pop.end(),
                scheduled_op.attributes.fused_op_inputs_to_pop_last.begin(),
                scheduled_op.attributes.fused_op_inputs_to_pop_last.end(),
                std::inserter(set_inputs_to_pop, set_inputs_to_pop.begin()));
            if (is_output_also_input and (not is_output_dest)) {
                if (netlist_utils::is_valid_matmul_op(scheduled_op.type) and (scheduled_op.attributes.m_k > 1)) {
                    log_fatal(
                        "scheduled_op={} Cannot have input and output using same output=intermed{} when type is matmul "
                        "and m_k={} is > 1",
                        scheduled_op.name,
                        output_intermed_index,
                        scheduled_op.attributes.m_k);
                }
                bool input_output_intermed_is_popped = false;
                for (const auto& input_to_pop : set_inputs_to_pop) {
                    if (input_to_pop.find("intermed") == 0) {
                        int intermed_index = -1;
                        intermed_index = stoi(input_to_pop.substr(input_to_pop.find_first_of("0123456789")));
                        log_assert(
                            intermed_index < m_fused_op_info.num_intermediates,
                            "input_to_pop={} in scheduled_op={} is outside the number of intermediates expected for "
                            "this fusion op id={}",
                            input_to_pop,
                            scheduled_op.name,
                            m_fused_op_info.name);
                        if (output_intermed_index == intermed_index) {
                            input_output_intermed_is_popped = true;
                        }
                    }
                }
                log_assert(
                    input_output_intermed_is_popped,
                    "output={} in scheduled_op={} fusion op id={} is used as an input and output, so it needs to be "
                    "explicitly popped as well",
                    output_string,
                    scheduled_op.name,
                    m_fused_op_info.name);
            } else if (output_intermed_index > -1) {
                log_assert(
                    intermed_buffers.at(output_intermed_index).is_shape_only(),
                    "output={} in scheduled_op={} fusion op id={} specified being overwritten -- This is either "
                    "missing a pop or two drivers to the same intermed",
                    output_string,
                    scheduled_op.name,
                    m_fused_op_info.name);
            }
            
            std::vector<tt_tensor*> post_tm_input_tensors = {};
            std::vector<tt_tensor> tm_input_tensors_tmp_storage;
            tm_input_tensors_tmp_storage.reserve(input_tensors.size());
            for (int input_index = 0; input_index < input_tensors.size(); input_index++) {
                if (scheduled_op.input_tm_ops.find(input_index) != scheduled_op.input_tm_ops.end()) {
                    // Apply tms
                    tm_input_tensors_tmp_storage.push_back(
                        *input_tensors.at(input_index));  // Create a new storage container
                    post_tm_input_tensors.push_back(
                        &tm_input_tensors_tmp_storage.back());  // Point the post_tm input to the container
                    // Process list of all TM's in place
                    for (const auto& tm : scheduled_op.input_tm_ops.at(input_index)) {
                        string tm_name = get<0>(tm);
                        tt_tm_config config({
                            .op = netlist_utils::get_valid_tm_op(tm_name),
                            .args = get<1>(tm),
                        });
                        log_assert(
                            scheduled_op.input_names.at(input_index) != "dest",
                            "tms are not supported for dest buffer inputs in fused ops for op={} in fused_op_id={}",
                            scheduled_op.name,
                            m_fused_op_info.name);
                        log_assert(
                            (config.op == TmOp::cBroadcast) or (config.op == TmOp::rBroadcast) or
                                (config.op == TmOp::zBroadcast) or (config.op == TmOp::TileBroadcast),
                            "Only broadcast/tile_broadcast tms are supported in fused ops for op={} in fused_op_id={}",
                            scheduled_op.name,
                            m_fused_op_info.name);
                        log_assert(
                            (config.op != TmOp::TileBroadcast) or
                                ((input_index == 1) and (netlist_utils::is_valid_binary_op(scheduled_op.type))),
                            "Can only fo a tile_broadcast if it is the second input of a binary op for op={} in "
                            "fused_op_id={}",
                            scheduled_op.name,
                            m_fused_op_info.name);
                        log_debug(
                            tt::LogOp,
                            "fused_op_id={} -- input_index={} -- shape={} -- tm={}",
                            m_fused_op_info.name,
                            input_index,
                            tm_input_tensors_tmp_storage.back().get_shape(),
                            tm_name);

                        // Create input tensor vector for TM op
                        vector<tt_tensor*> tm_input = {&tm_input_tensors_tmp_storage.back()};
                        tt_tm::utils::golden_model(config, tm_input, &tm_input_tensors_tmp_storage.back());
                    }
                } else if (scheduled_op.input_names.at(input_index) == "dest") {
                    post_tm_input_tensors.push_back(&dest_buffers.at(0));
                } else {
                    log_debug(
                        tt::LogGolden,
                        "fused_op_id={} -- input_index={} -- shape={} -- no tms",
                        m_fused_op_info.name,
                        input_index,
                        input_tensors.at(input_index)->get_shape());
                    post_tm_input_tensors.push_back(input_tensors.at(input_index));
                }
                log_assert(
                    post_tm_input_tensors.back()->get_data_format() != DataFormat::Invalid,
                    "Input {} data_format to fused_op is invalid",
                    input_index);
            }

            // Run Scheduled Op
            if (netlist_utils::is_valid_binary_op(scheduled_op.type)) {
                tt_eltwise_binary_config config({.op = netlist_utils::get_binary_op(scheduled_op.type)});

                config.input_tile_dims.push_back(default_tile_dims);
                config.input_tile_dims.push_back(default_tile_dims);
                config.output_tile_dims = default_tile_dims;
                bool is_dequant_op = scheduled_op.type == "dequantization";
                config.zero_point = is_dequant_op ? -scheduled_op.attributes.zero_point
                                                                          : scheduled_op.attributes.zero_point;

                config.is_quantization_op_from_dest = is_dequant_op && scheduled_op.input_names.at(1) == "dest";

                tt_eltwise_binary::utils::golden_model(config, post_tm_input_tensors, current_output_tensor);
            } else if (netlist_utils::is_valid_matmul_op(scheduled_op.type)) {
                log_assert(
                    not scheduled_op.attributes.identity,
                    "Fused Op Id={} is trying to use an identity matmul which is not supported",
                    m_fused_op_info.name);
                tt_matmul_config config = {
                    .block_tile_dim = static_cast<uint32_t>(scheduled_op.attributes.u_kt),
                    .block_cnt = static_cast<uint32_t>(scheduled_op.attributes.m_k),
                    .batch_cnt = static_cast<uint32_t>(scheduled_op.output_dim.t),
                    .num_m_sub_blocks = static_cast<uint32_t>(scheduled_op.output_dim.mblock_m),
                    .num_n_sub_blocks = static_cast<uint32_t>(scheduled_op.output_dim.mblock_n),
                    .num_tiles_per_m_sub_block = static_cast<uint32_t>(scheduled_op.output_dim.ublock_rt),
                    .num_tiles_per_n_sub_block = static_cast<uint32_t>(scheduled_op.output_dim.ublock_ct),
                    .transpose = static_cast<uint32_t>(scheduled_op.transpose),
                    .gradient_op = static_cast<uint32_t>(scheduled_op.gradient_op),
                    .identity = scheduled_op.attributes.identity,
                    .num_index_tiles = static_cast<uint32_t>(scheduled_op.attributes.num_index_tiles),
                    .num_sparse_tiles = static_cast<uint32_t>(scheduled_op.attributes.num_sparse_tiles),
                    .num_columns = static_cast<uint32_t>(scheduled_op.attributes.num_columns),
                    .sparse_tile_ptr_bits = static_cast<uint32_t>(scheduled_op.attributes.sparse_tile_ptr_bits),
                    .indices_len = static_cast<uint32_t>(scheduled_op.attributes.indices_len),
                    .fracture_factor = static_cast<uint32_t>(scheduled_op.attributes.fracture_factor),
                    .starting_row = static_cast<uint32_t>(scheduled_op.attributes.starting_row),
                    .grid_shape = grid_shape,
                };

                // temp workaround for fused op
                config.input_tile_dims.push_back(default_tile_dims);
                config.input_tile_dims.push_back(default_tile_dims);
                config.output_tile_dims = default_tile_dims;

                tt_matmul::utils::golden_model(config, post_tm_input_tensors, current_output_tensor);

            } else if (netlist_utils::is_valid_sfpu_op(scheduled_op.type)) {
                tt_eltwise_unary_config config = {
                    .op = netlist_utils::get_sfpu_op(scheduled_op.type),
                    .approximation_mode = scheduled_op.attributes.approximate_mode,
                    .transpose = scheduled_op.transpose,
                    .probability = scheduled_op.attributes.probability,
                    .scale = scheduled_op.attributes.scale,
                    .exponent = scheduled_op.attributes.exponent,
                    .vector_mode = scheduled_op.attributes.vector_mode};
                tt_eltwise_unary::utils::golden_model(config, post_tm_input_tensors, current_output_tensor);
            } else if (netlist_utils::is_valid_unary_op(scheduled_op.type)) {

                tt_unary_config config {
                    .op = netlist_utils::get_unary_op(scheduled_op.type),
                    .transpose = scheduled_op.transpose,
                };

                tt_unary::utils::golden_model(config, post_tm_input_tensors, current_output_tensor);
            }
            else if (netlist_utils::is_valid_reduce_op(scheduled_op.type)) {
                tt_reduce_config config = {
                    .reduce_func = scheduled_op.attributes.reduce_type,
                    .reduce_dim = scheduled_op.attributes.reduce_dim};
                tt_reduce::utils::golden_model(config, post_tm_input_tensors, current_output_tensor);

            } else {
                log_fatal(
                    "Unsupported op type={} used in fusion_op_id={}",
                    scheduled_op.type,
                    m_fused_op_info.name);
            }
            // Apply relu if enabled
            if (scheduled_op.attributes.relu_en) {
                tensor_lib::relu_with_threshold(
                    *current_output_tensor,
                    *current_output_tensor, 
                    Dim::RC,
                    scheduled_op.attributes.relu_mode, 
                    scheduled_op.attributes.relu_threshold
                );
            }

            if (scheduled_op.type == "quantization" || scheduled_op.type == "requantization") {
                current_output_tensor->set_data_format(DataFormat::Int8);
                current_output_tensor->adjust_tensor_for_accuracy();
            } else if (output_data_format == DataFormat::Int8 || output_data_format == DataFormat::Int32) {
                current_output_tensor->set_data_format(output_data_format);
                current_output_tensor->adjust_tensor_for_accuracy();
            }

            if (output_intermed_index != -1) {
                intermed_buffers_producer_op_infos.at(output_intermed_index) = scheduled_op;
            }
            // Processing the popping of inputs.  Skip if the output == input, then we don't "pop" the intermediate
            // buffer
            for (const auto& input_to_pop : set_inputs_to_pop) {
                if (input_to_pop.find("intermed") == 0) {
                    if (input_to_pop != scheduled_op.attributes.fused_op_output) {
                        int intermed_index = -1;
                        intermed_index = stoi(input_to_pop.substr(input_to_pop.find_first_of("0123456789")));
                        log_assert(
                            intermed_index < m_fused_op_info.num_intermediates,
                            "input_to_pop={} in scheduled_op={} is outside the number of intermediates expected for "
                            "this "
                            "fusion op id={}",
                            input_to_pop,
                            scheduled_op.name,
                            m_fused_op_info.name);
                        intermed_buffers.at(intermed_index).tile_tensor.clear();
                        intermed_buffers_producer_op_infos.at(intermed_index) = {};
                    }
                }
            }

        } catch (const std::exception& e) {
            log_error("{}", e.what());
            log_fatal(
                "Error running fused_op_id={}, scheduled_op={}",
                m_fused_op_info.name,
                scheduled_op.name);
        }
    }
    for (int intermed_index = 0; intermed_index < intermed_buffers.size(); intermed_index++) {
        log_assert(
            intermed_buffers.at(intermed_index).is_shape_only(),
            "At the end of a fused op, intermed{} wasn't popped yet",
            intermed_index);
    }
    log_assert(out->get_data_format() != DataFormat::Invalid, "out data_format to tt_fused_op is invalid");
}
