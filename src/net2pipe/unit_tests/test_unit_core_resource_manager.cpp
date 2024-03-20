// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "core_resource_tracker.h"
#include "router.hpp"
#include "router_types.h"
#include "common/size_lib.hpp"

#include "net2pipe_constants.h"

#include "test_unit_common.hpp"
#include "gtest/gtest.h"

using router::BufferAllocationFailureReason;
using router::HwCoreAttributes;
using router::BufferAttributes;
using router::pipe_segment_id_t;
using router::pipe_segment_hash_t;

TEST(Net2PipeCoreResourceManager, TestL1BufferAllocation) {
    constexpr int l1_size_bytes = 1499136;
    auto core = router::HwCoreAttributes(l1_size_bytes, MAX_DRAM_IO_INPUT_STREAMS, MAX_DRAM_IO_OUTPUT_STREAMS, MAX_TOTAL_ACTIVE_DRAM_QUEUES, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);

    tt_cxy_pair core_loc = tt_cxy_pair(0,0,0);

    int tile_size_in_bytes = tt::size::get_tile_size_in_bytes(DataFormat::Float16, false);
    int tile_size_in_bytes_with_header = tt::size::get_tile_size_in_bytes(DataFormat::Float16, true);
    int header_size = tile_size_in_bytes_with_header - tile_size_in_bytes;
    int num_l1_tiles = l1_size_bytes / (tile_size_in_bytes + header_size);
    const auto &[first_buf_id, first_buffer] = generate_dummy_test_router_buffer(core_loc, tt::RouterBufferType::Relay, num_l1_tiles - 2);
    core.add_buffer(first_buf_id, first_buffer.info(), {});

    int expected_allocation_size_bytes = first_buffer.info().allocated_size_in_bytes();
    ASSERT_EQ(core.get_num_allocated_bytes(), expected_allocation_size_bytes);
    ASSERT_TRUE(core.can_allocate_bytes(l1_size_bytes - expected_allocation_size_bytes));

    const auto &[second_buf_id, second_buffer] = generate_dummy_test_router_buffer(core_loc, tt::RouterBufferType::Relay, 1);
    ASSERT_TRUE(core.can_add_buffer(second_buffer.info(), {}));
    core.add_buffer(second_buf_id, second_buffer.info(), {});

    expected_allocation_size_bytes += second_buffer.info().allocated_size_in_bytes();
    ASSERT_EQ(core.get_num_allocated_bytes(), expected_allocation_size_bytes);
    ASSERT_TRUE(core.can_allocate_bytes(l1_size_bytes - expected_allocation_size_bytes));

    const auto &[third_buf_id, third_buffer] = generate_dummy_test_router_buffer(core_loc, tt::RouterBufferType::Relay, 2);
    ASSERT_FALSE(core.can_add_buffer(third_buffer.info(), {}));
}

