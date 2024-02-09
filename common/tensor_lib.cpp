// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tensor_lib.hpp"

#include <cmath>
#include <random>
#include <vector>

#include "model/base_types.hpp"
#include "tensor.hpp"
#include "tile.hpp"
#include "tile_lib.hpp"
#include "utils/logger.hpp"
#include "common/tt_parallel_for.h"
#include "device/cpuset_lib.hpp"

// *** These are all Tensor library operations meant to replace ones in tensor.cpp,
// ***   they are all inplace and optimized to be as quick as possible
namespace tt::tensor_lib {

struct InputsContext {
    InputsContext(std::vector<const tt_tensor*> ins, tt_tensor* out, const tt::tt_shape& expected_output_shape) {
        fallback_inputs.resize(ins.size());
        inputs = ins;

        const tt::tt_shape input0_shape = ins[0]->get_shape();
        bool input_mismatched_from_output = false;
        bool inputs_have_mismatched_shape = false;
        std::vector<bool> input_alias_with_output(ins.size(), false);
        for (int i = 0; i < (int)ins.size(); i++) {
            auto in = ins[i];
            if (!input_mismatched_from_output) {
                input_mismatched_from_output =
                    (out->get_shape() != expected_output_shape) || (out->get_data_format() != in->get_data_format());
            }
            if (!inputs_have_mismatched_shape) {
                log_assert(
                    input0_shape == in->get_shape(),
                    "Input shapes be equal between inputs: input0.shape={} -- input.shape={}",
                    input0_shape,
                    in->get_shape());
            }
            input_alias_with_output[i] = (in == out);
        }

        for (int i = 0; i < (int)ins.size(); i++) {
            if (input_alias_with_output[i]) {
                fallback_inputs[i] = *ins[i];
                inputs[i] = &fallback_inputs[i];
            }
        }

        if (input_mismatched_from_output) {
            *out = tt_tensor(expected_output_shape, ins[0]->get_data_format());
        }
        if (out->is_shape_only() || (out->get_num_stored_tiles() == 0)) {
            out->reserve_tile_tensor();
        }
    }

    std::vector<tt_tensor> fallback_inputs;
    std::vector<const tt_tensor*> inputs;
};

struct TileIterator {
    TileIterator(tt_shape s) : shape(s) {
        rt_ct = shape.rt * shape.ct;
        z_rt_ct = shape.z * rt_ct;
    }
    inline void get_indices(int tile_index, int& w, int& z, int& r, int& c) const {
        w = tile_index / z_rt_ct;
        z = (tile_index / rt_ct) % shape.z;
        r = (tile_index / shape.ct) % shape.rt;
        c = tile_index % shape.ct;
    }
    int rt_ct;
    int z_rt_ct;
    tt_shape shape;
};

namespace basic_ops {
void eltwise_binary(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor& input0,
    const tt::tt_tensor& input1,
    std::function<void(tt::tt_tile&, const tt::tt_tile&, const tt::tt_tile&)> binary_tile_callback) {
    InputsContext ctx({&input0, &input1}, &output_tensor, input0.get_shape());

    log_assert(
        ctx.inputs[0]->get_shape() == ctx.inputs[1]->get_shape(),
        "Shape for binary operation must be equal between inputs: input0.shape={} -- input1.shape={}",
        ctx.inputs[0]->get_shape(),
        ctx.inputs[1]->get_shape());

    TileIterator i(output_tensor.get_shape());
    tt::parallel_for(
        0,
        output_tensor.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            binary_tile_callback(
                output_tensor.tile_tensor[w][z][r][c],
                ctx.inputs[0]->tile_tensor[w][z][r][c],
                ctx.inputs[1]->tile_tensor[w][z][r][c]);
        },
        tt::cpuset::get_allowed_num_threads());
}

void eltwise_unary(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor& input0,
    const Dim& vector_mode,
    std::function<void(tt::tt_tile&, const tt::tt_tile&, const Dim&)> unary_tile_callback) {
    InputsContext ctx({&input0}, &output_tensor, input0.get_shape());

    TileIterator i(output_tensor.get_shape());
    tt::parallel_for(
        0,
        output_tensor.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            unary_tile_callback(
                output_tensor.tile_tensor[w][z][r][c], ctx.inputs[0]->tile_tensor[w][z][r][c], vector_mode);
        },
        tt::cpuset::get_allowed_num_threads());
}

