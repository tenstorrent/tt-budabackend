// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "topk.hpp"

#include <algorithm>

#include "model/tt_core.hpp"
#include "netlist_op_info_types.hpp"
#include "tensor.hpp"
#include "tile.hpp"
#include "tt_backend_api_types.hpp"
#include "utils/logger.hpp"
#include "common/model/hlk_desc.hpp"

void tt_topk_op::set_hlk_args_t(
    std::uint32_t block_tile_dim,
    std::uint32_t block_cnt,
    std::uint32_t batch_cnt,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    DataFormat in_0_data_format,
    DataFormat in_1_data_format,
    DataFormat out_data_format,
    std::uint32_t k,
    std::uint32_t sort) {

    tt::tt_hlk_args_desc hlk_args_descriptor;
    hlk_args_descriptor.push_scalar_value("block_tile_dim", num_tiles_per_m_sub_block * num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("block_cnt", num_m_sub_blocks * num_n_sub_blocks * batch_cnt);
    hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
    hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("k", k);
    hlk_args_descriptor.push_scalar_value("sort", sort);
    
    uint32_t N = num_n_sub_blocks*num_tiles_per_n_sub_block*tt::constants::TILE_WIDTH;
    hlk_args_descriptor.push_scalar_value("N", N);  // Add op param
    hlk_args_descriptor.push_scalar_value("M", std::log2(N / k));
    hlk_args_descriptor.push_scalar_value("logk", std::log2(k));
    hlk_args_descriptor.push_scalar_value("num_k_sequences", N / k);
    hlk_args_descriptor.push_scalar_value(
        "seqs_per_2tiles", std::max((2 * 32) / k, (uint32_t)2));  // MT: Is there a define TILE_WIDTH
    hlk_args_descriptor.push_scalar_value(
        "tiles_per_seq", std::max(k / 32, (uint32_t)1));  // MT: Is there a define TILE_WIDTH

    cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
}

tt_topk_op::tt_topk_op(
    string name,
    string type,
    tt_grid_shape grid_shape,
    tt_grid_shape grid_loc,
    bool grid_transpose,
    std::uint32_t block_tile_dim,
    std::uint32_t block_cnt,
    std::uint32_t batch_cnt,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    bool untilize_output,
    MathFidelity math_fidelity,
    bool fp32_dest_acc_en,
    DataFormat in_0_data_format,
    DataFormat in_1_data_format,
    DataFormat out_data_format,
    const vector<std::pair<int, bool>>& kernel_broadcast,
    int k,
    TopKSort sort,
    const std::vector<std::vector<int>> &input_tile_dims,
    const std::vector<int> &output_tile_dims) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    config = {
        .kernel_broadcast = kernel_broadcast,
        .input_tile_dims = input_tile_dims,
        .output_tile_dims = output_tile_dims,
        .k = k,
        .sort = sort,
    };

    set_hlk_args_t(
        block_tile_dim,
        block_cnt,
        batch_cnt,
        num_m_sub_blocks,
        num_n_sub_blocks,
        num_tiles_per_m_sub_block,
        num_tiles_per_n_sub_block,
        in_0_data_format,
        in_1_data_format,
        out_data_format,
        k,
        static_cast<std::uint32_t>(sort));

    set_hlk_cpp_file_name_all_cores("hlks/topk/top_k.cpp");

    set_hlk_operand_dataformat_all_cores(HlkOperand::in0, in_0_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::in1, in_1_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::intermed0, in_0_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::intermed1, in_1_data_format);

    set_hlk_math_fidelity_all_cores(math_fidelity);

    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;

    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};

    for (int i = 0; i < input_tile_dims.size(); i++) {
        this->input_tile_dims.push_back({(uint32_t)input_tile_dims[i][0], (uint32_t)input_tile_dims[i][1]});
    }

    this->kernel_broadcasts = kernel_broadcast;
}

std::string tt_topk_op::type_to_str() {
    std::string ret = "tt_topk_bare_op";
    return ret;
}

