// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "router.hpp"
#include "test_unit_common.hpp"
#include "gtest/gtest.h"

using router::pipe_segment_id_t;
using router::pipe_input_buffers_t;

TEST(Net2PipeRouterBufferAttributes, IdentifyBufferAttributes_OutputsToDram) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {{0, 0, 1},
        {0, 0, 1},
        {0, 0, 1},
        {0, 0, 1},
        {0, 0, 1},
        {0, 0, 1},
        {0, 0, 1},
        {0, 0, 1}}
    );
    auto buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {{tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 1, 3), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 1, 4), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 1, 5), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 1, 6), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 1, 7), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 2, 1), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 2, 2), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 2, 3), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 2, 4), tt::RouterBufferType::Relay, 1},
        {tt_cxy_pair(0, 2, 5), tt::RouterBufferType::Relay, 1}}
    );

    auto test_router = generate_test_router(soc_descriptor, 1, 
    eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();

    int buf_idx = 0;
    int q_idx = 0;
    // TEST UNICAST TO DRAM - 1 output to dram
    {
        auto test_buf_id = buffers.at(buf_idx++).id;
        auto pipe_1 = router->create_unicast_pipe_connection_at_core(buffers.at(buf_idx++).id, test_buf_id, router->get_buffer_location(test_buf_id), 1);
        auto unicast_to_dram_pipe_id = router->create_unicast_pipe_connection_at_core(test_buf_id, queues.at(q_idx++).q_buf_id, router->get_buffer_location(test_buf_id), 1);
        const auto &attrs = router->identify_buffer_attributes(test_buf_id);
        ASSERT_TRUE(attrs.has_outputs_to_dram());
        ASSERT_EQ(attrs.get_num_outputs_to_dram(), 1);
        ASSERT_FALSE(attrs.has_input_from_dram());
        ASSERT_FALSE(attrs.has_multicast());
        ASSERT_FALSE(attrs.has_ethernet_io_streams());
    }

    // TEST Fork (2 unicasts) TO DRAM - 2 output to dram
    {
        auto test_buf_id = buffers.at(buf_idx++).id;
        auto pipe_1 = router->create_unicast_pipe_connection_at_core(buffers.at(buf_idx++).id, test_buf_id, router->get_buffer_location(test_buf_id), 1);
        auto unicast_to_dram_pipe_id_fork1 = router->create_unicast_pipe_connection_at_core(test_buf_id, queues.at(q_idx++).q_buf_id, router->get_buffer_location(test_buf_id), 1);
        auto unicast_to_dram_pipe_id_fork2 = router->create_unicast_pipe_connection_at_core(test_buf_id, queues.at(q_idx++).q_buf_id, router->get_buffer_location(test_buf_id), 1);
        const auto &attrs = router->identify_buffer_attributes(test_buf_id);
        ASSERT_TRUE(attrs.has_outputs_to_dram());
        ASSERT_EQ(attrs.get_num_outputs_to_dram(), 2);
        ASSERT_FALSE(attrs.has_input_from_dram());
        ASSERT_FALSE(attrs.has_multicast());
        ASSERT_FALSE(attrs.has_ethernet_io_streams());
    }

    // TEST Multicast TO DRAM - both to dram, 2 output to dram
    {
        auto test_buf_id = buffers.at(buf_idx++).id;
        auto pipe_1 = router->create_unicast_pipe_connection_at_core(buffers.at(buf_idx++).id, test_buf_id, router->get_buffer_location(test_buf_id), 1);
        const auto first_mcast_target_buf_id = queues.at(q_idx++).q_buf_id;
        auto unicast_to_dram_pipe_id = router->create_multicast_pipe_connection_at_core(test_buf_id, {first_mcast_target_buf_id, queues.at(q_idx++).q_buf_id}, router->get_buffer_location(first_mcast_target_buf_id), 1);
        const auto &attrs = router->identify_buffer_attributes(test_buf_id);
        ASSERT_TRUE(attrs.has_outputs_to_dram());
        ASSERT_EQ(attrs.get_num_outputs_to_dram(), 1);
        ASSERT_FALSE(attrs.has_input_from_dram());
        ASSERT_TRUE(attrs.has_multicast());
        ASSERT_EQ(attrs.get_num_multicasts(), 1);
        ASSERT_FALSE(attrs.has_ethernet_io_streams());
    }

    // TEST Multicast TO DRAM - only one output to dram
    {
        auto test_buf_id = buffers.at(buf_idx++).id;
        auto pipe_1 = router->create_unicast_pipe_connection_at_core(buffers.at(buf_idx++).id, test_buf_id, router->get_buffer_location(test_buf_id), 1);
        const auto first_mcast_target_buf_id = buffers.at(buf_idx++).id;
        auto unicast_to_dram_pipe_id = router->create_multicast_pipe_connection_at_core(test_buf_id, {first_mcast_target_buf_id, queues.at(q_idx++).q_buf_id}, router->get_buffer_location(first_mcast_target_buf_id), 1);
        const auto &attrs = router->identify_buffer_attributes(test_buf_id);
        ASSERT_TRUE(attrs.has_outputs_to_dram());
        ASSERT_EQ(attrs.get_num_outputs_to_dram(), 1);
        ASSERT_FALSE(attrs.has_input_from_dram());
        ASSERT_TRUE(attrs.has_multicast());
        ASSERT_EQ(attrs.get_num_multicasts(), 1);
        ASSERT_FALSE(attrs.has_ethernet_io_streams());
    }

    // TEST Fork (1 unicast + 1 multicast) TO DRAM - 2 output to dram
    {
        auto test_buf_id = buffers.at(buf_idx++).id;
        auto pipe_1 = router->create_unicast_pipe_connection_at_core(buffers.at(buf_idx++).id, test_buf_id, router->get_buffer_location(test_buf_id), 1);
        auto unicast_to_dram_pipe_id_fork1 = router->create_unicast_pipe_connection_at_core(test_buf_id, queues.at(q_idx++).q_buf_id, router->get_buffer_location(test_buf_id), 1);
        const auto first_mcast_target_buf_id = buffers.at(buf_idx++).id;
        auto unicast_to_dram_pipe_id_fork2 = router->create_multicast_pipe_connection_at_core(test_buf_id, {first_mcast_target_buf_id, queues.at(q_idx++).q_buf_id}, router->get_buffer_location(first_mcast_target_buf_id), 1);
        const auto &attrs = router->identify_buffer_attributes(test_buf_id);
        ASSERT_TRUE(attrs.has_outputs_to_dram());
        ASSERT_EQ(attrs.get_num_outputs_to_dram(), 2);
        ASSERT_FALSE(attrs.has_input_from_dram());
        ASSERT_TRUE(attrs.has_multicast());
        ASSERT_EQ(attrs.get_num_multicasts(), 1);
        ASSERT_FALSE(attrs.has_ethernet_io_streams());
    }
}

