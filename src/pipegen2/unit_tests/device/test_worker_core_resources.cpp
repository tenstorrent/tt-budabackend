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
#include "pipegen2_exceptions.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;
using namespace unit_test_utils;

/**********************************************************************************************************************
    Test for function: allocate_packer_stream
**********************************************************************************************************************/

TEST(Pipegen2_WorkerCoreResources, AllocatePackerStream_InvalidOperandID)
{
    const tt_cxy_pair physical_core_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(physical_core_location);

    // We should be able to allocate packer stream only for valid output operand ID. Everything else should throw an
    // error.
    for (unsigned int operand_id = OPERAND_INPUT_START_INDEX; operand_id < OPERAND_OUTPUT_START_INDEX; operand_id++)
    {
        verify_throws_proper_exception<IllegalCoreResourceAllocationException>(
            [&]()
            {
                worker_core_resources.allocate_packer_stream(operand_id);
            },
            [&](const IllegalCoreResourceAllocationException& ex)
            {
                verify_illegal_resource_allocation_exception(
                    ex,
                    physical_core_location,
                    IllegalCoreResourceAllocationException::CoreResourceType::kPackerStreams);
            });
    }

    for (unsigned int operand_id = OPERAND_RELAY_START_INDEX; operand_id < MAX_NUM_OPERANDS; operand_id++)
    {
        verify_throws_proper_exception<IllegalCoreResourceAllocationException>(
            [&]()
            {
                worker_core_resources.allocate_packer_stream(operand_id);
            },
            [&](const IllegalCoreResourceAllocationException& ex)
            {
                 verify_illegal_resource_allocation_exception(
                    ex,
                    physical_core_location,
                    IllegalCoreResourceAllocationException::CoreResourceType::kPackerStreams);
            });
    }
}

TEST(Pipegen2_WorkerCoreResources, AllocatePackerStream_AllocateTwiceForSameOperandID)
{
    const tt_cxy_pair physical_core_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(physical_core_location);

    EXPECT_NO_THROW(worker_core_resources.allocate_packer_stream(OPERAND_OUTPUT_START_INDEX));

    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]()
        {
            worker_core_resources.allocate_packer_stream(OPERAND_OUTPUT_START_INDEX);
        },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                physical_core_location,
                OutOfCoreResourcesException::CoreResourceType::kPackerStreams,
                1 /* available_core_resources */,
                2 /* used_core_resources */);
        });
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
    const tt_cxy_pair physical_core_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(physical_core_location);

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
    const tt_cxy_pair physical_core_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(physical_core_location);

    // We should be able to allocate unpacker stream only for valid input operand ID. Everything else should throw an
    // error.
    for (unsigned int operand_id = OPERAND_OUTPUT_START_INDEX;
         operand_id < OPERAND_INTERMEDIATES_START_INDEX;
         operand_id++)
    {
        verify_throws_proper_exception<IllegalCoreResourceAllocationException>(
            [&]()
            {
                worker_core_resources.allocate_unpacker_stream(operand_id);
            },
            [&](const IllegalCoreResourceAllocationException& ex)
            {
                verify_illegal_resource_allocation_exception(
                    ex,
                    physical_core_location,
                    IllegalCoreResourceAllocationException::CoreResourceType::kUnpackerStreams);
            });
    }

    for (unsigned int operand_id = OPERAND_RELAY_START_INDEX; operand_id < MAX_NUM_OPERANDS; operand_id++)
    {
        verify_throws_proper_exception<IllegalCoreResourceAllocationException>(
            [&]()
            {
                worker_core_resources.allocate_unpacker_stream(operand_id);
            },
            [&](const IllegalCoreResourceAllocationException& ex)
            {
                verify_illegal_resource_allocation_exception(
                    ex,
                    physical_core_location,
                    IllegalCoreResourceAllocationException::CoreResourceType::kUnpackerStreams);
            });
    }
}

