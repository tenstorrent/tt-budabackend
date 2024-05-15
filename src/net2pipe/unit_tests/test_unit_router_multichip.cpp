// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "router/router_multichip.h"
#include "test_unit_common.hpp"
#include "router/router_passes_common.h"
#include "gtest/gtest.h"

#include <unordered_map>

using router::router_buffer_info_t;
using router::unique_id_t;
using router::pipe_t;
using router::time_multiplexed_outputs_t;

void print_pipe_dbg(std::uint64_t pipe_unique_id, const pipe_t &pipe) {
  log_debug(tt::LogRouter, "pipe: {}:", pipe_unique_id);
  log_debug(tt::LogRouter, "\tinputs:");
  for (auto id : pipe.input_buffer_ids) {
    log_debug(tt::LogRouter, "\t\t{}", id);
  }
  log_debug(tt::LogRouter, "\toutputs:");

  if (pipe.has_multiple_timesteps()) {
    for (const auto &output_buf_ids : pipe.time_multiplexed_output_buffer_ids()) {
      log_debug(tt::LogRouter, "\t\t[");
      for (auto id : output_buf_ids) {
        log_debug(tt::LogRouter, "\t\t\t{}", id);
      }
      log_debug(tt::LogRouter, "\t\t]");
    }
  } else {
    for (auto id : pipe.output_buffer_ids()) {
      log_debug(tt::LogRouter, "\t\t{}", id);
    }
  }
}

void print_buf_dbg(std::uint64_t buf_id, const router_buffer_info_t &buf) {
  log_debug(tt::LogRouter, "buf: {}, location: (c={},y={},x={})", buf_id, buf.core_location().chip, buf.core_location().y, buf.core_location().x);
}

void print_router_graph(const router::Router &router) {
    log_debug(tt::LogModel, "------ PIPES -------");
    for (const auto &[pipe_id, pipe] : router.get_pipes()) {
        print_pipe_dbg(pipe_id, pipe);
    }
    log_debug(tt::LogModel, "------ BUFFERS -------");
    for (const auto &[buf_id, buf] : router.get_buffer_map()) {
        print_buf_dbg(buf_id, buf);
    }

}

void validate_all_ethernet_pipes_are_unicasts(router::Router &router, const std::vector<unique_id_t> &ethernet_pipes_ids) {
    for (const auto &eth_pipe_id : ethernet_pipes_ids) {
        const auto &eth_pipe = router.get_pipes().at(eth_pipe_id);
        ASSERT_EQ(eth_pipe.input_buffer_ids.size(), 1); // Can only unicast over ethernet
        ASSERT_FALSE(eth_pipe.has_multiple_timesteps());
        ASSERT_EQ(eth_pipe.output_buffer_ids().size(), 1);
    }
}

TEST(Net2PipeRouterMultichip, Helper_IsChipToChipPipe_True) {
    const auto &[buf_id_1, buf_1] = generate_dummy_test_router_buffer(tt_cxy_pair(0,1,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_2, buf_2] = generate_dummy_test_router_buffer(tt_cxy_pair(1,1,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_3, buf_3] = generate_dummy_test_router_buffer(tt_cxy_pair(2,1,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_4, buf_4] = generate_dummy_test_router_buffer(tt_cxy_pair(3,1,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_5, buf_5] = generate_dummy_test_router_buffer(tt_cxy_pair(3,1,1), tt::RouterBufferType::Relay);

    const auto &[buf_id_6, buf_6] = generate_dummy_test_router_buffer(tt_cxy_pair(0,2,2), tt::RouterBufferType::Relay);
    const auto &[buf_id_7, buf_7] = generate_dummy_test_router_buffer(tt_cxy_pair(3,2,2), tt::RouterBufferType::Relay);

    const auto &[buf_id_8, buf_8] = generate_dummy_test_router_buffer(tt_cxy_pair(0,3,3), tt::RouterBufferType::Relay);
    const auto &[buf_id_9, buf_9] = generate_dummy_test_router_buffer(tt_cxy_pair(0,4,4), tt::RouterBufferType::Relay);
    const auto &[buf_id_10, buf_10] = generate_dummy_test_router_buffer(tt_cxy_pair(1,4,4), tt::RouterBufferType::Relay);

    const auto &[buf_id_11, buf_11] = generate_dummy_test_router_buffer(tt_cxy_pair(0,3,3), tt::RouterBufferType::Relay);
    const auto &[buf_id_12, buf_12] = generate_dummy_test_router_buffer(tt_cxy_pair(1,5,5), tt::RouterBufferType::Relay);
    const auto &[buf_id_13, buf_13] = generate_dummy_test_router_buffer(tt_cxy_pair(1,6,6), tt::RouterBufferType::Relay);

    const auto &[buf_id_14, buf_14] = generate_dummy_test_router_buffer(tt_cxy_pair(2,3,3), tt::RouterBufferType::Relay);
    const auto &[buf_id_15, buf_15] = generate_dummy_test_router_buffer(tt_cxy_pair(2,4,4), tt::RouterBufferType::Relay);
    const auto &[buf_id_16, buf_16] = generate_dummy_test_router_buffer(tt_cxy_pair(3,6,6), tt::RouterBufferType::Relay);
    const auto &[buf_id_17, buf_17] = generate_dummy_test_router_buffer(tt_cxy_pair(3,7,7), tt::RouterBufferType::Relay);
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{
        {buf_id_1, buf_1},
        {buf_id_2, buf_2},
        {buf_id_3, buf_3},
        {buf_id_4, buf_4},
        {buf_id_5, buf_5},
        {buf_id_6, buf_6},
        {buf_id_7, buf_7},
        {buf_id_8, buf_8},
        {buf_id_9, buf_9},
        {buf_id_10, buf_10},
        {buf_id_11, buf_11},
        {buf_id_12, buf_12},
        {buf_id_13, buf_13},
        {buf_id_14, buf_14},
        {buf_id_15, buf_15},
        {buf_id_16, buf_16},
        {buf_id_17, buf_17}
    };
    auto queue_map = std::unordered_map<unique_id_t,tt_queue_info>{};
    auto op_queue_output_scatter = std::map<std::string, bool>{};
    auto queue_settings_map = std::map<std::string, QueueSettings>{};
    auto test_router = generate_test_router(
        *(wh_soc_desc.get()), 
        4, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map}
    );
    auto router = test_router->router.get();
    const auto &pipe_1 = router->create_unicast_pipe_connection_at_core(buf_id_1, buf_id_2, buf_2.core_location(), 1);
    const auto &pipe_2 = router->create_unicast_pipe_connection_at_core(buf_id_1, buf_id_3, buf_3.core_location(), 1);
    const auto &pipe_3 = router->create_unicast_pipe_connection_at_core(buf_id_1, buf_id_4, buf_4.core_location(), 1);
    const auto &pipe_4 = router->create_unicast_pipe_connection_at_core(buf_id_1, buf_id_5, buf_5.core_location(), 1);
    
    const auto &pipe_5 = router->create_gather_pipe_connection_at_core({buf_id_1, buf_id_6}, buf_id_7, buf_7.core_location(), 1);

    const auto &pipe_6 = router->create_multicast_pipe_connection_at_core(buf_id_8, {buf_id_9, buf_id_10}, buf_10.core_location(), 1);
    const auto &pipe_7 = router->create_multicast_pipe_connection_at_core(buf_id_11, {buf_id_12, buf_id_13}, buf_13.core_location(), 1);

    const auto &pipe_8 = router->create_gather_multicast_pipe_connection_at_core({buf_id_14,buf_id_15}, {buf_id_16, buf_id_17}, buf_16.core_location(), 1);

    ASSERT_TRUE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_1), *router));
    ASSERT_TRUE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_2), *router));
    ASSERT_TRUE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_3), *router));
    ASSERT_TRUE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_4), *router));
    ASSERT_TRUE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_5), *router));
    ASSERT_TRUE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_6), *router));
    ASSERT_TRUE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_7), *router));
    ASSERT_TRUE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_8), *router));

    auto chip_to_chip_pipes_expected = std::unordered_set<unique_id_t>{pipe_1, pipe_2, pipe_3, pipe_4, pipe_5, pipe_6, pipe_7, pipe_8};
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);
    ASSERT_EQ(chip_to_chip_pipes.size(), chip_to_chip_pipes_expected.size());
    for (auto pipe_id : chip_to_chip_pipes) {
        ASSERT_TRUE(chip_to_chip_pipes_expected.find(pipe_id) != chip_to_chip_pipes_expected.end());
    }
}