TEST(Net2PipeCoreResourceManager, TestBufferStreamAllocation) {
    constexpr int l1_size_bytes = 10000000;
    constexpr int relay_stream_count_limit = HwCoreAttributes::RELAY_BUFFER_STREAM_LAST - HwCoreAttributes::RELAY_BUFFER_STREAM_FIRST;
    auto core = router::HwCoreAttributes(l1_size_bytes, MAX_DRAM_IO_INPUT_STREAMS, MAX_DRAM_IO_OUTPUT_STREAMS, MAX_TOTAL_ACTIVE_DRAM_QUEUES, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    int tile_size_in_bytes = tt::size::get_tile_size_in_bytes(DataFormat::Float16, false);
    int num_l1_tiles = l1_size_bytes / tile_size_in_bytes;
    log_assert(num_l1_tiles >= relay_stream_count_limit, "This test didn't allocate a large enough l1 size");

    for (int i = 0; i < relay_stream_count_limit; i++) {
        const auto &[buffer_id, buffer] = generate_dummy_test_router_buffer(tt_cxy_pair(0,0,0), tt::RouterBufferType::Relay, 1);
        ASSERT_TRUE(core.can_add_buffer(buffer.info(), {}));
        core.add_buffer(buffer_id, buffer.info(), {});
    }
    const auto &buffer = generate_dummy_buffer_info(1, {});
    ASSERT_FALSE(core.can_add_buffer(buffer, {}));

    const auto &allocation_failure_reasons = core.reasons_for_buffer_allocation_failure(buffer, {});
    ASSERT_TRUE(allocation_failure_reasons.has_value());
    ASSERT_EQ(allocation_failure_reasons.value().size(), 1);
    ASSERT_EQ(allocation_failure_reasons.value().at(0), BufferAllocationFailureReason::Insufficient_Relay_Streams);
}

TEST(Net2PipeCoreResourceManager, TestDramIOStreamAllocation) {
    constexpr int l1_size_bytes = 10000000;

    // TEST DRAM INPUT STREAM LIMIT
    {
        constexpr int dram_input_stream_limit = MAX_DRAM_IO_INPUT_STREAMS;
        auto core = router::HwCoreAttributes(l1_size_bytes, MAX_DRAM_IO_INPUT_STREAMS, MAX_DRAM_IO_OUTPUT_STREAMS, MAX_TOTAL_ACTIVE_DRAM_QUEUES, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
        int tile_size_in_bytes = tt::size::get_tile_size_in_bytes(DataFormat::Float16, false);
        int num_l1_tiles = l1_size_bytes / tile_size_in_bytes;
        log_assert(num_l1_tiles >= dram_input_stream_limit, "This test didn't allocate a large enough l1 size");

        for (int i = 0; i < dram_input_stream_limit; i++) {
            const auto &[buffer_id, buffer] = generate_dummy_test_router_buffer(tt_cxy_pair(0,0,0), tt::RouterBufferType::Relay, 1);
            ASSERT_TRUE(core.can_add_buffer(buffer.info(), BufferAttributes({.input_from_dram=true})));
            core.add_buffer(buffer_id, buffer.info(), BufferAttributes({.input_from_dram=true}));
        }
        const auto &buffer = generate_dummy_buffer_info(1, {});
        ASSERT_FALSE(core.can_add_buffer(buffer, BufferAttributes({.input_from_dram=true})));

        const auto &allocation_failure_reasons = core.reasons_for_buffer_allocation_failure(buffer, BufferAttributes({.input_from_dram=true}));
        ASSERT_TRUE(allocation_failure_reasons.has_value());
        ASSERT_EQ(allocation_failure_reasons.value().size(), 1);
        ASSERT_EQ(allocation_failure_reasons.value().at(0), BufferAllocationFailureReason::Insufficient_DRAM_Input_Streams);
    }

    // TEST DRAM OUTPUT STREAM LIMIT
    {
        constexpr int dram_output_stream_limit = MAX_DRAM_IO_OUTPUT_STREAMS;
        auto core = router::HwCoreAttributes(l1_size_bytes, MAX_DRAM_IO_INPUT_STREAMS, MAX_DRAM_IO_OUTPUT_STREAMS, MAX_TOTAL_ACTIVE_DRAM_QUEUES, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
        int tile_size_in_bytes = tt::size::get_tile_size_in_bytes(DataFormat::Float16, false);
        int num_l1_tiles = l1_size_bytes / tile_size_in_bytes;
        log_assert(num_l1_tiles >= dram_output_stream_limit, "This test didn't allocate a large enough l1 size");

        for (int i = 0; i < dram_output_stream_limit; i++) {
            const auto &[buffer_id, buffer] = generate_dummy_test_router_buffer(tt_cxy_pair(0,0,0), tt::RouterBufferType::Relay, 1);
            ASSERT_TRUE(core.can_add_buffer(buffer.info(), BufferAttributes({.outputs_to_dram=1})));
            core.add_buffer(buffer_id, buffer.info(), BufferAttributes({.outputs_to_dram=1}));
        }
        const auto &buffer = generate_dummy_buffer_info(1, {});
        ASSERT_FALSE(core.can_add_buffer(buffer, BufferAttributes({.outputs_to_dram=1})));

        const auto &allocation_failure_reasons = core.reasons_for_buffer_allocation_failure(buffer, BufferAttributes({.outputs_to_dram=1}));
        ASSERT_TRUE(allocation_failure_reasons.has_value());
        ASSERT_EQ(allocation_failure_reasons.value().size(), 1);
        ASSERT_EQ(allocation_failure_reasons.value().at(0), BufferAllocationFailureReason::Insufficient_DRAM_Output_Streams);
    }
}

TEST(Net2PipeRouterResourceManagement, TestDramReadStreamCountsAfterGatherPipeDisconnectAndReconnect) {

 const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {
            {0,1,1},
            {0,1,1},
            {0,1,1}}
    );
 
    auto chip_0_receiver_buffer = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
        }
    );

    std::map<unique_id_t, pipe_t> pipe_map = {};
    std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes = {};
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes = {};
    auto pipe_id = unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN);
    std::vector<unique_id_t> input_buffer_ids = {};
    std::transform(queues.begin(), queues.end(), std::back_inserter(input_buffer_ids), [](auto q_entry) { return q_entry.q_buf_id; });
    std::vector<unique_id_t> output_buffer_ids = {chip_0_receiver_buffer.at(0).id};
    
    pipe_map.insert({pipe_id, 
        pipe_t(tt_cxy_pair(0,1,1), input_buffer_ids, output_buffer_ids)
    });

    buffer_input_pipes.insert({chip_0_receiver_buffer.at(0).id, pipe_id});
    for (unique_id_t id : input_buffer_ids) {
        buffer_output_pipes[id].insert(pipe_id);
    }

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map, .pipes=pipe_map, .buffer_output_pipes=buffer_output_pipes, .buffer_input_pipes=buffer_input_pipes}
    );
    auto router = test_router->router.get();

    auto &resource_tracker = router->get_cluster_resource_model();
    
    // Validate initial conditions
    for (unique_id_t id : input_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        log_debug(tt::LogTest, "Initial used eth streams at (c={},y={},x={}) = {}", loc.chip, loc.y, loc.x, used_eth_streams);
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }
    for (unique_id_t id : output_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        log_debug(tt::LogTest, "Initial used eth streams at (c={},y={},x={}) = {}", loc.chip, loc.y, loc.x, used_eth_streams);
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 1);
        ASSERT_EQ(used_output_dram_streams, 0);
    }


    // Manipulate the dataflow graph (disconnect)
    router->disconnect_pipe(pipe_id);
    router->remove_pipe(pipe_id);

    // Validate 
    for (unique_id_t id : input_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        log_debug(tt::LogTest, "Initial used eth streams at (c={},y={},x={}) = {}", loc.chip, loc.y, loc.x, used_eth_streams);
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }
    for (unique_id_t id : output_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        log_debug(tt::LogTest, "Initial used eth streams at (c={},y={},x={}) = {}", loc.chip, loc.y, loc.x, used_eth_streams);
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }

    // Manipulate the dataflow graph (reconnect)
    router->create_gather_pipe_connection_at_core(input_buffer_ids, output_buffer_ids.at(0), tt_cxy_pair(0,1,1), 1);

    // Validate

    for (unique_id_t id : input_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        log_debug(tt::LogTest, "Initial used eth streams at (c={},y={},x={}) = {}", loc.chip, loc.y, loc.x, used_eth_streams);
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }
    for (unique_id_t id : output_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        log_debug(tt::LogTest, "Initial used eth streams at (c={},y={},x={}) = {}", loc.chip, loc.y, loc.x, used_eth_streams);
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 1);
        ASSERT_EQ(used_output_dram_streams, 0);
    }

}


