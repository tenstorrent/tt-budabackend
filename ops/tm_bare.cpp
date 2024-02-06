// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tm_bare.hpp"

#include "common/param_lib.hpp"
#include "model/model.hpp"
#include "model/utils.hpp"

tt_tm_bare_op::tt_tm_bare_op(string name, string type, tt_grid_shape grid_shape, TmOp tm_op, std::vector<int> args) :
    tt_op(type, name, grid_shape) {
    m_config = {
        .op = tm_op,
        .args = args,
    };
    std::string root_dir = buda_home();
}

std::string tt_tm_bare_op::type_to_str() { return "tt_tm_bare_op"; }

void tt_tm_bare_op::model(vector<tt_tensor*>& inputs, tt_tensor* out) {
    tt_tm::utils::golden_model(m_config, inputs, out);
}

string tt_tm::utils::tm_op_to_string(TmOp tm_op) {
    switch (tm_op) {
        case TmOp::rBroadcast: return "r_broadcast";
        case TmOp::cBroadcast: return "c_broadcast";
        case TmOp::zBroadcast: return "z_broadcast";
        case TmOp::hSlice: return "hslice";
        case TmOp::hStack: return "hstack";
        case TmOp::vSlice: return "vslice";
        case TmOp::vStack: return "vstack";
        case TmOp::Transpose: return "transpose";
        case TmOp::TileBroadcast: return "tile_broadcast";
        default: log_assert(false, "Unrecognized TmOp supported");
    }
    return "";
}
void tt_tm::utils::golden_model(const tt_tm_config& config, vector<tt_tensor*>& inputs, tt_tensor* out) {
    log_assert(inputs.size() == 1, "Incorrect input size for TM");
    log_assert(
        inputs[0]->get_data_format() != DataFormat::Invalid,
        "Input data_format to tt_tm is invalid"
    );
    tt_shape shape = inputs[0]->get_shape();
    log_debug(tt::LogOp, "TM Op: {} -- Args: {}", tm_op_to_string(config.op), fmt::join(config.args, ", "));
    switch (config.op) {
        case TmOp::rBroadcast:
            log_assert(config.args.size() == 1, " {} must have 1 argument", tm_op_to_string(config.op));
            shape.rt *= config.args.at(0);
            if (config.full_broadcast) {
                *out = inputs[0]->broadcast(shape, Dim::R);
            } else {
                *out = inputs[0]->broadcast_tiles(shape, Dim::R);
            }
            break;
        case TmOp::cBroadcast:
            log_assert(config.args.size() == 1,  " {} must have 1 argument", tm_op_to_string(config.op));
            shape.ct *= config.args.at(0);
            if (config.full_broadcast) {
                *out = inputs[0]->broadcast(shape, Dim::C);
            } else {
                *out = inputs[0]->broadcast_tiles(shape, Dim::C);
            }
            break;
        case TmOp::zBroadcast:
            log_assert(config.args.size() == 1,  " {} must have 1 argument", tm_op_to_string(config.op));
            shape.z *= config.args.at(0);
            if (config.full_broadcast) {
                *out = inputs[0]->broadcast(shape, Dim::Z);
            } else {
                *out = inputs[0]->broadcast_tiles(shape, Dim::Z);
            }
            break;
        case TmOp::hSlice:
            log_assert(config.args.size() == 1,  " {} must have 1 argument", tm_op_to_string(config.op));
            *out = inputs[0]->reshape_c_dim_into_z_dim_and_c_dim(config.args.at(0));
            break;
        case TmOp::hStack:
            log_assert(config.args.size() == 1,  " {} must have 1 argument", tm_op_to_string(config.op));
            *out = inputs[0]->reshape_z_dim_into_c_dim(config.args.at(0));
            break;
        case TmOp::vSlice:
            log_assert(config.args.size() == 1,  " {} must have 1 argument", tm_op_to_string(config.op));
            *out = inputs[0]->reshape_r_dim_into_z_dim_and_r_dim(config.args.at(0));
            break;
        case TmOp::vStack:
            log_assert(config.args.size() == 1,  " {} must have 1 argument", tm_op_to_string(config.op));
            *out = inputs[0]->reshape_z_dim_into_r_dim(config.args.at(0));
            break;
        case TmOp::Transpose:
            log_assert(config.args.size() == 0,  " {} must have 1 argument", tm_op_to_string(config.op));
            *out = inputs[0]->transpose_xy(false, true, false);
            break;  // transpose tiles only
        case TmOp::TileBroadcast:
            log_assert(config.args.size() == 1,  " {} must have 1 argument", tm_op_to_string(config.op));
            *out = inputs[0]->broadcast_within_tiles(static_cast<Dim>(config.args.at(0)), false);// 
            break;  // transpose tiles only
        default: log_assert(false, "Unrecognized TmOp");
    }
    log_assert(
        out->get_data_format() != DataFormat::Invalid,
        "out data_format to tt_tm is invalid"
    );
}
