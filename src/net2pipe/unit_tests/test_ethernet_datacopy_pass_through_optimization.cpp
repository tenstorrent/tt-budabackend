// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "common/buda_soc_descriptor.h"
#include "gtest/gtest.h"
#include "netlist/netlist.hpp"
#include "netlist/netlist_op_info_types.hpp"
#include "router/router_passes.h"
#include "router_types.h"
#include "test_unit_common.hpp"

using router::pipe_output_list_t;
using router::pipe_t;
using router::router_buffer_info_t;
using router::unique_id_t;

std::vector<loc_buftype_ntiles_t> eth_datacopy_buffers(
    const buda_SocDescriptor &soc_descriptor,
    const std::vector<ethernet_channel_t> &dest_channels,
    chip_id_t chip,
    tt::RouterBufferType buffer_type,
    int buffer_size_tiles) {
    auto result = std::vector<loc_buftype_ntiles_t>{};
    std::transform(
        dest_channels.begin(),
        dest_channels.end(),
        std::back_inserter(result),
        [&soc_descriptor, chip, buffer_type, buffer_size_tiles](const ethernet_channel_t &channel) {
            return loc_buftype_ntiles_t{
                tt_cxy_pair(chip, soc_descriptor.ethernet_cores.at(channel)),
                buffer_type,
                buffer_size_tiles,
            };
        });
    return result;
}

/* Generates an assumed layout where the same ethernet channel is used on each end of the
 * ethernet link - doesn't support specified cluster descriptors
 */
tt_op_info generate_ethernet_datacopy_test_info(
    const std::string &op_name,
    const buda_SocDescriptor &soc_descriptor,
    chip_id_t dest_chip,
    chip_id_t src_chip,
    const std::vector<ethernet_channel_t> &dest_channels,
    const std::vector<ethernet_channel_t> &src_channels,
    const tt::tt_grid_shape &grid_shape,
    const tt_dim_info &dim_info,
    buffer_map_t &buffer_map,
    pipe_map_t &pipe_map,
    op_output_buf_map_t &op_output_buf_map,
    op_input_buf_map_t &op_input_buf_map,
    std::unordered_map<unique_id_t, unique_id_t> &buffer_input_pipes,
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> &buffer_output_pipes) {
    EXPECT_EQ(grid_shape.at(0) * grid_shape.at(1), dest_channels.size());
    EXPECT_EQ(src_channels.size(), dest_channels.size());

    auto unpacker_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor,
        buffer_map,
        eth_datacopy_buffers(
            soc_descriptor,
            dest_channels,
            dest_chip,
            tt::RouterBufferType::Input,
            dim_info.ublock_rt * dim_info.ublock_ct));

    auto output_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor,
        buffer_map,
        eth_datacopy_buffers(
            soc_descriptor,
            dest_channels,
            dest_chip,
            tt::RouterBufferType::Output,
            dim_info.ublock_rt * dim_info.ublock_ct));

    std::vector<loc_buftype_ntiles_t> input_buffer_specs = {};
    for (const auto &test_buf_entry : unpacker_buffers) {
        input_buffer_specs.push_back(loc_buftype_ntiles_t{
            tt_cxy_pair(src_chip, test_buf_entry.buf.core_location().x, test_buf_entry.buf.core_location().y),
            tt::RouterBufferType::Relay,
            dim_info.ublock_rt * dim_info.ublock_ct});
    }
    auto input_buffers = generate_multiple_dummy_router_buffers(soc_descriptor, buffer_map, input_buffer_specs);

    for (int i = 0; i < input_buffers.size(); i++) {
        const auto &input_buf = input_buffers.at(i);
        const auto &unpacker_buf = unpacker_buffers.at(i);
        unique_id_t pipe_id = unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN);
        pipe_map.insert(
            {pipe_id,
             pipe_t(
                 tt_cxy_pair(src_chip, input_buf.buf.core_location().x, input_buf.buf.core_location().y),
                 {input_buf.id},
                 {unpacker_buf.id})});
        buffer_input_pipes[unpacker_buf.id] = pipe_id;
        buffer_output_pipes[input_buf.id].insert(pipe_id);
    }

    // Always default non-essential fields (dataformat) to something consistent/standard
    // const auto &op_name = "test_ethernet_datacopy";
    auto eth_datacopy_op = tt_op_info{
        .name = op_name,
        .type = "ethernet_datacopy",
        .grid_loc = {-1, -1},
        .grid_size = grid_shape,
        .input_names = {"operand0"},
        .input_data_formats = {DataFormat::Float16},
        .math_fidelity = MathFidelity::HiFi3,
        .output_dim = dim_info,
        .buf_size_mb = 1,
        .attributes =
            {.ethernet_datacopy_attr =
                 {
                     .ingress_channels = src_channels,
                     .egress_channels = dest_channels,
                     .dest_device = dest_chip,
                 }},
        .input_dims = {dim_info},
        .input_core_grids = {grid_shape}};

    for (int r = 0; r < grid_shape.r; r++) {
        for (int c = 0; c < grid_shape.c; c++) {
            op_input_buf_map[op_name][r][c][0] = input_buffers.at(r * grid_shape.c + c).id;
            op_output_buf_map[op_name][r][c] = output_buffers.at(r * grid_shape.c + c).id;
        }
    }

    return eth_datacopy_op;
}

tt_op_info generate_ethernet_datacopy_test_info_on_chip1(
    const std::string &op_name,
    const buda_SocDescriptor &soc_descriptor,
    const tt::tt_grid_shape &grid_shape,
    const tt_dim_info &dim_info,
    buffer_map_t &buffer_map,
    pipe_map_t &pipe_map,
    op_output_buf_map_t &op_output_buf_map,
    op_input_buf_map_t &op_input_buf_map,
    std::unordered_map<unique_id_t, unique_id_t> &buffer_input_pipes,
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> &buffer_output_pipes) {
    return generate_ethernet_datacopy_test_info(
        op_name,
        soc_descriptor,
        1,
        0,
        // based on nebula connectivity (can generalize this to pull from cluster desc instead)
        std::vector<ethernet_channel_t>(grid_shape.r * grid_shape.c, 0),
        std::vector<ethernet_channel_t>(grid_shape.r * grid_shape.c, 8),
        grid_shape,
        dim_info,
        buffer_map,
        pipe_map,
        op_output_buf_map,
        op_input_buf_map,
        buffer_input_pipes,
        buffer_output_pipes);
}