TEST(Net2PipeRouterResourceManagement, TestDramWriteStreamCountsAfterMulticastPipeDisconnectAndReconnect) {

 const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {
            {0,1,1},
            {0,1,1},
            {0,1,1}}
    );
 
    auto dram_write_producer_buffer = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
        }
    );

    std::map<unique_id_t, pipe_t> pipe_map = {};
    std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes = {};
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes = {};
    auto pipe_id = unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN);
    std::vector<unique_id_t> input_buffer_ids = {dram_write_producer_buffer.at(0).id};
    std::vector<unique_id_t> output_buffer_ids = {};
    std::transform(queues.begin(), queues.end(), std::back_inserter(output_buffer_ids), [](auto q_entry) { return q_entry.q_buf_id; });
    
    pipe_map.insert({pipe_id, 
        pipe_t(tt_cxy_pair(0,1,1), input_buffer_ids, output_buffer_ids)
    });

    buffer_output_pipes[input_buffer_ids.at(0)].insert(pipe_id);
    for (unique_id_t id : output_buffer_ids) {
        buffer_input_pipes.insert({id, pipe_id});
    }

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map, .pipes=pipe_map, .buffer_output_pipes=buffer_output_pipes, .buffer_input_pipes=buffer_input_pipes}
    );
    auto router = test_router->router.get();

    auto &resource_tracker = router->get_cluster_resource_model();
    
    // Validate initial conditions
    for (unique_id_t id : input_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 1);
    }
    for (unique_id_t id : output_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }


    // Manipulate the dataflow graph (disconnect)
    router->disconnect_pipe(pipe_id);
    router->remove_pipe(pipe_id);

    // Validate 
    for (unique_id_t id : input_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }
    for (unique_id_t id : output_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }

    // Manipulate the dataflow graph (reconnect)
    router->create_multicast_pipe_connection_at_core(input_buffer_ids.at(0), output_buffer_ids, tt_cxy_pair(0,1,1), 1);

    // Validate

    for (unique_id_t id : input_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 1);
    }
    for (unique_id_t id : output_buffer_ids) {
        const auto &buf = router->get_buffer(id);
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }

}



