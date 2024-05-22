// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/l1/l1_data_buffers_memory_layout.h"

#include "device/l1/extra_overlay_blob_l1_buffer.h"
#include "device/l1/l1_buffer.h"
#include "device/l1/l1_data_buffers_factory.h"
#include "device/l1/ncrisc_fallback_l1_buffer.h"
#include "device/l1/stream_l1_buffer.h"
#include "device/l1/tile_header_l1_buffer.h"
#include "dram_address_map.h"
#include "eth_l1_address_map.h"
#include "l1_address_map.h"
#include "logger.hpp"
#include "pipegen2_constants.h"
#include "pipegen2_exceptions.h"

namespace pipegen2
{

L1DataBuffersMemoryLayout::L1DataBuffersMemoryLayout(
    const tt_cxy_pair &core_physical_location,
    const tt_cxy_pair &core_logical_location,
    const unsigned int l1_data_buffers_space_start_address,
    const unsigned int l1_data_buffers_space_end_address,
    const unsigned int l1_predefined_tile_header_buffer_address) :
    m_core_physical_location(core_physical_location),
    m_core_logical_location(core_logical_location),
    c_l1_data_buffers_space_start_address(l1_data_buffers_space_start_address),
    c_l1_data_buffers_space_end_address(l1_data_buffers_space_end_address - constants::unused_data_buffers_space_bytes),
    c_l1_predefined_tile_header_buffer_address(l1_predefined_tile_header_buffer_address),
    m_l1_extra_overlay_blob_space(0),
    m_l1_current_data_buffers_space_address(c_l1_data_buffers_space_end_address)
{
}

L1DataBuffersMemoryLayout::~L1DataBuffersMemoryLayout() = default;

const L1Buffer *L1DataBuffersMemoryLayout::allocate_l1_tile_header_buffer(const unsigned int tile_size)
{
    // If tile header buffer was already allocated for this tile_size, return it's
    // address.
    if (m_allocated_tile_header_buffers.find(tile_size) != m_allocated_tile_header_buffers.end())
    {
        return m_allocated_tile_header_buffers.at(tile_size);
    }

    // First tile header buffer is always allocated at the predefined address
    // after TRISC2. All other tile header buffers are allocated at the end of L1
    // data buffers space.
    unsigned int thb_address = m_allocated_tile_header_buffers.empty()
                                   ? c_l1_predefined_tile_header_buffer_address
                                   : allocate_space_for_l1_data_buffer(constants::tile_header_buffer_size_bytes);

    std::unique_ptr<TileHeaderL1Buffer> thb =
        L1DataBuffersFactory::create_tile_header_buffer(m_core_physical_location, thb_address, tile_size);

    // Map this tile size to this THB.
    m_allocated_tile_header_buffers.emplace(tile_size, thb.get());

    return store_buffer(std::move(thb));
}

const L1Buffer *L1DataBuffersMemoryLayout::allocate_l1_stream_buffer(
    const StreamNode *stream_node, const unsigned int buffer_size)
{
    if (buffer_size == 0)
    {
        return nullptr;
    }

    const unsigned int buffer_address = allocate_space_for_l1_data_buffer(buffer_size);
    return store_buffer(L1DataBuffersFactory::create_stream_buffer(stream_node, buffer_address, buffer_size));
}

const L1Buffer *L1DataBuffersMemoryLayout::allocate_l1_ncrisc_fallback_buffer(const unsigned int buffer_size)
{
    if (buffer_size == 0)
    {
        return nullptr;
    }

    const unsigned int buffer_address = allocate_space_for_l1_data_buffer(buffer_size);
    return store_buffer(
        L1DataBuffersFactory::create_ncrisc_fallback_buffer(m_core_physical_location, buffer_address, buffer_size));
}

const L1Buffer *L1DataBuffersMemoryLayout::allocate_l1_extra_overlay_blob_space(
    const unsigned int total_blob_size, const bool is_ethernet_core)
{
    const unsigned int extra_blob_space_in_bytes =
        calculate_extra_blob_space_to_allocate(total_blob_size, is_ethernet_core);

    if (extra_blob_space_in_bytes == 0)
    {
        return nullptr;
    }

    const ExtraOverlayBlobL1Buffer *already_allocated_extra_overlay_blob_space =
        dynamic_cast<ExtraOverlayBlobL1Buffer *>(m_l1_data_buffers_start_section.front().get());

    if (already_allocated_extra_overlay_blob_space != nullptr)
    {
        // This is the consequence of having `overlay_blob_size` in pipe graph
        // attributed to all unpacker nodes of an op on one core. Thus we end up
        // trying to allocate same extra space for one and the same core.
        // TODO Once TODO in unpacker_output_node.h is attended to, this will be
        // solved and this function should assert to make sure we can only allocate
        // extra blob space once per core.
        log_debug(
            tt::LogPipegen2,
            "L1DataBuffersMemoryLayout: Already allocated extra overlay blob space at the beginning of L1 "
            "data buffers space");

        return already_allocated_extra_overlay_blob_space;
    }

    m_l1_extra_overlay_blob_space = extra_blob_space_in_bytes;

    return store_buffer(L1DataBuffersFactory::create_extra_overlay_blob_buffer(
        m_core_physical_location, c_l1_data_buffers_space_start_address, extra_blob_space_in_bytes));
}

void L1DataBuffersMemoryLayout::check_if_out_of_l1_data_buffers_memory()
{
    // If two regions growing in opposite directions overlaped, we ran out of
    // space.
    if (m_l1_current_data_buffers_space_address < c_l1_data_buffers_space_start_address + m_l1_extra_overlay_blob_space)
    {
        const int allocated_space_in_bytes = c_l1_data_buffers_space_end_address -
                                             m_l1_current_data_buffers_space_address + m_l1_extra_overlay_blob_space;

        const int total_available_space = c_l1_data_buffers_space_end_address - c_l1_data_buffers_space_start_address;

        throw OutOfCoreResourcesException(
            "Core " + m_core_physical_location.str() + " (logical location: " + m_core_logical_location.str() +
                ") is out of data buffers memory (allocated " + std::to_string(allocated_space_in_bytes) +
                " bytes out of available " + std::to_string(total_available_space) + " bytes).\n" +
                "Allocated data buffers:\n" + get_allocation_info(),
            m_core_physical_location,
            m_core_logical_location,
            OutOfCoreResourcesException::CoreResourceType::kL1DataBuffersMemory,
            total_available_space,
            allocated_space_in_bytes);
    }
}

unsigned int L1DataBuffersMemoryLayout::get_tile_header_buffer_address(const unsigned int tile_size)
{
    auto it = m_allocated_tile_header_buffers.find(tile_size);

    log_assert(
        it != m_allocated_tile_header_buffers.end(),
        "Core {}: Tile header buffer for tile size {} is missing",
        m_core_physical_location.str(),
        tile_size);

    return it->second->get_address();
}

unsigned int L1DataBuffersMemoryLayout::allocate_space_for_l1_data_buffer(const unsigned int size_in_bytes)
{
    m_l1_current_data_buffers_space_address -= size_in_bytes;
    return m_l1_current_data_buffers_space_address;
}

unsigned int L1DataBuffersMemoryLayout::calculate_extra_blob_space_to_allocate(
    unsigned int blob_size_in_bytes, const bool is_ethernet_core)
{
    unsigned int min_blob_size =
        is_ethernet_core ? eth_l1_mem::address_map::OVERLAY_BLOB_SIZE : l1_mem::address_map::OVERLAY_BLOB_SIZE;

    if (blob_size_in_bytes == 0)
    {
        // If blob size is not specified, then we check for blob size in address
        // map. It considers environment variable
        // TT_BACKEND_OVERLAY_MAX_EXTRA_BLOB_SIZE.
        blob_size_in_bytes = is_ethernet_core ? eth_l1_mem::address_map::OVERLAY_FULL_BLOB_SIZE()
                                              : dram_mem::address_map::OVERLAY_FULL_BLOB_SIZE();
    }

    if (blob_size_in_bytes <= min_blob_size)
    {
        // No need to allocate extra space.
        return 0;
    }

    // Calculate difference and trim it to 4KB boundary.
    unsigned int extra_blob_size_in_bytes = (blob_size_in_bytes - min_blob_size) & 0xFFFFF000;

    return extra_blob_size_in_bytes;
}

const L1Buffer *L1DataBuffersMemoryLayout::store_buffer(std::unique_ptr<L1Buffer> &&l1_buffer)
{
    if (dynamic_cast<ExtraOverlayBlobL1Buffer *>(l1_buffer.get()) != nullptr)
    {
        // If underlying buffer is overlay blob buffer, store it at the beginning.
        m_l1_data_buffers_start_section.push_back(std::move(l1_buffer));
        return m_l1_data_buffers_start_section.back().get();
    }
    else
    {
        // Any other data buffer is stored from end towards beginning.
        m_l1_data_buffers_end_section.push_front(std::move(l1_buffer));
        return m_l1_data_buffers_end_section.front().get();
    }
}

std::string L1DataBuffersMemoryLayout::get_allocation_info() const
{
    std::stringstream string_stream;

    if (m_l1_data_buffers_start_section.size() == 0 && m_l1_data_buffers_end_section.size() == 0)
    {
        // No allocations done.
        return string_stream.str();
    }

    for (const std::unique_ptr<L1Buffer> &l1_buffer : m_l1_data_buffers_start_section)
    {
        string_stream << l1_buffer->get_allocation_info() << "\n";
    }

    for (const std::unique_ptr<L1Buffer> &l1_buffer : m_l1_data_buffers_end_section)
    {
        string_stream << l1_buffer->get_allocation_info() << "\n";
    }

    string_stream << "\n";

    return string_stream.str();
}

std::vector<const L1Buffer *> L1DataBuffersMemoryLayout::get_all_allocated_buffers() const
{
    std::vector<const L1Buffer *> allocated_buffers;

    for (const std::unique_ptr<L1Buffer> &l1_buffer : m_l1_data_buffers_start_section)
    {
        allocated_buffers.push_back(l1_buffer.get());
    }

    for (const std::unique_ptr<L1Buffer> &l1_buffer : m_l1_data_buffers_end_section)
    {
        allocated_buffers.push_back(l1_buffer.get());
    }

    return allocated_buffers;
}

}  // namespace pipegen2