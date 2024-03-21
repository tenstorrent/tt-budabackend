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
    hlk_args_descriptor.push_scalar_value("block_cnt", num_m_sub_blocks * num_n_sub_blocks);
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
    bool kreduce,
    const std::vector<std::vector<int>> &input_tile_dims,
    const std::vector<int> &output_tile_dims) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    config = {
        .kernel_broadcast = kernel_broadcast,
        .input_tile_dims = input_tile_dims,
        .output_tile_dims = output_tile_dims,
        .k = k,
        .sort = sort,
        .kreduce = kreduce,
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

    if (kreduce) {
        set_hlk_cpp_file_name_all_cores("hlks/topk/top_k_reduce.cpp");
    } else {
        set_hlk_cpp_file_name_all_cores("hlks/topk/top_k.cpp");
    }

    set_hlk_operand_dataformat_all_cores(HlkOperand::in0, in_0_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::in1, in_1_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::intermed0, in_0_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::intermed1, in_1_data_format);

    set_hlk_math_fidelity_all_cores(math_fidelity);

    log_assert(!fp32_dest_acc_en || (fp32_dest_acc_en && (in_1_data_format != DataFormat::UInt16)), "Not supported to have Uint16 input for indices and fp32 view of dest registers");

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

struct data {
        data(float v, int i) : value(v), index(i) {}
        float value;
        int index;
    };

void cmp_and_swap(bool direction, int left_idx, int right_idx, std::vector<data> &array, bool unconditional_swap = false) {
    if (direction) {
        // Swap if in1 greater than in0
        if (array.at(right_idx).value > array.at(left_idx).value) {
            data tmp = array.at(left_idx);
            array.at(left_idx) = array.at(right_idx);
            array.at(right_idx) = tmp;
        } else if (array.at(right_idx).value < 0 && array.at(left_idx).value < 0 &&
                array.at(right_idx).value == array.at(left_idx).value) {
            data tmp = array.at(left_idx);
            array.at(left_idx) = array.at(right_idx);
            array.at(right_idx) = tmp;
        }
    } else {
        // Swap if in0 smaller than in1
        if (array.at(right_idx).value < array.at(left_idx).value) {
            data tmp = array.at(left_idx);
            array.at(left_idx) = array.at(right_idx);
            array.at(right_idx) = tmp;
        } else if (array.at(right_idx).value > 0 && array.at(left_idx).value > 0 &&
                array.at(right_idx).value == array.at(left_idx).value) {
            data tmp = array.at(left_idx);
            array.at(left_idx) = array.at(right_idx);
            array.at(right_idx) = tmp;
        }
    }
    if (unconditional_swap) {
        data tmp = array.at(left_idx);
        array.at(left_idx) = array.at(right_idx);
        array.at(right_idx) = tmp;
    }
}

void topk_sort(std::vector<std::vector<data>> &input_arrays, int k, const int array_size, bool kreduce) {
    int logk = (int)std::log2(k);
    int end_phase = logk;
    bool dir_max;
    int dist;

    for (auto &array : input_arrays) {
        if (!kreduce) {
            // Local sort
            for (int ph=0; ph<end_phase; ph++) {
                dir_max = true;
                int num_steps = ph+1;
                int sorted_subseq_len = 1 << num_steps;
                for (int subs=0; subs<(array_size/sorted_subseq_len); subs++) {
                    int subs_base = subs*sorted_subseq_len;

                    // switching of sorting direction in phases 3 & 4 for k>=64
                    bool unconditional_swap = false;
                    if (k >= 64) {
                        if (ph >= 3 && ph < 5 && subs_base % 128 >= 64) {
                            unconditional_swap = true;
                        }
                    }

                    for (int ss=num_steps; ss>0; ss--) {
                        dist = (1 << ss) / 2;
                        
                        int outer_comparisons = sorted_subseq_len/(2*dist); 
                        for (int i=0; i<outer_comparisons; i++) {
                            for (int j=0; j<dist; j++) {
                                cmp_and_swap(dir_max, subs_base+i*2*dist+j, subs_base+i*2*dist+j+dist, array, unconditional_swap);
                            }
                        }
                    }
                    dir_max = !dir_max;
                }
            }
        } else if (k > 16) {
            // Rebuild phase 0
            int num_k_sequences = array_size/k;
            dir_max = true;
            
            for (int kseq=0; kseq<num_k_sequences; kseq++) {
                int subs_base = kseq*k;
                int num_steps = logk;
                for (int ss=num_steps; ss>0; ss--) {
                    dist = (1 << ss) / 2;
                    int outer_comparisons = k/(2*dist); 
                    for (int i=0; i<outer_comparisons; i++) {
                        for (int j=0; j<dist; j++) {
                            cmp_and_swap(dir_max, subs_base+i*2*dist+j, subs_base+i*2*dist+j+dist, array);
                        }
                    }
                }
                dir_max = !dir_max;
            }
        }

        // Merge & rebuild
        int mnr_loops = std::log2(array_size/k);
        int num_k_sequences = array_size/k;
        
        for (int m=0; m<mnr_loops; m++) {
            // Merge 
            dist = (1<<m)*k;
            dir_max = true;
            for (int i=0; i<num_k_sequences/2; i++) {
                for (int j=0; j<k; j++) {
                    cmp_and_swap(dir_max, i*((1<<m)*2*k)+j, i*((1<<m)*2*k)+j+dist, array);
                }
            }
            num_k_sequences = num_k_sequences >> 1;

            // Rebuild
            int k_seq_step = (1<<m)*2*k;
            dir_max = true;
            
            for (int kseq=0; kseq<num_k_sequences; kseq++) {
                int subs_base = kseq*k_seq_step;
                int num_steps = logk;
                for (int ss=num_steps; ss>0; ss--) {
                    dist = (1 << ss) / 2;
                    int outer_comparisons = k/(2*dist); 
                    for (int i=0; i<outer_comparisons; i++) {
                        for (int j=0; j<dist; j++) {
                            cmp_and_swap(dir_max, subs_base+i*2*dist+j, subs_base+i*2*dist+j+dist, array);
                        }
                    }
                }
                dir_max = !dir_max;
            }
        }
    }
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

    int num_cores = this->get_grid_shape().c;

    const int array_size = input.getw() * input.getct() / num_cores * 32;
    
    std::vector<std::vector<data>> input_arrays(num_cores * input.getrt() * input.getz() * 32);
    for (auto &array : input_arrays) {
        array.reserve(array_size);
    }

    for (unsigned int core = 0; core < num_cores; ++core) {
        for (unsigned int wi = 0; wi < input.getw(); ++wi) {
            for (unsigned int zi = 0; zi < input.getz(); ++zi) {
                for (unsigned int ri = 0; ri < input.getrt(); ++ri) {
                    for (unsigned int ci = 0; ci < (input.getct() / num_cores); ++ci) {
                        const tt_tile &tile = input.tile_tensor[wi][zi][ri][core * (input.getct() / num_cores) + ci];
                        const tt_tile &indices_tile = indices.tile_tensor[wi][zi][ri][core * (input.getct() / num_cores) + ci];
                        for (int row = 0; row < tile.tile_height; row++) {
                            std::vector<data> &input_array = input_arrays[core * input.getz() * input.getrt() * 32 + zi * input.getrt() * 32 + ri * 32 + row];
                            for (int col = 0; col < tile.tile_width; col++) {
                                input_array.emplace_back(tile.get(row, col), indices_tile.get(row, col));
                            }
                        }
                    }
                }
            }
        }
    }

    // topk sort
    topk_sort(input_arrays, config.k, array_size, this->config.kreduce);

    tt_shape output_shape{
        .rt = input.getrt(), .ct = num_cores * static_cast<std::uint32_t>(std::ceil(config.k / 32.0f)), .z = input.getz(), .w = 1};

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
    for (unsigned int core = 0; core < num_cores; ++core) {
        for (unsigned int wi = 0; wi < output.getw(); ++wi) {
            for (unsigned int zi = 0; zi < output.getz(); ++zi) {
                for (unsigned int ri = 0; ri < output.getrt(); ++ri) {
                    for (unsigned int ci = 0; ci < output.getct() / num_cores; ++ci) {
                        tt_tile &tile = output.tile_tensor[wi][zi][ri][core * (output.getct() / num_cores) + ci];
                        for (int row = 0; row < tile.tile_height; row++) {
                            std::vector<data> &input_array = input_arrays[core * input.getz() * input.getrt() * 32 + zi * input.getrt() * 32 + ri * 32 + row];
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
