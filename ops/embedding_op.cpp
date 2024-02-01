// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "embedding_op.hpp"

#include "common/model/tt_core.hpp"
#include "common/param_lib.hpp"
#include "common/tensor_lib.hpp"
#include "common/tile_lib.hpp"

namespace embedding_op_stream {
constexpr std::uint32_t INV_INDEX = 0xffffffff;
}

string tt_embedding_op::get_hlks_file_name() { return "embedding/embedding_op_stream.cpp"; }

void tt_embedding_op::set_hlk_args_t(
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
    std::uint32_t in_num_n_sub_blocks,
    std::uint32_t in_num_tiles_per_n_sub_block) {

    tt::tt_hlk_args_desc hlk_args_descriptor;
    hlk_args_descriptor.push_scalar_value("block_tile_dim", num_tiles_per_m_sub_block * num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("block_cnt", num_m_sub_blocks * num_n_sub_blocks * batch_cnt);
    hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
    hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
    hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
    hlk_args_descriptor.push_scalar_value("in_num_n_sub_blocks", in_num_n_sub_blocks);
    hlk_args_descriptor.push_scalar_value("in_num_tiles_per_n_sub_block", in_num_tiles_per_n_sub_block);

    cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
}

tt_embedding_op::~tt_embedding_op() {}

tt_embedding_op::tt_embedding_op(
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
    bool relu_en,
    std::uint32_t relu_threshold,
    ReluMode relu_mode,
    DataFormat in_0_data_format,
    DataFormat in_1_data_format,
    DataFormat out_data_format,
    DataFormat intermed_data_format,
    int num_indices,
    std::uint32_t in_num_n_sub_blocks,
    std::uint32_t in_num_tiles_per_n_sub_block,
    const std::vector<std::vector<int>> &input_tile_dims,
    const std::vector<int> &output_tile_dims,
    const StochRndMode stoch_rnd_mode
    ) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {
    m_config = {
        .num_indices = num_indices,
        .out_data_format = out_data_format,
        .output_ublock_shape =
            {
                .r = num_tiles_per_m_sub_block,
                .c = num_tiles_per_n_sub_block,
            },
        .output_mblock_shape =
            {
                .r = num_m_sub_blocks,
                .c = num_n_sub_blocks,
            },
        .batch_cnt = batch_cnt,
        .grid_shape = grid_shape,
    };
    std::string root_dir = buda_home();

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
        in_num_n_sub_blocks,
        in_num_tiles_per_n_sub_block);

    set_hlk_cpp_file_name_all_cores("hlks/" + get_hlks_file_name());

    set_hlk_operand_dataformat_all_cores(HlkOperand::in0, in_0_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::in1, in_1_data_format);
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);

    set_hlk_math_fidelity_all_cores(math_fidelity);

    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;
    this->relu_en = relu_en;
    this->relu_threshold = relu_threshold;
    this->relu_mode = relu_mode;
    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};
    this->stoch_rnd_mode = stoch_rnd_mode;

    for (int i = 0; i < input_tile_dims.size(); i++) {
        this->input_tile_dims.push_back({(uint32_t)input_tile_dims[i][0], (uint32_t)input_tile_dims[i][1]});
    }
}

std::string tt_embedding_op::type_to_str() {
    std::string ret = "tt_embedding_op";
    return ret;
}

void tt_embedding_op::model(vector<tt_tensor *> &inputs, tt_tensor *out) {
    tt_embedding::utils::golden_model(m_config, inputs, out);
}
void tt_embedding::utils::golden_model(const tt_embedding_config &config, vector<tt_tensor *> &inputs, tt_tensor *out) {
    log_assert(inputs.size() == 2, "Embeddings must have two inputs");
    auto indices_per_grid_r = tensor_lib::split_merge_ops::vsplit(*inputs[1], config.grid_shape.r, false);
    vector<tt_tensor> outputs_per_grid_r(config.grid_shape.r);
    for (int core_r = 0; core_r < config.grid_shape.r; core_r++) {
        embedding_per_core(config, *inputs[0], indices_per_grid_r.at(core_r), outputs_per_grid_r.at(core_r));
    }
    *out = tensor_lib::split_merge_ops::vmerge(outputs_per_grid_r, false);
    log_assert(out->get_data_format() != DataFormat::Invalid, "out data_format to tt_matmul is invalid");
    log_debug(tt::LogOp, "matmul out dim {}", out->get_shape());
}

void tt_embedding::utils::embedding_per_core(const tt_embedding_config &config, tt_tensor& table, tt_tensor& indices, tt_tensor& out) {
    tt_shape out_shape = {
        .rt = static_cast<unsigned int>(ceil(static_cast<double>(config.num_indices) / 32.0f)),
        .ct = config.grid_shape.c * config.output_mblock_shape.c * config.output_ublock_shape.c,
        .z = config.batch_cnt,
        .w = 1};
    log_debug(tt::LogOp, "Embedding Op shape={}", out_shape);
    if (out.get_num_stored_tiles() == 0) {
        out = tt_tensor(out_shape, config.out_data_format);
        out.reserve_tile_tensor();
    }

    // Read in second input as a flat vector of integers for row index to table
    vector<float> flattened_float_indices;
    indices.untilize_to_flat_tensor_data(true, false, false, flattened_float_indices);

    // Setup a zero tensor for using as padding zeros when index is an invalid
    auto zero_row = tt_tensor(
        {
            .rt = 1,
            .ct = config.grid_shape.c * config.output_mblock_shape.c * config.output_ublock_shape.c,
            .z = config.batch_cnt,
            .w = 1,
        },
        config.out_data_format);
    zero_row.init_to_input_value(0);
    int write_row_index = 0;
    bool is_input_layout_flat = true;
    // Do the embedding lookup and composition into output.
    for (int index = 0; index < config.num_indices; index++) {
        log_assert (
            write_row_index < (out_shape.rt*tt::constants::TILE_HEIGHT),
            "write_row_index={} value has to be within the out_shape={} limits", 
            write_row_index, out_shape
        );

        // Cast the row index to a uint32_t
        uint32_t *index_ptr = (uint32_t *)&flattened_float_indices.at(index);
        uint32_t read_row_index = *index_ptr;

        // If index is invalid, we write a padded 0  
        if (read_row_index == embedding_op_stream::INV_INDEX) {
            // output padded 0 
            tensor_lib::vector_datacopy(out, zero_row, Dim::R, 0, write_row_index, is_input_layout_flat);
        // if index is valid, we read from input table
        } else {
            log_assert (
                read_row_index*config.output_mblock_shape.c*config.output_ublock_shape.c*tt::constants::TILE_HEIGHT < table.get_shape().volume_full(),
                "read_row_index={} num_elements_per_row={} is referring to a row has to be within the table shape={} limits", 
                read_row_index, config.output_mblock_shape.c*config.output_ublock_shape.c*tt::constants::TILE_HEIGHT, table.get_shape()
            );
            tensor_lib::vector_datacopy(out, table, Dim::R, read_row_index, write_row_index, is_input_layout_flat);
        }
        write_row_index++;
    }
}