TEST(Net2PipeRouterResourceManagement, TestEthernetStreamCountsAfterGatherPipeDisconnectAndReconnect) {
    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
    auto chip_0_gather_input_bufers = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(0, 1, 1), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 2), tt::RouterBufferType::Relay, 1},
            {tt_cxy_pair(0, 1, 3), tt::RouterBufferType::Relay, 1}
        }
    );
    auto chip_1_receiver_buffer = generate_multiple_dummy_router_buffers(soc_descriptor,
        buffer_map,
        {
            {tt_cxy_pair(1, 1, 1), tt::RouterBufferType::Relay, 1},
        }
    );

    std::map<unique_id_t, pipe_t> pipe_map = {};
    std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes = {};
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes = {};
    auto pipe_id = unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN);
    pipe_map.insert({pipe_id, pipe_t(
        tt_cxy_pair(0, 1, 1),
        {chip_0_gather_input_bufers.at(0).id, chip_0_gather_input_bufers.at(1).id, chip_0_gather_input_bufers.at(2).id,},
        {chip_1_receiver_buffer.at(0).id}
    )});

    buffer_input_pipes.insert({chip_1_receiver_buffer.at(0).id, pipe_id});
    for (const auto &[id,buf] : chip_0_gather_input_bufers) {
        buffer_output_pipes[id].insert(pipe_id);
    }

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map, .pipes=pipe_map, .buffer_output_pipes=buffer_output_pipes, .buffer_input_pipes=buffer_input_pipes}
    );
    auto router = test_router->router.get();

    auto &resource_tracker = router->get_cluster_resource_model();
    auto input_buffer_ids = router->get_pipe(pipe_id).input_buffer_ids;
    auto output_buffer_ids = router->get_pipe(pipe_id).output_buffer_ids();
    // Validate initial conditions
    for (const auto &[id, buf] : router->get_buffer_map()) {
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        log_debug(tt::LogTest, "Initial used eth streams at (c={},y={},x={}) = {}", loc.chip, loc.y, loc.x, used_eth_streams);
        ASSERT_EQ(used_eth_streams, 1);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }

    // Manipulate the dataflow graph (disconnect)
    router->disconnect_pipe(pipe_id);
    router->remove_pipe(pipe_id);

    // Validate 
    for (const auto &[id, buf] : router->get_buffer_map()) {
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        log_debug(tt::LogTest, "After disconnect, used eth streams at (c={},y={},x={}) = {}", loc.chip, loc.y, loc.x, used_eth_streams);
        ASSERT_EQ(used_eth_streams, 0);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }

    // Manipulate the dataflow graph (reconnect)
    router->create_gather_pipe_connection(input_buffer_ids, output_buffer_ids.at(0), 1);

    // Validate
    for (const auto &[id, buf] : router->get_buffer_map()) {
        const auto &loc = buf.core_location();
        int used_eth_streams = resource_tracker.get_core_attributes(loc).get_used_ethernet_stream_count();
        int used_input_from_dram_streams = resource_tracker.get_core_attributes(loc).get_used_input_from_dram_streams();
        int used_output_dram_streams = resource_tracker.get_core_attributes(loc).get_used_output_to_dram_streams();
        log_debug(tt::LogTest, "After reconnect, used eth streams at (c={},y={},x={}) = {}", loc.chip, loc.y, loc.x, used_eth_streams);
        ASSERT_EQ(used_eth_streams, 1);
        ASSERT_EQ(used_input_from_dram_streams, 0);
        ASSERT_EQ(used_output_dram_streams, 0);
    }
}


TEST(Net2PipeRouterResourceManagement, TestEthernetStreamCountsAfterMulticastPipeDisconnectAndReconnect) { }
TEST(Net2PipeRouterResourceManagement, TestEthernetStreamCountsAfterUnicastPipeDisconnectAndReconnect) { }
TEST(Net2PipeRouterResourceManagement, TestEthernetStreamCountsAfterGatherMulticastPipeDisconnectAndReconnect) { }
TEST(Net2PipeRouterResourceManagement, TestEthernetStreamCountsAfterTMPipeDisconnectAndReconnect) { }

TEST(Net2PipeRouterResourceManagement, TestEthernetStreamCountsAfterSplittingChipToChipPipe) {
}