TEST(Net2PipeRouterMultichip, Helper_IsChipToChipPipe_False) {
    const auto &[buf_id_1, buf_1] = generate_dummy_test_router_buffer(tt_cxy_pair(0,1,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_2, buf_2] = generate_dummy_test_router_buffer(tt_cxy_pair(0,1,1), tt::RouterBufferType::Relay);

    const auto &[buf_id_3, buf_3] = generate_dummy_test_router_buffer(tt_cxy_pair(1,2,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_4, buf_4] = generate_dummy_test_router_buffer(tt_cxy_pair(1,3,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_5, buf_5] = generate_dummy_test_router_buffer(tt_cxy_pair(1,4,1), tt::RouterBufferType::Relay);

    const auto &[buf_id_6, buf_6] = generate_dummy_test_router_buffer(tt_cxy_pair(2,3,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_7, buf_7] = generate_dummy_test_router_buffer(tt_cxy_pair(2,4,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_8, buf_8] = generate_dummy_test_router_buffer(tt_cxy_pair(2,5,1), tt::RouterBufferType::Relay);

    const auto &[buf_id_9, buf_9] = generate_dummy_test_router_buffer(tt_cxy_pair(3,1,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_10, buf_10] = generate_dummy_test_router_buffer(tt_cxy_pair(3,2,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_11, buf_11] = generate_dummy_test_router_buffer(tt_cxy_pair(3,2,1), tt::RouterBufferType::Relay);
    const auto &[buf_id_12, buf_12] = generate_dummy_test_router_buffer(tt_cxy_pair(3,3,1), tt::RouterBufferType::Relay);

    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{
        {buf_id_1, buf_1},
        {buf_id_2, buf_2},
        {buf_id_3, buf_3},
        {buf_id_4, buf_4},
        {buf_id_5, buf_5},
        {buf_id_6, buf_6},
        {buf_id_7, buf_7},
        {buf_id_8, buf_8},
        {buf_id_9, buf_9},
        {buf_id_10, buf_10},
        {buf_id_11, buf_11},
        {buf_id_12, buf_12}
    };
    auto queue_map = std::unordered_map<unique_id_t,tt_queue_info>{};
    auto op_queue_output_scatter = std::map<std::string, bool>{};
    auto queue_settings_map = std::map<std::string, QueueSettings>{};
    auto test_router = generate_test_router(
        *(wh_soc_desc.get()), 
        4, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();
    const auto &pipe_1 = router->create_unicast_pipe_connection_at_core(buf_id_1, buf_id_2, buf_2.core_location(), 1);

    const auto &pipe_2 = router->create_gather_pipe_connection_at_core({buf_id_3, buf_id_4}, buf_id_5, buf_5.core_location(), 1);

    const auto &pipe_3 = router->create_multicast_pipe_connection_at_core(buf_id_6, {buf_id_7, buf_id_8}, buf_8.core_location(), 1);

    const auto &pipe_4 = router->create_gather_multicast_pipe_connection_at_core({buf_id_9,buf_id_10}, {buf_id_11, buf_id_12}, buf_12.core_location(), 1);

    /// ---------- VALIDATION AND CHECKING BELOW ---------- //
    EXPECT_FALSE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_1), *router));
    EXPECT_FALSE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_2), *router));
    EXPECT_FALSE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_3), *router));
    EXPECT_FALSE(router::is_chip_to_chip_pipe(router->get_pipes().at(pipe_4), *router));

    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);
    ASSERT_EQ(chip_to_chip_pipes.size(), 0);
}