struct eth_datacopy_spec_t {
    tt::tt_grid_shape grid_shape;
    tt_dim_info dim_info;
};

eth_datacopy_test_info_t initialize_ethernet_datacopies_for_test(
    const buda_SocDescriptor &soc_descriptor, std::vector<eth_datacopy_spec_t> eth_datacopy_specs) {
    eth_datacopy_test_info_t test_info;
    auto buffer_map = std::unordered_map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] =
        generate_multiple_dummy_router_queues(soc_descriptor, test_info.buffer_map, {});
    test_info.queues = queues;
    test_info.queue_map = queue_map;
    test_info.op_queue_output_scatter = op_queue_output_scatter;
    test_info.queue_settings_map = queue_settings_map;

    int count = 0;
    for (const auto &spec : eth_datacopy_specs) {
        const auto &grid_shape = spec.grid_shape;
        const auto &dim_info = spec.dim_info;
        // const auto &grid_shape = eth_datacopy_spec.grid_shape;
        // const auto &dim_info = eth_datacopy_spec.dim_info;
        const auto &op_name = "test_ethernet_datacopy_" + std::to_string(count);
        const auto &op_info = generate_ethernet_datacopy_test_info_on_chip1(
            op_name,
            soc_descriptor,
            grid_shape,
            dim_info,
            test_info.buffer_map,
            test_info.pipes,
            test_info.op_output_buf_map,
            test_info.op_input_buf_map,
            test_info.buffer_input_pipes,
            test_info.buffer_output_pipes);
        test_info.op_map[op_name] = op_info;
        count++;

        for (int r = 0; r < grid_shape.r; r++) {
            for (int c = 0; c < grid_shape.c; c++) {
                test_info.input_buffers.push_back(test_info.op_input_buf_map[op_name][r][c][0]);
                test_info.output_buffers.push_back(test_info.op_output_buf_map[op_name][r][c]);
            }
        }
    }

    return test_info;
}

TEST(
    EthernetDataCopyPassThroughOptimization,
    IsSingleSourcePipeThatForwardsAllInputBufferTilesInOrder_False_ScatterPipeScenarios) {
    const auto &soc_descriptor = *(wh_soc_desc.get());

    {
        eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
            soc_descriptor,
            {eth_datacopy_spec_t{
                .grid_shape = {1, 1},
                .dim_info = {
                    .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
        unique_id_t eth_datacopy_input_buffer_0_id = test_info.input_buffers.at(0);
        unique_id_t eth_datacopy_output_buffer_0_id = test_info.output_buffers.at(0);

        TT_ASSERT(test_info.op_map.size() > 0);
        const auto &eth_datacopy_name = test_info.op_map.begin()->first;
        auto chip_0_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}});
        auto chip_1_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor,
            test_info.buffer_map,
            {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
             {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1}});

        unique_id_t producer_buffer_0 = chip_0_buffers.at(0).id;
        unique_id_t consumer_buffer_0 = chip_1_buffers.at(0).id;
        unique_id_t consumer_buffer_1 = chip_1_buffers.at(1).id;

        const auto &pipes = generate_test_pipes(
            soc_descriptor,
            {pipe_t(tt_cxy_pair(0, 1, 1), {producer_buffer_0}, {eth_datacopy_input_buffer_0_id}),
             pipe_t(
                 {tt_cxy_pair(1, 1, 1), tt_cxy_pair(1, 2, 1)},
                 {eth_datacopy_output_buffer_0_id},
                 {{consumer_buffer_0}, {consumer_buffer_1}})},
            test_info);

        auto test_router = generate_test_router(soc_descriptor, 2, test_info);
        auto router = test_router->router.get();

        ASSERT_TRUE(router->get_pipe(pipes.at(1)).has_multiple_timesteps());
        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(1))));
    }
}

TEST(
    EthernetDataCopyPassThroughOptimization,
    IsSingleSourcePipeThatForwardsAllInputBufferTilesInOrder_False_UnicastScenarios) {
    // Are there any cases possible where this won't be true? I think only if it's a forked output buffer
    // that doesn't send full output for all fork edges (but ether way, a fork implies it's not linear pass through)
    const auto &soc_descriptor = *(wh_soc_desc.get());

    {
        // Single core fork - single ublock
        eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
            soc_descriptor,
            {eth_datacopy_spec_t{
                .grid_shape = {1, 1},
                .dim_info = {
                    .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
        unique_id_t output_buffer_id = test_info.output_buffers.at(0);

        auto chip_1_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor,
            test_info.buffer_map,
            {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
             {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1}});

        const auto &pipes = generate_test_pipes(
            soc_descriptor,
            {pipe_t(tt_cxy_pair(1, 1, 1), {output_buffer_id}, {chip_1_buffers.at(0).id}),
             pipe_t(tt_cxy_pair(1, 1, 1), {output_buffer_id}, {chip_1_buffers.at(1).id})},
            test_info);

        auto test_router = generate_test_router(soc_descriptor, 2, test_info);
        auto router = test_router->router.get();

        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(0))));
        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(1))));
    }

    {
        // Single core fork - multiple ublock
        eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
            soc_descriptor,
            {{.grid_shape = {1, 1},
              .dim_info = {
                  .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 2, .mblock_n = 2, .ublock_order = Dim::R}}});
        auto output_buffer_id = test_info.output_buffers.at(0);
        auto chip_1_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor,
            test_info.buffer_map,
            {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 4},
             {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 4}});
        const auto &pipes = generate_test_pipes(
            soc_descriptor,
            {pipe_t(tt_cxy_pair(1, 1, 1), {output_buffer_id}, {chip_1_buffers.at(0).id}, "", -1, 4),
             pipe_t(tt_cxy_pair(1, 1, 1), {output_buffer_id}, {chip_1_buffers.at(1).id}, "", -1, 4)},
            test_info);

        auto test_router = generate_test_router(soc_descriptor, 2, test_info);
        auto router = test_router->router.get();

        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(0))));
        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(1))));
    }

    {
        // multicore fork - single ublock
        eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
            soc_descriptor,
            {{.grid_shape = {1, 2},
              .dim_info = {
                  .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
        // auto output_buffer_id = test_info.output_buffers.at(0);

        auto chip_1_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor,
            test_info.buffer_map,
            {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
             {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1},
             {tt_cxy_pair(1, 3, 1), tt::RouterBufferType::Relay, 1}});

        const auto &pipes = generate_test_pipes(
            soc_descriptor,
            {pipe_t(tt_cxy_pair(1, 1, 1), {test_info.output_buffers.at(0)}, {chip_1_buffers.at(0).id}),
             pipe_t(tt_cxy_pair(1, 1, 1), {test_info.output_buffers.at(0)}, {chip_1_buffers.at(1).id}),
             pipe_t(tt_cxy_pair(1, 1, 1), {test_info.output_buffers.at(1)}, {chip_1_buffers.at(2).id})},
            test_info);

        auto test_router = generate_test_router(soc_descriptor, 2, test_info);
        auto router = test_router->router.get();

        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(0))));
        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(1))));
        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(2))));
    }

    {
        // multicore fork - multipl ublock
        eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
            soc_descriptor,
            {eth_datacopy_spec_t{
                .grid_shape = {1, 2},
                .dim_info = {
                    .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 2, .mblock_n = 2, .ublock_order = Dim::R}}});
        auto chip_1_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor,
            test_info.buffer_map,
            {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 4},
             {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 4},
             {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 4},
             {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 4}});

        const auto &pipes = generate_test_pipes(
            soc_descriptor,
            {pipe_t(
                 tt_cxy_pair(1, 1, 1), {test_info.output_buffers.at(0)}, {chip_1_buffers.at(0).id}, "", -1, 4),
             pipe_t(
                 tt_cxy_pair(1, 1, 1), {test_info.output_buffers.at(0)}, {chip_1_buffers.at(1).id}, "", -1, 4),
             pipe_t(
                 tt_cxy_pair(1, 2, 1), {test_info.output_buffers.at(1)}, {chip_1_buffers.at(2).id}, "", -1, 4),
             pipe_t(
                 tt_cxy_pair(1, 2, 1), {test_info.output_buffers.at(1)}, {chip_1_buffers.at(3).id}, "", -1, 4)},
            test_info);

        auto test_router = generate_test_router(soc_descriptor, 2, test_info);
        auto router = test_router->router.get();

        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(0))));
        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(1))));
        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(2))));
        ASSERT_FALSE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(3))));
    }
}