TEST(Net2PipeRouterResourceManagement, TestGatherPipeStreamUsage_CheckExtraStreamsFor3rdGatherPipeOnCore_3ExtraStreamsExpected) {

    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
 
    std::map<unique_id_t, pipe_t> pipe_map = {};
    std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes = {};
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes = {};
    
    tt_cxy_pair const& gathering_core_cxy = tt_cxy_pair(0,1,8);
    tt_cxy_pair const& non_gathering_core_cxy = tt_cxy_pair(0,2,8);

    std::vector<std::tuple<std::vector<tt_cxy_pair>, tt_cxy_pair, tt_cxy_pair>> pipe_input_buffer_output_buffer_gather_locations = {
        {{tt_cxy_pair(0, 1, 1),tt_cxy_pair(0, 2, 1), tt_cxy_pair(0, 3, 1)}, tt_cxy_pair(0, 1, 8), gathering_core_cxy},
        {{tt_cxy_pair(0, 1, 2),tt_cxy_pair(0, 2, 2), tt_cxy_pair(0, 3, 2)}, tt_cxy_pair(0, 2, 8), gathering_core_cxy},
        {{tt_cxy_pair(0, 1, 3),tt_cxy_pair(0, 2, 3), tt_cxy_pair(0, 3, 3)}, tt_cxy_pair(0, 3, 8), non_gathering_core_cxy}
    };

    std::vector<unique_id_t> gather_pipe_ids = {};

    for (const auto &[input_buffer_locations, output_buffer_location, gather_location] : pipe_input_buffer_output_buffer_gather_locations) {
        auto pipe_id = unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN);
        gather_pipe_ids.push_back(pipe_id);

        std::vector<loc_buftype_ntiles_t> input_buffer_specs = {};
        std::transform(
            input_buffer_locations.begin(),
            input_buffer_locations.end(),
            std::back_inserter(input_buffer_specs),
            [](auto loc) {
                return loc_buftype_ntiles_t{loc, tt::RouterBufferType::Relay, 1};
            });
        auto pipe_input_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
            buffer_map, input_buffer_specs);
        auto pipe_output_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
            buffer_map,
            {
                {output_buffer_location, tt::RouterBufferType::Relay, 1}
            }
        );
        std::vector<unique_id_t> pipe_input_buffer_ids = {pipe_input_buffers.at(0).id, pipe_input_buffers.at(1).id, pipe_input_buffers.at(2).id};
        pipe_map.insert({pipe_id, 
            pipe_t(gather_location, pipe_input_buffer_ids, {pipe_output_buffers.at(0).id})});
        buffer_input_pipes.insert({pipe_output_buffers.at(0).id, pipe_id});
        for (unique_id_t id : pipe_input_buffer_ids) { buffer_output_pipes[id].insert(pipe_id);}
    }

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map, .pipes=pipe_map, .buffer_output_pipes=buffer_output_pipes, .buffer_input_pipes=buffer_input_pipes}
    );
    auto router = test_router->router.get();

    int num_gather_streams_for_fourth_gather_on_core = compute_pipe_segment_extra_stream_use(*router, pipe_segment_id_t{.pipe_id=gather_pipe_ids.back(), .segment=0}, gathering_core_cxy);
    ASSERT_EQ(num_gather_streams_for_fourth_gather_on_core, 3);
}