TEST(Net2PipeRouterMultichip, TestRouteChipToChip_TwoEthernetLinks) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();

    int c0_buf_idx = 0;
    int c0_eth_buf_idx = 0;
    int c1_buf_idx = 0;
    int c1_eth_buf_idx = 0;
    int q_idx = 0;
    // TEST 1 ethernet stream
    
    auto chip0_buf_id = chip_0_buffers.at(c0_buf_idx++).id;
    auto chip1_buf_id = chip_1_buffers.at(c1_buf_idx++).id;
    
    unique_id_t pipe_id_identified_as_chip_to_chip = router->create_unicast_pipe_connection_at_core(chip0_buf_id, chip1_buf_id, router->get_buffer_location(chip1_buf_id), 1);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);


    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), pipe_id_identified_as_chip_to_chip);

    router::insert_ethernet_buffers_for_chip_to_chip_pipes(*router, router->get_cluster_graph());


    /// ---------- VALIDATION AND CHECKING BELOW ---------- //
    const auto &chip0_non_eth_buffer = chip0_buf_id;
    const auto &chip0_pipes_to_eth_sender_core = router->get_buffer_output_pipes().at(chip0_non_eth_buffer);
    ASSERT_EQ(chip0_pipes_to_eth_sender_core.size(), 1);

    const auto &chip1_non_eth_buffer = chip1_buf_id;
    auto chip1_eth_to_buf_pipe_id = router->get_buffer_input_pipes().at(chip1_buf_id);
    const pipe_t &chip1_eth_to_buf_pipe = router->get_pipes().at(chip1_eth_to_buf_pipe_id);
    ASSERT_EQ(chip1_eth_to_buf_pipe.input_buffer_ids.size(),1);
    unique_id_t eth_receiver_buf_id = chip1_eth_to_buf_pipe.input_buffer_ids.at(0);
    ASSERT_FALSE(chip1_eth_to_buf_pipe.has_multiple_timesteps());
    ASSERT_EQ(chip1_eth_to_buf_pipe.output_buffer_ids().size(),1);

    unique_id_t pipe_id_chip0_buf_to_eth = *(chip0_pipes_to_eth_sender_core.begin());
    const pipe_t &pipe_chip0_buf_to_eth = router->get_pipes().at(pipe_id_chip0_buf_to_eth);
    ASSERT_EQ(pipe_chip0_buf_to_eth.input_buffer_ids.size(), 1);
    ASSERT_FALSE(pipe_chip0_buf_to_eth.has_multiple_timesteps());
    ASSERT_EQ(pipe_chip0_buf_to_eth.output_buffer_ids().size(), 1);
    const unique_id_t eth_sender_buf_id = *(pipe_chip0_buf_to_eth.output_buffer_ids().begin());
    const auto &eth_sender_buf = router->get_buffer_map().at(eth_sender_buf_id);
    ASSERT_TRUE(std::find(soc_descriptor.ethernet_cores.begin(), soc_descriptor.ethernet_cores.end(), tt_xy_pair(eth_sender_buf.core_location().x, eth_sender_buf.core_location().y)) != soc_descriptor.ethernet_cores.end());
    const unique_id_t eth_sender_buf_input_pipe_id = router->get_buffer_input_pipes().at(eth_sender_buf_id);
    ASSERT_EQ(pipe_id_chip0_buf_to_eth, eth_sender_buf_input_pipe_id);
    ASSERT_EQ(eth_sender_buf.core_location().chip, 0);

    const auto &eth_to_eth_pipe_ids = router->get_buffer_output_pipes().at(eth_sender_buf_id);
    ASSERT_EQ(eth_to_eth_pipe_ids.size(), 1);
    const pipe_t &eth_to_eth_pipe = router->get_pipes().at(*(eth_to_eth_pipe_ids.begin()));
    ASSERT_EQ(eth_to_eth_pipe.input_buffer_ids.size(), 1);
    ASSERT_FALSE(eth_to_eth_pipe.has_multiple_timesteps());
    ASSERT_EQ(eth_to_eth_pipe.output_buffer_ids().size(), 1);
    ASSERT_EQ(eth_to_eth_pipe.input_buffer_ids.at(0), eth_sender_buf_id);
    ASSERT_EQ(*(eth_to_eth_pipe.output_buffer_ids().begin()), eth_receiver_buf_id);
}

