// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "nary_bare.hpp"

#include "common/model/tt_core.hpp"
#include "common/env_lib.hpp"
#include "common/tensor_lib.hpp"
#include "common/tile_lib.hpp"

#include "hlks/inc/splice_common.h"
#include "netlist_utils.hpp"

string tt_nary_bare_op::get_hlks_file_name(NaryOp nary_op) {
    return "splice/" + tt_nary::utils::nary_op_to_string(nary_op) + "_" +
           tt_nary::utils::splice_mode_to_string(m_config.splice_mode) + "_stream" + ".cpp";
}

tt_nary_bare_op::~tt_nary_bare_op() {
}

void tt_nary_bare_op::set_hlk_args_t(
    std::uint32_t block_tile_dim,
    std::uint32_t block_cnt,
    std::uint32_t batch_cnt,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    std::uint32_t gradient_op,
    std::uint32_t num_inputs,
    std::vector<splice_config> splice_configs,
    std::vector<DataFormat> in_data_formats,
    DataFormat out_data_format,
    std::vector<std::pair<int, bool>> kernel_broadcast) {
    if (m_config.op == NaryOp::Splice) {
        if (m_config.splice_mode == SpliceMode::Ublock || m_config.splice_mode == SpliceMode::T) {
            log_assert(
                kernel_broadcast.size() <= SPLICE_MAX_NUM_INPUTS,
                "Passed kernel broadcast size {} is greater then the maximum number of supported inputs {}",
                kernel_broadcast.size(),
                SPLICE_MAX_NUM_INPUTS);

            tt::tt_hlk_args_desc hlk_args_descriptor;
            hlk_args_descriptor.push_scalar_value(
                "block_tile_dim", num_tiles_per_m_sub_block * num_tiles_per_n_sub_block);
            hlk_args_descriptor.push_scalar_value("block_cnt", num_m_sub_blocks * num_n_sub_blocks);
            hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
            hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
            hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
            hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
            hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
            hlk_args_descriptor.push_scalar_value("gradient_op", 0);  // unused, set to zero
            hlk_args_descriptor.push_scalar_value("transpose", 0);    // unused, set to zero
            hlk_args_descriptor.push_scalar_value("num_inputs", num_inputs);

            vector<vector<int32_t>> input_idx_args_2d;
            for (int i = 0; i < SPLICE_MAX_NUM_INPUTS; i++) {
                vector<int32_t> input_idx;
                if (i < splice_configs.size()) {
                    input_idx.push_back(splice_configs.at(i).index);
                    input_idx.push_back(splice_configs.at(i).length);
                    input_idx.push_back(splice_configs.at(i).stride);
                } else {
                    // Fill with zeroes
                    for (int j = 0; j < SPLICE_INPUT_IDX_COLUMNS; j++) {
                        input_idx.push_back(0);
                    }
                }
                input_idx_args_2d.push_back(input_idx);
            }

            hlk_args_descriptor.push_2d_vector_values("input_idx", input_idx_args_2d);

            cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
        } else {
            log_fatal("Unsupported splice mode!");
        }
    } else {
        log_fatal("Unsupported nary op!");
    }
}