TEST(
    EthernetDataCopyPassThroughOptimization,
    IsSingleSourcePipeThatForwardsAllInputBufferTilesInOrder_True_UnicastScenarios) {
    const auto &soc_descriptor = *(wh_soc_desc.get());

    {
        // Single core - single ublock
        eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
            soc_descriptor,
            {eth_datacopy_spec_t{
                .grid_shape = {1, 1},
                .dim_info = {
                    .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
        unique_id_t output_buffer_id = test_info.output_buffers.at(0);

        auto chip_1_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1}});

        const auto &pipes = generate_test_pipes(
            soc_descriptor,
            {pipe_t(tt_cxy_pair(1, 1, 1), {output_buffer_id}, {chip_1_buffers.at(0).id})},
            test_info);

        auto test_router = generate_test_router(soc_descriptor, 2, test_info);
        auto router = test_router->router.get();

        ASSERT_TRUE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(0))));
    }

    {
        // Single core - multiple ublock
        eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
            soc_descriptor,
            {{.grid_shape = {1, 1},
              .dim_info = {
                  .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 2, .mblock_n = 2, .ublock_order = Dim::R}}});
        auto output_buffer_id = test_info.output_buffers.at(0);
        auto chip_1_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 4}});
        const auto &pipes = generate_test_pipes(
            soc_descriptor,
            {pipe_t(tt_cxy_pair(1, 1, 1), {output_buffer_id}, {chip_1_buffers.at(0).id}, "", -1, 4)},
            test_info);

        auto test_router = generate_test_router(soc_descriptor, 2, test_info);
        auto router = test_router->router.get();

        ASSERT_TRUE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(0))));
    }

    {
        // multicore - single ublock
        eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
            soc_descriptor,
            {{.grid_shape = {1, 2},
              .dim_info = {
                  .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
        // auto output_buffer_id = test_info.output_buffers.at(0);

        auto chip_1_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1}});

        const auto &pipes = generate_test_pipes(
            soc_descriptor,
            {pipe_t(tt_cxy_pair(1, 1, 1), {test_info.output_buffers.at(0)}, {chip_1_buffers.at(0).id})},
            test_info);

        auto test_router = generate_test_router(soc_descriptor, 2, test_info);
        auto router = test_router->router.get();

        ASSERT_TRUE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(0))));
    }

    {
        // multicore fork - multipl ublock
        eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
            soc_descriptor,
            {eth_datacopy_spec_t{
                .grid_shape = {1, 2},
                .dim_info = {
                    .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 2, .mblock_n = 2, .ublock_order = Dim::R}}});
        auto chip_1_buffers = generate_multiple_dummy_router_buffers(
            soc_descriptor,
            test_info.buffer_map,
            {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 4},
             {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 4}});

        const auto &pipes = generate_test_pipes(
            soc_descriptor,
            {pipe_t(
                 tt_cxy_pair(1, 1, 1), {test_info.output_buffers.at(0)}, {chip_1_buffers.at(0).id}, "", -1, 4),
             pipe_t(
                 tt_cxy_pair(1, 2, 1), {test_info.output_buffers.at(1)}, {chip_1_buffers.at(1).id}, "", -1, 4)},
            test_info);

        auto test_router = generate_test_router(soc_descriptor, 2, test_info);
        auto router = test_router->router.get();

        ASSERT_TRUE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(0))));
        ASSERT_TRUE(router::is_single_source_pipe_that_forwards_all_input_buffer_tiles_in_order(
            *router, router->get_pipe(pipes.at(1))));
    }
}