void eltwise_nary(
    tt::tt_tensor& output_tensor,
    const std::vector<tt::tt_tensor>& inputs,
    std::function<void(tt::tt_tile&, const std::vector<tt::tt_tile>&)> nary_tile_callback) {
    log_assert(inputs.size() >= 1, "eltwise_nary should have at least 1 input");

    std::vector<const tt_tensor*> input_ptrs;
    for (const auto& t : inputs) {
        input_ptrs.emplace_back(&t);
    }
    InputsContext ctx(input_ptrs, &output_tensor, inputs[0].get_shape());

    TileIterator i(output_tensor.get_shape());
    tt::parallel_for(
        0,
        output_tensor.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            tt_tile& out_tile = output_tensor.tile_tensor[w][z][r][c];
            std::vector<tt::tt_tile> input_tiles = {};
                    for (const auto input : ctx.inputs) {
                        input_tiles.push_back(input->tile_tensor[w][z][r][c]);
                    }
            nary_tile_callback(out_tile, input_tiles);
        },
        tt::cpuset::get_allowed_num_threads());
}

void depthwise_binary(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor input0,
    const tt::tt_tensor input1,
    std::uint32_t block_cnt,
    std::function<void(tt::tt_tile&, const tt::tt_tile&, const tt::tt_tile&)> binary_tile_callback) {

    tt::tt_shape shape = input0.get_shape();
    shape.ct /= block_cnt;
    bool input0_mismatched_from_output = (output_tensor.get_shape() != shape) or
                                        (output_tensor.get_data_format() != input0.get_data_format());
    if (input0_mismatched_from_output) {
        output_tensor = tt_tensor(shape, input0.get_data_format());
    }

    log_assert(input1.getrt() == block_cnt, "Input 1 must have {} rows", block_cnt);

    if (output_tensor.is_shape_only() or (output_tensor.get_num_stored_tiles() == 0)) {
        output_tensor.reserve_tile_tensor();
    }

    // TODO: Missing accumulate for z dimension

    for (unsigned int wi = 0; wi < output_tensor.getw(); ++wi) {
        for (unsigned int zi = 0; zi < output_tensor.getz(); ++zi) {
            for (unsigned int block_id = 0; block_id < block_cnt; ++block_id) {
                for (unsigned int ri = 0; ri < output_tensor.getrt(); ++ri) {
                    for (unsigned int ci = 0; ci < output_tensor.getct(); ++ci) {
                        binary_tile_callback(
                            output_tensor.tile_tensor[wi][zi][ri][ci],
                            input0.tile_tensor[wi][zi][ri][ci + output_tensor.getct() * block_id],
                            input1.tile_tensor[wi][zi][block_id][ci]);
                    }
                }
            }
        }
    }
}


}  // namespace basic_ops
namespace split_merge_ops {
tt_tensor hmerge(const vector<tt_tensor>& inputs, bool shape_only) {
    log_assert(inputs.size() > 0, "Must try to merge 1 or more inputs -- input_size={}", inputs.size());

    unsigned int input_w = inputs.at(0).getw();
    unsigned int input_z = inputs.at(0).getz();
    unsigned int input_rt = inputs.at(0).getrt();
    unsigned int input_ct = inputs.at(0).getct();
    unsigned int final_ct = inputs.size() * input_ct;
    unsigned int tile_height = inputs.at(0).get_tile_height();
    unsigned int tile_width = inputs.at(0).get_tile_width();

    tt_tensor result(
        tt_shape{
            .rt = input_rt,
            .ct = final_ct,
            .z = input_z,
            .w = input_w,
            .tile_height = tile_height,
            .tile_width = tile_width},
        inputs.at(0).get_data_format());
    if (shape_only) {
        return result;
    }

    result.reserve_tile_tensor();
    for (unsigned int tensor_idx = 0; tensor_idx < inputs.size(); tensor_idx++) {
        const auto& input = inputs.at(tensor_idx);
        log_assert( 
            (input.getw() == input_w) and (input.getw() == input_w) and (input.getw() == input_w) and
                (input.getw() == input_w),
            "hmerge inputs must have same size across whole vector. tensor{} in inputs has different size",
            tensor_idx);

        TileIterator i(inputs[0].get_shape());
        tt::parallel_for(
            0,
            inputs[0].total_tiles(),
            [&](int tile_index) {
                int w, z, r, c;
                i.get_indices(tile_index, w, z, r, c);
                result.tile_tensor[w][z][r][tensor_idx * input_ct + c] = input.tile_tensor[w][z][r][c];
            },
            tt::cpuset::get_allowed_num_threads());
    }
    return result;
}

tt_tensor vmerge(const vector<tt_tensor>& inputs, bool shape_only) {
    log_assert(inputs.size() > 0, "Must try to merge 1 or more inputs -- input_size={}", inputs.size());

    unsigned int input_w = inputs.at(0).getw();
    unsigned int input_z = inputs.at(0).getz();
    unsigned int input_rt = inputs.at(0).getrt();
    unsigned int input_ct = inputs.at(0).getct();
    unsigned int final_rt = inputs.size() * input_rt;
    unsigned int tile_height = inputs.at(0).get_tile_height();
    unsigned int tile_width = inputs.at(0).get_tile_width();

    tt_tensor result(
        tt_shape{
            .rt = final_rt,
            .ct = input_ct,
            .z = input_z,
            .w = input_w,
            .tile_height = tile_height,
            .tile_width = tile_width},
        inputs.at(0).get_data_format());
    if (shape_only) {
        return result;
    }

    result.reserve_tile_tensor();
    for (unsigned int tensor_idx = 0; tensor_idx < inputs.size(); tensor_idx++) {
        const auto& input = inputs.at(tensor_idx);
        log_assert( 
            (input.getw() == input_w) and (input.getw() == input_w) and (input.getw() == input_w) and
                (input.getw() == input_w),
            "vmerge inputs must have same size across whole vector. tensor{} in inputs has different size",
            tensor_idx);
        TileIterator i(inputs[0].get_shape());
        tt::parallel_for(
            0,
            inputs[0].total_tiles(),
            [&](int tile_index) {
                int w, z, r, c;
                i.get_indices(tile_index, w, z, r, c);
                result.tile_tensor[w][z][tensor_idx * input_rt + r][c]  = input.tile_tensor[w][z][r][c];
            },
            tt::cpuset::get_allowed_num_threads());
    }
    return result;
}

vector<tt_tensor> hsplit(tt_tensor& input, unsigned int split_factor, bool shape_only) {
    log_assert( 
        (input.getct() % split_factor) == 0,
        "split_factor={} must cleanly divide out the ct={}",
        split_factor,
        input.getct());

    unsigned int final_ct = input.getct() / split_factor;
    tt_shape new_tensor_shape{
        .rt = input.getrt(),
        .ct = final_ct,
        .z = input.getz(),
        .w = input.getw(),
        .tile_height = input.get_tile_height(),
        .tile_width = input.get_tile_width()};

    vector<tt_tensor> result;
    if (shape_only) {
        for (unsigned int tensor_idx = 0; tensor_idx < split_factor; tensor_idx++) {
            tt_tensor t(
                new_tensor_shape,
                input.get_data_format());
            result.emplace_back(std::move(t));
        }
        return result;
    }

    for (unsigned int tensor_idx = 0; tensor_idx < split_factor; tensor_idx++) {
        tt_tensor t(
            new_tensor_shape,
            input.get_data_format());
        t.reserve_tile_tensor();
        TileIterator i(new_tensor_shape);
        tt::parallel_for(
            0,
            t.total_tiles(),
            [&](int tile_index) {
                int w, z, r, c_new;
                i.get_indices(tile_index, w, z, r, c_new);
                t.tile_tensor[w][z][r][c_new] = input.tile_tensor[w][z][r][c_new + tensor_idx * final_ct];
            },
            tt::cpuset::get_allowed_num_threads());
        result.emplace_back(std::move(t));
    }
    return result;
}

vector<tt_tensor> vsplit(tt_tensor& input, unsigned int split_factor, bool shape_only) {
    log_assert( 
        (input.getrt() % split_factor) == 0,
        "split_factor={} must cleanly divide out the ct={}",
        split_factor,
        input.getrt());

    unsigned int final_rt = input.getrt() / split_factor;
    tt_shape new_tensor_shape = {
        .rt = final_rt,
        .ct = input.getct(),
        .z = input.getz(),
        .w = input.getw(),
        .tile_height = input.get_tile_height(),
        .tile_width = input.get_tile_width()};

    vector<tt_tensor> result;
    if (shape_only) {
        for (unsigned int tensor_idx = 0; tensor_idx < split_factor; tensor_idx++) {
            tt_tensor t(
                new_tensor_shape,
                input.get_data_format());
            result.emplace_back(std::move(t));
        }
        return result;
    }

    for (unsigned int tensor_idx = 0; tensor_idx < split_factor; tensor_idx++) {
        tt_tensor t(
            new_tensor_shape,
            input.get_data_format());
        t.reserve_tile_tensor();
        TileIterator i(new_tensor_shape);
        tt::parallel_for(
            0,
            t.total_tiles(),
            [&](int tile_index) {
                int w, z, r_new, c;
                i.get_indices(tile_index, w, z, r_new, c);
                t.tile_tensor[w][z][r_new][c] = input.tile_tensor[w][z][r_new + tensor_idx * final_rt][c];
            },
            tt::cpuset::get_allowed_num_threads());
        result.emplace_back(std::move(t));
    }
    return result;
}
vector<tt_tensor> zsplit(tt_tensor& input, bool shape_only) {
    log_assert(input.getw() == 1, "Can't Z-split tensor with W > 1");

    tt_shape new_tensor_shape = {
        .rt = input.getrt(),
        .ct = input.getct(),
        .z = 1,
        .w = 1,
        .tile_height = input.get_tile_height(),
        .tile_width = input.get_tile_width()};

    vector<tt_tensor> result;
    if (shape_only) {
        for (unsigned int z = 0; z < input.getz(); z++) {
            result.emplace_back(new_tensor_shape, input.get_data_format());
        }
        return result;
    }

    for (unsigned int z = 0; z < input.getz(); z++) {
        tt_tensor t(new_tensor_shape, input.get_data_format());
        t.reserve_tile_tensor();
        t.tile_tensor[0][0] = input.tile_tensor[0][z];
        result.emplace_back(std::move(t));
    }
    return result;
}

vector<tt_tensor> wsplit(tt_tensor& input, bool shape_only) {
    vector<tt_tensor> result;

    tt_shape new_tensor_shape = {
        .rt = input.getrt(),
        .ct = input.getct(),
        .z = input.getz(),
        .w = 1,
        .tile_height = input.get_tile_height(),
        .tile_width = input.get_tile_width()};

    if (shape_only) {
        for (unsigned int w = 0; w < input.getw(); w++) {
            result.emplace_back(new_tensor_shape, input.get_data_format());
        }
        return result;
    }

    for (unsigned int w = 0; w < input.getw(); w++) {
        tt_tensor t(
            new_tensor_shape, input.get_data_format());
        t.reserve_tile_tensor();
        t.tile_tensor[0] = input.tile_tensor[w];
        result.emplace_back(std::move(t));
    }
    return result;
}

}  // namespace split_merge_ops