tt_nary_bare_op::tt_nary_bare_op(
    string name,
    string type,
    tt_grid_shape grid_shape,
    tt_grid_shape grid_loc,
    bool grid_transpose,
    NaryOp nary_op,
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
    bool relu_en,
    std::uint32_t relu_threshold,
    ReluMode relu_mode,
    std::vector<DataFormat> in_data_formats,
    DataFormat out_data_format,
    DataFormat intermed_data_format,
    Dim output_ublock_order,
    SpliceMode splice_mode,
    std::vector<std::vector<int>> splice_configs,
    std::vector<std::pair<int, bool>> kernel_broadcast,
    StochRndMode stoch_rnd_mode,
    const std::vector<std::vector<int>>& input_tile_dims,
    const std::vector<int>& output_tile_dims
    ) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    std::vector<splice_config> tmp_splice_configs = {};
    for (const auto &splice_config : splice_configs) {
        tmp_splice_configs.push_back({
            .index = splice_config.at(0),
            .length = splice_config.at(1),
            .stride = splice_config.at(2),
        });
    }
    m_config = {
        .op = nary_op,
        .num_inputs = static_cast<int>(in_data_formats.size()),
        .output_grid_shape =
            {
                .r = grid_shape.r,
                .c = grid_shape.c,
            },
        .output_mblock_shape =
            {
                .r = num_m_sub_blocks,
                .c = num_n_sub_blocks,
            },
        .output_ublock_shape =
            {
                .r = num_tiles_per_m_sub_block,
                .c = num_tiles_per_n_sub_block,
            },
        .batch_cnt = batch_cnt,
        .out_data_format = out_data_format,
        .output_ublock_order = output_ublock_order,
        .splice_mode = splice_mode,
        .splice_configs = tmp_splice_configs,
        .kernel_broadcast = kernel_broadcast
    };
    // Missing some static graph analysis to determine input shapes/sizes

    std::string root_dir = buda_home();

    input_cnt = in_data_formats.size();

    set_hlk_args_t(
        block_tile_dim,
        block_cnt,
        batch_cnt,
        num_m_sub_blocks,
        num_n_sub_blocks,
        num_tiles_per_m_sub_block,
        num_tiles_per_n_sub_block,
        gradient_op,
        input_cnt,
        tmp_splice_configs,
        in_data_formats,
        out_data_format, 
        kernel_broadcast);

    set_hlk_cpp_file_name_all_cores("hlks/" + get_hlks_file_name(nary_op));

    for (uint8_t i = 0; i < input_cnt; i++) {
        set_hlk_operand_dataformat_all_cores((HlkOperand)((std::uint8_t)HlkOperand::in0 + i), in_data_formats[i]);
    }

    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);

    set_hlk_math_fidelity_all_cores(math_fidelity);

    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;
    this->int_fpu_en = netlist_utils::is_int_format(in_data_formats[0]);
    this->relu_en = relu_en;
    this->relu_threshold = relu_threshold;
    this->relu_mode = relu_mode;
    this->stoch_rnd_mode = stoch_rnd_mode;
    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};
    this->kernel_broadcasts = kernel_broadcast;
    for (int i = this->kernel_broadcasts.size(); i < SPLICE_MAX_NUM_INPUTS; i++) {
        this->kernel_broadcasts.emplace_back(0, false);
    }

    for (int i = 0; i < input_tile_dims.size(); i++) {
        this->input_tile_dims.push_back({(uint32_t)input_tile_dims[i][0], (uint32_t)input_tile_dims[i][1]});
    }
}

std::string tt_nary_bare_op::type_to_str() {
    std::string ret = "tt_nary_bare_op";
    return ret;
}

void tt_nary_bare_op::model(vector<tt_tensor *> &inputs, tt_tensor *out) {
    tt_nary::utils::golden_model(m_config, inputs, out);
}