TEST(Net2PipeRouterResourceManagement, TestGatherPipeStreamUsage_CheckExtraStreamsFor5thGatherPipeOnCore_1ExtraStreamExpected) {

    const auto &soc_descriptor = *(wh_soc_desc.get());
    auto buffer_map = std::map<unique_id_t, router_buffer_info_t>{};
    auto [queues, queue_map, op_queue_output_scatter, queue_settings_map] = generate_multiple_dummy_router_queues(soc_descriptor,
        buffer_map,
        {}
    );
 
    std::map<unique_id_t, pipe_t> pipe_map = {};
    std::unordered_map<unique_id_t, unique_id_t> buffer_input_pipes = {};
    std::unordered_map<unique_id_t, std::unordered_set<unique_id_t>> buffer_output_pipes = {};

    tt_cxy_pair const& gathering_core_cxy = tt_cxy_pair(0,1,8);
    tt_cxy_pair const& non_gathering_core_cxy = tt_cxy_pair(0,2,8);

    std::vector<std::tuple<std::vector<tt_cxy_pair>, tt_cxy_pair, tt_cxy_pair>> pipe_input_buffer_output_buffer_gather_locations = {
        {{tt_cxy_pair(0, 1, 1),tt_cxy_pair(0, 2, 1), tt_cxy_pair(0, 3, 1)}, tt_cxy_pair(0, 1, 8), gathering_core_cxy},
        {{tt_cxy_pair(0, 1, 2),tt_cxy_pair(0, 2, 2), tt_cxy_pair(0, 3, 2)}, tt_cxy_pair(0, 2, 8), gathering_core_cxy},
        {{tt_cxy_pair(0, 1, 3),tt_cxy_pair(0, 2, 3), tt_cxy_pair(0, 3, 3)}, tt_cxy_pair(0, 3, 8), gathering_core_cxy},
        {{tt_cxy_pair(0, 1, 4),tt_cxy_pair(0, 2, 4), tt_cxy_pair(0, 3, 4)}, tt_cxy_pair(0, 4, 8), gathering_core_cxy},
        {{tt_cxy_pair(0, 1, 5),tt_cxy_pair(0, 2, 5), tt_cxy_pair(0, 3, 5)}, tt_cxy_pair(0, 5, 8), non_gathering_core_cxy},
    };

    std::vector<unique_id_t> gather_pipe_ids = {};

    for (const auto &[input_buffer_locations, output_buffer_location, gather_location] : pipe_input_buffer_output_buffer_gather_locations) {
        auto pipe_id = unique_id_generator.get_next_unique_id(n2p::UNIQUE_ID_ALIGN);
        gather_pipe_ids.push_back(pipe_id);

        std::vector<loc_buftype_ntiles_t> input_buffer_specs = {};
        std::transform(
            input_buffer_locations.begin(),
            input_buffer_locations.end(),
            std::back_inserter(input_buffer_specs),
            [](auto loc) {
                return loc_buftype_ntiles_t{loc, tt::RouterBufferType::Relay, 1};
            });
        auto pipe_input_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
            buffer_map, input_buffer_specs);
        auto pipe_output_buffers = generate_multiple_dummy_router_buffers(soc_descriptor,
            buffer_map,
            {
                {output_buffer_location, tt::RouterBufferType::Relay, 1}
            }
        );
        std::vector<unique_id_t> pipe_input_buffer_ids = {pipe_input_buffers.at(0).id, pipe_input_buffers.at(1).id, pipe_input_buffers.at(2).id};
        pipe_map.insert({pipe_id, 
            pipe_t(gather_location, pipe_input_buffer_ids, {pipe_output_buffers.at(0).id})});
        buffer_input_pipes.insert({pipe_output_buffers.at(0).id, pipe_id});
        for (unique_id_t id : pipe_input_buffer_ids) { buffer_output_pipes[id].insert(pipe_id);}
    }

    auto test_router = generate_test_router(
        soc_descriptor, 
        2, 
        eth_datacopy_test_info_t{.buffer_map=buffer_map, .queue_map=queue_map, .op_queue_output_scatter=op_queue_output_scatter, .queue_settings_map=queue_settings_map, .pipes=pipe_map, .buffer_output_pipes=buffer_output_pipes, .buffer_input_pipes=buffer_input_pipes}
    );
    auto router = test_router->router.get();

    int num_gather_streams_for_fourth_gather_on_core = compute_pipe_segment_extra_stream_use(*router, pipe_segment_id_t{.pipe_id=gather_pipe_ids.back(), .segment=0}, gathering_core_cxy);
    ASSERT_EQ(num_gather_streams_for_fourth_gather_on_core, 1);
}

class HwCoreAttributesUnderTest : public HwCoreAttributes {
   public:
    template <typename... Args>
    HwCoreAttributesUnderTest(Args &&...args) : HwCoreAttributes(std::forward<Args>(args)...) {}
    void register_resident_multicast_pipe(pipe_segment_hash_t const&  pipe_segment_id) {
        HwCoreAttributes::register_resident_multicast_pipe(pipe_segment_id);
    }
    void deregister_resident_multicast_pipe(pipe_segment_hash_t const&  pipe_segment_id) {
        HwCoreAttributes::deregister_resident_multicast_pipe(pipe_segment_id);
    }
    void register_resident_gather_pipe(pipe_segment_hash_t const&  pipe_segment_id, int number_of_gather_inputs) {
        HwCoreAttributes::register_resident_gather_pipe(pipe_segment_id, number_of_gather_inputs);
    }
    void deregister_resident_gather_pipe(pipe_segment_hash_t const&  pipe_segment_id) {
        HwCoreAttributes::deregister_resident_gather_pipe(pipe_segment_id);
    }
    void register_resident_gather_multicast_pipe(pipe_segment_hash_t const&  pipe_segment_id, int number_of_gather_inputs) {
        HwCoreAttributes::register_resident_gather_multicast_pipe(pipe_segment_id, number_of_gather_inputs);
    }
    void deregister_resident_gather_multicast_pipe(pipe_segment_hash_t const&  pipe_segment_id) {
        HwCoreAttributes::deregister_resident_gather_multicast_pipe(pipe_segment_id);
    }
    void adjust_extra_streams(int amount) { HwCoreAttributes::adjust_extra_streams(amount); }
    int count_extra_streams_from_gather_and_multicast() const {
        return HwCoreAttributes::count_extra_streams_from_gather_and_multicast();
    }
};