TEST(Net2PipeRouterBufferAttributes, IdentifyBufferAttributes_EthernetIoLinks) {
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
            {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 3), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_0_eth_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 2, 0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 2, 0), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 1, 2), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 1, 3), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_eth_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 2, 0), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(1, 2, 0), tt::RouterBufferType::Relay, 1}
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
    {
        auto test_send_buf_id = chip_0_eth_buffers.at(c0_eth_buf_idx++).id;
        auto test_recv_buf_id = chip_1_eth_buffers.at(c1_eth_buf_idx++).id;
        router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(c0_buf_idx++).id, test_send_buf_id, router->get_buffer_location(test_send_buf_id), 1);
        router->create_unicast_pipe_connection_at_core(test_send_buf_id, test_recv_buf_id, router->get_buffer_location(test_recv_buf_id), 1);
        router->create_unicast_pipe_connection_at_core(test_recv_buf_id, chip_1_buffers.at(c1_buf_idx++).id, router->get_buffer_location(test_recv_buf_id), 1);
        const auto &sender_attrs = router->identify_buffer_attributes(test_send_buf_id);
        ASSERT_FALSE(sender_attrs.has_outputs_to_dram());
        ASSERT_FALSE(sender_attrs.has_input_from_dram());
        ASSERT_FALSE(sender_attrs.has_multicast());
        ASSERT_TRUE(sender_attrs.has_ethernet_io_streams());
        ASSERT_EQ(sender_attrs.get_num_ethernet_io_streams(), 1);

        const auto &receiver_attrs = router->identify_buffer_attributes(test_recv_buf_id);
        ASSERT_FALSE(receiver_attrs.has_outputs_to_dram());
        ASSERT_FALSE(receiver_attrs.has_input_from_dram());
        ASSERT_FALSE(receiver_attrs.has_multicast());
        ASSERT_TRUE(receiver_attrs.has_ethernet_io_streams());
        ASSERT_EQ(receiver_attrs.get_num_ethernet_io_streams(), 1);
    }

    // TEST 2 separate ethernet pipes on the same core - 1 eth stream per buffer - 2 used ethernet streams per core
    {
        {
            auto test_send_buf_id = chip_0_eth_buffers.at(c0_eth_buf_idx++).id;
            auto test_recv_buf_id = chip_1_eth_buffers.at(c1_eth_buf_idx++).id;
            router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(c0_buf_idx++).id, test_send_buf_id, router->get_buffer_location(test_send_buf_id), 1);
            router->create_unicast_pipe_connection_at_core(test_send_buf_id, test_recv_buf_id, router->get_buffer_location(test_recv_buf_id), 1);
            router->create_unicast_pipe_connection_at_core(test_recv_buf_id, chip_1_buffers.at(c1_buf_idx++).id, router->get_buffer_location(test_recv_buf_id), 1);
            const auto &sender_attrs = router->identify_buffer_attributes(test_send_buf_id);
            ASSERT_FALSE(sender_attrs.has_outputs_to_dram());
            ASSERT_FALSE(sender_attrs.has_input_from_dram());
            ASSERT_FALSE(sender_attrs.has_multicast());
            ASSERT_TRUE(sender_attrs.has_ethernet_io_streams());
            ASSERT_EQ(sender_attrs.get_num_ethernet_io_streams(), 1);

            const auto &receiver_attrs = router->identify_buffer_attributes(test_recv_buf_id);
            ASSERT_FALSE(receiver_attrs.has_outputs_to_dram());
            ASSERT_FALSE(receiver_attrs.has_input_from_dram());
            ASSERT_FALSE(receiver_attrs.has_multicast());
            ASSERT_TRUE(receiver_attrs.has_ethernet_io_streams());
            ASSERT_EQ(receiver_attrs.get_num_ethernet_io_streams(), 1);
        }

        {
            auto test_send_buf_id = chip_0_eth_buffers.at(c0_eth_buf_idx++).id;
            auto test_recv_buf_id = chip_1_eth_buffers.at(c1_eth_buf_idx++).id;
            router->create_unicast_pipe_connection_at_core(chip_0_buffers.at(c0_buf_idx++).id, test_send_buf_id, router->get_buffer_location(test_send_buf_id), 1);
            router->create_unicast_pipe_connection_at_core(test_send_buf_id, test_recv_buf_id, router->get_buffer_location(test_recv_buf_id), 1);
            router->create_unicast_pipe_connection_at_core(test_recv_buf_id, chip_1_buffers.at(c1_buf_idx++).id, router->get_buffer_location(test_recv_buf_id), 1);
            const auto &sender_attrs = router->identify_buffer_attributes(test_send_buf_id);
            ASSERT_FALSE(sender_attrs.has_outputs_to_dram());
            ASSERT_FALSE(sender_attrs.has_input_from_dram());
            ASSERT_FALSE(sender_attrs.has_multicast());
            ASSERT_TRUE(sender_attrs.has_ethernet_io_streams());
            ASSERT_EQ(sender_attrs.get_num_ethernet_io_streams(), 1);

            const auto &receiver_attrs = router->identify_buffer_attributes(test_recv_buf_id);
            ASSERT_FALSE(receiver_attrs.has_outputs_to_dram());
            ASSERT_FALSE(receiver_attrs.has_input_from_dram());
            ASSERT_FALSE(receiver_attrs.has_multicast());
            ASSERT_TRUE(receiver_attrs.has_ethernet_io_streams());
            ASSERT_EQ(receiver_attrs.get_num_ethernet_io_streams(), 1);
        }

        const auto &cluster_resource_model = router->get_cluster_resource_model();
        const auto &sender_core_attrs = cluster_resource_model.get_core_attributes(tt_cxy_pair(0,2,0));
        ASSERT_EQ(sender_core_attrs.get_buffer_count(), 2);
        ASSERT_EQ(sender_core_attrs.get_used_ethernet_stream_count(), 2);
        ASSERT_EQ(sender_core_attrs.get_used_mcast_stream_count(),0);
        
        const auto &receiver_core_attrs = cluster_resource_model.get_core_attributes(tt_cxy_pair(1,2,0));
        ASSERT_EQ(receiver_core_attrs.get_buffer_count(), 2);
        ASSERT_EQ(receiver_core_attrs.get_used_ethernet_stream_count(), 2);
        ASSERT_EQ(receiver_core_attrs.get_used_mcast_stream_count(),0);
    }
}