string tt_nary::utils::nary_op_to_string(NaryOp nary_op) {
    switch (nary_op) {
        case NaryOp::Splice: return "splice";
        default: log_fatal("Unsupported eltwise nary op!");
    }
    return "";
}
string tt_nary::utils::splice_mode_to_string(SpliceMode splice_mode) {
    switch (splice_mode) {
        case SpliceMode::Ublock: return "ublock";
        case SpliceMode::T: return "z";
        default: log_fatal("Unsupported eltwise nary op!");
    }
    return "";
}
void tt_nary::utils::golden_model(const tt_nary_config &config, vector<tt_tensor *> &inputs, tt_tensor *out) {
    log_assert(
        inputs.size() == config.num_inputs,
        "num_inputs={} for nary golden must match the num_inputs={} detected from formats",
        inputs.size(),
        config.num_inputs);
    if ((config.op == NaryOp::Splice) and (config.splice_mode == SpliceMode::Ublock)) {
        // Need to split inputs and combine things based off the inputs/outputs
        vector<vector<vector<tt_tensor>>> multi_core_inputs(config.output_grid_shape.r);
        for (int core_r = 0; core_r < config.output_grid_shape.r; core_r++) {
            multi_core_inputs.at(core_r) = vector<vector<tt_tensor>>(config.output_grid_shape.c);
            for (int core_c = 0; core_c < config.output_grid_shape.c; core_c++) {
                multi_core_inputs.at(core_r).at(core_c) = vector<tt_tensor>(inputs.size());
            }
        }
        for (int input_index = 0; input_index < inputs.size(); input_index++) {
            auto input_per_rows = tensor_lib::split_merge_ops::vsplit(*inputs.at(input_index), config.output_grid_shape.r, false);
            for (int core_r = 0; core_r < config.output_grid_shape.r; core_r++) {
                auto input_per_core = tensor_lib::split_merge_ops::hsplit(input_per_rows.at(core_r), config.output_grid_shape.c, false);
                for (int core_c = 0; core_c < config.output_grid_shape.c; core_c++) {
                    multi_core_inputs.at(core_r).at(core_c).at(input_index) = input_per_core.at(core_c);
                }
            }
        }

        vector<vector<tt_tensor>> multi_core_outputs = {};
        // compute the grid of outputs
        for (int core_r = 0; core_r < config.output_grid_shape.r; core_r++) {
            multi_core_outputs.push_back(vector<tt_tensor>(config.output_grid_shape.c));
            for (int core_c = 0; core_c < config.output_grid_shape.c; core_c++) {
                ublock_splice(
                    multi_core_outputs.at(core_r).at(core_c), multi_core_inputs.at(core_r).at(core_c), config);
            }
        }
        // recombine the outputs
        vector<tt_tensor> row_outputs(config.output_grid_shape.r);
        for (int core_r = 0; core_r < config.output_grid_shape.r; core_r++) {
            row_outputs.at(core_r) = tensor_lib::split_merge_ops::hmerge(multi_core_outputs.at(core_r), false);
        }
        *out = tensor_lib::split_merge_ops::vmerge(row_outputs, false);

    } else if ((config.op == NaryOp::Splice) and (config.splice_mode == SpliceMode::T)) {
        // T splicing doesn't need to be split across cores, since we always expect the split/concat that occurs
        // is orthogonal to t splicing.
        t_splice(*out, inputs, config);
    }
    log_assert(out->get_data_format() != DataFormat::Invalid, "out data_format to tt_nary is invalid");
    log_assert(out->get_num_stored_tiles() > 0, "out get_num_stored_tiles to tt_nary is empty");
}
tt::tt_block_shape tt_nary::utils::get_tile_indices_from_ublock_offsets(
    const tt::tt_block_shape &mblock,
    const tt::tt_block_shape &ublock,
    const Dim ublock_order,  // FIXME: Take a look here
    const int ublock_offset,
    const int tile_offset) {
    // Get exact mblock location
    tt::tt_block_shape mblock_index = {};
    if (ublock_order == Dim::R) {
        mblock_index = {
            .r = ublock_offset / mblock.c,
            .c = ublock_offset % mblock.c,
        };
    } else if (ublock_order == Dim::C) {
        mblock_index = {
            .r = ublock_offset % mblock.r,
            .c = ublock_offset / mblock.r,
        };
    }
    log_assert(
        (mblock_index.c < mblock.c) and (mblock_index.r < mblock.r),
        "Calculated mblock_index={} must be within than mblock shape={} for ublock_order={}",
        mblock_index,
        mblock,
        ublock_order);
    // Get Exact tile location
    tt::tt_block_shape ublock_index = {
        .r = tile_offset / ublock.c,
        .c = tile_offset % ublock.c,
    };
    log_assert(
        (ublock_index.c < ublock.c) and (ublock_index.r < ublock.r),
        "Calculated ublock_index={} must be within than ublock shape={}",
        ublock_index,
        ublock);

    return {
        .r = mblock_index.r * ublock.r + ublock_index.r,
        .c = mblock_index.c * ublock.c + ublock_index.c,
    };
}
void tt_nary::utils::ublock_splice(
    tt::tt_tensor &output_tensor, const std::vector<tt::tt_tensor> &inputs, const tt_nary_config &config) {
    for (int input_index = 0; input_index < inputs.size(); input_index++) {
        validate_input(config, inputs.at(input_index), input_index);
    }
    output_tensor = tt_tensor(
        {.rt = config.output_mblock_shape.r * config.output_ublock_shape.r,
         .ct = config.output_mblock_shape.c * config.output_ublock_shape.c,
         .z = config.batch_cnt,
         .w = 1},
        config.out_data_format);
    if (output_tensor.is_shape_only() or (output_tensor.get_num_stored_tiles() == 0)) {
        output_tensor.reserve_tile_tensor();
    }
    // actually splice
    for (int wi = 0; wi < output_tensor.getw(); ++wi) {
        for (int zi = 0; zi < output_tensor.getz(); ++zi) {
            int output_ublock_index = 0;
            std::vector<int> input_start_ublock_offset(inputs.size(), 0);
            while (output_ublock_index < config.output_mblock_shape.volume()) {
                for (int input_index = 0; input_index < config.splice_configs.size(); input_index++) {
                    const auto &splice_config = config.splice_configs.at(input_index);
                    input_start_ublock_offset.at(input_index) += splice_config.index;  // Drop initial index amount
                    log_assert(
                        output_ublock_index < config.output_mblock_shape.volume(),
                        "output_ublock_index={} must be less than the volume of the specified output_mblock_shape={}",
                        output_ublock_index,
                        config.output_mblock_shape);
                    // Copy the input ublocks for the length specified.
                    for (int length_index = 0; length_index < splice_config.length; length_index++) {
                        for (int tile_index = 0; tile_index < config.output_ublock_shape.volume(); tile_index++) {
                            tt::tt_block_shape input_tensor_tile_index = get_tile_indices_from_ublock_offsets(
                                {// We have to adjust the input mblock since we reblock inputs to be output ublock
                                 // shape.
                                 .r = inputs.at(input_index).getrt() / config.output_ublock_shape.r,
                                 .c = inputs.at(input_index).getct() / config.output_ublock_shape.c},
                                config.output_ublock_shape,
                                config.output_ublock_order,  // Matches pipegen, we always set ublock_order of inputs
                                                             // the same as what we have set in output.
                                input_start_ublock_offset.at(input_index) + length_index,
                                tile_index);
                            tt::tt_block_shape output_tensor_tile_index = get_tile_indices_from_ublock_offsets(
                                config.output_mblock_shape,
                                config.output_ublock_shape,
                                config.output_ublock_order,
                                output_ublock_index + length_index,
                                tile_index);
                            log_trace(
                                tt::LogOp,
                                "Splice, output_tensor_tile_index={} = input_index={} at input_tensor_tile_index={}",
                                output_tensor_tile_index,
                                input_index,
                                input_tensor_tile_index);
                            // Copy the tile over.
                            tile_lib::unary::datacopy(
                                *output_tensor.get_tile_ptr(
                                    output_tensor_tile_index.r, output_tensor_tile_index.c, zi, wi),
                                *inputs.at(input_index)
                                     .get_tile_ptr(input_tensor_tile_index.r, input_tensor_tile_index.c, zi, wi));
                        }
                    }
                    input_start_ublock_offset.at(input_index) += splice_config.stride;  // Skip length -> stride amount
                    output_ublock_index += splice_config.length;
                }
            }

            for (int input_index = 0; input_index < config.splice_configs.size(); input_index++) {
                log_assert(
                    input_start_ublock_offset.at(input_index) * config.output_ublock_shape.volume() ==
                        inputs.at(input_index).getrt() * inputs.at(input_index).getct(),
                    "Input_index={} must be consumed/dropped fully by the end of the batch (based off stride and how "
                    "many times we repeated), or we will hang input streams",
                    input_index);
            }
        }
    }
}
void tt_nary::utils::t_splice(
    tt::tt_tensor &output_tensor, const std::vector<tt::tt_tensor *> &inputs, const tt_nary_config &config) {
    for (int input_index = 0; input_index < inputs.size(); input_index++) {
        validate_input(config, *inputs.at(input_index), input_index);
    }
    output_tensor = tt_tensor(
        {.rt = config.output_mblock_shape.r * config.output_ublock_shape.r * config.output_grid_shape.r,
         .ct = config.output_mblock_shape.c * config.output_ublock_shape.c * config.output_grid_shape.c,
         .z = config.batch_cnt,
         .w = 1},
        config.out_data_format);
    if (output_tensor.is_shape_only() or (output_tensor.get_num_stored_tiles() == 0)) {
        output_tensor.reserve_tile_tensor();
    }
    // actually splice
    for (int wi = 0; wi < output_tensor.getw(); ++wi) {
        // The mblock*ublock between inputs and outputs should be the same
        // Go through inputs,
        int output_z_index = 0;
        std::vector<int> input_start_z_offset(inputs.size(), 0);
        while (output_z_index < output_tensor.getz()) {
            //
            for (int input_index = 0; input_index < config.splice_configs.size(); input_index++) {
                const auto &splice_config = config.splice_configs.at(input_index);
                input_start_z_offset.at(input_index) += splice_config.index;  // Drop initial index amount
                // Copy the input t for the length specified.
                for (int length_index = 0; length_index < splice_config.length; length_index++) {
                    log_assert(
                        output_z_index < output_tensor.getz(),
                        "output_z_index={} must be less than the zdim of the output tensor={}",
                        output_z_index,
                        config.batch_cnt);
                    log_assert(
                        (inputs.at(input_index)->getrt() == output_tensor.getrt()) and
                            (inputs.at(input_index)->getct() == output_tensor.getct()),
                        "Splicing at T granularity assumes the rt/ct of the input_shape={} match the rt/ct of "
                        "output_shape={}",
                        inputs.at(input_index)->get_shape(),
                        output_tensor.get_shape());
                    for (int rti = 0; rti < output_tensor.getrt(); ++rti) {
                        for (int cti = 0; cti < output_tensor.getct(); ++cti) {
                            tile_lib::unary::datacopy(
                                *output_tensor.get_tile_ptr(rti, cti, output_z_index + length_index, wi),
                                *inputs.at(input_index)
                                     ->get_tile_ptr(rti, cti, input_start_z_offset.at(input_index) + length_index, wi));
                        }
                    }
                }
                input_start_z_offset.at(input_index) += splice_config.stride;  // Skip length -> stride amount
                output_z_index += splice_config.length;
            }
        }
        for (int input_index = 0; input_index < config.splice_configs.size(); input_index++) {
            log_assert(
                input_start_z_offset.at(input_index) == inputs.at(input_index)->getz(),
                "Input_index={} must be consumed/dropped in t granularity by the end (based off stride and how many "
                "times we repeated), or we will hang input streams",
                input_index);
        }
    }
}

