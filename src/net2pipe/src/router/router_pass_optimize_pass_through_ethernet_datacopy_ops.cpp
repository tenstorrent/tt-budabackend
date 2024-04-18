// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0


#include <string>
#include <vector>

#include "utils/logger.hpp"
#include "netlist/netlist_utils.hpp"
#include "router.hpp"
#include "router/router_passes.h"
#include "router/router_passes_common.h"
#include "router_types.h"

namespace router {

/*
 * An ethernet datacopy op is considered pass-through if it each core in its grid can be fully
 * implemented with a single relay buffer. In other words, the following conditions must be met
 * per core:
 * 1. The op has a single consumer op (no forking)
 * 2. The input tile order matches the output tile order (no scattering)
 * 3. All input tiles to the core are forwarded to the consumer core
 */
static bool is_pass_through_ethernet_datacopy_op(
    router::Router &router, const std::string &op_name, const tt_op_info &op_info) {
    if (not netlist_utils::is_valid_ethernet_op(op_info.type)) {
        return false;
    }

    if (op_forks_to_multiple_consumers(router, op_name)) {
        return false;
    }

    bool all_output_pipes_are_inorder = true;
    bool all_output_pipes_forward_all_tiles = true;

    auto output_pipes = std::vector<unique_id_t>{};
    output_pipes.reserve(op_info.grid_size.at(0) * op_info.grid_size.at(1));
    const auto &buffer_output_pipes = router.get_buffer_output_pipes();
    auto get_value = [&router](const auto &pair) {
        const auto &output_pipes = router.get_buffer_output_pipes().at(pair.second);
        TT_ASSERT(output_pipes.size() == 1);
        return *(output_pipes.begin());
    };
    for (const auto &[grid_r, row_output_buffers] : router.get_op_output_buf_map().at(op_name)) {
        std::transform(
            std::begin(row_output_buffers), std::end(row_output_buffers), std::back_inserter(output_pipes), get_value);
    }

    auto implements_pass_through = [&router](unique_id_t pipe_id) {
        const pipe_t &pipe = router.get_pipe(pipe_id);
        return is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(router, pipe);
    };
    bool all_cores_implement_straight_pass_through =
        std::all_of(std::begin(output_pipes), std::end(output_pipes), implements_pass_through);

    return all_cores_implement_straight_pass_through;
}

static void optimize_away_ethernet_datacopy_op_core(router::Router &router, const std::string &op_name, int r, int c) {
    unique_id_t input_buffer_id = router.get_op_input_buf_map().at(op_name).at(r).at(c).at(0);
    unique_id_t op_input_pipe_id = router.get_buffer_input_pipes().at(input_buffer_id);
    router.get_pipes_mutable().at(op_input_pipe_id).clear_consumer_metadata();

    unique_id_t unpacker_buffer_id = input_buffer_id;
    while (router.get_buffer_output_pipes().find(unpacker_buffer_id) != router.get_buffer_output_pipes().end() &&
           router.get_buffer_output_pipes().at(unpacker_buffer_id).size() > 0) {
        TT_ASSERT(router.get_buffer_output_pipes().at(unpacker_buffer_id).size() == 1);
        unique_id_t pipe = *(router.get_buffer_output_pipes().at(unpacker_buffer_id).begin());
        unpacker_buffer_id = router.get_pipe(pipe).output_buffer_ids().at(0);
    }
    unique_id_t output_buffer_id = router.get_op_output_buf_map().at(op_name).at(r).at(c);

    // merge the input and output buffers -

    tt::buffer_info merged_relay_buffer_info = create_relay_buffer_info(
        router.get_buffer(unpacker_buffer_id).info().allocated_size_in_tiles(),
        router.get_scatter_gather_granularity(unpacker_buffer_id),
        router.get_buffer_total_epoch_tiles(unpacker_buffer_id),
        router.get_buffer_data_format(unpacker_buffer_id),
        router.get_buffer_tile_dims(unpacker_buffer_id));

    const auto core_location = router.get_buffer_location(unpacker_buffer_id);
    unique_id_t merged_relay_buffer_id = router.add_new_buffer(merged_relay_buffer_info, core_location.chip);
    log_trace(
        tt::LogRouter,
        "\tMerging buffer {} and {} into {}. Sized to {} B",
        input_buffer_id,
        output_buffer_id,
        merged_relay_buffer_id,
        router.get_buffer(merged_relay_buffer_id).info().allocated_size_in_bytes());

    unique_id_t unpacker_input_pipe_id = router.get_buffer_input_pipes().at(unpacker_buffer_id);
    TT_ASSERT(
        router.get_buffer_output_pipes().find(unpacker_buffer_id) == router.get_buffer_output_pipes().end() ||
            router.get_buffer_output_pipes().at(unpacker_buffer_id).size() == 0,
        "Ethernet datacopy ops cannot be optimized away if they fork.");
    TT_ASSERT(
        router.get_buffer_output_pipes().at(output_buffer_id).size() == 1,
        "Ethernet datacopy ops cannot be optimized away if they fork.");
    unique_id_t output_pipe_id = *(router.get_buffer_output_pipes().at(output_buffer_id).begin());

    log_trace(
        tt::LogRouter,
        "\told dataflow: ({}) producer_pipe -> ({}) \"unpacker\" buffer  -> op -> ({}) \"packer\" buffer -> ({}) "
        "consumer_pipe",
        unpacker_input_pipe_id,
        unpacker_buffer_id,
        output_buffer_id,
        output_pipe_id);

    router.replace_pipe_output(unpacker_input_pipe_id, unpacker_buffer_id, merged_relay_buffer_id);
    router.remove_buffer(unpacker_buffer_id);
    router.replace_pipe_input(output_pipe_id, output_buffer_id, merged_relay_buffer_id);
    router.remove_buffer(output_buffer_id);
    router.assign_buffer_to_core(merged_relay_buffer_id, core_location);
    log_trace(
        tt::LogRouter,
        "\tnew dataflow: ({}) producer_pipe -> ({}) relay buffer  -> ({}) consumer_pipe",
        unpacker_input_pipe_id,
        router.get_pipe(unpacker_input_pipe_id).output_buffer_ids(),
        output_pipe_id);
}

static void optimize_away_ethernet_datacopy_op(router::Router &router, const std::string &op_name) {
    log_debug(tt::LogRouter, "Optimizing away ethernet datacopy op: {}", op_name);
    const auto &op_info = router.get_op_info_map().at(op_name);
    TT_ASSERT(
        netlist_utils::is_valid_ethernet_op(op_info.type),
        "Internal logic error. optimize_away_ethernet_datacopy_op only callable with ethernet datacopy ops");
    for (int r = 0; r < op_info.grid_size.at(0); r++) {
        for (int c = 0; c < op_info.grid_size.at(1); c++) {
            optimize_away_ethernet_datacopy_op_core(router, op_name, r, c);
        }
    }
    router.erase_op(op_name);
}

/*
 * For now we only optimize the ethernet datacopy in its entirety if all of its
 * cores implement a straight pass-through. In the future we could optimize
 * do per-core optimization but it would require some improvements to the net2pipe
 * infrastructure to not visit all cores of the ethernet datacopy op
 *
 */
void optimize_out_pass_through_ethernet_datacopy_ops(router::Router &router) {
    // copy in case we want to delete
    const auto op_info_map_copy = router.get_op_info_map();
    for (const auto &[op_name, op_info] : op_info_map_copy) {
        bool is_pass_through = is_pass_through_ethernet_datacopy_op(router, op_name, op_info);

        if (is_pass_through) {
            optimize_away_ethernet_datacopy_op(router, op_name);
        }
    }
}

};  // namespace router