TEST(Net2PipeRouterMultichip, TestRouteChipToChip_TwoChipToChipGather) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();
    std::vector<unique_id_t> chip_0_buffer_ids = {};
    auto id_from_test_buf_entry = [](auto &test_buf_entry) { return test_buf_entry.id; };
    std::transform(chip_0_buffers.begin(), chip_0_buffers.end(), std::back_inserter(chip_0_buffer_ids), id_from_test_buf_entry);
    std::vector<unique_id_t> chip_1_buffer_ids = {};
    std::transform(chip_1_buffers.begin(), chip_1_buffers.end(), std::back_inserter(chip_1_buffer_ids), id_from_test_buf_entry);
    unique_id_t pipe_id_identified_as_chip_to_chip = router->create_gather_pipe_connection_at_core(chip_0_buffer_ids, chip_1_buffer_ids.at(0), router->get_buffer_location(chip_0_buffer_ids.at(0)), 1);


    log_debug(tt::LogModel, "------ OLD GRAPH -------");
    print_router_graph(*router);

    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), pipe_id_identified_as_chip_to_chip);

    router::insert_ethernet_buffers_for_chip_to_chip_pipes(*router, router->get_cluster_graph());

    const auto &ethernet_pipes = collect_chip_to_chip_pipes(*router);

    /// ---------- VALIDATION AND CHECKING BELOW ---------- //
    // This checking assumes we don't do anything exotic like breaking pipes into more eth streams than there are inputs/outputs
    bool gather_performed_on_producer = ethernet_pipes.size() == 1;
    bool gather_performed_on_consumer = ethernet_pipes.size() == chip_0_buffer_ids.size();
    ASSERT_TRUE(
        gather_performed_on_consumer || // Gather is performed on consumer side and all inputs sent over eth separately
        gather_performed_on_producer // OR Gather is performed on producer chip and gather result is sent over ethernet as a single stream
    );

    validate_all_ethernet_pipes_are_unicasts(*router, ethernet_pipes);

    log_debug(tt::LogModel, "------ NEW GRAPH -------");
    print_router_graph(*router);

    // The validation here assumes the same buffer does not both gather from workers onto the eth core and feed directly into unicast over eth - it assumes an intermediate buffer
    if (gather_performed_on_producer) {
        unique_id_t eth_pipe_id = ethernet_pipes.at(0);
        const pipe_t &eth_pipe = router->get_pipes().at(eth_pipe_id);
        unique_id_t sender_eth_buf_id = eth_pipe.input_buffer_ids.at(0);
        auto sender_input_pipe_id = router->get_buffer_input_pipes().at(sender_eth_buf_id);
        const pipe_t &sender_input_pipe = router->get_pipes().at(sender_input_pipe_id);
        unique_id_t gather_pipe_id = sender_input_pipe_id;
        if (sender_input_pipe.input_buffer_ids.size() != chip_0_buffer_ids.size()) {
            ASSERT_EQ(sender_input_pipe.input_buffer_ids.size(), 1);
            unique_id_t gather_output_buf_id = sender_input_pipe.input_buffer_ids.at(0);
            gather_pipe_id = router->get_buffer_input_pipes().at(gather_output_buf_id);
        }
        const auto &gather_pipe = router->get_pipes().at(gather_pipe_id);
        ASSERT_FALSE(gather_pipe.has_multiple_timesteps());
        ASSERT_EQ(gather_pipe.output_buffer_ids().size(), 1);
        ASSERT_EQ(0, router->get_buffer_map().at(gather_pipe.output_buffer_ids().at(0)).core_location().chip);
        ASSERT_EQ(chip_0_buffer_ids.size(), gather_pipe.input_buffer_ids.size());
        ASSERT_TRUE(std::equal(chip_0_buffer_ids.begin(), chip_0_buffer_ids.end(), gather_pipe.input_buffer_ids.begin()));

        unique_id_t receiver_eth_buf_id = eth_pipe.output_buffer_ids().at(0);
        const auto &receiver_out_pipe_ids = router->get_buffer_output_pipes().at(receiver_eth_buf_id);
        ASSERT_EQ(receiver_out_pipe_ids.size(), 1);
        const pipe_t &receiver_out_pipe = router->get_pipes().at(*(receiver_out_pipe_ids.begin()));
        ASSERT_EQ(receiver_out_pipe.input_buffer_ids.size(), 1);
        ASSERT_FALSE(receiver_out_pipe.has_multiple_timesteps());
        ASSERT_EQ(receiver_out_pipe.output_buffer_ids().size(), chip_1_buffer_ids.size());
        ASSERT_TRUE(std::equal(chip_1_buffer_ids.begin(), chip_1_buffer_ids.end(), receiver_out_pipe.output_buffer_ids().begin()));

    } else  {
        log_assert(false, "Checks not implemented for this path");
    }
}


TEST(Net2PipeRouterMultichip, TestRouteChipToChip_TwoChipToChipMulticast) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 1, 2), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();
    std::vector<unique_id_t> chip_0_buffer_ids = {};
    auto id_from_test_buf_entry = [](auto &test_buf_entry) { return test_buf_entry.id; };
    std::transform(chip_0_buffers.begin(), chip_0_buffers.end(), std::back_inserter(chip_0_buffer_ids), id_from_test_buf_entry);
    std::vector<unique_id_t> chip_1_buffer_ids = {};
    std::transform(chip_1_buffers.begin(), chip_1_buffers.end(), std::back_inserter(chip_1_buffer_ids), id_from_test_buf_entry);

    unique_id_t pipe_id_identified_as_chip_to_chip = router->create_multicast_pipe_connection_at_core(chip_0_buffer_ids.at(0), chip_1_buffer_ids, router->get_buffer_location(chip_1_buffer_ids.at(0)), 1);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), pipe_id_identified_as_chip_to_chip);

    log_debug(tt::LogModel, "------ OLD GRAPH -------");
    print_router_graph(*router);
    router::insert_ethernet_buffers_for_chip_to_chip_pipes(*router, router->get_cluster_graph());

    log_debug(tt::LogModel, "------ NEW GRAPH -------");
    print_router_graph(*router);
    const auto &ethernet_pipes = collect_chip_to_chip_pipes(*router);

    /// ---------- VALIDATION AND CHECKING BELOW ---------- //
    // This checking assumes we don't do anything exotic like breaking pipes into more eth streams than there are inputs/outputs
    bool multicast_performed_on_consumer = ethernet_pipes.size() == 1;
    bool multicast_performed_on_producer = ethernet_pipes.size() == chip_1_buffer_ids.size();
    ASSERT_TRUE(
        multicast_performed_on_consumer || // multicast is performed on consumer side and all inputs sent over eth separately
        multicast_performed_on_producer // OR multicast is performed on producer chip and multicast result is sent over ethernet as a single stream
    );

    validate_all_ethernet_pipes_are_unicasts(*router, ethernet_pipes);

    // The validation here assumes the same buffer does not both receive from eth and feed directly into multicast
    if (multicast_performed_on_consumer) {
        unique_id_t eth_pipe_id = ethernet_pipes.at(0);
        const pipe_t &eth_pipe = router->get_pipes().at(eth_pipe_id);
        unique_id_t sender_eth_buf_id = eth_pipe.input_buffer_ids.at(0);
        auto sender_input_pipe_id = router->get_buffer_input_pipes().at(sender_eth_buf_id);
        const pipe_t &sender_input_pipe = router->get_pipes().at(sender_input_pipe_id);
        ASSERT_EQ(sender_input_pipe.input_buffer_ids.size(), chip_0_buffer_ids.size());
        ASSERT_FALSE(sender_input_pipe.has_multiple_timesteps());
        ASSERT_EQ(sender_input_pipe.output_buffer_ids().size(), 1);
        ASSERT_EQ(chip_0_buffer_ids.size(), sender_input_pipe.input_buffer_ids.size());
        ASSERT_TRUE(std::equal(chip_0_buffer_ids.begin(), chip_0_buffer_ids.end(), sender_input_pipe.input_buffer_ids.begin()));

        unique_id_t receiver_eth_buf_id = eth_pipe.output_buffer_ids().at(0);
        const auto &receiver_out_pipe_ids = router->get_buffer_output_pipes().at(receiver_eth_buf_id);
        ASSERT_EQ(receiver_out_pipe_ids.size(), 1);
        const pipe_t &receiver_out_pipe = router->get_pipes().at(*(receiver_out_pipe_ids.begin()));
        ASSERT_EQ(receiver_out_pipe.input_buffer_ids.size(), 1);
        unique_id_t multicast_pipe_id = *(receiver_out_pipe_ids.begin());
        if (receiver_out_pipe.output_buffer_ids().size() != chip_1_buffer_ids.size()) {
            ASSERT_EQ(receiver_out_pipe.output_buffer_ids().size(), 1);
            unique_id_t multicast_input_buf_id = receiver_out_pipe.output_buffer_ids().at(0);
            const auto &out_pipes = router->get_buffer_output_pipes().at(multicast_input_buf_id);
            multicast_pipe_id = *(out_pipes.begin());
        }
        const auto &multicast_pipe = router->get_pipes().at(multicast_pipe_id);
        ASSERT_FALSE(multicast_pipe.has_multiple_timesteps());
        ASSERT_EQ(multicast_pipe.output_buffer_ids().size(), chip_1_buffer_ids.size());
        ASSERT_TRUE(std::equal(chip_1_buffer_ids.begin(), chip_1_buffer_ids.end(), multicast_pipe.output_buffer_ids().begin()));

    } else {
        log_assert(false, "Checks not implemented for this path");
    }
}

