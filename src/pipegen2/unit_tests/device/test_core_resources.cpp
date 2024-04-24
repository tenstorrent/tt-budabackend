// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <set>
#include <stdexcept>

#include <gtest/gtest.h>

#include "l1_address_map.h"
#include "stream_io_map.h"

#include "core_resources_unit_test_utils.h"
#include "device/core_resources_constants.h"
#include "device/l1/l1_buffer.h"
#include "device/worker_core_resources_gs.h"
#include "model/stream_graph/stream_node.h"
#include "model/typedefs.h"
#include "pipegen2_constants.h"

using namespace pipegen2;
using namespace unit_test_utils;

/**********************************************************************************************************************
    Tests for function: allocate_gather_stream
    (which also indirectly tests get_allocated_stream_ids)
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateGatherStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0}, {0, 0, 0});

    // Should be empty.
    EXPECT_TRUE(worker_core_resources.get_allocated_stream_ids().empty());

    EXPECT_NO_THROW(worker_core_resources.allocate_gather_stream());

    // Should contain only one stream id from the beginning of gather-multicast range.
    EXPECT_EQ(
        worker_core_resources.get_allocated_stream_ids(),
        std::set<StreamId>{worker_core_resources_gs_constants::gather_multicast_streams_id_range_start});
}

/**********************************************************************************************************************
    Tests for function: allocate_multicast_stream
    (which also indirectly tests get_allocated_stream_ids)
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateMulticastStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0}, {0, 0, 0});

    // Should be empty.
    EXPECT_TRUE(worker_core_resources.get_allocated_stream_ids().empty());

    EXPECT_NO_THROW(worker_core_resources.allocate_multicast_stream());

    // Should contain only one stream id from the beginning of gather-multicast range.
    EXPECT_EQ(
        worker_core_resources.get_allocated_stream_ids(),
        std::set<StreamId>{worker_core_resources_gs_constants::gather_multicast_streams_id_range_start});
}

/**********************************************************************************************************************
    Tests for function: allocate_general_purpose_stream
    (which also indirectly tests get_allocated_stream_ids)
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateGeneralPurposeStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0}, {0, 0, 0});

    // Should be empty.
    EXPECT_TRUE(worker_core_resources.get_allocated_stream_ids().empty());

    EXPECT_NO_THROW(worker_core_resources.allocate_general_purpose_stream());

    // Should contain only one stream id from the beginning of extra streams range.
    EXPECT_EQ(
        worker_core_resources.get_allocated_stream_ids(),
        std::set<StreamId>{static_cast<StreamId>(END_IO_STREAM + 1)});
}

/**********************************************************************************************************************
    Tests for function: allocate_kernel_input
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateKernelInput_RepeatedCallsUntilExcThrown)
{
    const tt_cxy_pair core_physical_location{0, 0, 0};
    const tt_cxy_pair core_logical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location, core_logical_location);
    std::vector<StreamId> expected_kernel_inputs;

    for (unsigned int input_index = 0;
         input_index < core_resources_constants::max_kernel_inputs_count;
         input_index++)
    {
        expected_kernel_inputs.push_back(input_index);
    }

    // Expecting kernel inputs to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<unsigned int, OutOfCoreResourcesException>(
        [&]() -> unsigned int
        {
            return worker_core_resources.allocate_kernel_input();
        },
        expected_kernel_inputs,
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                core_physical_location,
                core_logical_location,
                OutOfCoreResourcesException::CoreResourceType::kKernelInputIndex,
                expected_kernel_inputs.size(),
                expected_kernel_inputs.size() + 1);
        });
}

/**********************************************************************************************************************
    Tests for function: allocate_kernel_output
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateKernelOutput_RepeatedCallsUntilExcThrown)
{
    const tt_cxy_pair core_physical_location{0, 0, 0};
    const tt_cxy_pair core_logical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location, core_logical_location);
    std::vector<StreamId> expected_kernel_outputs;

    for (unsigned int output_index = 0;
         output_index < core_resources_constants::max_kernel_outputs_count;
         output_index++)
    {
        expected_kernel_outputs.push_back(output_index);
    }

    // Expecting kernel outputs to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<unsigned int, OutOfCoreResourcesException>(
        [&]() -> unsigned int
        {
            return worker_core_resources.allocate_kernel_output();
        },
        expected_kernel_outputs,
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                core_physical_location,
                core_logical_location,
                OutOfCoreResourcesException::CoreResourceType::kKernelOutputIndex,
                expected_kernel_outputs.size(),
                expected_kernel_outputs.size() + 1);
        });
}

/**********************************************************************************************************************
    Tests for function: get_multicast_streams_count
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, GetMulticastStreamsCount_ExpectingExactReturnValue)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0}, {0, 0, 0});

    // Number of multicast streams on worker core is equal to gather/multicast stream ids range since multicast streams
    // are allocated from gather/multicast pool.
    unsigned int expected_multicast_streams_count =
        (worker_core_resources_gs_constants::gather_multicast_streams_id_range_end -
         worker_core_resources_gs_constants::gather_multicast_streams_id_range_start + 1);

    EXPECT_EQ(worker_core_resources.get_multicast_streams_count(), expected_multicast_streams_count);
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_tile_header_buffer
    (this is just sanity test for this thin wrapper. Thorough test is provided in test_l1_data_buffers_memory_layout)
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateL1TileHeaderBuffer_SanityCheck)
{
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    int l1_data_buffers_space_start_address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    int l1_data_buffers_space_end_address = l1_mem::address_map::MAX_SIZE - constants::unused_data_buffers_space_bytes;
    int l1_data_buffers_available_space = l1_data_buffers_space_end_address - l1_data_buffers_space_start_address;
    int tile_header_buffer_size = constants::tile_header_buffer_size_bytes;

    // Max number of tile header buffers that can be placed in l1 space for data buffers.
    unsigned int max_num_tile_header_buffers = l1_data_buffers_available_space / tile_header_buffer_size;

    const tt_cxy_pair core_physical_location{0, 0, 0};
    const tt_cxy_pair core_logical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location, core_logical_location);

    // The first allocated tile header buffer is allocated in predesignated space for a single tile header buffer.
    const L1Buffer* tile_header_buffer = worker_core_resources.allocate_l1_tile_header_buffer(0);
    EXPECT_EQ(tile_header_buffer->get_address(), worker_core_resources_constants::l1_predefined_tile_header_buffer_address);

    unsigned int tile_header_buff_expected_addr = l1_data_buffers_space_end_address;
    // All the following allocations will end up in l1 data buffer space.
    for (unsigned int test_tile_size = 1; test_tile_size <= max_num_tile_header_buffers; test_tile_size++)
    {
        tile_header_buff_expected_addr -= constants::tile_header_buffer_size_bytes;
        tile_header_buffer = worker_core_resources.allocate_l1_tile_header_buffer(test_tile_size);
        EXPECT_EQ(tile_header_buffer->get_address(), tile_header_buff_expected_addr);
    }

    // Verify we are not out of space.
    EXPECT_NO_THROW(worker_core_resources.check_if_out_of_l1_data_buffers_memory());
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_stream_buffer
    (this is just sanity test for this thin wrapper. Thorough test is provided in test_l1_data_buffers_memory_layout)
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateL1StreamBuffer_SanityCheck)
{
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    int l1_data_buffers_space_start_address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    int l1_data_buffers_space_end_address = l1_mem::address_map::MAX_SIZE - constants::unused_data_buffers_space_bytes;
    unsigned int available_space = l1_data_buffers_space_end_address - l1_data_buffers_space_start_address;

    const tt_cxy_pair core_physical_location{0, 0, 0};
    const tt_cxy_pair core_logical_location{0, 0, 0};
    StreamNode stream_node(StreamType::Gather, core_physical_location, 0);
    WorkerCoreResourcesGS worker_core_resources(core_physical_location, core_logical_location);

    // Allocate nothing, dummy call.
    const L1Buffer* l1_current_data_buffer = worker_core_resources.allocate_l1_stream_buffer(&stream_node, 0);
    EXPECT_EQ(l1_current_data_buffer, nullptr);

    // Allocate almost entire space, just leave one byte unallocated.
    unsigned int allocated_l1_data_buffers_size = available_space - 1;

    EXPECT_NO_THROW(
        l1_current_data_buffer = worker_core_resources.allocate_l1_stream_buffer(
            &stream_node, allocated_l1_data_buffers_size));
    EXPECT_EQ(
        l1_current_data_buffer->get_address(), l1_data_buffers_space_end_address - allocated_l1_data_buffers_size);
    EXPECT_EQ(
        l1_current_data_buffer->get_size(), allocated_l1_data_buffers_size);
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_ncrisc_fallback_buffer
    (this is just sanity test for this thin wrapper. Thorough test is provided in test_l1_data_buffers_memory_layout)
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateL1NcriscFallbackBuffer_SanityCheck)
{
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    int l1_data_buffers_space_start_address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    int l1_data_buffers_space_end_address = l1_mem::address_map::MAX_SIZE - constants::unused_data_buffers_space_bytes;
    unsigned int available_space = l1_data_buffers_space_end_address - l1_data_buffers_space_start_address;

    const tt_cxy_pair core_physical_location{0, 0, 0};
    const tt_cxy_pair core_logical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location, core_logical_location);

    // Allocate nothing, dummy call.
    const L1Buffer* l1_current_data_buffer = worker_core_resources.allocate_l1_ncrisc_fallback_buffer(0);
    EXPECT_EQ(l1_current_data_buffer, nullptr);

    // Allocate almost entire space, just leave one byte unallocated.
    unsigned int allocated_l1_data_buffers_size = available_space - 1;

    EXPECT_NO_THROW(
        l1_current_data_buffer = worker_core_resources.allocate_l1_ncrisc_fallback_buffer(
            allocated_l1_data_buffers_size));
    EXPECT_EQ(
        l1_current_data_buffer->get_address(), l1_data_buffers_space_end_address - allocated_l1_data_buffers_size);
    EXPECT_EQ(
        l1_current_data_buffer->get_size(), allocated_l1_data_buffers_size);
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_extra_overlay_blob_space
    (this is just sanity test for this thin wrapper. Thorough test is provided in test_l1_data_buffers_memory_layout)
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateL1ExtraOverlayBlobSpace_SanityCheck)
{
    // Memory is allocated from the beginning of L1 data buffers space towards end.
    int l1_data_buffers_space_start_address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;

    const tt_cxy_pair core_physical_location{0, 0, 0};
    const tt_cxy_pair core_logical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location, core_logical_location);

    // Allocate extra 60KB of blob space.
    unsigned int extra_blob_space = 0xF000;
    unsigned int total_blob_size = l1_mem::address_map::OVERLAY_BLOB_SIZE + extra_blob_space;

    const L1Buffer* l1_current_data_buffer;
    EXPECT_NO_THROW(
        l1_current_data_buffer = worker_core_resources.allocate_l1_extra_overlay_blob_space(
            total_blob_size, false));
    EXPECT_EQ(
        l1_current_data_buffer->get_address(), l1_data_buffers_space_start_address);
    EXPECT_EQ(
        l1_current_data_buffer->get_size(), extra_blob_space);
}


/**********************************************************************************************************************
    Tests for function: get_tile_header_buffer_address
    (this is just sanity test for this thin wrapper. Thorough test is provided in test_l1_data_buffers_memory_layout)
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, GetTileHeaderBufferAddress_ExpectingExactReturnValue)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0}, {0, 0, 0});

    const unsigned int l1_data_buffers_space_end_address =
        l1_mem::address_map::MAX_SIZE - constants::unused_data_buffers_space_bytes;

    const unsigned int dummy_tile_size_1 = 1;
    const unsigned int dummy_tile_size_2 = 2;

    // No tile header buffers allocated still.
    EXPECT_THROW(worker_core_resources.get_tile_header_buffer_address(dummy_tile_size_1), std::runtime_error);

    // First THB is allocated at predefined location.
    worker_core_resources.allocate_l1_tile_header_buffer(dummy_tile_size_1);
    EXPECT_EQ(
        worker_core_resources.get_tile_header_buffer_address(dummy_tile_size_1),
        worker_core_resources_constants::l1_predefined_tile_header_buffer_address);

    // All other THBs are allocated from end towards beginning of L1 data buffers space.
    worker_core_resources.allocate_l1_tile_header_buffer(dummy_tile_size_2);
    const unsigned int expected_thb_address =
        l1_data_buffers_space_end_address - constants::tile_header_buffer_size_bytes;

    EXPECT_EQ(worker_core_resources.get_tile_header_buffer_address(dummy_tile_size_2), expected_thb_address);
}