TEST(Net2PipeRouterResourceManagement, TestFunctionCountExtraStreamsFromGatherAndMulticast_MCastMCastMCast_Total0) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});

    ASSERT_EQ(core.count_extra_streams_from_gather_and_multicast(), 0);
}

TEST(Net2PipeRouterResourceManagement, TestFunctionCountExtraStreamsFromGatherAndMulticast_MCastMCastMCastGather_Total1) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 4);

    ASSERT_EQ(core.count_extra_streams_from_gather_and_multicast(), 1);
}

TEST(Net2PipeRouterResourceManagement, TestFunctionCountExtraStreamsFromGatherAndMulticast_MCastMCastGather4Input_Total3) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 4);

    ASSERT_EQ(core.count_extra_streams_from_gather_and_multicast(), 3);
}

TEST(Net2PipeRouterResourceManagement, TestFunctionCountExtraStreamsFromGatherAndMulticast_MCastMCastGather2Input_Total4) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 2);

    ASSERT_EQ(core.count_extra_streams_from_gather_and_multicast(), 2);
}

TEST(
    Net2PipeRouterResourceManagement,
    TestFunctionCountExtraStreamsFromGatherAndMulticast_MCastGather3InputGather2Input_Total5) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}}, 3);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}}, 2);

    ASSERT_EQ(core.count_extra_streams_from_gather_and_multicast(), 5);
}

TEST(
    Net2PipeRouterResourceManagement,
    TestFunctionCountExtraStreamsFromGatherAndMulticast_Gather5InputGather3InputGather2Input_Total6Or7) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 2);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}}, 5);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}}, 3);
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});

    // We currently don't expect gather pipes to be sorted by input count so we could grant 2 extra streams for the 2
    // input gather or 3 extra streams for one of the other gather piepes
    int num_extra_streams = core.count_extra_streams_from_gather_and_multicast();
    ASSERT_GE(num_extra_streams, 6);
    ASSERT_LE(num_extra_streams, 7);
}

TEST(
    Net2PipeRouterResourceManagement,
    TestFunctionCountExtraStreamsFromGatherAndMulticast_Gather5InputGather3InputGatherMCast2Input_Total6) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}}, 5);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}}, 3);
    core.register_resident_gather_multicast_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 2);
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=4, .outputs={0}});

    ASSERT_EQ(core.count_extra_streams_from_gather_and_multicast(), 6);
}

TEST(
    Net2PipeRouterResourceManagement,
    TestFunctionCountExtraStreamsFromGatherAndMulticast_Gather5InputGatherMCast2InputGatherMCast2InputGatherMCast2InputMCast_Total6) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}}, 5);
    core.register_resident_gather_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}}, 2);
    core.register_resident_gather_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}}, 2);
    core.register_resident_gather_multicast_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 2);
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=4, .outputs={0}});

    ASSERT_EQ(core.count_extra_streams_from_gather_and_multicast(), 6);
}

TEST(
    Net2PipeRouterResourceManagement,
    TestFunctionCountExtraStreamsFromGatherAndMulticast_Gather5InputGatherMCast2InputGatherMCast2InputGatherMCast2Input_Total7) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}}, 5);
    core.register_resident_gather_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}}, 2);
    core.register_resident_gather_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}}, 2);
    core.register_resident_gather_multicast_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 2);

    ASSERT_EQ(core.count_extra_streams_from_gather_and_multicast(), 7);
}

TEST(Net2PipeRouterResourceManagement, TestRegisterMCastMCastMCastGather_1ExtraStream) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 5);
    ASSERT_EQ(core.get_used_extra_streams(), 1);
}

TEST(Net2PipeRouterResourceManagement, TestRegisterGatherMCastMCastMCast_1ExtraStreams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}}, 5);

    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}});
    ASSERT_EQ(core.get_used_extra_streams(), 1);
}

