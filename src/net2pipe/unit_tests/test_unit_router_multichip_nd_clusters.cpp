// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "router/router_multichip.h"
#include "router/router_multichip_routing_algorithms.h"
#include "test_unit_common.hpp"
#include "router/router_passes_common.h"
#include "gtest/gtest.h"

#include <unordered_map>

using router::router_buffer_info_t;
using router::unique_id_t;
using router::pipe_t;
using router::time_multiplexed_outputs_t;

std::tuple<std::unique_ptr<TestRouter>, unique_id_t> create_router_with_chip_to_chip_pipe(const std::string &cluster_description_file_path, const tt_cxy_pair &src_buf_location, const tt_cxy_pair &dest_buf_location) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {src_buf_location, tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {dest_buf_location, tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        cluster_description_file_path, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map}
    );
    auto router = test_router->router.get();

    int c0_buf_idx = 0;
    int c0_eth_buf_idx = 0;
    int c1_buf_idx = 0;
    int c1_eth_buf_idx = 0;
    int q_idx = 0;
    // TEST 1 ethernet stream
    
    auto chip0_buf_id = chip_0_buffers.at(c0_buf_idx++).id;
    auto chip1_buf_id = chip_1_buffers.at(c1_buf_idx++).id;
    
    unique_id_t pipe_id_identified_as_chip_to_chip = router->create_unicast_pipe_connection_at_core(chip0_buf_id, chip1_buf_id, router->get_buffer_location(chip1_buf_id),  1);

    return {std::move(test_router), pipe_id_identified_as_chip_to_chip};
}

TEST(RouterMultichip, TestShortestPathRouting_3x3Cluster_route_0_3_6) {
    auto router_uniq_ptr__c2c_pipe_id = create_router_with_chip_to_chip_pipe(
        "src/net2pipe/unit_tests/cluster_descriptions/wormhole_3x3_cluster.yaml", 
        tt_cxy_pair(0,0,0), 
        tt_cxy_pair(6,0,0)
    );
    auto test_router = std::move(std::get<0>(router_uniq_ptr__c2c_pipe_id));
    auto router = test_router->router.get();
    auto chip_to_chip_pipe = std::get<1>(router_uniq_ptr__c2c_pipe_id);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), chip_to_chip_pipe);

    const auto &route = router::generate_chip_to_chip_route_for_pipe(*router, router->get_cluster_graph(), chip_to_chip_pipe);

    ASSERT_EQ(route.size(), 2); // size == # hops
    const auto &golden_route = std::vector<std::pair<chip_id_t, chip_id_t>>{ {0,3}, {3,6} };
    ASSERT_EQ(route, golden_route);
}

TEST(RouterMultichip, TestShortestPathRouting_3x3Cluster_route_0_1_2) {
    auto router_uniq_ptr__c2c_pipe_id = create_router_with_chip_to_chip_pipe(
        "src/net2pipe/unit_tests/cluster_descriptions/wormhole_3x3_cluster.yaml", 
        tt_cxy_pair(0,0,0), 
        tt_cxy_pair(2,0,0)
    );
    auto test_router = std::move(std::get<0>(router_uniq_ptr__c2c_pipe_id));
    auto router = test_router->router.get();
    auto chip_to_chip_pipe = std::get<1>(router_uniq_ptr__c2c_pipe_id);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), chip_to_chip_pipe);

    const auto &route = router::generate_chip_to_chip_route_for_pipe(*router, router->get_cluster_graph(), chip_to_chip_pipe);

    ASSERT_EQ(route.size(), 2); // size == # hops
    const auto &golden_route = std::vector<std::pair<chip_id_t, chip_id_t>>{ {0,1}, {1,2} };
    ASSERT_EQ(route, golden_route);
}

