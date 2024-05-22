// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// clang-format off
#include "device/l1/l1_data_buffers_memory_layout.h"

#include <memory>

#include <gtest/gtest.h>

#include "device/tt_xy_pair.h"

#include "device/core_resources_constants.h"
#include "device/l1/l1_buffer.h"
#include "model/stream_graph/stream_node.h"
#include "pipegen2_constants.h"

#include "core_resources_unit_test_utils.h"
#include "test_utils/unit_test_utils.h"
// clang-format on

using namespace pipegen2;
using namespace pipegen2::unit_test_utils;

/**********************************************************************************************************************
    Test fixtures used for tests in this file.
**********************************************************************************************************************/

class Pipegen2_L1DataBuffersMemoryLayout : public testing::Test
{
protected:
    virtual void SetUp() override
    {
        m_core_location = tt_cxy_pair(0, 0, 0);
        m_l1_data_buffers_space_start_address = get_data_buffers_space_base();
        m_l1_data_buffers_space_end_address = get_max_size() - get_unused_space_bytes();

        m_l1_data_buffers_memory = std::make_unique<L1DataBuffersMemoryLayout>(
            m_core_location,
            m_core_location,
            get_data_buffers_space_base(),
            get_max_size(),
            get_predefined_tile_header_buffer_address());
    }

    unsigned int get_available_space() const
    {
        return m_l1_data_buffers_space_end_address - m_l1_data_buffers_space_start_address;
    }

    unsigned int get_overlay_blob_size() const { return l1_mem::address_map::OVERLAY_BLOB_SIZE; }

    unsigned int get_data_buffers_space_base() const { return l1_mem::address_map::DATA_BUFFER_SPACE_BASE; }

    unsigned int get_max_size() const { return l1_mem::address_map::MAX_SIZE; }

    unsigned int get_unused_space_bytes() const { return constants::unused_data_buffers_space_bytes; }

    unsigned int get_predefined_tile_header_buffer_address() const
    {
        return worker_core_resources_constants::l1_predefined_tile_header_buffer_address;
    }

    tt_cxy_pair m_core_location;

    unsigned int m_l1_data_buffers_space_start_address;

    unsigned int m_l1_data_buffers_space_end_address;

    std::unique_ptr<L1DataBuffersMemoryLayout> m_l1_data_buffers_memory;
};

/**********************************************************************************************************************
    Tests for function: allocate_l1_tile_header_buffer
    (which also tests check_if_out_of_l1_data_buffers_memory and allocate_space_for_l1_data_buffer)
**********************************************************************************************************************/

TEST_F(Pipegen2_L1DataBuffersMemoryLayout, AllocateL1TileHeaderBuffer_AllocateUntilOutOfMemory)
{
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    int l1_data_buffers_available_space = m_l1_data_buffers_space_end_address - m_l1_data_buffers_space_start_address;
    int tile_header_buffer_size = constants::tile_header_buffer_size_bytes;

    // Max number of tile header buffers that can be placed in l1 space for data buffers.
    unsigned int max_num_tile_header_buffers = l1_data_buffers_available_space / tile_header_buffer_size;

    // The first allocated tile header buffer is allocated in predesignated space for a single tile header buffer.
    const L1Buffer* tile_header_buffer = m_l1_data_buffers_memory->allocate_l1_tile_header_buffer(0);
    EXPECT_EQ(tile_header_buffer->get_address(), get_predefined_tile_header_buffer_address());

    unsigned int tile_header_buff_expected_addr = m_l1_data_buffers_space_end_address;
    // All the following allocations will end up in l1 data buffer space.
    for (unsigned int test_tile_size = 1; test_tile_size <= max_num_tile_header_buffers; test_tile_size++)
    {
        tile_header_buff_expected_addr -= constants::tile_header_buffer_size_bytes;
        tile_header_buffer = m_l1_data_buffers_memory->allocate_l1_tile_header_buffer(test_tile_size);
        EXPECT_EQ(tile_header_buffer->get_address(), tile_header_buff_expected_addr);
    }

    // Verify we are not out of space.
    EXPECT_NO_THROW(m_l1_data_buffers_memory->check_if_out_of_l1_data_buffers_memory());

    // When we do another pass with the same values, nothing should changes since no allocations should happen.
    for (unsigned int test_tile_size = 1; test_tile_size <= max_num_tile_header_buffers; test_tile_size++)
    {
        m_l1_data_buffers_memory->allocate_l1_tile_header_buffer(test_tile_size);
    }
    EXPECT_NO_THROW(m_l1_data_buffers_memory->check_if_out_of_l1_data_buffers_memory());

    // Allocate one too many tile headers. This will overflow L1 space.
    // Note that we use max_num_tile_header_buffers + 1 as tile_size here. It's only important that it's a new value
    m_l1_data_buffers_memory->allocate_l1_tile_header_buffer(max_num_tile_header_buffers + 1);

    // Expect an error to be thrown when specifically checking if L1 is out of memory.
    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]() { m_l1_data_buffers_memory->check_if_out_of_l1_data_buffers_memory(); },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                m_core_location,
                m_core_location,
                OutOfCoreResourcesException::CoreResourceType::kL1DataBuffersMemory,
                l1_data_buffers_available_space,
                (max_num_tile_header_buffers + 1) * tile_header_buffer_size);
        });
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_stream_buffer
    (which also tests check_if_out_of_l1_data_buffers_memory and allocate_space_for_l1_data_buffer)
