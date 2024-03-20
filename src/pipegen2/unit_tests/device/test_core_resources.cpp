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
#include "device/core_resources_unit_test_utils.h"
#include "device/l1_memory_allocation.h"
#include "device/worker_core_resources_gs.h"
#include "model/stream_graph/stream_node.h"
#include "model/typedefs.h"
#include "pipegen2_constants.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;
using namespace unit_test_utils;

/**********************************************************************************************************************
    Tests for function: allocate_gather_stream
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateGatherStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

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
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateMulticastStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

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
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateGeneralPurposeStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    // Should be empty.
    EXPECT_TRUE(worker_core_resources.get_allocated_stream_ids().empty());

    EXPECT_NO_THROW(worker_core_resources.allocate_general_purpose_stream());

    // Should contain only one stream id from the beginning of extra streams range.
    EXPECT_EQ(
        worker_core_resources.get_allocated_stream_ids(),
        std::set<StreamId>{static_cast<StreamId>(END_IO_STREAM + 1)});
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_extra_tile_headers_space
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateL1ExtraTileHeadersSpace_AllocateUntilOutOfMemory)
{
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    int l1_data_buffers_available_space =
        (l1_mem::address_map::MAX_SIZE - constants::unused_data_buffers_space_bytes) -
        l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    int tile_header_buffer_size = constants::tile_header_size_bytes * constants::general_max_num_tiles_per_phase;
    unsigned int max_num_tile_header_buffers = l1_data_buffers_available_space / tile_header_buffer_size;

    const tt_cxy_pair core_physical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location);

    // Fill entire data buffers memory with tile headers, but don't overflow.
    worker_core_resources.allocate_l1_extra_tile_headers_space(max_num_tile_header_buffers);

    // Allocate one too many tile headers. This will overflow L1 space.
    worker_core_resources.allocate_l1_extra_tile_headers_space(1);

    // Expect an error to be thrown when specifically checking if L1 is out of memory.
    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]()
        {
            worker_core_resources.check_if_out_of_l1_data_buffers_memory();
        },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                core_physical_location,
                OutOfCoreResourcesException::CoreResourceType::kL1DataBuffersMemory,
                l1_data_buffers_available_space,
                (max_num_tile_header_buffers + 1) * tile_header_buffer_size);
        });
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_data_buffer
    (which also indirectly tests allocate_l1_extra_overlay_blob_space and check_if_out_of_l1_data_buffers_memory)
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateL1DataBuffer_AllocateUntilOutOfMemory)
{
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    int l1_data_buffers_space_start_address = l1_mem::address_map::DATA_BUFFER_SPACE_BASE;
    int l1_data_buffers_space_end_address = l1_mem::address_map::MAX_SIZE - constants::unused_data_buffers_space_bytes;
    unsigned int available_space = l1_data_buffers_space_end_address - l1_data_buffers_space_start_address;
    unsigned int allocated_l1_data_buffers_size = available_space - 1;

    const tt_cxy_pair core_physical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location);

    // Allocate nothing, dummy call.
    unsigned int l1_current_data_buffers_space_address;
    EXPECT_NO_THROW(l1_current_data_buffers_space_address = worker_core_resources.allocate_l1_data_buffer(0));
    EXPECT_EQ(l1_current_data_buffers_space_address, l1_data_buffers_space_end_address);

    // Allocate almost entire space, just leave one byte unallocated.
    EXPECT_NO_THROW(
        l1_current_data_buffers_space_address = worker_core_resources.allocate_l1_data_buffer(
            allocated_l1_data_buffers_size));
    EXPECT_EQ(
        l1_current_data_buffers_space_address, l1_data_buffers_space_end_address - allocated_l1_data_buffers_size);

    // Allocate one more byte for extra overlay blob space.
    EXPECT_NO_THROW(worker_core_resources.allocate_l1_extra_overlay_blob_space(1));

    // Allocate one too many tile headers. This will overflow L1 space.
    worker_core_resources.allocate_l1_data_buffer(1);

    // Expect an error to be thrown when specifically checking if L1 is out of memory.
    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]()
        {
            worker_core_resources.check_if_out_of_l1_data_buffers_memory();
        },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                core_physical_location,
                OutOfCoreResourcesException::CoreResourceType::kL1DataBuffersMemory,
                allocated_l1_data_buffers_size,
                allocated_l1_data_buffers_size + 1);
        });
}

/**********************************************************************************************************************
    Tests for function: allocate_kernel_input
**********************************************************************************************************************/

TEST(Pipegen2_CoreResources, AllocateKernelInput_RepeatedCallsUntilExcThrown)
{
    const tt_cxy_pair core_physical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location);
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
    WorkerCoreResourcesGS worker_core_resources(core_physical_location);
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
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    // Number of multicast streams on worker core is equal to gather/multicast stream ids range since multicast streams
    // are allocated from gather/multicast pool.
    unsigned int expected_multicast_streams_count =
        (worker_core_resources_gs_constants::gather_multicast_streams_id_range_end -
         worker_core_resources_gs_constants::gather_multicast_streams_id_range_start + 1);

    EXPECT_EQ(worker_core_resources.get_multicast_streams_count(), expected_multicast_streams_count);
}

/**********************************************************************************************************************
    Tests for function: track_stream_buffer_allocation
**********************************************************************************************************************/
TEST(Pipegen2_CoreResources, AddStreamBufferAllocation_AddingOneStream)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});
    StreamNode stream_node(StreamType::Gather, tt_cxy_pair(0, 0, 0), 0);
    stream_node.assign_stream_id(1);
    
    worker_core_resources.track_stream_buffer_allocation(&stream_node, 1000, 0);
    EXPECT_EQ(worker_core_resources.get_all_memory_allocations().size(), 1);
    EXPECT_EQ(worker_core_resources.get_all_memory_allocations()[0]->get_size(), 1000);
    EXPECT_EQ(worker_core_resources.get_all_memory_allocations()[0]->get_address(), 0);
}