TEST(RouterMultichip, TestShortestPathRouting_3x3Cluster_route_1_2_5_8_or_1_4_7_8_or_1_4_5_8) {
    auto router_uniq_ptr__c2c_pipe_id = create_router_with_chip_to_chip_pipe(
        "src/net2pipe/unit_tests/cluster_descriptions/wormhole_3x3_cluster.yaml", 
        tt_cxy_pair(1,0,0), 
        tt_cxy_pair(8,0,0)
    );
    auto test_router = std::move(std::get<0>(router_uniq_ptr__c2c_pipe_id));
    auto router = test_router->router.get();
    auto chip_to_chip_pipe = std::get<1>(router_uniq_ptr__c2c_pipe_id);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), chip_to_chip_pipe);

    const auto &route = router::generate_chip_to_chip_route_for_pipe(*router, router->get_cluster_graph(), chip_to_chip_pipe);

    ASSERT_EQ(route.size(), 3); // size == # hops
    const auto &golden_route1 = std::vector<std::pair<chip_id_t, chip_id_t>>{ {1,2}, {2,5}, {5,8} };
    const auto &golden_route2 = std::vector<std::pair<chip_id_t, chip_id_t>>{ {1,4}, {4,5}, {5,8} };
    const auto &golden_route3 = std::vector<std::pair<chip_id_t, chip_id_t>>{ {1,4}, {4,7}, {7,8} };
    ASSERT_TRUE((route == golden_route1) || (route == golden_route2) || (route == golden_route3));
}


  
TEST(RouterMultichip, TestShortestPathRouting_Intersecting3x3RingCluster_route_1_0_C_B_9_A) {
// 0-1-2
// |   |
// |   3---4   
// |   |   |
// C-B-7   8
//   |     |
//   9-A-5-6
    auto router_uniq_ptr__c2c_pipe_id = create_router_with_chip_to_chip_pipe(
        "src/net2pipe/unit_tests/cluster_descriptions/wormhole_weird_intersecting_3x3_ring_cluster.yaml", 
        tt_cxy_pair(1,0,0), 
        tt_cxy_pair(10,0,0)
    );
    auto test_router = std::move(std::get<0>(router_uniq_ptr__c2c_pipe_id));
    auto router = test_router->router.get();
    auto chip_to_chip_pipe = std::get<1>(router_uniq_ptr__c2c_pipe_id);
    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 1);
    ASSERT_EQ(chip_to_chip_pipes.at(0), chip_to_chip_pipe);

    const auto &route = router::generate_chip_to_chip_route_for_pipe(*router, router->get_cluster_graph(), chip_to_chip_pipe);

    ASSERT_EQ(route.size(), 5); // size == # hops
    const auto &golden_route1 = std::vector<std::pair<chip_id_t, chip_id_t>>{ {1,0}, {0,12}, {12,11}, {11,9}, {9,10} };
    ASSERT_TRUE((route == golden_route1));
}

/* This test is disabled because it requires advanced routing functionality that isn't implemented yet */
TEST(RouterMultichip, DISABLED_TestShortestPathRouting_2x2Cluster8CoresCrissCross) { 
// 0-1   chip0 : a (x8)-> chip3, chip1 : c (x8)-> chip2 d -> Requires some intelligent global understanding of the routing problem since exactly 4 pipes must be
// | |                                                       routed each way (right-down and down-right for a and left-down and down-left for c)
// 2-3

    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0,0,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0,1,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0,2,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0,3,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0,0,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0,1,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0,2,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0,3,1), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1,0,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1,1,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1,2,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1,3,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1,0,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1,1,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1,2,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1,3,1), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_2_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(2,0,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(2,1,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(2,2,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(2,3,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(2,0,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(2,1,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(2,2,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(2,3,1), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_3_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(3,0,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(3,1,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(3,2,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(3,3,0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(3,0,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(3,1,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(3,2,1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(3,3,1), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        "src/net2pipe/unit_tests/cluster_descriptions/wormhole_2x2_cluster_1_link_per_pair.yaml", 
        eth_datacopy_test_info_t{
            .buffer_map=buffer_map, 
            .queue_map=queue_map,
            .op_queue_output_scatter=op_queue_output_scatter,
            .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();

    // Chip 0 -> Chip 3 pipes    
    router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(0).id, chip_3_buffers.at(0).id, router->get_buffer_location(chip_3_buffers.at(0).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(1).id, chip_3_buffers.at(1).id, router->get_buffer_location(chip_3_buffers.at(1).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(2).id, chip_3_buffers.at(2).id, router->get_buffer_location(chip_3_buffers.at(2).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(3).id, chip_3_buffers.at(3).id, router->get_buffer_location(chip_3_buffers.at(3).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(4).id, chip_3_buffers.at(4).id, router->get_buffer_location(chip_3_buffers.at(4).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(5).id, chip_3_buffers.at(5).id, router->get_buffer_location(chip_3_buffers.at(5).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(6).id, chip_3_buffers.at(6).id, router->get_buffer_location(chip_3_buffers.at(6).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(7).id, chip_3_buffers.at(7).id, router->get_buffer_location(chip_3_buffers.at(7).id), 1);
    
    // Chip 1 -> Chip 2 pipes
    router->create_unicast_pipe_connection_at_core(chip_1_buffers.at(0).id, chip_2_buffers.at(0).id, router->get_buffer_location(chip_2_buffers.at(0).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_1_buffers.at(1).id, chip_2_buffers.at(1).id, router->get_buffer_location(chip_2_buffers.at(1).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_1_buffers.at(2).id, chip_2_buffers.at(2).id, router->get_buffer_location(chip_2_buffers.at(2).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_1_buffers.at(3).id, chip_2_buffers.at(3).id, router->get_buffer_location(chip_2_buffers.at(3).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_1_buffers.at(4).id, chip_2_buffers.at(4).id, router->get_buffer_location(chip_2_buffers.at(4).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_1_buffers.at(5).id, chip_2_buffers.at(5).id, router->get_buffer_location(chip_2_buffers.at(5).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_1_buffers.at(6).id, chip_2_buffers.at(6).id, router->get_buffer_location(chip_2_buffers.at(6).id), 1);
    router->create_unicast_pipe_connection_at_core(chip_1_buffers.at(7).id, chip_2_buffers.at(7).id, router->get_buffer_location(chip_2_buffers.at(7).id), 1);



    const auto &chip_to_chip_pipes = router::collect_chip_to_chip_pipes(*router);

    ASSERT_EQ(chip_to_chip_pipes.size(), 32);
}