TEST(Net2PipeRouterPipeFunctions, PipeImplementsGather_False) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 3), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 4), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 5), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 6), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 7), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 8), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        1, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();

    { // Single input => False
        unique_id_t pipe_id = router->create_unicast_pipe_connection_at_core(buffers.at(0).id, buffers.at(1).id, router->get_buffer_location(buffers.at(0).id), 1);
        ASSERT_FALSE(router->pipe_implements_gather(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0}));
    }

    { // Multiple inputs (2), all are same buffer => False
        pipe_input_buffers_t input_buffers = pipe_input_buffers_t(2, buffers.at(2).id);
        unique_id_t pipe_id = router->create_gather_pipe_connection_at_core(input_buffers, buffers.at(3).id, router->get_buffer_location(buffers.at(0).id), 1);
        ASSERT_FALSE(router->pipe_implements_gather(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0}));
    }

    { // Multiple inputs (100) (non-scatter), all are same buffer => False
        pipe_input_buffers_t input_buffers = pipe_input_buffers_t(100, buffers.at(4).id);
        unique_id_t pipe_id = router->create_gather_pipe_connection_at_core(input_buffers, buffers.at(5).id, router->get_buffer_location(buffers.at(0).id), 1);
        ASSERT_FALSE(router->pipe_implements_gather(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0}));
    }

}