void datacopy(tt::tt_tensor& output_tensor, const tt::tt_tensor& input0) {
    InputsContext ctx({&input0}, &output_tensor, input0.get_shape());
    TileIterator i(output_tensor.get_shape());
    tt::parallel_for(
        0,
        output_tensor.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            tile_lib::unary::datacopy(
                        output_tensor.tile_tensor[w][z][r][c], ctx.inputs[0]->tile_tensor[w][z][r][c]);
        },
        tt::cpuset::get_allowed_num_threads());
}

void power(tt::tt_tensor& output_tensor, const tt::tt_tensor& input0, const Dim& vector_mode, int32_t exponent) {
    InputsContext ctx({&input0}, &output_tensor, input0.get_shape());
    TileIterator i(output_tensor.get_shape());
    tt::parallel_for(
        0,
        output_tensor.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            tile_lib::unary::power(
                output_tensor.tile_tensor[w][z][r][c], ctx.inputs[0]->tile_tensor[w][z][r][c], vector_mode, exponent);
        },
        tt::cpuset::get_allowed_num_threads());
}

void lrelu(tt::tt_tensor& output_tensor, const tt::tt_tensor& input0, const Dim& vector_mode, float slope) {
    InputsContext ctx({&input0}, &output_tensor, input0.get_shape());
    TileIterator i(output_tensor.get_shape());
    tt::parallel_for(
        0,
        output_tensor.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            tile_lib::unary::lrelu(
                output_tensor.tile_tensor[w][z][r][c], ctx.inputs[0]->tile_tensor[w][z][r][c], vector_mode, slope);
        },
        tt::cpuset::get_allowed_num_threads());
}