TEST(EthernetDataCopyPassThroughOptimization, OptimizeOutSingleCorePassThrougUnicasthNoTStreaming) {
    const auto &soc_descriptor = *(wh_soc_desc.get());

    // Single core - single ublock
    eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
        soc_descriptor,
        {eth_datacopy_spec_t{
            .grid_shape = {1, 1},
            .dim_info = {
                .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
    unique_id_t eth_datacopy_input_buffer_id = test_info.input_buffers.at(0);
    unique_id_t eth_datacopy_output_buffer_id = test_info.output_buffers.at(0);

    TT_ASSERT(test_info.op_map.size() > 0);
    const auto &eth_datacopy_name = test_info.op_map.begin()->first;
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}});
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1}});
    unique_id_t producer_buffer = chip_0_buffers.at(0).id;
    unique_id_t consumer_buffer = chip_1_buffers.at(0).id;

    const auto &pipes = generate_test_pipes(
        soc_descriptor,
        {pipe_t(tt_cxy_pair(0, 1, 1), {producer_buffer}, {eth_datacopy_input_buffer_id}),
         pipe_t(tt_cxy_pair(1, 1, 1), {eth_datacopy_output_buffer_id}, {consumer_buffer})},
        test_info);

    auto test_router = generate_test_router(soc_descriptor, 2, test_info);
    auto router = test_router->router.get();

    std::vector<unique_id_t> unpacker_buffers;
    std::transform(test_info.input_buffers.begin(), test_info.input_buffers.end(), std::back_inserter(unpacker_buffers), [&](unique_id_t id) {
        return router->get_pipe(*(router->get_buffer_output_pipes().at(id).begin())).output_buffer_ids().at(0);
    });

    int num_buffers_preoptimization = 5;
    int num_pipes_preoptimization = 3;
    ASSERT_EQ(router->get_buffer_map().size(), num_buffers_preoptimization);
    ASSERT_EQ(router->get_pipes().size(), num_pipes_preoptimization);
    router::optimize_out_pass_through_ethernet_datacopy_ops(*router);

    // Ensure all ethernet datacopy op buffer map entries were deleted
    ASSERT_EQ(router->get_op_info_map().find(eth_datacopy_name), router->get_op_info_map().end());
    ASSERT_EQ(router->get_op_output_buf_map().find(eth_datacopy_name), router->get_op_output_buf_map().end());
    ASSERT_EQ(router->get_op_input_buf_map().find(eth_datacopy_name), router->get_op_input_buf_map().end());
    ASSERT_EQ(
        router->get_op_intermediate_buf_map().find(eth_datacopy_name), router->get_op_intermediate_buf_map().end());

    // None of the original buffers can remain because we need to implement the ethernet datacopy as a single
    // relay buffer. Since the original buffers are Input and Output buffers they can't implement the relay
    ASSERT_EQ(
        router->get_buffer_output_pipes().find(eth_datacopy_output_buffer_id), router->get_buffer_output_pipes().end());
    bool all_unpacker_buffer_output_pipes_entries_removed =
        std::all_of(unpacker_buffers.begin(), unpacker_buffers.end(), [&](unique_id_t id) {
            return router->get_buffer_output_pipes().find(id) == router->get_buffer_output_pipes().end();
        });
    ASSERT_TRUE(all_unpacker_buffer_output_pipes_entries_removed);
    ASSERT_EQ(router->get_buffer_map().size(), num_buffers_preoptimization - 1);
    ASSERT_EQ(router->get_pipes().size(), num_pipes_preoptimization);

    ASSERT_TRUE(router->get_buffer_output_pipes().find(producer_buffer) != router->get_buffer_output_pipes().end());
    ASSERT_TRUE(router->get_buffer_output_pipes().at(producer_buffer).size() == 1);
    unique_id_t relay_input_pipe_id = *(router->get_buffer_output_pipes().at(producer_buffer).begin());
    const auto &relay_input_pipe = router->get_pipe(relay_input_pipe_id);
    ASSERT_EQ(relay_input_pipe.input_buffer_ids.size(), 1);
    ASSERT_EQ(relay_input_pipe.input_buffer_ids.at(0), producer_buffer);
    ASSERT_FALSE(relay_input_pipe.has_multiple_timesteps());
    ASSERT_EQ(relay_input_pipe.output_buffer_ids().size(), 1);

    unique_id_t first_relay_buffer_id = relay_input_pipe.output_buffer_ids().at(0);
    ASSERT_TRUE(router->get_buffer_output_pipes().find(first_relay_buffer_id) != router->get_buffer_output_pipes().end());
    ASSERT_EQ(router->get_buffer_output_pipes().at(first_relay_buffer_id).size(), 1);
    ASSERT_EQ(first_relay_buffer_id, eth_datacopy_input_buffer_id);

    unique_id_t relay_buffer_id = router->get_pipe(*(router->get_buffer_output_pipes().at(eth_datacopy_input_buffer_id).begin())).output_buffer_ids().at(0);

    unique_id_t relay_output_pipe_id = *(router->get_buffer_output_pipes().at(relay_buffer_id).begin());
    const auto &relay_output_pipe = router->get_pipe(relay_output_pipe_id);
    ASSERT_EQ(relay_output_pipe.input_buffer_ids.size(), 1);
    ASSERT_EQ(relay_output_pipe.input_buffer_ids.at(0), relay_buffer_id);
    ASSERT_FALSE(relay_output_pipe.has_multiple_timesteps());
    ASSERT_EQ(relay_output_pipe.output_buffer_ids().size(), 1);
    ASSERT_EQ(relay_output_pipe.output_buffer_ids().at(0), consumer_buffer);
};