void tt_nary::utils::validate_input(const tt_nary_config &config, const tt_tensor &input, const int input_index) {
    // work though attributes
    const auto &splice_info = config.splice_configs.at(input_index);
    const auto &input_shape = input.get_shape();
    int limit_value = 0;
    string limit_string = "";
    if (config.splice_mode == SpliceMode::T) {
        limit_string = "t";
        limit_value = input_shape.z;
    } else if (config.splice_mode == SpliceMode::Ublock) {
        limit_string = "mblock_volume";
        log_assert(
            not((input_shape.ct * input_shape.rt) % config.output_ublock_shape.volume()),
            "Input={} has output ublock_shape={} which needs to divide post TM rt*ct of input_shape={} evenly for "
            "splice ublock",
            input_index,
            config.output_ublock_shape,
            input_shape);
        limit_value = (input_shape.ct * input_shape.rt) / config.output_ublock_shape.volume();
    } else {
    }
    log_assert(
        splice_info.index + splice_info.length <= limit_value,
        "Splice op input={} must have splice_info.index={} + splice_info.length={} <= {}={}",
        input_index,
        splice_info.index,
        splice_info.length,
        limit_string,
        limit_value);
    log_assert(
        splice_info.index + splice_info.stride <= limit_value,
        "Splice op input={} must have splice_info.index={} + splice_info.stride={} <= {}={}",
        input_index,
        splice_info.index,
        splice_info.stride,
        limit_string,
        limit_value);
    log_assert(
        (limit_value % (splice_info.index + splice_info.stride)) == 0,
        "Splice op input={} must have splice_info.index={} + splice_info.stride={} must divide {}={} evenly",
        input_index,
        splice_info.index,
        splice_info.stride,
        limit_string,
        limit_value);
    log_assert(
        splice_info.index < limit_value,
        "Splice op input={} must have splice_info.index={} < {}={}",
        input_index,
        splice_info.index,
        limit_string,
        limit_value);
    log_assert(
        splice_info.stride >= splice_info.length,
        "Splice op input={} must have splice_info.stride={} >= splice_info.length={}",
        input_index,
        splice_info.stride,
        splice_info.length);
}