TEST(Net2PipeRouterMultichip, TestRouteChipToChip_TwoChipToChipGatherMulticast) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 1, 2), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();
    std::vector<unique_id_t> chip_0_buffer_ids = {};
    auto id_from_test_buf_entry = [](auto &test_buf_entry) { return test_buf_entry.id; };
    std::transform(chip_0_buffers.begin(), chip_0_buffers.end(), std::back_inserter(chip_0_buffer_ids), id_from_test_buf_entry);
    std::vector<unique_id_t> chip_1_buffer_ids = {};
    std::transform(chip_1_buffers.begin(), chip_1_buffers.end(), std::back_inserter(chip_1_buffer_ids), id_from_test_buf_entry);

    unique_id_t pipe_id_identified_as_chip_to_chip = router->create_gather_multicast_pipe_connection_at_core(chip_0_buffer_ids, chip_1_buffer_ids, router->get_buffer_location(chip_1_buffer_ids.at(0)), 1);
    const auto &chip_to_chip_pipes = collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), pipe_id_identified_as_chip_to_chip);

    log_debug(tt::LogModel, "------ OLD GRAPH -------");
    print_router_graph(*router);
    router::insert_ethernet_buffers_for_chip_to_chip_pipes(*router, router->get_cluster_graph());

    /// ---------- VALIDATION AND CHECKING BELOW ---------- //
    log_debug(tt::LogModel, "------ NEW GRAPH -------");
    print_router_graph(*router);

    const auto &ethernet_pipes = collect_chip_to_chip_pipes(*router);
    bool gather_on_producer_and_multicast_on_consumer = ethernet_pipes.size() == 1;
    bool gather_and_multicast_on_consumer = ethernet_pipes.size() == chip_0_buffer_ids.size();
    bool gather_and_multicast_on_producer = ethernet_pipes.size() == chip_1_buffer_ids.size();
    ASSERT_TRUE(
        gather_on_producer_and_multicast_on_consumer || 
        gather_and_multicast_on_consumer ||
        gather_and_multicast_on_producer
    );

    validate_all_ethernet_pipes_are_unicasts(*router, ethernet_pipes);

    if (gather_on_producer_and_multicast_on_consumer) {
        ASSERT_EQ(ethernet_pipes.size(), 1);
        const auto &ethernet_pipe = router->get_pipes().at(ethernet_pipes.at(0));
        unique_id_t eth_sender_buf_id = ethernet_pipe.input_buffer_ids.at(0);
        unique_id_t input_to_eth_pipe_id = router->get_buffer_input_pipes().at(eth_sender_buf_id);
        const auto &input_to_eth_pipe = router->get_pipes().at(input_to_eth_pipe_id);
        
        bool gathers_directly_into_eth_buf = input_to_eth_pipe.input_buffer_ids.size() > 1;
        unique_id_t gather_pipe_id = input_to_eth_pipe_id;
        if (!gathers_directly_into_eth_buf) {
            ASSERT_EQ(input_to_eth_pipe.input_buffer_ids.size(), 1);
            gather_pipe_id = router->get_buffer_input_pipes().at(input_to_eth_pipe.input_buffer_ids.at(0));
        }

        const auto &gather_pipe = router->get_pipes().at(gather_pipe_id);
        ASSERT_EQ(gather_pipe.input_buffer_ids.size(), chip_0_buffer_ids.size());
        ASSERT_TRUE(std::equal(std::begin(gather_pipe.input_buffer_ids), std::end(gather_pipe.input_buffer_ids), std::begin(chip_0_buffer_ids)));
        ASSERT_FALSE(gather_pipe.has_multiple_timesteps());
        ASSERT_EQ(gather_pipe.output_buffer_ids().size(), 1);

        unique_id_t eth_receiver_buf_id = ethernet_pipe.output_buffer_ids().at(0);
        const auto &output_from_eth_pipe_ids = router->get_buffer_output_pipes().at(eth_receiver_buf_id);
        ASSERT_EQ(output_from_eth_pipe_ids.size(), 1);
        unique_id_t output_from_eth_pipe_id = *(output_from_eth_pipe_ids.begin());
        const auto &output_from_eth_pipe = router->get_pipes().at(output_from_eth_pipe_id);

        bool multicasts_directly_from_eth_buf = output_from_eth_pipe.output_buffer_ids().size() > 1;
        unique_id_t multicast_pipe_id = output_from_eth_pipe_id;
        if (!multicasts_directly_from_eth_buf) {
            ASSERT_EQ(output_from_eth_pipe.output_buffer_ids().size(), 1);
            const auto &out_pipes = router->get_buffer_output_pipes().at(output_from_eth_pipe.output_buffer_ids().at(0));
            ASSERT_EQ(out_pipes.size(), 1);
            multicast_pipe_id = *(out_pipes.begin());
        }

        const auto &multicast_pipe = router->get_pipes().at(multicast_pipe_id);
        ASSERT_EQ(multicast_pipe.input_buffer_ids.size(), 1);
        ASSERT_FALSE(multicast_pipe.has_multiple_timesteps());
        ASSERT_EQ(multicast_pipe.output_buffer_ids().size(), chip_1_buffer_ids.size());
        ASSERT_TRUE(std::equal(std::begin(multicast_pipe.output_buffer_ids()), std::end(multicast_pipe.output_buffer_ids()), std::begin(chip_1_buffer_ids)));

    } else {
        log_assert(false, "Checks unimplemented for gather_and_multicast_on_consumer and gather_and_multicast_on_producer cases");
    }
}