TEST(EthernetDataCopyPassThroughOptimization, OptimizeOutSingleCorePassThrougMulticastNoTStreaming) {
    const auto &soc_descriptor = *(wh_soc_desc.get());

    // Single core - single ublock
    eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
        soc_descriptor,
        {eth_datacopy_spec_t{
            .grid_shape = {1, 1},
            .dim_info = {
                .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
    unique_id_t eth_datacopy_input_buffer_id = test_info.input_buffers.at(0);
    unique_id_t eth_datacopy_output_buffer_id = test_info.output_buffers.at(0);

    TT_ASSERT(test_info.op_map.size() > 0);
    const auto &eth_datacopy_name = test_info.op_map.begin()->first;
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}});
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor,
        test_info.buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1},
        });
    unique_id_t producer_buffer = chip_0_buffers.at(0).id;
    unique_id_t consumer_buffer_0 = chip_1_buffers.at(0).id;
    unique_id_t consumer_buffer_1 = chip_1_buffers.at(1).id;

    const auto &pipes = generate_test_pipes(
        soc_descriptor,
        {pipe_t(tt_cxy_pair(0, 1, 1), {producer_buffer}, {eth_datacopy_input_buffer_id}),
         pipe_t(tt_cxy_pair(1, 1, 1), {eth_datacopy_output_buffer_id}, {consumer_buffer_0, consumer_buffer_1})},
        test_info);

    auto test_router = generate_test_router(soc_descriptor, 2, test_info);
    auto router = test_router->router.get();

    std::vector<unique_id_t> unpacker_buffers;
    std::transform(test_info.input_buffers.begin(), test_info.input_buffers.end(), std::back_inserter(unpacker_buffers), [&](unique_id_t id) {
        return router->get_pipe(*(router->get_buffer_output_pipes().at(id).begin())).output_buffer_ids().at(0);
    });

    int num_buffers_preoptimization = 6;
    int num_pipes_preoptimization = 3;
    ASSERT_EQ(router->get_buffer_map().size(), num_buffers_preoptimization);
    ASSERT_EQ(router->get_pipes().size(), num_pipes_preoptimization);
    router::optimize_out_pass_through_ethernet_datacopy_ops(*router);

    // Ensure all ethernet datacopy op buffer map entries were deleted
    ASSERT_EQ(router->get_op_info_map().find(eth_datacopy_name), router->get_op_info_map().end());
    ASSERT_EQ(router->get_op_output_buf_map().find(eth_datacopy_name), router->get_op_output_buf_map().end());
    ASSERT_EQ(router->get_op_input_buf_map().find(eth_datacopy_name), router->get_op_input_buf_map().end());
    ASSERT_EQ(
        router->get_op_intermediate_buf_map().find(eth_datacopy_name), router->get_op_intermediate_buf_map().end());

    // None of the original buffers can remain because we need to implement the ethernet datacopy as a single
    // relay buffer. Since the original buffers are Input and Output buffers they can't implement the relay
    ASSERT_EQ(
        router->get_buffer_output_pipes().find(eth_datacopy_output_buffer_id), router->get_buffer_output_pipes().end());
    bool all_unpacker_buffer_output_pipes_entries_removed =
        std::all_of(unpacker_buffers.begin(), unpacker_buffers.end(), [&](unique_id_t id) {
            return router->get_buffer_output_pipes().find(id) == router->get_buffer_output_pipes().end();
        });
    ASSERT_TRUE(all_unpacker_buffer_output_pipes_entries_removed);
    ASSERT_EQ(router->get_buffer_map().size(), num_buffers_preoptimization - 1);
    ASSERT_EQ(router->get_pipes().size(), num_pipes_preoptimization);

    ASSERT_TRUE(router->get_buffer_output_pipes().find(producer_buffer) != router->get_buffer_output_pipes().end());
    ASSERT_TRUE(router->get_buffer_output_pipes().at(producer_buffer).size() == 1);
    unique_id_t relay_input_pipe_id = *(router->get_buffer_output_pipes().at(producer_buffer).begin());
    const auto &relay_input_pipe = router->get_pipe(relay_input_pipe_id);
    ASSERT_EQ(relay_input_pipe.input_buffer_ids.size(), 1);
    ASSERT_EQ(relay_input_pipe.input_buffer_ids.at(0), producer_buffer);
    ASSERT_FALSE(relay_input_pipe.has_multiple_timesteps());
    ASSERT_EQ(relay_input_pipe.output_buffer_ids().size(), 1);

    unique_id_t first_relay_buffer_id = relay_input_pipe.output_buffer_ids().at(0);
    ASSERT_TRUE(router->get_buffer_output_pipes().find(first_relay_buffer_id) != router->get_buffer_output_pipes().end());
    ASSERT_EQ(router->get_buffer_output_pipes().at(first_relay_buffer_id).size(), 1);
    ASSERT_EQ(first_relay_buffer_id, eth_datacopy_input_buffer_id);

    unique_id_t relay_buffer_id = router->get_pipe(*(router->get_buffer_output_pipes().at(eth_datacopy_input_buffer_id).begin())).output_buffer_ids().at(0);

    unique_id_t relay_output_pipe_id = *(router->get_buffer_output_pipes().at(relay_buffer_id).begin());
    const auto &relay_output_pipe = router->get_pipe(relay_output_pipe_id);
    ASSERT_EQ(relay_output_pipe.input_buffer_ids.size(), 1);
    ASSERT_EQ(relay_output_pipe.input_buffer_ids.at(0), relay_buffer_id);
    ASSERT_FALSE(relay_output_pipe.has_multiple_timesteps());
    ASSERT_EQ(relay_output_pipe.output_buffer_ids().size(), 2);
    ASSERT_EQ(relay_output_pipe.output_buffer_ids().at(0), consumer_buffer_0);
    ASSERT_EQ(relay_output_pipe.output_buffer_ids().at(1), consumer_buffer_1);
};

