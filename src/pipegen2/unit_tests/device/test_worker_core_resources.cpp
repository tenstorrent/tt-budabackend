// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <set>
#include <stdexcept>

#include <gtest/gtest.h>

#include "l1_address_map.h"
#include "noc/noc_overlay_parameters.h"
#include "stream_io_map.h"

#include "core_resources_unit_test_utils.h"
#include "device/core_resources_constants.h"
#include "device/operand_stream_map.h"
#include "device/worker_core_resources_gs.h"
#include "device/worker_core_resources_wh.h"
#include "model/typedefs.h"
#include "pipegen2_constants.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;
using namespace unit_test_utils;

/**********************************************************************************************************************
    Test for function: allocate_packer_stream
**********************************************************************************************************************/

TEST(Pipegen2_WorkerCoreResources, AllocatePackerStream_InvalidOperandID)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    // We should be able to allocate packer stream only for valid output operand ID. Everything else should throw an
    // error.
    for (unsigned int operand_id = OPERAND_INPUT_START_INDEX; operand_id < OPERAND_OUTPUT_START_INDEX; operand_id++)
    {
        verify_throws_exception_with_message<std::runtime_error>(
            [&]()
            {
                worker_core_resources.allocate_packer_stream(operand_id);
            },
            "(.*)Trying to allocate packer stream (.*) with invalid operand ID(.*)");
    }

    for (unsigned int operand_id = OPERAND_RELAY_START_INDEX; operand_id < MAX_NUM_OPERANDS; operand_id++)
    {
        verify_throws_exception_with_message<std::runtime_error>(
            [&]()
            {
                worker_core_resources.allocate_packer_stream(operand_id);
            },
            "(.*)Trying to allocate packer stream (.*) with invalid operand ID(.*)");
    }
}

TEST(Pipegen2_WorkerCoreResources, AllocatePackerStream_AllocateTwiceForSameOperandID)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    EXPECT_NO_THROW(worker_core_resources.allocate_packer_stream(OPERAND_OUTPUT_START_INDEX));

    verify_throws_exception_with_message<std::runtime_error>(
        [&]()
        {
            worker_core_resources.allocate_packer_stream(OPERAND_OUTPUT_START_INDEX);
        },
        "(.*)Trying to allocate packer stream (.*) which was already allocated(.*)");
}

TEST(Pipegen2_WorkerCoreResources, AllocatePackerStream_AllocateAllValidOperandIDs)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    for (unsigned int operand_id = OPERAND_OUTPUT_START_INDEX; operand_id < OPERAND_RELAY_START_INDEX; operand_id++)
    {
        StreamId expected_allocated_stream = OperandStreamMap::get_operand_stream_id(operand_id);
        verify_no_throw_and_return_value_eq<StreamId>(
            [&]() -> StreamId
            {
                return worker_core_resources.allocate_packer_stream(operand_id);
            },
            expected_allocated_stream);
    }
}

TEST(Pipegen2_WorkerCoreResources, AllocatePackerStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    // Should be empty.
    EXPECT_TRUE(worker_core_resources.get_allocated_stream_ids().empty());

    EXPECT_NO_THROW(worker_core_resources.allocate_packer_stream(OPERAND_OUTPUT_START_INDEX));

    // Should contain only one stream id from the beginning of packer range.
    StreamId expected_allocated_stream = OperandStreamMap::get_operand_stream_id(OPERAND_OUTPUT_START_INDEX);
    EXPECT_EQ(worker_core_resources.get_allocated_stream_ids(), std::set<StreamId>{expected_allocated_stream});
}

/**********************************************************************************************************************
    Test for function: allocate_unpacker_stream
**********************************************************************************************************************/