TEST(Net2PipeRouterMultichip, TestRouteChipToChip_TwoChipToChipNoDramTMWithUnicasts) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 1, 2), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();
    std::vector<unique_id_t> chip_0_buffer_ids = {};
    auto id_from_test_buf_entry = [](auto &test_buf_entry) { return test_buf_entry.id; };
    std::transform(chip_0_buffers.begin(), chip_0_buffers.end(), std::back_inserter(chip_0_buffer_ids), id_from_test_buf_entry);
    std::vector<unique_id_t> chip_1_buffer_ids = {};
    std::transform(chip_1_buffers.begin(), chip_1_buffers.end(), std::back_inserter(chip_1_buffer_ids), id_from_test_buf_entry);

    const auto timestep_output_buffers = time_multiplexed_outputs_t{
        {chip_1_buffer_ids.at(0)}, 
        {chip_1_buffer_ids.at(1)}
    };
    auto pipe_locations = std::vector<tt_cxy_pair>{};
    for (const auto &timestep_bufs : timestep_output_buffers) {
        pipe_locations.push_back(router->get_buffer_location(timestep_bufs.at(0)));
    }
    unique_id_t pipe_id_identified_as_chip_to_chip = router->create_scatter_pipe_connection_at_cores(chip_0_buffer_ids, timestep_output_buffers, pipe_locations);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), pipe_id_identified_as_chip_to_chip);

    log_debug(tt::LogModel, "------ OLD GRAPH -------");
    print_router_graph(*router);
    router::insert_ethernet_buffers_for_chip_to_chip_pipes(*router, router->get_cluster_graph());
    const auto &chip_to_chip_pipes_after_pass = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes_after_pass.size(), 2);

    // 2 eth pipes -> one has input of chip_0_buffers.at(0), other has chip_0_buffers.at(1)
    // each must be unicast, chip

    const auto &tm_pipe = router->get_pipes().at(pipe_id_identified_as_chip_to_chip); // currently assume the TM pipe isn't deleted
    const auto &timestep_output_buf_ids = tm_pipe.time_multiplexed_output_buffer_ids();
    for (int t = 0; t < timestep_output_buf_ids.size(); t++) {
        ASSERT_EQ(timestep_output_buf_ids[t].size(), 1);
        unique_id_t out_buf_id = timestep_output_buf_ids[t][0];
        const auto &out_pipes = router->get_buffer_output_pipes().at(out_buf_id);
        ASSERT_EQ(out_pipes.size(), 1);
        unique_id_t out_pipe_id = *(out_pipes.begin());
        const auto &out_pipe = router->get_pipes().at(out_pipe_id);

        ASSERT_FALSE(out_pipe.has_multiple_timesteps());
        ASSERT_EQ(out_pipe.output_buffer_ids().size(), 1);
        unique_id_t eth_pipe_id = out_pipe_id;
        if (!router::is_chip_to_chip_pipe(out_pipe, *router)) {
            const auto &next_out_pipes = router->get_buffer_output_pipes().at(out_pipe.output_buffer_ids().at(0));
            ASSERT_EQ(next_out_pipes.size(), 1);
            eth_pipe_id = *(next_out_pipes.begin());
            const auto &next_output_pipe = router->get_pipes().at(eth_pipe_id);
            ASSERT_FALSE(next_output_pipe.has_multiple_timesteps());
            ASSERT_EQ(next_output_pipe.output_buffer_ids().size(), 1);
            ASSERT_TRUE(is_chip_to_chip_pipe(next_output_pipe, *router));
        }

        const auto &eth_pipe = router->get_pipes().at(eth_pipe_id);
        unique_id_t pipe_id_to_c1_buf = *(router->get_buffer_output_pipes().at(eth_pipe.output_buffer_ids().at(0)).begin());
        const auto &pipe_to_c1_buf = router->get_pipes().at(pipe_id_to_c1_buf);

        ASSERT_EQ(pipe_to_c1_buf.output_buffer_ids().size(), timestep_output_buffers.at(t).size());
        ASSERT_TRUE(std::equal(
            std::begin(pipe_to_c1_buf.output_buffer_ids()), std::end(pipe_to_c1_buf.output_buffer_ids()),
            std::begin(timestep_output_buffers.at(t)), std::end(timestep_output_buffers.at(t))
        ));
            
    }
    
}