TEST(EthernetDataCopyPassThroughOptimization, OptimizeOutMultiCorePassThroughUnicastNoTStreaming) {
    const auto &soc_descriptor = *(wh_soc_desc.get());

    eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
        soc_descriptor,
        {eth_datacopy_spec_t{
            .grid_shape = {1, 2},
            .dim_info = {
                .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
    unique_id_t eth_datacopy_input_buffer_0_id = test_info.input_buffers.at(0);
    unique_id_t eth_datacopy_input_buffer_1_id = test_info.input_buffers.at(1);
    unique_id_t eth_datacopy_output_buffer_0_id = test_info.output_buffers.at(0);
    unique_id_t eth_datacopy_output_buffer_1_id = test_info.output_buffers.at(1);

    TT_ASSERT(test_info.op_map.size() > 0);
    const auto &eth_datacopy_name = test_info.op_map.begin()->first;
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor,
        test_info.buffer_map,
        {{tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
         {tt_cxy_pair(0, 2, 1), tt::RouterBufferType::Relay, 1}});
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor,
        test_info.buffer_map,
        {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
         {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1}});

    unique_id_t producer_buffer_0 = chip_0_buffers.at(0).id;
    unique_id_t producer_buffer_1 = chip_0_buffers.at(1).id;
    unique_id_t consumer_buffer_0 = chip_1_buffers.at(0).id;
    unique_id_t consumer_buffer_1 = chip_1_buffers.at(1).id;

    const auto &pipes = generate_test_pipes(
        soc_descriptor,
        {pipe_t(tt_cxy_pair(0, 1, 1), {producer_buffer_0}, {eth_datacopy_input_buffer_0_id}),
         pipe_t(tt_cxy_pair(0, 1, 1), {producer_buffer_1}, {eth_datacopy_input_buffer_1_id}),
         pipe_t(tt_cxy_pair(1, 1, 1), {eth_datacopy_output_buffer_0_id}, {consumer_buffer_0}),
         pipe_t(tt_cxy_pair(1, 1, 1), {eth_datacopy_output_buffer_1_id}, {consumer_buffer_1})},
        test_info);

    auto test_router = generate_test_router(soc_descriptor, 2, test_info);
    auto router = test_router->router.get();

    std::vector<unique_id_t> unpacker_buffers;
    std::transform(test_info.input_buffers.begin(), test_info.input_buffers.end(), std::back_inserter(unpacker_buffers), [&](unique_id_t id) {
        return router->get_pipe(*(router->get_buffer_output_pipes().at(id).begin())).output_buffer_ids().at(0);
    });

    int num_eth_datacopy_cores_optimized = 2;
    int num_buffers_preoptimization = 10;
    int num_pipes_preoptimization = 6;
    ASSERT_EQ(router->get_buffer_map().size(), num_buffers_preoptimization);
    ASSERT_EQ(router->get_pipes().size(), num_pipes_preoptimization);
    router::optimize_out_pass_through_ethernet_datacopy_ops(*router);

    // Ensure all ethernet datacopy op buffer map entries were deleted
    ASSERT_EQ(router->get_op_info_map().find(eth_datacopy_name), router->get_op_info_map().end());
    ASSERT_EQ(router->get_op_output_buf_map().find(eth_datacopy_name), router->get_op_output_buf_map().end());
    ASSERT_EQ(router->get_op_input_buf_map().find(eth_datacopy_name), router->get_op_input_buf_map().end());
    ASSERT_EQ(
        router->get_op_intermediate_buf_map().find(eth_datacopy_name), router->get_op_intermediate_buf_map().end());

    // None of the original buffers can remain because we need to implement the ethernet datacopy as a single
    // relay buffer. Since the original buffers are Input and Output buffers they can't implement the relay
    ASSERT_TRUE(
        router->get_buffer_output_pipes().find(eth_datacopy_output_buffer_0_id) ==
        router->get_buffer_output_pipes().end());
    ASSERT_TRUE(
        router->get_buffer_output_pipes().find(eth_datacopy_output_buffer_1_id) ==
        router->get_buffer_output_pipes().end());
    bool all_unpacker_buffer_output_pipes_entries_removed =
        std::all_of(unpacker_buffers.begin(), unpacker_buffers.end(), [&](unique_id_t id) {
            return router->get_buffer_output_pipes().find(id) == router->get_buffer_output_pipes().end();
        });
    ASSERT_TRUE(all_unpacker_buffer_output_pipes_entries_removed);
    ASSERT_EQ(router->get_buffer_map().size(), num_buffers_preoptimization - num_eth_datacopy_cores_optimized);
    ASSERT_EQ(router->get_pipes().size(), num_pipes_preoptimization);

    ASSERT_TRUE(router->get_buffer_output_pipes().find(producer_buffer_0) != router->get_buffer_output_pipes().end());
    ASSERT_TRUE(router->get_buffer_output_pipes().find(producer_buffer_1) != router->get_buffer_output_pipes().end());
    ASSERT_TRUE(router->get_buffer_output_pipes().at(producer_buffer_0).size() == 1);
    ASSERT_TRUE(router->get_buffer_output_pipes().at(producer_buffer_1).size() == 1);
    std::vector<std::pair<unique_id_t, unique_id_t>> producer_consumer_buffer_pairs = {
        {producer_buffer_0, consumer_buffer_0}, {producer_buffer_1, consumer_buffer_1}};

    for (int op_index = 0; op_index < producer_consumer_buffer_pairs.size(); op_index++) {
        const auto [producer_buffer_id, consumer_buffer_id] = producer_consumer_buffer_pairs.at(op_index);
        unique_id_t relay_input_pipe_id = *(router->get_buffer_output_pipes().at(producer_buffer_id).begin());
        const auto &relay_input_pipe = router->get_pipe(relay_input_pipe_id);
        ASSERT_EQ(relay_input_pipe.input_buffer_ids.size(), 1);
        ASSERT_EQ(relay_input_pipe.input_buffer_ids.at(0), producer_buffer_id);
        ASSERT_FALSE(relay_input_pipe.has_multiple_timesteps());
        ASSERT_EQ(relay_input_pipe.output_buffer_ids().size(), 1);

        unique_id_t first_relay_buffer_id = relay_input_pipe.output_buffer_ids().at(0);

        ASSERT_TRUE(router->get_buffer_output_pipes().find(first_relay_buffer_id) != router->get_buffer_output_pipes().end());
        ASSERT_EQ(router->get_buffer_output_pipes().at(first_relay_buffer_id).size(), 1);
        ASSERT_EQ(first_relay_buffer_id, test_info.input_buffers.at(op_index));

        unique_id_t relay_buffer_id = router->get_pipe(*(router->get_buffer_output_pipes().at(test_info.input_buffers.at(op_index)).begin())).output_buffer_ids().at(0);

        unique_id_t relay_output_pipe_id = *(router->get_buffer_output_pipes().at(relay_buffer_id).begin());
        const auto &relay_output_pipe = router->get_pipe(relay_output_pipe_id);
        ASSERT_EQ(relay_output_pipe.input_buffer_ids.size(), 1);
        ASSERT_EQ(relay_output_pipe.input_buffer_ids.at(0), relay_buffer_id);
        ASSERT_FALSE(relay_output_pipe.has_multiple_timesteps());
        ASSERT_EQ(relay_output_pipe.output_buffer_ids().size(), 1);
        ASSERT_EQ(relay_output_pipe.output_buffer_ids().at(0), consumer_buffer_id);
    }
}

TEST(EthernetDataCopyPassThroughOptimization, OptimizeOutSingleCorePassThroughImplementedAsScatterPipe) {
    const auto &soc_descriptor = *(wh_soc_desc.get());

    eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
        soc_descriptor,
        {eth_datacopy_spec_t{
            .grid_shape = {1, 1},
            .dim_info = {
                .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
    unique_id_t eth_datacopy_input_buffer_0_id = test_info.input_buffers.at(0);
    unique_id_t eth_datacopy_output_buffer_0_id = test_info.output_buffers.at(0);

    TT_ASSERT(test_info.op_map.size() > 0);
    const auto &eth_datacopy_name = test_info.op_map.begin()->first;
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}});
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1}});

    unique_id_t producer_buffer_0 = chip_0_buffers.at(0).id;
    unique_id_t consumer_buffer_0 = chip_1_buffers.at(0).id;

    const auto &pipes = generate_test_pipes(
        soc_descriptor,
        {pipe_t(tt_cxy_pair(0, 1, 1), {producer_buffer_0}, {eth_datacopy_input_buffer_0_id}),
         pipe_t(
             {tt_cxy_pair(1, 2, 1), tt_cxy_pair(1, 2, 1), tt_cxy_pair(1, 2, 1), tt_cxy_pair(1, 2, 1)},
             {eth_datacopy_output_buffer_0_id},
             {{consumer_buffer_0}, {consumer_buffer_0}, {consumer_buffer_0}, {consumer_buffer_0}})},
        test_info);

    auto test_router = generate_test_router(soc_descriptor, 2, test_info);
    auto router = test_router->router.get();

    std::vector<unique_id_t> unpacker_buffers;
    std::transform(test_info.input_buffers.begin(), test_info.input_buffers.end(), std::back_inserter(unpacker_buffers), [&](unique_id_t id) {
        return router->get_pipe(*(router->get_buffer_output_pipes().at(id).begin())).output_buffer_ids().at(0);
    });

    int num_eth_datacopy_cores_optimized = 1;
    int num_buffers_preoptimization = 5;
    int num_pipes_preoptimization = 3;
    int num_output_pipe_scatter_segments = 4;
    ASSERT_EQ(router->get_buffer_map().size(), num_buffers_preoptimization);
    ASSERT_EQ(router->get_pipes().size(), num_pipes_preoptimization);
    router::optimize_out_pass_through_ethernet_datacopy_ops(*router);

    // Ensure all ethernet datacopy op buffer map entries were deleted
    ASSERT_EQ(router->get_op_info_map().find(eth_datacopy_name), router->get_op_info_map().end());
    ASSERT_EQ(router->get_op_output_buf_map().find(eth_datacopy_name), router->get_op_output_buf_map().end());
    ASSERT_EQ(router->get_op_input_buf_map().find(eth_datacopy_name), router->get_op_input_buf_map().end());
    ASSERT_EQ(
        router->get_op_intermediate_buf_map().find(eth_datacopy_name), router->get_op_intermediate_buf_map().end());

    // None of the original buffers can remain because we need to implement the ethernet datacopy as a single
    // relay buffer. Since the original buffers are Input and Output buffers they can't implement the relay
    ASSERT_TRUE(
        router->get_buffer_output_pipes().find(eth_datacopy_output_buffer_0_id) ==
            router->get_buffer_output_pipes().end());
    bool all_unpacker_buffer_output_pipes_entries_removed =
    std::all_of(unpacker_buffers.begin(), unpacker_buffers.end(), [&](unique_id_t id) {
        return router->get_buffer_output_pipes().find(id) == router->get_buffer_output_pipes().end();
    });
    ASSERT_EQ(router->get_buffer_map().size(), num_buffers_preoptimization - num_eth_datacopy_cores_optimized);
    ASSERT_EQ(router->get_pipes().size(), num_pipes_preoptimization);

    ASSERT_TRUE(router->get_buffer_output_pipes().find(producer_buffer_0) != router->get_buffer_output_pipes().end());
    ASSERT_TRUE(router->get_buffer_output_pipes().at(producer_buffer_0).size() == 1);

    unique_id_t relay_input_pipe_id = *(router->get_buffer_output_pipes().at(producer_buffer_0).begin());
    const auto &relay_input_pipe = router->get_pipe(relay_input_pipe_id);
    ASSERT_EQ(relay_input_pipe.input_buffer_ids.size(), 1);
    ASSERT_EQ(relay_input_pipe.input_buffer_ids.at(0), producer_buffer_0);
    ASSERT_FALSE(relay_input_pipe.has_multiple_timesteps());
    ASSERT_EQ(relay_input_pipe.output_buffer_ids().size(), 1);

    unique_id_t first_relay_buffer_id = relay_input_pipe.output_buffer_ids().at(0);

    ASSERT_TRUE(router->get_buffer_output_pipes().find(first_relay_buffer_id) != router->get_buffer_output_pipes().end());
    ASSERT_EQ(router->get_buffer_output_pipes().at(first_relay_buffer_id).size(), 1);
    ASSERT_EQ(first_relay_buffer_id , eth_datacopy_input_buffer_0_id);

    unique_id_t relay_buffer_id = router->get_pipe(*(router->get_buffer_output_pipes().at(eth_datacopy_input_buffer_0_id).begin())).output_buffer_ids().at(0);

    unique_id_t relay_output_pipe_id = *(router->get_buffer_output_pipes().at(relay_buffer_id).begin());
    const auto &relay_output_pipe = router->get_pipe(relay_output_pipe_id);
    ASSERT_EQ(relay_output_pipe.input_buffer_ids.size(), 1);
    ASSERT_EQ(relay_output_pipe.input_buffer_ids.at(0), relay_buffer_id);
    ASSERT_TRUE(relay_output_pipe.has_multiple_timesteps());
    ASSERT_EQ(relay_output_pipe.time_multiplexed_output_buffer_ids().size(), num_output_pipe_scatter_segments);
    const auto &expected_output_pipe_output_buffers = std::vector<std::vector<unique_id_t>>{
        {consumer_buffer_0}, {consumer_buffer_0}, {consumer_buffer_0}, {consumer_buffer_0}};
    ASSERT_TRUE(relay_output_pipe.time_multiplexed_output_buffer_ids() == expected_output_pipe_output_buffers);
}
// TEST(EthernetDataCopyPassThroughOptimization, OptimizeOutMultiCorePassThroughImplementedAsScatterPipe) {
//     ASSERT_TRUE(false);  // Not implemented
// }