TEST(Pipegen2_WorkerCoreResources, AllocateUnpackerStream_AllocateTwiceForSameOperandID)
{
    const tt_cxy_pair physical_core_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(physical_core_location);

    EXPECT_NO_THROW(worker_core_resources.allocate_unpacker_stream(OPERAND_INPUT_START_INDEX));

    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]()
        {
            worker_core_resources.allocate_unpacker_stream(OPERAND_INPUT_START_INDEX);
        },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                physical_core_location,
                OutOfCoreResourcesException::CoreResourceType::kUnpackerStreams,
                1 /* available_core_resources */,
                2 /* used_core_resources */);
        });
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
    const tt_cxy_pair physical_core_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(physical_core_location);

    // We should be able to allocate intermed stream only for valid intermed operand ID. Everything else should throw an
    // error.
    for (unsigned int operand_id = OPERAND_INPUT_START_INDEX;
         operand_id < OPERAND_INTERMEDIATES_START_INDEX;
         operand_id++)
    {
        verify_throws_proper_exception<IllegalCoreResourceAllocationException>(
            [&]()
            {
                worker_core_resources.allocate_intermed_stream(operand_id);
            },
            [&](const IllegalCoreResourceAllocationException& ex)
            {
                verify_illegal_resource_allocation_exception(
                    ex,
                    physical_core_location,
                    IllegalCoreResourceAllocationException::CoreResourceType::kIntermediateStreams);
            });
    }

    for (unsigned int operand_id = OPERAND_RELAY_START_INDEX; operand_id < MAX_NUM_OPERANDS; operand_id++)
    {
        verify_throws_proper_exception<IllegalCoreResourceAllocationException>(
            [&]()
            {
                worker_core_resources.allocate_intermed_stream(operand_id);
            },
            [&](const IllegalCoreResourceAllocationException& ex)
            {
                verify_illegal_resource_allocation_exception(
                    ex,
                    physical_core_location,
                    IllegalCoreResourceAllocationException::CoreResourceType::kIntermediateStreams);
            });
    }
}

TEST(Pipegen2_WorkerCoreResources, AllocateIntermedStream_AllocateTwiceForSameOperandID)
{
    const tt_cxy_pair physical_core_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(physical_core_location);

    EXPECT_NO_THROW(worker_core_resources.allocate_intermed_stream(OPERAND_INTERMEDIATES_START_INDEX));

    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]()
        {
            worker_core_resources.allocate_intermed_stream(OPERAND_INTERMEDIATES_START_INDEX);
        },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                physical_core_location,
                OutOfCoreResourcesException::CoreResourceType::kIntermediateStreams,
                1 /* available_core_resources */,
                2 /* used_core_resources */);
        });
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
    const tt_cxy_pair physical_core_location{0, 0, 0};
    WorkerCoreResourcesWH worker_core_resources(physical_core_location);

    EXPECT_NO_THROW(worker_core_resources.allocate_packer_multicast_stream(
        worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start));

    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]()
        {
            worker_core_resources.allocate_packer_multicast_stream(
                worker_core_resources_wh_constants::packer_multicast_stream_operand_id_range_start);
        },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                physical_core_location,
                OutOfCoreResourcesException::CoreResourceType::kPackerMulticastStreams,
                1 /* available_core_resources */,
                2 /* used_core_resources */);
        });
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
    const tt_cxy_pair core_physical_location{0, 0, 0};
    WorkerCoreResourcesGS worker_core_resources(core_physical_location);
    std::vector<StreamId> expected_stream_ids;

    // Expecting streams to be allocated in a certain order.
    for (uint8_t stream_id = static_cast<StreamId>(END_IO_STREAM + 1); /* extra streams range start */
         stream_id <= static_cast<StreamId>(NOC_NUM_STREAMS - 1); /* extra streams range end */
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // Expecting streams to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<StreamId, OutOfCoreResourcesException>(
        [&]() -> StreamId
        {
            return worker_core_resources.allocate_general_purpose_stream();
        },
        expected_stream_ids,
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                core_physical_location,
                OutOfCoreResourcesException::CoreResourceType::kGeneralPurposeStreams,
                expected_stream_ids.size(),
                expected_stream_ids.size() + 1);
        });
}