TEST(Pipegen2_WorkerCoreResources, AllocateUnpackerStream_InvalidOperandID)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    // We should be able to allocate unpacker stream only for valid input operand ID. Everything else should throw an
    // error.
    for (unsigned int operand_id = OPERAND_OUTPUT_START_INDEX;
         operand_id < OPERAND_INTERMEDIATES_START_INDEX;
         operand_id++)
    {
        verify_throws_exception_with_message<std::runtime_error>(
            [&]()
            {
                worker_core_resources.allocate_unpacker_stream(operand_id);
            },
            "(.*)Trying to allocate unpacker stream (.*) with invalid operand ID(.*)");
    }

    for (unsigned int operand_id = OPERAND_RELAY_START_INDEX; operand_id < MAX_NUM_OPERANDS; operand_id++)
    {
        verify_throws_exception_with_message<std::runtime_error>(
            [&]()
            {
                worker_core_resources.allocate_unpacker_stream(operand_id);
            },
            "(.*)Trying to allocate unpacker stream (.*) with invalid operand ID(.*)");
    }
}

TEST(Pipegen2_WorkerCoreResources, AllocateUnpackerStream_AllocateTwiceForSameOperandID)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    EXPECT_NO_THROW(worker_core_resources.allocate_unpacker_stream(OPERAND_INPUT_START_INDEX));

    verify_throws_exception_with_message<std::runtime_error>(
        [&]()
        {
            worker_core_resources.allocate_unpacker_stream(OPERAND_INPUT_START_INDEX);
        },
        "(.*)Trying to allocate unpacker stream (.*) which was already allocated(.*)");
}

TEST(Pipegen2_WorkerCoreResources, AllocateUnpackerStream_AllocateAllValidOperandIDs)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    for (unsigned int operand_id = OPERAND_INPUT_START_INDEX; operand_id < OPERAND_OUTPUT_START_INDEX; operand_id++)
    {
        StreamId expected_allocated_stream = OperandStreamMap::get_operand_stream_id(operand_id);
        verify_no_throw_and_return_value_eq<StreamId>(
            [&]() -> StreamId
            {
                return worker_core_resources.allocate_unpacker_stream(operand_id);
            },
            expected_allocated_stream);
    }

    // Intermediate operand IDs are also considered input operand IDs.
    for (unsigned int operand_id = OPERAND_INTERMEDIATES_START_INDEX;
         operand_id < OPERAND_RELAY_START_INDEX;
         operand_id++)
    {
        StreamId expected_allocated_stream = OperandStreamMap::get_operand_stream_id(operand_id);
        verify_no_throw_and_return_value_eq<StreamId>(
            [&]() -> StreamId
            {
                return worker_core_resources.allocate_unpacker_stream(operand_id);
            },
            expected_allocated_stream);
    }
}

TEST(Pipegen2_WorkerCoreResources, AllocateUnpackerStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    // Should be empty.
    EXPECT_TRUE(worker_core_resources.get_allocated_stream_ids().empty());

    EXPECT_NO_THROW(worker_core_resources.allocate_unpacker_stream(OPERAND_INPUT_START_INDEX));

    // Should contain only one stream id from the beginning of packer range.
    StreamId expected_allocated_stream = OperandStreamMap::get_operand_stream_id(OPERAND_INPUT_START_INDEX);
    EXPECT_EQ(worker_core_resources.get_allocated_stream_ids(), std::set<StreamId>{expected_allocated_stream});
}

/**********************************************************************************************************************
    Test for function: allocate_intermed_stream
**********************************************************************************************************************/

TEST(Pipegen2_WorkerCoreResources, AllocateIntermedStream_InvalidOperandID)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    // We should be able to allocate intermed stream only for valid intermed operand ID. Everything else should throw an
    // error.
    for (unsigned int operand_id = OPERAND_INPUT_START_INDEX;
         operand_id < OPERAND_INTERMEDIATES_START_INDEX;
         operand_id++)
    {
        verify_throws_exception_with_message<std::runtime_error>(
            [&]()
            {
                worker_core_resources.allocate_intermed_stream(operand_id);
            },
            "(.*)Trying to allocate intermed stream (.*) with invalid operand ID(.*)");
    }

    for (unsigned int operand_id = OPERAND_RELAY_START_INDEX; operand_id < MAX_NUM_OPERANDS; operand_id++)
    {
        verify_throws_exception_with_message<std::runtime_error>(
            [&]()
            {
                worker_core_resources.allocate_intermed_stream(operand_id);
            },
            "(.*)Trying to allocate intermed stream (.*) with invalid operand ID(.*)");
    }
}