TEST(EthernetDataCopyPassThroughOptimization, KeepSingleCoreForkEthDatacopyAsNonPassThrough) {
    const auto &soc_descriptor = *(wh_soc_desc.get());

    eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
        soc_descriptor,
        {eth_datacopy_spec_t{
            .grid_shape = {1, 1},
            .dim_info = {
                .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
    unique_id_t eth_datacopy_input_buffer_0_id = test_info.input_buffers.at(0);
    unique_id_t eth_datacopy_output_buffer_0_id = test_info.output_buffers.at(0);

    TT_ASSERT(test_info.op_map.size() > 0);
    const auto &eth_datacopy_name = test_info.op_map.begin()->first;
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}});
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor,
        test_info.buffer_map,
        {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
         {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1}});

    unique_id_t producer_buffer_0 = chip_0_buffers.at(0).id;
    unique_id_t consumer_buffer_0 = chip_1_buffers.at(0).id;
    unique_id_t consumer_buffer_1 = chip_1_buffers.at(1).id;

    const auto &pipes = generate_test_pipes(
        soc_descriptor,
        {pipe_t(tt_cxy_pair(0, 1, 1), {producer_buffer_0}, {eth_datacopy_input_buffer_0_id}),
         pipe_t(tt_cxy_pair(1, 1, 1), {eth_datacopy_output_buffer_0_id}, {consumer_buffer_0}),
         pipe_t(tt_cxy_pair(1, 1, 1), {eth_datacopy_output_buffer_0_id}, {consumer_buffer_1})},
        test_info);

    auto test_router = generate_test_router(soc_descriptor, 2, test_info);
    auto router = test_router->router.get();

    auto buf_map_old = router->get_buffer_map();
    auto pipes_old = router->get_pipes();

    router::optimize_out_pass_through_ethernet_datacopy_ops(*router);
    auto buf_map_new = router->get_buffer_map();
    auto pipes_new = router->get_pipes();

    bool buffers_unchanged = buf_map_old == buf_map_new;
    bool pipes_unchanged = pipes_old == pipes_new;
    ASSERT_TRUE(buffers_unchanged);
    ASSERT_TRUE(pipes_unchanged);
}