TEST(Net2PipeRouterResourceManagement, TestRegisterGatherMCastMCastMCastGather_2ExtraStreams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}}, 5);
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=4, .outputs={0}}, 5);
    ASSERT_EQ(core.get_used_extra_streams(), 2);
}

TEST(Net2PipeRouterResourceManagement, TestRegisterMCastMCastMCastGatherGather_2ExtraStreams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 5);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=4, .outputs={0}}, 5);
    ASSERT_EQ(core.get_used_extra_streams(), 2);
}

TEST(Net2PipeRouterResourceManagement, TestRegisterGatherMCastMCastMCastGatherGather_3ExtraStreams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}}, 5);
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=4, .outputs={0}}, 5);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=5, .outputs={0}}, 5);

    ASSERT_EQ(core.get_used_extra_streams(), 3);
}

TEST(Net2PipeRouterResourceManagement, TestRegisterMCastGatherGather_6ExtraStreams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}}, 5);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}}, 5);
    ASSERT_EQ(core.get_used_extra_streams(), 6);
}

TEST(Net2PipeRouterResourceManagement, TestRegisterMCastMCastGather_3ExtraStreams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 5);

    ASSERT_EQ(core.get_used_extra_streams(), 3);
}

TEST(Net2PipeRouterResourceManagement, TestRegisterMCastMCastGatherGather_4ExtraStreams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 5);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=4, .outputs={0}}, 5);
    ASSERT_EQ(core.get_used_extra_streams(), 4);
}

TEST(Net2PipeRouterResourceManagement, TestRegisterGatherGatherMCastMCast_4ExtraStreams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 5);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=4, .outputs={0}}, 5);
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});

    ASSERT_EQ(core.get_used_extra_streams(), 4);
}

TEST(Net2PipeRouterResourceManagement, TestMCastMCastMCastGather4Streams_RemoveMCast_3Streams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 5);
    ASSERT_EQ(core.get_used_extra_streams(), 1);  // 3 Mcast restricts multi-stream gather optimization
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});

    ASSERT_EQ(
        core.get_used_extra_streams(), 3);  // Only 2 Mcast now so we can implement the multi-stream gather optimization
}

TEST(Net2PipeRouterResourceManagement, TestMCastMCastMCastGather4Streams_RemoveMCastMCastMCast_3Streams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 5);

    ASSERT_EQ(core.get_used_extra_streams(), 1);  // 3 Mcast restricts multi-stream gather optimization
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});

    ASSERT_EQ(
        core.get_used_extra_streams(), 3);  // Only 2 Mcast now so we can implement the multi-stream gather optimization
}

TEST(Net2PipeRouterResourceManagement, TestMCastMCastMCastGather4Streams_RemoveMCastMCastGather_0Streams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 5);

    ASSERT_EQ(core.get_used_extra_streams(), 1);  // 3 Mcast restricts multi-stream gather optimization
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.deregister_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}});

    ASSERT_EQ(
        core.get_used_extra_streams(), 0);  
}

TEST(Net2PipeRouterResourceManagement, TestMCastMCastMCastGather4Streams_RemoveNetMCastMCastMCast_3Streams) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=3, .outputs={0}}, 5);

    ASSERT_EQ(core.get_used_extra_streams(), 1);  // 3 Mcast restricts multi-stream gather optimization

    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    ASSERT_EQ(
        core.get_used_extra_streams(), 3);  // Only 2 Mcast now so we can implement the multi-stream gather optimization

    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.register_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});

    ASSERT_EQ(core.get_used_extra_streams(), 1);  // 3 Mcast restricts multi-stream gather optimization

    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}});
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=1, .outputs={0}});
    core.deregister_resident_multicast_pipe(pipe_segment_hash_t{.pipe_id=2, .outputs={0}});
    ASSERT_EQ(
        core.get_used_extra_streams(), 3);  // Only 2 Mcast now so we can implement the multi-stream gather optimization
}

TEST(
    Net2PipeRouterResourceManagement,
    TestFunctionCountExtraStreamsFromGatherAndMulticast_GatherScatterPipe4Segments_Total12) {
    auto core = HwCoreAttributesUnderTest(10000000, 8, 8, 40, false, MAX_MCAST_STREAMS_PER_CORE_GRAYSKULL);
    // mcast doesn't use "extra" streams
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={0}}, 16);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={1}}, 16);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={2}}, 16);
    core.register_resident_gather_pipe(pipe_segment_hash_t{.pipe_id=0, .outputs={3}}, 16);

    ASSERT_EQ(core.count_extra_streams_from_gather_and_multicast(), 12);
}