TEST(Net2PipeRouterMultichip, TestRouteChipToChip_TwoChipToChipNoDramTMWithMulticasts) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 1, 2), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 2, 2), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 3, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 3, 2), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();
    std::vector<unique_id_t> chip_0_buffer_ids = {};
    auto id_from_test_buf_entry = [](auto &test_buf_entry) { return test_buf_entry.id; };
    std::transform(chip_0_buffers.begin(), chip_0_buffers.end(), std::back_inserter(chip_0_buffer_ids), id_from_test_buf_entry);
    std::vector<unique_id_t> chip_1_buffer_ids = {};
    std::transform(chip_1_buffers.begin(), chip_1_buffers.end(), std::back_inserter(chip_1_buffer_ids), id_from_test_buf_entry);

    const auto timestep_output_buffers = time_multiplexed_outputs_t{
        {chip_1_buffer_ids.at(0), chip_1_buffer_ids.at(1)},
        {chip_1_buffer_ids.at(2), chip_1_buffer_ids.at(3)},
        {chip_1_buffer_ids.at(4), chip_1_buffer_ids.at(5)}
    };
    auto pipe_locations = std::vector<tt_cxy_pair>{};
    for (const auto &timestep_bufs : timestep_output_buffers) {
        pipe_locations.push_back(router->get_buffer_location(timestep_bufs.at(0)));
    }
    unique_id_t pipe_id_identified_as_chip_to_chip = router->create_scatter_pipe_connection_at_cores(chip_0_buffer_ids, timestep_output_buffers, pipe_locations);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), pipe_id_identified_as_chip_to_chip);

    log_debug(tt::LogModel, "------ OLD GRAPH -------");
    print_router_graph(*router);
    router::insert_ethernet_buffers_for_chip_to_chip_pipes(*router, router->get_cluster_graph());
    const auto &chip_to_chip_pipes_after_pass = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes_after_pass.size(), 3);

    // 2 eth pipes -> one has input of chip_0_buffers.at(0), other has chip_0_buffers.at(1)
    // each must be unicast, chip

    const auto &tm_pipe = router->get_pipes().at(pipe_id_identified_as_chip_to_chip); // currently assume the TM pipe isn't deleted
    const auto &timestep_output_buf_ids = tm_pipe.time_multiplexed_output_buffer_ids();
    for (int t = 0; t < timestep_output_buf_ids.size(); t++) {
        ASSERT_EQ(timestep_output_buf_ids[t].size(), 1);
        unique_id_t out_buf_id = timestep_output_buf_ids[t][0];
        const auto &out_pipes = router->get_buffer_output_pipes().at(out_buf_id);
        ASSERT_EQ(out_pipes.size(), 1);
        unique_id_t out_pipe_id = *(out_pipes.begin());
        const auto &out_pipe = router->get_pipes().at(out_pipe_id);

        ASSERT_FALSE(out_pipe.has_multiple_timesteps());
        ASSERT_EQ(out_pipe.output_buffer_ids().size(), 1);
        unique_id_t eth_pipe_id = out_pipe_id;
        if (!router::is_chip_to_chip_pipe(out_pipe, *router)) {
            const auto &next_out_pipes = router->get_buffer_output_pipes().at(out_pipe.output_buffer_ids().at(0));
            ASSERT_EQ(next_out_pipes.size(), 1);
            eth_pipe_id = *(next_out_pipes.begin());
            const auto &next_output_pipe = router->get_pipes().at(eth_pipe_id);
            ASSERT_FALSE(next_output_pipe.has_multiple_timesteps());
            ASSERT_EQ(next_output_pipe.output_buffer_ids().size(), 1);
            ASSERT_TRUE(is_chip_to_chip_pipe(next_output_pipe, *router));
        }

        const auto &eth_pipe = router->get_pipes().at(eth_pipe_id);
        unique_id_t pipe_id_to_c1_bufs = *(router->get_buffer_output_pipes().at(eth_pipe.output_buffer_ids().at(0)).begin());
        const auto &pipe_to_c1_bufs = router->get_pipes().at(pipe_id_to_c1_bufs);

        ASSERT_EQ(pipe_to_c1_bufs.output_buffer_ids().size(), 1); //timestep_output_buffers.at(t).size());
        // ASSERT_TRUE(std::equal(
        //     std::begin(pipe_to_c1_bufs.output_buffer_ids()), std::end(pipe_to_c1_bufs.output_buffer_ids()),
        //     std::begin(timestep_output_buffers.at(t)), std::end(timestep_output_buffers.at(t))
        // ));
            
    }

}