TEST(EthernetDataCopyPassThroughOptimization, KeepSingleCoreEthDatacopyNonUnicastPipeScatterAsNonPassThrough) {
    const auto &soc_descriptor = *(wh_soc_desc.get());

    eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
        soc_descriptor,
        {eth_datacopy_spec_t{
            .grid_shape = {1, 1},
            .dim_info = {
                .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
    unique_id_t eth_datacopy_input_buffer_0_id = test_info.input_buffers.at(0);
    unique_id_t eth_datacopy_output_buffer_0_id = test_info.output_buffers.at(0);

    TT_ASSERT(test_info.op_map.size() > 0);
    const auto &eth_datacopy_name = test_info.op_map.begin()->first;
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}});
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor,
        test_info.buffer_map,
        {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
         {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1}});

    unique_id_t producer_buffer_0 = chip_0_buffers.at(0).id;
    unique_id_t consumer_buffer_0 = chip_1_buffers.at(0).id;
    unique_id_t consumer_buffer_1 = chip_1_buffers.at(1).id;

    const auto &pipes = generate_test_pipes(
        soc_descriptor,
        {pipe_t(tt_cxy_pair(0, 1, 1), {producer_buffer_0}, {eth_datacopy_input_buffer_0_id}),
         pipe_t(
             {tt_cxy_pair(1, 1, 1), tt_cxy_pair(1, 2, 1)},
             {eth_datacopy_output_buffer_0_id},
             {{consumer_buffer_0}, {consumer_buffer_1}})},
        test_info);

    auto test_router = generate_test_router(soc_descriptor, 2, test_info);
    auto router = test_router->router.get();

    auto buf_map_old = router->get_buffer_map();
    auto pipes_old = router->get_pipes();

    router::optimize_out_pass_through_ethernet_datacopy_ops(*router);
    auto buf_map_new = router->get_buffer_map();
    auto pipes_new = router->get_pipes();

    bool buffers_unchanged = buf_map_old == buf_map_new;
    bool pipes_unchanged = pipes_old == pipes_new;
    if (!buffers_unchanged) {
        log_debug(tt::LogTest, "\tBuffers changed");
       log_debug(tt::LogTest, "\tOld: ");
        for (const auto &buf : buf_map_old) {
            log_debug(tt::LogTest, "\t\t{}", buf.first);
            // log_debug(tt::LogTest, "\t\t{} -> {}", buf.first, buf.second);
        }
        log_debug(tt::LogTest, "\tNew: ");
        for (const auto &buf : buf_map_new) {
            log_debug(tt::LogTest, "\t\t{}", buf.first);
            // log_debug(tt::LogTest, "\t\t{} -> {}", buf.first, buf.second);
        }
    }
    ASSERT_TRUE(buffers_unchanged);
    ASSERT_TRUE(pipes_unchanged);
}

TEST(EthernetDataCopyPassThroughOptimization, KeepMultiCoreDueToSomeNonPassThroughCores) {
    const auto &soc_descriptor = *(wh_soc_desc.get());

    eth_datacopy_test_info_t test_info = initialize_ethernet_datacopies_for_test(
        soc_descriptor,
        {eth_datacopy_spec_t{
            .grid_shape = {1, 2},
            .dim_info = {
                .t = 1, .ublock_rt = 1, .ublock_ct = 1, .mblock_m = 1, .mblock_n = 1, .ublock_order = Dim::R}}});
    unique_id_t eth_datacopy_input_buffer_0_id = test_info.input_buffers.at(0);
    unique_id_t eth_datacopy_input_buffer_1_id = test_info.input_buffers.at(1);
    unique_id_t eth_datacopy_output_buffer_0_id = test_info.output_buffers.at(0);
    unique_id_t eth_datacopy_output_buffer_1_id = test_info.output_buffers.at(1);

    TT_ASSERT(test_info.op_map.size() > 0);
    const auto &eth_datacopy_name = test_info.op_map.begin()->first;
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor, test_info.buffer_map, {{tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}});
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(
        soc_descriptor,
        test_info.buffer_map,
        {{tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
         {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1},
         {tt_cxy_pair(1, 3, 1), tt::RouterBufferType::Relay, 1}});

    unique_id_t producer_buffer_0 = chip_0_buffers.at(0).id;
    unique_id_t consumer_buffer_0 = chip_1_buffers.at(0).id;
    unique_id_t consumer_buffer_1 = chip_1_buffers.at(1).id;
    unique_id_t consumer_buffer_2 = chip_1_buffers.at(2).id;

    const auto &pipes = generate_test_pipes(
        soc_descriptor,
        {pipe_t(tt_cxy_pair(0, 1, 1), {producer_buffer_0}, {eth_datacopy_input_buffer_0_id}),
         pipe_t(tt_cxy_pair(1, 1, 1), {eth_datacopy_output_buffer_0_id}, {consumer_buffer_0}),
         pipe_t(
             {tt_cxy_pair(1, 1, 1), tt_cxy_pair(1, 1, 1)},
             {eth_datacopy_output_buffer_1_id},
             {{consumer_buffer_1}, {consumer_buffer_2}})},
        test_info);

    auto test_router = generate_test_router(soc_descriptor, 2, test_info);
    auto router = test_router->router.get();

    auto buf_map_old = router->get_buffer_map();
    auto pipes_old = router->get_pipes();

    router::optimize_out_pass_through_ethernet_datacopy_ops(*router);
    auto buf_map_new = router->get_buffer_map();
    auto pipes_new = router->get_pipes();

    bool buffers_unchanged = buf_map_old == buf_map_new;
    bool pipes_unchanged = pipes_old == pipes_new;
    ASSERT_TRUE(buffers_unchanged);
    ASSERT_TRUE(pipes_unchanged);
}