void tt_topk_op::model(vector<tt_tensor *> &inputs, tt_tensor *out) {
    TT_ASSERT(inputs.size() == 2);
    log_assert(
        inputs[0]->same_shape(*inputs[1]),
        "inputs[0].shape={} must be same as inputs[1].shape={} in topk",
        inputs[0]->get_shape(),
        inputs[1]->get_shape());

    log_assert(
        out != nullptr, "Tensor output for topk golden model expected to be preallocated. Got a nullptr instead.");

    // Build 32 arrays out of the tensor
    const tt_tensor &input = *inputs[0];
    const tt_tensor &indices = *inputs[1];

    const int array_size = input.getw() * input.getz() * input.getct() * 32;
    struct data {
        data(float v, int i) : value(v), index(i) {}
        float value;
        int index;
    };
    std::vector<std::vector<data>> input_arrays(input.getrt() * 32);
    for (auto &array : input_arrays) {
        array.reserve(array_size);
    }

    for (unsigned int wi = 0; wi < input.getw(); ++wi) {
        for (unsigned int zi = 0; zi < input.getz(); ++zi) {
            for (unsigned int ri = 0; ri < input.getrt(); ++ri) {
                for (unsigned int ci = 0; ci < input.getct(); ++ci) {
                    const tt_tile &tile = input.tile_tensor[wi][zi][ri][ci];
                    const tt_tile &indices_tile = indices.tile_tensor[wi][zi][ri][ci];
                    for (int row = 0; row < tile.tile_height; row++) {
                        std::vector<data> &input_array = input_arrays[ri * 32 + row];
                        for (int col = 0; col < tile.tile_width; col++) {
                            input_array.emplace_back(tile.get(row, col), indices_tile.get(row, col));
                        }
                    }
                }
            }
        }
    }

    for (auto &array : input_arrays) {
        std::partial_sort(array.begin(), array.begin() + config.k, array.end(), [](const data &a, const data &b) {
            return a.value > b.value;
        });
    }

    tt_shape output_shape{
        .rt = input.getrt(), .ct = static_cast<std::uint32_t>(std::ceil(config.k / 32.0f)), .z = 1, .w = 1};

    DataFormat output_data_format = config.sort == TopKSort::Max ? input.get_data_format() : indices.get_data_format();
    bool input_missmatched = (out->get_shape() != output_shape) || (out->get_data_format() != output_data_format);
    if (input_missmatched) {
        *out = tt_tensor(output_shape, output_data_format);
    }

    if (out->is_shape_only() || (out->get_num_stored_tiles() == 0)) {
        out->reserve_tile_tensor();
    }

    const bool output_values = config.sort == TopKSort::Max ? true : false;
    tt_tensor &output = *out;
    for (unsigned int wi = 0; wi < output.getw(); ++wi) {
        for (unsigned int zi = 0; zi < output.getz(); ++zi) {
            for (unsigned int ri = 0; ri < output.getrt(); ++ri) {
                for (unsigned int ci = 0; ci < output.getct(); ++ci) {
                    tt_tile &tile = output.tile_tensor[wi][zi][ri][ci];
                    for (int row = 0; row < tile.tile_height; row++) {
                        std::vector<data> &input_array = input_arrays[ri * 32 + row];
                        for (int col = 0; col < tile.tile_width; col++) {
                            if (ci * tile.tile_width + col < config.k) {
                                const data &data = input_array[ci * tile.tile_width + col];
                                tile.set(row, col, output_values ? data.value : data.index);
                            } else {
                                // Pad with zero
                                tile.set(row, col, 0);
                            }
                        }
                    }
                }
            }
        }
    }

    // Reduce output tensor tiles to specified tile dims.
    out->clear_tile_values(
        std::min(config.input_tile_dims[0][0], config.output_tile_dims[0]),
        std::min(config.input_tile_dims[0][1], config.output_tile_dims[1]));

    // Set output dims of tensor to the ones specified in netlist
    // Example:
    //   if input0 tile dims are 16x32, and input1 tile dims are 16x32, logical output tile dims will be 16x32, and
    //   other values will be zeroed (see above) But if specified output_tile_dims are, for example, 32x32, we need to
    //   set tensor tile height/width to those values since the rest of the stack expects so.
    out->metadata.shape.tile_height = config.output_tile_dims[0];
    out->metadata.shape.tile_width = config.output_tile_dims[1];

    log_assert(out->get_num_stored_tiles() > 0, "out get_num_stored_tiles to topk is empty");
}