TEST(Net2PipeRouterPipeFunctions, PipeImplementsGather_True) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 3), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 4), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 5), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 6), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 7), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 2, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 2, 2), tt::RouterBufferType::Relay, 1}
        }
    );

    auto test_router = generate_test_router(
        soc_descriptor, 
        1, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map});
    auto router = test_router->router.get();

    { // Two different inputs (non-scatter)=> True
        unique_id_t pipe_id = router->create_gather_pipe_connection_at_core({buffers.at(0).id, buffers.at(1).id}, buffers.at(2).id, router->get_buffer_location(buffers.at(0).id), 1);
        ASSERT_TRUE(router->pipe_implements_gather(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0}));
    }

    { // Three different inputs (non-scatter) {0, 0, 1} => True
        unique_id_t pipe_id = router->create_gather_pipe_connection_at_core({buffers.at(3).id, buffers.at(3).id, buffers.at(4).id}, buffers.at(5).id, router->get_buffer_location(buffers.at(0).id), 1);
        ASSERT_TRUE(router->pipe_implements_gather(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0}));
    }

    { // Three different inputs (non-scatter) {0, 1, 0} => True
        unique_id_t pipe_id = router->create_gather_pipe_connection_at_core({buffers.at(6).id, buffers.at(7).id, buffers.at(6).id}, buffers.at(8).id, router->get_buffer_location(buffers.at(0).id), 1);
        ASSERT_TRUE(router->pipe_implements_gather(pipe_segment_id_t{.pipe_id=pipe_id, .segment=0}));
    }

}

// TEST(NET2PIPE_ROUTER, IdentifyBufferAttributes_Multicasts) {
//     ASSERT_TRUE(false);
// }

// TEST(NET2PIPE_ROUTER, IdentifyBufferAttributes_InputsFromDram ) {
//     ASSERT_TRUE(false);
// }

// TEST(NET2PIPE_ROUTER, IdentifyBufferAttributes_MultipleAttributes) {
//     ASSERT_TRUE(false);
// }

