// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "drainer.hpp"

#include "common/model/tt_core.hpp"
#include "common/tensor_lib.hpp"
#include "common/tile_lib.hpp"
#include "model/model.hpp"

#include "netlist_utils.hpp"

string tt_drainer_op::get_hlks_file_name(DrainerOp drainer_op) {
    
    switch(m_config.op) {
        case DrainerOp::Drainer:
            return "drainer/eltwise_drainer_stream.cpp";
        default:
            log_assert(false, "Unsupported DrainerOp");
            return "";
    }
}

tt_drainer_op::~tt_drainer_op() {
}
void tt_drainer_op::set_hlk_args_t(
    std::uint32_t block_tile_dim,
    std::uint32_t block_cnt,
    std::uint32_t batch_cnt,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    std::uint32_t gradient_op,
    std::uint32_t transpose,
    std::vector<std::pair<int, bool>> kernel_broadcast) {

    if (m_config.op == DrainerOp::Drainer) {

        tt::tt_hlk_args_desc hlk_args_descriptor;
        hlk_args_descriptor.push_scalar_value("block_tile_dim", num_tiles_per_m_sub_block * num_tiles_per_n_sub_block);
        hlk_args_descriptor.push_scalar_value("block_cnt", num_m_sub_blocks * num_n_sub_blocks);
        hlk_args_descriptor.push_scalar_value("batch_cnt", batch_cnt);
        hlk_args_descriptor.push_scalar_value("num_m_sub_blocks", num_m_sub_blocks);
        hlk_args_descriptor.push_scalar_value("num_n_sub_blocks", num_n_sub_blocks);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_m_sub_block", num_tiles_per_m_sub_block);
        hlk_args_descriptor.push_scalar_value("num_tiles_per_n_sub_block", num_tiles_per_n_sub_block);
        hlk_args_descriptor.push_scalar_value("gradient_op", gradient_op);
        hlk_args_descriptor.push_scalar_value("transpose", transpose);

        cores[0][0]->local_desc.hlk_args_descriptor = hlk_args_descriptor;
    } else {
        log_assert(false, "Unsupported DrainerOp");
    }    
}

tt_drainer_op::tt_drainer_op(
    string name,
    string type,
    tt_grid_shape grid_shape,
    tt_grid_shape grid_loc,
    bool grid_transpose,
    DrainerOp drainer_op,
    std::uint32_t block_tile_dim,
    std::uint32_t block_cnt,
    std::uint32_t batch_cnt,
    std::uint32_t num_m_sub_blocks,
    std::uint32_t num_n_sub_blocks,
    std::uint32_t num_tiles_per_m_sub_block,
    std::uint32_t num_tiles_per_n_sub_block,
    std::uint32_t gradient_op,
    std::uint32_t transpose,
    bool untilize_output,
    MathFidelity math_fidelity,
    bool fp32_dest_acc_en,
    bool relu_en,
    std::uint32_t relu_threshold,
    ReluMode relu_mode,
    std::vector<DataFormat> in_data_formats,
    DataFormat out_data_format,
    DataFormat intermed_data_format,
    std::vector<std::pair<int, bool>> kernel_broadcast,
    const std::vector<int>& input_tile_dims,
    const std::vector<int>& output_tile_dims,
    const StochRndMode stoch_rnd_mode
    ) :
    tt_op(type, name, grid_shape, grid_loc, grid_transpose) {

    m_config = {
        .op = drainer_op,
        .transpose = transpose,
        .kernel_broadcast = kernel_broadcast,
        .input_tile_dims = input_tile_dims,
        .output_tile_dims = output_tile_dims,
    };

    // Missing some static graph analysis to determine input shapes/sizes

    std::string root_dir;
    if (getenv("BUDA_HOME")) {
        root_dir = getenv("BUDA_HOME");
    } else {
        root_dir = "./";
    }
    if (root_dir.back() != '/') {
        root_dir += "/";
    }

    input_cnt = 1;

    set_hlk_args_t(
        block_tile_dim,
        block_cnt,
        batch_cnt,
        num_m_sub_blocks,
        num_n_sub_blocks,
        num_tiles_per_m_sub_block,
        num_tiles_per_n_sub_block,
        gradient_op,
        transpose,
        kernel_broadcast);

    set_hlk_cpp_file_name_all_cores("hlks/" + get_hlks_file_name(drainer_op));

    for (uint8_t i = 0; i < input_cnt; i++) {
        set_hlk_operand_dataformat_all_cores((HlkOperand)((std::uint8_t)HlkOperand::in0 + i), in_data_formats[i]);
    }
    set_hlk_operand_dataformat_all_cores(HlkOperand::out0, out_data_format);

    if (gradient_op) {
        set_hlk_operand_dataformat_all_cores(HlkOperand::intermed0, intermed_data_format);
    }

    set_hlk_math_fidelity_all_cores(math_fidelity);

    this->untilize_output = untilize_output;
    this->fp32_dest_acc_en = fp32_dest_acc_en;
    this->relu_en = relu_en;
    this->relu_threshold = relu_threshold;
    this->relu_mode = relu_mode;
    this->output_tile_dims = {(uint32_t)output_tile_dims[0], (uint32_t)output_tile_dims[1]};
    this->input_tile_dims.push_back({(uint32_t)input_tile_dims[0], (uint32_t)input_tile_dims[1]});
    this->int_fpu_en = netlist_utils::is_int_format(in_data_formats[0]);
    this->stoch_rnd_mode = stoch_rnd_mode;
    this->kernel_broadcasts = kernel_broadcast;
    for (int i = this->kernel_broadcasts.size(); i < 1; i++) {
        this->kernel_broadcasts.emplace_back(0, false);
    }
}

void tt_drainer_op::model(vector<tt_tensor *> &inputs, tt_tensor *out) {
    tt_drainer::utils::golden_model(m_config, inputs, out);
}

string tt_drainer::utils::drainer_op_to_string(DrainerOp drainer_op) {
    switch (drainer_op) {
        case DrainerOp::Drainer: return "drainer";
        default: log_fatal("Unsupported drainer op: {}!", drainer_op);
    }
    return "";
}
void tt_drainer::utils::golden_model(const tt_drainer_config &config, vector<tt_tensor *> &inputs, tt_tensor *out) {
    log_fatal("DrainerOp should only be used for perf measurements and should not be verified with golden.");
}
