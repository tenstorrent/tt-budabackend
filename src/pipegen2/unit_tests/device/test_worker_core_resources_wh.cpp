// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <stdexcept>

#include <gtest/gtest.h>

#include "stream_io_map.h"

#include "core_resources_unit_test_utils.h"
#include "device/core_resources_constants.h"
#include "device/operand_stream_map.h"
#include "device/worker_core_resources_wh.h"
#include "model/typedefs.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;
using namespace unit_test_utils;

/**********************************************************************************************************************
    Test fixture used where GS arch is expected
**********************************************************************************************************************/

class Pipegen2_WorkerCoreResourcesWH : public testing::Test
{
protected:
    void SetUp() override
    {
        if (!unit_test_utils::is_built_for_arch("wormhole_b0"))
        {
            // Test skipped since it is only valid for wormhole_b0 arch.
            GTEST_SKIP();
        }
    }
};

/**********************************************************************************************************************
    Tests for function: get_next_available_gather_multicast_stream_id
    (through public allocate_gather_stream / or allocate_multicast_stream, whichever is fine)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesWH, GetNextAvailableGatherMulticastStreamId_RepeatedCallsUntilExcThrown)
{
    WorkerCoreResourcesWH worker_core_resources({0, 0, 0});
    std::vector<StreamId> expected_stream_ids;

    for (uint8_t stream_id = worker_core_resources_wh_constants::gather_multicast_streams_id_range_start;
         stream_id <= worker_core_resources_wh_constants::gather_multicast_streams_id_range_end;
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // Expecting streams to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<StreamId>(
        [&]() -> StreamId
        {
            return worker_core_resources.allocate_gather_stream();
        },
        expected_stream_ids);
}

/**********************************************************************************************************************
    Tests for function: get_next_available_gather_stream_id
    (allocate_gather_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesWH, GetNextAvailableGatherStreamId_ExpectingExactReturnValue)
{
    WorkerCoreResourcesWH worker_core_resources({0, 0, 0});

    // Sanity check that first call will return stream from beginning of the range.
    verify_no_throw_and_return_value_eq<StreamId>(
        [&]() -> StreamId
        {
            return worker_core_resources.allocate_gather_stream();
        },
        worker_core_resources_wh_constants::gather_multicast_streams_id_range_start);
}

/**********************************************************************************************************************
    Tests for function: get_next_available_multicast_stream_id
    (allocate_multicast_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesWH, GetNextAvailableMulticastStreamId_ExpectingExactReturnValue)
{
    WorkerCoreResourcesWH worker_core_resources({0, 0, 0});

    // Sanity check that first call will return stream from beginning of the range.
    verify_no_throw_and_return_value_eq<StreamId>(
        [&]() -> StreamId
        {
            return worker_core_resources.allocate_multicast_stream();
        },
        worker_core_resources_wh_constants::gather_multicast_streams_id_range_start);
}

/**********************************************************************************************************************
    Tests for function: get_packer_multicast_stream_id
    (through public allocate_packer_multicast_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesWH, AllocatePackerMulticastStream_InvalidOperandID)
{
    WorkerCoreResourcesWH worker_core_resources({0, 0, 0});

    // We should be able to allocate packer-multicast stream only for valid packer-multicast stream operand ID.
    // Everything else should throw an error.
    for (unsigned int operand_id = OPERAND_INPUT_START_INDEX;
         operand_id < OPERAND_OUTPUT_START_INDEX;
         operand_id++)
    {
        EXPECT_THROW(worker_core_resources.allocate_packer_multicast_stream(operand_id), std::runtime_error);
    }

    for (unsigned int operand_id = worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_end + 1;
         operand_id < MAX_NUM_OPERANDS;
         operand_id++)
    {
        EXPECT_THROW(worker_core_resources.allocate_packer_multicast_stream(operand_id), std::runtime_error);
    }
}

TEST_F(Pipegen2_WorkerCoreResourcesWH, AllocatePackerMulticastStream_AllocateAllValidOperandIDs)
{
    WorkerCoreResourcesWH worker_core_resources({0, 0, 0});

    for (unsigned int operand_id = worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start;
         operand_id < worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_end;
         operand_id++)
    {
        StreamId expected_allocated_stream =
            worker_core_resources_wh_constants::gather_multicast_streams_id_range_end -
            OperandStreamMap::get_output_index(operand_id);

        verify_no_throw_and_return_value_eq<StreamId>(
            [&]() -> StreamId
            {
                return worker_core_resources.allocate_packer_multicast_stream(operand_id);
            },
            expected_allocated_stream);
    }
}

/**********************************************************************************************************************
    Tests for function: calculate_multicast_streams_count
    (through public get_multicast_streams_count)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesWH, CalculateMulticastStreamsCount_ExpectingExactReturnValue)
{
    WorkerCoreResourcesWH worker_core_resources({0, 0, 0});

    // Number of multicast streams on ethernet core is equal to gather/multicast stream ids range since multicast
    // streams are allocated from gather/multicast pool.
    unsigned int expected_multicast_streams_count =
        (worker_core_resources_wh_constants::gather_multicast_streams_id_range_end -
         worker_core_resources_wh_constants::gather_multicast_streams_id_range_start + 1);

    EXPECT_EQ(worker_core_resources.get_multicast_streams_count(), expected_multicast_streams_count);
}