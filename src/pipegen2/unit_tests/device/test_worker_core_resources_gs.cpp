// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "device/worker_core_resources_gs.h"

#include <stdexcept>

#include <gtest/gtest.h>

#include "device/tt_arch_types.h"
#include "stream_io_map.h"

#include "device/core_resources_constants.h"
#include "model/typedefs.h"

#include "core_resources_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"
// clang-format on

using namespace pipegen2;
using namespace unit_test_utils;

/**********************************************************************************************************************
    Test fixture used where GS arch is expected
**********************************************************************************************************************/

class Pipegen2_WorkerCoreResourcesGS : public testing::Test
{
protected:
    void SetUp() override
    {
        if (unit_test_utils::get_build_arch() != tt::ARCH::GRAYSKULL)
        {
            // Test skipped since it is only valid for grayskull arch.
            GTEST_SKIP();
        }
    }
};

/**********************************************************************************************************************
    Tests for function: get_next_available_gather_multicast_stream_id
    (through public allocate_gather_stream / or allocate_multicast_stream, whichever is fine)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesGS, GetNextAvailableGatherMulticastStreamId_RepeatedCallsUntilExcThrown)
{
    const tt_cxy_pair core_physical_location{0, 0, 0};
    const tt_cxy_pair core_logical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location, core_logical_location);
    std::vector<StreamId> expected_stream_ids;

    for (uint8_t stream_id = worker_core_resources_gs_constants::gather_multicast_streams_id_range_start;
         stream_id <= worker_core_resources_gs_constants::gather_multicast_streams_id_range_end;
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // Expecting streams to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<StreamId, OutOfCoreResourcesException>(
        [&]() -> StreamId { return worker_core_resources.allocate_gather_stream(); },
        expected_stream_ids,
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                core_physical_location,
                core_logical_location,
                OutOfCoreResourcesException::CoreResourceType::kGatherMulticastStreams,
                expected_stream_ids.size(),
                expected_stream_ids.size() + 1);
        });
}

/**********************************************************************************************************************
    Tests for function: get_next_available_gather_stream_id
    (allocate_gather_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesGS, GetNextAvailableGatherStreamId_ExpectingExactReturnValue)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0}, {0, 0, 0});

    // Sanity check that first call will return stream from beginning of the range.
    verify_no_throw_and_return_value_eq<StreamId>(
        [&]() -> StreamId { return worker_core_resources.allocate_gather_stream(); },
        worker_core_resources_gs_constants::gather_multicast_streams_id_range_start);
}

/**********************************************************************************************************************
    Tests for function: get_next_available_multicast_stream_id
    (allocate_multicast_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesGS, GetNextAvailableMulticastStreamId_ExpectingExactReturnValue)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0}, {0, 0, 0});

    // Sanity check that first call will return stream from beginning of the range.
    verify_no_throw_and_return_value_eq<StreamId>(
        [&]() -> StreamId { return worker_core_resources.allocate_multicast_stream(); },
        worker_core_resources_gs_constants::gather_multicast_streams_id_range_start);
}

/**********************************************************************************************************************
    Tests for function: get_packer_multicast_stream_id
    (through public allocate_packer_multicast_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesGS, GetPackerMulticastStreamId_AlwaysThrowsExc)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0}, {0, 0, 0});

    for (unsigned int operand_id = OPERAND_INPUT_START_INDEX; operand_id < MAX_NUM_OPERANDS; operand_id++)
    {
        EXPECT_THROW(worker_core_resources.allocate_packer_multicast_stream(operand_id), std::runtime_error);
    }
}

/**********************************************************************************************************************
    Tests for function: calculate_multicast_streams_count
    (through public get_multicast_streams_count)
**********************************************************************************************************************/

TEST_F(Pipegen2_WorkerCoreResourcesGS, CalculateMulticastStreamsCount_ExpectingExactReturnValue)
{
    WorkerCoreResourcesGS worker_core_resources({0, 0, 0}, {0, 0, 0});

    // Number of multicast streams on ethernet core is equal to gather/multicast stream ids range since multicast
    // streams are allocated from gather/multicast pool.
    unsigned int expected_multicast_streams_count =
        (worker_core_resources_gs_constants::gather_multicast_streams_id_range_end -
         worker_core_resources_gs_constants::gather_multicast_streams_id_range_start + 1);

    EXPECT_EQ(worker_core_resources.get_multicast_streams_count(), expected_multicast_streams_count);
}