void dropout(
    tt::tt_tensor& output_tensor, const tt::tt_tensor& input0, const Dim& vector_mode, float probability, float scale) {
    InputsContext ctx({&input0}, &output_tensor, input0.get_shape());
    TileIterator i(output_tensor.get_shape());
    tt::parallel_for(
        0,
        output_tensor.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            tile_lib::unary::dropout(
                output_tensor.tile_tensor[w][z][r][c],
                ctx.inputs[0]->tile_tensor[w][z][r][c],
                vector_mode,
                probability,
                scale);
        },
        tt::cpuset::get_allowed_num_threads());
}

void relu_with_threshold(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor& input0,
    const Dim& vector_mode,
    const ReluMode& mode,
    const float& threshold) {
    InputsContext ctx({&input0}, &output_tensor, input0.get_shape());
    TileIterator i(output_tensor.get_shape());
    tt::parallel_for(
        0,
        output_tensor.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            tile_lib::unary::relu_with_threshold(
                output_tensor.tile_tensor[w][z][r][c],
                ctx.inputs[0]->tile_tensor[w][z][r][c],
                vector_mode,
                mode,
                threshold);
        },
        tt::cpuset::get_allowed_num_threads());
}

void vector_datacopy(
    tt::tt_tensor& output_tensor,
    const tt::tt_tensor input0,
    const Dim& vector_type,
    const unsigned int input_index,
    const unsigned int output_index,
    const bool is_input_layout_flat) {
    log_assert((vector_type == Dim::R) or (vector_type == Dim::C), "Only a row vector or col vector can be copied");
    if (output_tensor.is_shape_only() or (output_tensor.get_num_stored_tiles() == 0)) {
        output_tensor.reserve_tile_tensor();
    }
    const auto& output_shape = output_tensor.get_shape();
    const auto& input0_shape = input0.get_shape();
    if (vector_type == Dim::R) {
        // Copying a row vector
        log_assert( 
            output_shape.ct == input0_shape.ct,
            "When copying a row vector, input.ct={} must be equal to output.ct={}",
            input0_shape.ct,
            output_shape.ct);
        if (is_input_layout_flat) {
            unsigned int output_tile_index = output_index / tt::constants::TILE_WIDTH;
            unsigned int output_within_tile_index = output_index % tt::constants::TILE_WIDTH;
            log_assert( 
                output_tile_index <= output_shape.rt,
                "output_index={} must be <= output_shape.rt={}",
                output_tile_index,
                output_shape.rt);
            // flat layout means no tilization, so row index
            unsigned int num_elements_in_row = input0_shape.ct * tt::constants::TILE_WIDTH;
            unsigned int start_row_offset_in_elements = num_elements_in_row * input_index;
            unsigned int start_tile_offset = start_row_offset_in_elements / tt::constants::TILE_WIDTH;

            for (unsigned int wi = 0; wi < output_tensor.getw(); ++wi) {
                for (unsigned int zi = 0; zi < output_tensor.getz(); ++zi) {
                    for (unsigned int ci = 0; ci < output_tensor.getct(); ++ci) {
                        unsigned row_offset = start_tile_offset + ci;
                        unsigned int input_tile_index = row_offset / tt::constants::TILE_HEIGHT;
                        unsigned int input_within_tile_index = row_offset % tt::constants::TILE_HEIGHT;
                        tile_lib::unary::vector_datacopy(
                            output_tensor.tile_tensor[wi][zi][output_tile_index][ci],
                            input0.get_tile_from_flat_index(input_tile_index, Dim::R),
                            vector_type,
                            input_within_tile_index,
                            output_within_tile_index);
                    }
                }
            }
        } else {
            unsigned int input_tile_index = input_index / tt::constants::TILE_WIDTH;
            unsigned int output_tile_index = output_index / tt::constants::TILE_WIDTH;
            unsigned int input_within_tile_index = input_index % tt::constants::TILE_WIDTH;
            unsigned int output_within_tile_index = output_index % tt::constants::TILE_WIDTH;
            log_assert( 
                input_tile_index <= input0_shape.rt,
                "input0_index={} must be <= input0_shape.rt={}",
                input_tile_index,
                input0_shape.rt);
            log_assert( 
                output_tile_index <= output_shape.rt,
                "output_index={} must be <= output_shape.rt={}",
                output_tile_index,
                output_shape.rt);
            for (unsigned int wi = 0; wi < output_tensor.getw(); ++wi) {
                for (unsigned int zi = 0; zi < output_tensor.getz(); ++zi) {
                    for (unsigned int ci = 0; ci < output_tensor.getct(); ++ci) {
                        tile_lib::unary::vector_datacopy(
                            output_tensor.tile_tensor[wi][zi][output_tile_index][ci],
                            input0.tile_tensor[wi][zi][input_tile_index][ci],
                            vector_type,
                            input_within_tile_index,
                            output_within_tile_index);
                    }
                }
            }
        }
    } else if (vector_type == Dim::C) {
        log_assert(not is_input_layout_flat, "flat input layout only supported on Dim::R for now");
        // Copying a col vector
        log_assert( 
            output_shape.rt == input0_shape.rt,
            "When copying a row vector, input.ct={} must be equal to output.ct={}",
            input0_shape.rt,
            output_shape.rt);
        unsigned int input_tile_index = input_index / tt::constants::TILE_HEIGHT;
        unsigned int output_tile_index = output_index / tt::constants::TILE_HEIGHT;
        unsigned int input_within_tile_index = input_index % tt::constants::TILE_HEIGHT;
        unsigned int output_within_tile_index = output_index % tt::constants::TILE_HEIGHT;
        log_assert( 
            input_tile_index <= input0_shape.ct,
            "input0_index={} must be <= input0_shape.ct={}",
            input_tile_index,
            input0_shape.ct);
        log_assert( 
            output_tile_index <= output_shape.rt,
            "output_index={} must be <= output_shape.ct={}",
            output_tile_index,
            output_shape.ct);
        for (unsigned int wi = 0; wi < output_tensor.getw(); ++wi) {
            for (unsigned int zi = 0; zi < output_tensor.getz(); ++zi) {
                for (unsigned int ri = 0; ri < output_tensor.getrt(); ++ri) {
                    tile_lib::unary::vector_datacopy(
                        output_tensor.tile_tensor[wi][zi][ri][output_tile_index],
                        input0.tile_tensor[wi][zi][ri][input_tile_index],
                        vector_type,
                        input_within_tile_index,
                        output_within_tile_index);
                }
            }
        }
    }
}