TEST(Pipegen2_WorkerCoreResources, AllocateIntermedStream_AllocateTwiceForSameOperandID)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    EXPECT_NO_THROW(worker_core_resources.allocate_intermed_stream(OPERAND_INTERMEDIATES_START_INDEX));

    verify_throws_exception_with_message<std::runtime_error>(
        [&]()
        {
            worker_core_resources.allocate_intermed_stream(OPERAND_INTERMEDIATES_START_INDEX);
        },
        "(.*)Trying to allocate intermed stream (.*) which was already allocated(.*)");
}

TEST(Pipegen2_WorkerCoreResources, AllocateIntermedStream_AllocateAllValidOperandIDs)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    for (unsigned int operand_id = OPERAND_INTERMEDIATES_START_INDEX;
         operand_id < OPERAND_RELAY_START_INDEX;
         operand_id++)
    {
        StreamId expected_allocated_stream = OperandStreamMap::get_operand_stream_id(operand_id);
        verify_no_throw_and_return_value_eq<StreamId>(
            [&]() -> StreamId
            {
                return worker_core_resources.allocate_intermed_stream(operand_id);
            },
            expected_allocated_stream);
    }
}

TEST(Pipegen2_WorkerCoreResources, AllocateIntermedStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});

    // Should be empty.
    EXPECT_TRUE(worker_core_resources.get_allocated_stream_ids().empty());

    EXPECT_NO_THROW(worker_core_resources.allocate_intermed_stream(OPERAND_INTERMEDIATES_START_INDEX));

    // Should contain only one stream id from the beginning of packer range.
    StreamId expected_allocated_stream = OperandStreamMap::get_operand_stream_id(OPERAND_INTERMEDIATES_START_INDEX);
    EXPECT_EQ(worker_core_resources.get_allocated_stream_ids(), std::set<StreamId>{expected_allocated_stream});
}

/**********************************************************************************************************************
    Test for function: allocate_packer_multicast_stream
**********************************************************************************************************************/

TEST(Pipegen2_WorkerCoreResources, AllocatePackerMulticastStream_AllocateTwiceForSameOperandId)
{
    WorkerCoreResourcesWH worker_core_resources({0, 0, 0});

    EXPECT_NO_THROW(worker_core_resources.allocate_packer_multicast_stream(
        worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start));

    verify_throws_exception_with_message<std::runtime_error>(
        [&]()
        {
            worker_core_resources.allocate_packer_multicast_stream(
                worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start);
        },
        "(.*)Trying to allocate packer-multicast stream (.*) which was already allocated(.*)");
}

TEST(Pipegen2_WorkerCoreResources, AllocatePackerMulticastStream_ProperAllocatedStreamStorage)
{
    WorkerCoreResourcesWH worker_core_resources({0, 0, 0});

    // Should be empty.
    EXPECT_TRUE(worker_core_resources.get_allocated_stream_ids().empty());

    EXPECT_NO_THROW(worker_core_resources.allocate_packer_multicast_stream(
        worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start));

    // Should contain only one stream id from the beginning of packer-multicast range.
    StreamId expected_allocated_stream =
        worker_core_resources_wh_constants::gather_multicast_streams_id_range_end -
        OperandStreamMap::get_output_index(
            worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start);

    EXPECT_EQ(worker_core_resources.get_allocated_stream_ids(), std::set<StreamId>{expected_allocated_stream});
}

/**********************************************************************************************************************
    Test for function: get_next_available_general_purpose_stream_id
    (through public allocate_general_purpose_stream)
**********************************************************************************************************************/

TEST(Pipegen2_WorkerCoreResources, GetNextAvailableGeneralPurposeStreamId_RepeatedCallsUntilExcThrown)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0});
    std::vector<StreamId> expected_stream_ids;

    // Expecting streams to be allocated in a certain order.
    for (uint8_t stream_id = static_cast<StreamId>(END_IO_STREAM + 1); /* extra streams range start */
         stream_id <= static_cast<StreamId>(NOC_NUM_STREAMS - 1); /* extra streams range end */
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // Expecting streams to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<StreamId>(
        [&]() -> StreamId
        {
            return worker_core_resources.allocate_general_purpose_stream();
        },
        expected_stream_ids);
}