**********************************************************************************************************************/

TEST_F(Pipegen2_L1DataBuffersMemoryLayout, AllocateL1StreamBuffer_AllocateUntilOutOfMemory)
{
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    unsigned int available_space = get_available_space();
    StreamNode stream_node(StreamType::Gather, m_core_location, 0);

    // Allocate nothing, dummy call.
    const L1Buffer* l1_current_data_buffer = m_l1_data_buffers_memory->allocate_l1_stream_buffer(&stream_node, 0);
    EXPECT_EQ(l1_current_data_buffer, nullptr);

    // Allocate almost entire space, just leave one byte unallocated.
    unsigned int allocated_l1_data_buffers_size = available_space - 1;

    EXPECT_NO_THROW(
        l1_current_data_buffer =
            m_l1_data_buffers_memory->allocate_l1_stream_buffer(&stream_node, allocated_l1_data_buffers_size));
    EXPECT_EQ(
        l1_current_data_buffer->get_address(), m_l1_data_buffers_space_end_address - allocated_l1_data_buffers_size);
    EXPECT_EQ(l1_current_data_buffer->get_size(), allocated_l1_data_buffers_size);

    // Allocate one more byte which will take up entire available space.
    m_l1_data_buffers_memory->allocate_l1_stream_buffer(&stream_node, 1);
    EXPECT_NO_THROW(m_l1_data_buffers_memory->check_if_out_of_l1_data_buffers_memory());

    // Allocate one more byte which will overflow L1 space.
    m_l1_data_buffers_memory->allocate_l1_stream_buffer(&stream_node, 1);

    // Expect an error to be thrown when specifically checking if L1 is out of memory.
    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]() { m_l1_data_buffers_memory->check_if_out_of_l1_data_buffers_memory(); },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                m_core_location,
                m_core_location,
                OutOfCoreResourcesException::CoreResourceType::kL1DataBuffersMemory,
                available_space,
                available_space + 1);
        });
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_ncrisc_fallback_buffer
    (which also tests check_if_out_of_l1_data_buffers_memory and allocate_space_for_l1_data_buffer)
**********************************************************************************************************************/

TEST_F(Pipegen2_L1DataBuffersMemoryLayout, AllocateL1NcriscFallbackBuffer_AllocateUntilOutOfMemory)
{
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    unsigned int available_space = get_available_space();

    // Allocate nothing, dummy call.
    const L1Buffer* l1_current_data_buffer = m_l1_data_buffers_memory->allocate_l1_ncrisc_fallback_buffer(0);
    EXPECT_EQ(l1_current_data_buffer, nullptr);

    // Allocate almost entire space, just leave one byte unallocated.
    unsigned int allocated_l1_data_buffers_size = available_space - 1;

    EXPECT_NO_THROW(
        l1_current_data_buffer =
            m_l1_data_buffers_memory->allocate_l1_ncrisc_fallback_buffer(allocated_l1_data_buffers_size));
    EXPECT_EQ(
        l1_current_data_buffer->get_address(), m_l1_data_buffers_space_end_address - allocated_l1_data_buffers_size);
    EXPECT_EQ(l1_current_data_buffer->get_size(), allocated_l1_data_buffers_size);

    // Allocate one more byte which will take up entire available space.
    m_l1_data_buffers_memory->allocate_l1_ncrisc_fallback_buffer(1);
    EXPECT_NO_THROW(m_l1_data_buffers_memory->check_if_out_of_l1_data_buffers_memory());

    // Allocate one more byte which will overflow L1 space.
    m_l1_data_buffers_memory->allocate_l1_ncrisc_fallback_buffer(1);

    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]() { m_l1_data_buffers_memory->check_if_out_of_l1_data_buffers_memory(); },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                m_core_location,
                m_core_location,
                OutOfCoreResourcesException::CoreResourceType::kL1DataBuffersMemory,
                available_space,
                available_space + 1);
        });
}

/**********************************************************************************************************************
    Tests for function: allocate_l1_extra_overlay_blob_space
    (which also tests check_if_out_of_l1_data_buffers_memory and allocate_space_for_l1_data_buffer and
     calculate_extra_blob_space_to_allocate)
**********************************************************************************************************************/