TEST(Net2PipeRouterMultichip, TestRouteChipToChip_TwoChipToChipNoDramTMWithMixOfUnicastsAndMulticasts) {

    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 1, 2), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 2, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 2, 2), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 3, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 3, 2), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();
    std::vector<unique_id_t> chip_0_buffer_ids = {};
    auto id_from_test_buf_entry = [](auto &test_buf_entry) { return test_buf_entry.id; };
    std::transform(chip_0_buffers.begin(), chip_0_buffers.end(), std::back_inserter(chip_0_buffer_ids), id_from_test_buf_entry);
    std::vector<unique_id_t> chip_1_buffer_ids = {};
    std::transform(chip_1_buffers.begin(), chip_1_buffers.end(), std::back_inserter(chip_1_buffer_ids), id_from_test_buf_entry);

    const auto timestep_output_buffers = time_multiplexed_outputs_t{
        {chip_1_buffer_ids.at(0), chip_1_buffer_ids.at(1), chip_1_buffer_ids.at(2)},
        {chip_1_buffer_ids.at(3)},
        {chip_1_buffer_ids.at(4), chip_1_buffer_ids.at(5)}
    };
    auto pipe_locations = std::vector<tt_cxy_pair>{};
    for (const auto &timestep_bufs : timestep_output_buffers) {
        pipe_locations.push_back(router->get_buffer_location(timestep_bufs.at(0)));
    }
    unique_id_t pipe_id_identified_as_chip_to_chip = router->create_scatter_pipe_connection_at_cores(chip_0_buffer_ids, timestep_output_buffers, pipe_locations);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), pipe_id_identified_as_chip_to_chip);

    log_debug(tt::LogModel, "------ OLD GRAPH -------");
    print_router_graph(*router);
    router::insert_ethernet_buffers_for_chip_to_chip_pipes(*router, router->get_cluster_graph());
    const auto &chip_to_chip_pipes_after_pass = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes_after_pass.size(), 3);

    // 2 eth pipes -> one has input of chip_0_buffers.at(0), other has chip_0_buffers.at(1)
    // each must be unicast, chip

    const auto &tm_pipe = router->get_pipes().at(pipe_id_identified_as_chip_to_chip); // currently assume the TM pipe isn't deleted
    const auto &timestep_output_buf_ids = tm_pipe.time_multiplexed_output_buffer_ids();
    for (int t = 0; t < timestep_output_buf_ids.size(); t++) {
        ASSERT_EQ(timestep_output_buf_ids[t].size(), 1);
        unique_id_t out_buf_id = timestep_output_buf_ids[t][0];
        const auto &out_pipes = router->get_buffer_output_pipes().at(out_buf_id);
        ASSERT_EQ(out_pipes.size(), 1);
        unique_id_t out_pipe_id = *(out_pipes.begin());
        const auto &out_pipe = router->get_pipes().at(out_pipe_id);

        ASSERT_FALSE(out_pipe.has_multiple_timesteps());
        ASSERT_EQ(out_pipe.output_buffer_ids().size(), 1);
        unique_id_t eth_pipe_id = out_pipe_id;
        if (!router::is_chip_to_chip_pipe(out_pipe, *router)) {
            const auto &next_out_pipes = router->get_buffer_output_pipes().at(out_pipe.output_buffer_ids().at(0));
            ASSERT_EQ(next_out_pipes.size(), 1);
            eth_pipe_id = *(next_out_pipes.begin());
            const auto &next_output_pipe = router->get_pipes().at(eth_pipe_id);
            ASSERT_FALSE(next_output_pipe.has_multiple_timesteps());
            ASSERT_EQ(next_output_pipe.output_buffer_ids().size(), 1);
            ASSERT_TRUE(is_chip_to_chip_pipe(next_output_pipe, *router));
        }

        const auto &eth_pipe = router->get_pipes().at(eth_pipe_id);
        unique_id_t pipe_id_to_c1_bufs = *(router->get_buffer_output_pipes().at(eth_pipe.output_buffer_ids().at(0)).begin());
        const auto &pipe_to_c1_bufs = router->get_pipes().at(pipe_id_to_c1_bufs);

        ASSERT_EQ(pipe_to_c1_bufs.output_buffer_ids().size(), 1); //timestep_output_buffers.at(t).size());
        // ASSERT_TRUE(std::equal(
        //     std::begin(pipe_to_c1_bufs.output_buffer_ids()), std::end(pipe_to_c1_bufs.output_buffer_ids()),
        //     std::begin(timestep_output_buffers.at(t)), std::end(timestep_output_buffers.at(t))
        // ));
            
    }
}

TEST(Net2PipeRouterMultichip, TestRouteChipToChip_MultiHopUnicast_4chips) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_3_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(3, 1, 1), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        4, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();

    int c0_buf_idx = 0;
    int c0_eth_buf_idx = 0;
    int c3_buf_idx = 0;
    int c3_eth_buf_idx = 0;
    int q_idx = 0;
    // TEST 1 ethernet stream
    
    auto chip0_buf_id = chip_0_buffers.at(c0_buf_idx++).id;
    auto chip3_buf_id = chip_3_buffers.at(c3_buf_idx++).id;
    
    unique_id_t pipe_id_identified_as_chip_to_chip = router->create_unicast_pipe_connection_at_core(chip0_buf_id, chip3_buf_id, router->get_buffer_location(chip3_buf_id), 1);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);


    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), pipe_id_identified_as_chip_to_chip);

    router::insert_ethernet_buffers_for_chip_to_chip_pipes(*router, router->get_cluster_graph());

    const auto &chip_to_chip_pipes_after = router::collect_chip_to_chip_pipes(*router);
    ASSERT_EQ(chip_to_chip_pipes_after.size(), 3);

    /// ---------- VALIDATION AND CHECKING BELOW ---------- //
    // Validate we get from `chip0_buf_id` to `chip3_buf_id` via pipes.
    bool routed_to_dest = false;
    unique_id_t current_buf_id = chip0_buf_id;
    while (!routed_to_dest || (router->get_buffer_output_pipes().at(current_buf_id).size() > 0)) {
        const auto &output_pipe_ids = router->get_buffer_output_pipes().at(current_buf_id);
        ASSERT_EQ(output_pipe_ids.size(), 1);
        const auto &pipe = router->get_pipe(*(output_pipe_ids.begin()));
        ASSERT_TRUE(pipe.input_buffer_ids.size() == 1);
        ASSERT_TRUE(pipe.output_buffer_ids().size() == 1);

        const auto &src_chip = router->get_buffer_location(current_buf_id).chip;
        unique_id_t dest_buf_id = pipe.output_buffer_ids().at(0);
        const auto &dest_chip = router->get_buffer_location(dest_buf_id).chip;
        ASSERT_TRUE((src_chip == dest_chip) || (src_chip + 1 == dest_chip));
        routed_to_dest = (dest_buf_id == chip3_buf_id);

        current_buf_id = dest_buf_id;
    }
}