// Pads the input_tensor in the r, c dimensions with the value specified in val
tt::tt_tensor pad_rc_val(const tt::tt_tensor& input_tensor, uint32_t r, uint32_t c, float val) {
    tt_shape shape = input_tensor.get_shape();

    shape.rt += r;
    shape.ct += c;

    tt_tensor result(shape, input_tensor.get_data_format());
    result.reserve_tile_tensor();

    result.set_number(val);

    TileIterator i(input_tensor.get_shape());
    tt::parallel_for(
        0,
        input_tensor.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            tile_lib::unary::datacopy(result.tile_tensor[w][z][r][c], input_tensor.tile_tensor[w][z][r][c]);
        },
        tt::cpuset::get_allowed_num_threads());
    return result;
}

// Removes padding from the input_tensor in the r, c dimensions
tt::tt_tensor unpad(const tt::tt_tensor& input_tensor, uint32_t r, uint32_t c) {
    tt_shape shape = input_tensor.get_shape();

    shape.rt -= r;
    shape.ct -= c;

    tt_tensor result(shape, input_tensor.get_data_format());
    result.reserve_tile_tensor();

    TileIterator i(result.get_shape());
    tt::parallel_for(
        0,
        result.total_tiles(),
        [&](int tile_index) {
            int w, z, r, c;
            i.get_indices(tile_index, w, z, r, c);
            tile_lib::unary::datacopy(result.tile_tensor[w][z][r][c], input_tensor.tile_tensor[w][z][r][c]);
        },
        tt::cpuset::get_allowed_num_threads());
    return result;
}

}  // namespace tt::tensor_lib