TEST_F(Pipegen2_L1DataBuffersMemoryLayout, AllocateL1ExtraOverlayBlobSpace_AllocateUnderMinimumSize)
{
    // Allocate nothing, dummy call.
    const L1Buffer* l1_current_data_buffer;
    EXPECT_NO_THROW(l1_current_data_buffer = m_l1_data_buffers_memory->allocate_l1_extra_overlay_blob_space(0, false));
    EXPECT_EQ(l1_current_data_buffer, nullptr);

    // Allocating <= default blob size won't allocate additional space.
    EXPECT_NO_THROW(
        l1_current_data_buffer =
            m_l1_data_buffers_memory->allocate_l1_extra_overlay_blob_space(get_overlay_blob_size(), false));
    EXPECT_EQ(l1_current_data_buffer, nullptr);
}

TEST_F(Pipegen2_L1DataBuffersMemoryLayout, AllocateL1ExtraOverlayBlobSpace_AllocateTwiceThrowsErr)
{
    // Allocate extra 60KB of blob space.
    unsigned int extra_blob_space = 0xF000;
    unsigned int total_blob_size = get_overlay_blob_size() + extra_blob_space;

    const L1Buffer* l1_current_data_buffer;
    EXPECT_NO_THROW(
        l1_current_data_buffer =
            m_l1_data_buffers_memory->allocate_l1_extra_overlay_blob_space(total_blob_size, false));
    EXPECT_EQ(l1_current_data_buffer->get_address(), m_l1_data_buffers_space_start_address);
    EXPECT_EQ(l1_current_data_buffer->get_size(), extra_blob_space);

    // Trying to allocate extra overlay blob space again will return the same buffer.
    EXPECT_EQ(
        m_l1_data_buffers_memory->allocate_l1_extra_overlay_blob_space(total_blob_size, false), l1_current_data_buffer);
}

TEST_F(Pipegen2_L1DataBuffersMemoryLayout, AllocateL1ExtraOverlayBlobSpace_AllocateUntilOutOfMemory)
{
    // Allocate 60KB over available space.
    unsigned int extra_blob_space = (get_available_space() + 0xF000) & 0xFFFFF000;
    unsigned int total_blob_size = get_overlay_blob_size() + extra_blob_space;

    const L1Buffer* l1_current_data_buffer = nullptr;
    EXPECT_NO_THROW(
        l1_current_data_buffer =
            m_l1_data_buffers_memory->allocate_l1_extra_overlay_blob_space(total_blob_size, false));
    EXPECT_EQ(l1_current_data_buffer->get_address(), m_l1_data_buffers_space_start_address);
    EXPECT_EQ(l1_current_data_buffer->get_size(), extra_blob_space);

    verify_throws_proper_exception<OutOfCoreResourcesException>(
        [&]() { m_l1_data_buffers_memory->check_if_out_of_l1_data_buffers_memory(); },
        [&](const OutOfCoreResourcesException& ex)
        {
            verify_out_of_core_resource_exception(
                ex,
                m_core_location,
                m_core_location,
                OutOfCoreResourcesException::CoreResourceType::kL1DataBuffersMemory,
                get_available_space(),
                extra_blob_space);
        });
}

/**********************************************************************************************************************
    Tests for function: get_tile_header_buffer_address
**********************************************************************************************************************/

TEST_F(Pipegen2_L1DataBuffersMemoryLayout, GetTileHeaderBufferAddress_ExpectingExactReturnValue)
{
    const unsigned int l1_data_buffers_space_end_address =
        l1_mem::address_map::MAX_SIZE - constants::unused_data_buffers_space_bytes;

    const unsigned int dummy_tile_size_1 = 1;
    const unsigned int dummy_tile_size_2 = 2;

    // No tile header buffers allocated still.
    EXPECT_THROW(m_l1_data_buffers_memory->get_tile_header_buffer_address(dummy_tile_size_1), std::runtime_error);

    // First THB is allocated at predefined location.
    m_l1_data_buffers_memory->allocate_l1_tile_header_buffer(dummy_tile_size_1);
    EXPECT_EQ(
        m_l1_data_buffers_memory->get_tile_header_buffer_address(dummy_tile_size_1),
        worker_core_resources_constants::l1_predefined_tile_header_buffer_address);

    // All other THBs are allocated from end towards beginning of L1 data buffers space.
    m_l1_data_buffers_memory->allocate_l1_tile_header_buffer(dummy_tile_size_2);
    const unsigned int expected_thb_address =
        l1_data_buffers_space_end_address - pipegen2::constants::tile_header_buffer_size_bytes;

    EXPECT_EQ(m_l1_data_buffers_memory->get_tile_header_buffer_address(dummy_tile_size_2), expected_thb_address);
}
