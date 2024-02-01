// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <stdexcept>

#include <gtest/gtest.h>

#include "device/tt_xy_pair.h"

#include "core_resources_unit_test_utils.h"
#include "device/core_resources_constants.h"
#include "device/ethernet_core_resources.h"
#include "model/typedefs.h"
#include "test_utils/unit_test_utils.h"

using namespace pipegen2;

/**********************************************************************************************************************
    Test fixture used for all tests in this file
**********************************************************************************************************************/

class Pipegen2_EthernetCoreResources : public testing::Test
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
    Tests for function: allocate_ethernet_stream
**********************************************************************************************************************/

TEST_F(Pipegen2_EthernetCoreResources, AllocateEthernetStream_ProperAllocatedStreamStorage)
{
    EthernetCoreResources eth_core_resources({0, 0, 0});

    // Should be empty.
    EXPECT_EQ(eth_core_resources.get_allocated_stream_ids(), std::set<StreamId>{});

    eth_core_resources.allocate_ethernet_stream();

    // Should contain only one stream id from the beginning of ethernet range.
    EXPECT_EQ(
        eth_core_resources.get_allocated_stream_ids(),
        std::set<StreamId>{ethernet_core_resources_constants::ethernet_stream_id_range_start});
}

/**********************************************************************************************************************
    Tests for function: get_next_available_ethernet_stream_id
    (which also tests get_next_available_gather_multicast_stream_id, all through public allocate_ethernet_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_EthernetCoreResources, GetNextAvailableEthernetStreamId_RepeatedCallsUntilExceptionThrown)
{
    EthernetCoreResources eth_core_resources({0, 0, 0});
    std::vector<StreamId> expected_stream_ids;

    for (uint8_t stream_id = ethernet_core_resources_constants::ethernet_stream_id_range_start;
         stream_id <= ethernet_core_resources_constants::ethernet_stream_id_range_end;
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // We ehausted ethernet streams and we should start allocating from gather multicast streams range.
    for (uint8_t stream_id = ethernet_core_resources_constants::gather_multicast_streams_id_range_start;
         stream_id <= ethernet_core_resources_constants::gather_multicast_streams_id_range_end;
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // Expecting streams to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<StreamId>(
        [&]() -> StreamId
        {
            return eth_core_resources.allocate_ethernet_stream();
        },
        expected_stream_ids);
}

/**********************************************************************************************************************
    Tests for function: get_next_available_gather_stream_id
    (which also tests get_next_available_gather_multicast_stream_id, all through public allocate_gather_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_EthernetCoreResources, GetNextAvailableGatherStreamId_RepeatedCallsUntilExceptionThrown)
{
    EthernetCoreResources eth_core_resources({0, 0, 0});
    std::vector<StreamId> expected_stream_ids;

    for (uint8_t stream_id = ethernet_core_resources_constants::gather_multicast_streams_id_range_start;
         stream_id <= ethernet_core_resources_constants::gather_multicast_streams_id_range_end;
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // Expecting streams to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<StreamId>(
        [&]() -> StreamId
        {
            return eth_core_resources.allocate_gather_stream();
        },
        expected_stream_ids);
}

/**********************************************************************************************************************
    Tests for function: get_next_available_multicast_stream_id
    (which also tests get_next_available_gather_multicast_stream_id, all through public allocate_multicast_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_EthernetCoreResources, GetNextAvailableMulticastStreamId_RepeatedCallsUntilExceptionThrown)
{
    EthernetCoreResources eth_core_resources({0, 0, 0});
    std::vector<StreamId> expected_stream_ids;

    for (uint8_t stream_id = ethernet_core_resources_constants::gather_multicast_streams_id_range_start;
         stream_id <= ethernet_core_resources_constants::gather_multicast_streams_id_range_end;
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // Expecting streams to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<StreamId>(
        [&]() -> StreamId
        {
            return eth_core_resources.allocate_multicast_stream();
        },
        expected_stream_ids);
}

/**********************************************************************************************************************
    Tests for function: get_next_available_general_purpose_stream_id
    (through public allocate_general_purpose_stream)
**********************************************************************************************************************/

TEST_F(Pipegen2_EthernetCoreResources, GetNextAvailableGeneralPurposeStreamId_RepeatedCallsUntilExceptionThrown)
{
    EthernetCoreResources eth_core_resources({0, 0, 0});
    std::vector<StreamId> expected_stream_ids;

    // Expecting streams to be allocated in a certain order.
    for (uint8_t stream_id =
            ethernet_core_resources_constants::ethernet_stream_id_range_end + 1; /* extra streams range start */
         stream_id <=
            ethernet_core_resources_constants::ethernet_core_num_noc_streams - 1; /* extra streams range end */
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // We exhausted general purpose streams and now we start allocating after gather multicast streams range.
    for (uint8_t stream_id = ethernet_core_resources_constants::gather_multicast_streams_id_range_end + 1;
         stream_id < ethernet_core_resources_constants::ethernet_stream_id_range_start;
         stream_id++)
    {
        expected_stream_ids.push_back(stream_id);
    }

    // Expecting streams to be allocated in a certain order, and after exhausted range an error thrown.
    test_function_repeated_calls_until_exception_thrown<StreamId>(
        [&]() -> StreamId
        {
            return eth_core_resources.allocate_general_purpose_stream();
        },
        expected_stream_ids);
}

/**********************************************************************************************************************
    Tests for function: calculate_multicast_streams_count
    (through public get_multicast_streams_count)
**********************************************************************************************************************/

TEST_F(Pipegen2_EthernetCoreResources, CalculateMulticastStreamsCount_ExpectingExactReturnValue)
{
    EthernetCoreResources eth_core_resources({0, 0, 0});

    // Number of multicast streams on ethernet core is equal to gather/multicast stream ids range since multicast
    // streams are allocated from gather/multicast pool.
    unsigned int expected_multicast_streams_count =
        (ethernet_core_resources_constants::gather_multicast_streams_id_range_end -
         ethernet_core_resources_constants::gather_multicast_streams_id_range_start + 1);

    EXPECT_EQ(eth_core_resources.get_multicast_streams_count(), expected_multicast_streams_count);
}