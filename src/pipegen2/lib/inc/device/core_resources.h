// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <set>

// clang-format off
#include "device/tt_xy_pair.h"

#include "device/l1/l1_data_buffers_memory_layout.h"
#include "model/stream_graph/stream_node.h"
#include "model/typedefs.h"
// clang-format on

namespace pipegen2
{

class L1Buffer;
class TileHeaderL1Buffer;

class CoreResources
{
public:
    // Virtual destructor, necessary for forward declarations of classes in smart pointer members.
    virtual ~CoreResources();

    // Allocates stream with capability of gathering and returns its ID.
    StreamId allocate_gather_stream();

    // Allocates stream with capability of multicasting and returns its ID.
    StreamId allocate_multicast_stream();

    // Allocates extra stream with general purpose capabilities and returns its ID.
    StreamId allocate_general_purpose_stream();

    // Allocates kernel input and returns its index, which is index into `inputs` array field of epoch_t structure
    // not based on operand ID or stream ID, because those IDs are discontiguous and some streams do not even have
    // operand ID (like forks).
    unsigned int allocate_kernel_input();

    // Allocates kernel output and returns its index, which is index into `outputs` array field of epoch_t structure
    // not based on operand ID or stream ID, because those IDs are discontiguous and some streams do not even have
    // operand ID (like forks).
    unsigned int allocate_kernel_output();

    // Returns total number of multicast streams available.
    unsigned int get_multicast_streams_count() const;

    // Returns streams that have been allocated on the core.
    const std::set<StreamId>& get_allocated_stream_ids() const { return m_allocated_stream_ids; }

    // Allocates tile header buffer for the given tile size and returns object representing that buffer.
    const L1Buffer* allocate_l1_tile_header_buffer(const unsigned int tile_size);

    // Allocates chunk of memory in L1's data buffers section used as stream buffer and returns object representing
    // that buffer.
    const L1Buffer* allocate_l1_stream_buffer(const StreamNode* stream_node, const unsigned int buffer_size);

    // Allocates chunk of memory in L1's data buffers section used as NCRISC fallback buffer and returns object
    // representing that buffer.
    const L1Buffer* allocate_l1_ncrisc_fallback_buffer(const unsigned int buffer_size);

    // Allocates extra chunk of space in L1 memory reserved for overlay blob and returns object representing that memory
    // chunk.
    const L1Buffer* allocate_l1_extra_overlay_blob_space(
        const unsigned int total_blob_size, const bool is_ethernet_core);

    // Checks if core is out of L1 memory for data buffers space and throws an exception if it is.
    void check_if_out_of_l1_data_buffers_memory() const;

    // Returns tile header buffer address for a given tile size. Asserts if such buffer was not allocated previously.
    unsigned int get_tile_header_buffer_address(const unsigned int tile_size) const;

    void set_op_name(const std::string& op_name);

    // Returns a string with formatted allocation info about buffers allocated on this core.
    const std::string get_l1_memory_layout_info() const;

    // Returns vector of all L1 buffers allocated on this core in rising order or buffer addresses.
    // TODO this is a temporary API to support L1 profiler from runtime. We don't want to couple runtime code to pipegen
    // objects, thus better API should be devised.
    std::vector<const L1Buffer*> get_all_allocated_data_buffers() const
    {
        return m_l1_data_buffers_memory->get_all_allocated_buffers();
    }

    // Gets logical location of the core tied to this core resource object.
    const tt_cxy_pair& get_logical_location() const { return m_core_logical_location; }

protected:
    // Constructor, protected from public.
    CoreResources(
        const tt_cxy_pair& core_physical_location,
        const tt_cxy_pair& core_logical_location,
        StreamId extra_streams_id_range_start,
        StreamId extra_streams_id_range_end,
        const unsigned int l1_data_buffers_space_start_address,
        const unsigned int l1_data_buffers_space_end_address,
        const unsigned int l1_predefined_tile_header_buffer_address);

    // Returns next available gather stream ID.
    virtual StreamId get_next_available_gather_stream_id() = 0;

    // Returns next available multicast stream ID.
    virtual StreamId get_next_available_multicast_stream_id() = 0;

    // Returns next available general purpose stream ID.
    virtual StreamId get_next_available_general_purpose_stream_id() = 0;

    // Calculates total number of multicast streams available.
    virtual unsigned int calculate_multicast_streams_count() const = 0;

    // Returns first valid extra stream ID.
    StreamId get_extra_streams_id_range_start() const { return c_extra_streams_id_range_start; }

    // Returns last valid extra stream ID.
    StreamId get_extra_streams_id_range_end() const { return c_extra_streams_id_range_end; }

    // Returns physical location of the core.
    const tt_cxy_pair& get_physical_location() const { return m_core_physical_location; }

    // ID of the next available extra stream.
    StreamId m_next_available_extra_stream_id;

    // Allocated stream IDs.
    std::set<StreamId> m_allocated_stream_ids;

private:
    // Starting ID in the extra streams ID range.
    // Extra streams are used for general purpose, for example as gather input streams.
    const StreamId c_extra_streams_id_range_start;

    // Ending ID in the extra streams ID range.
    const StreamId c_extra_streams_id_range_end;

    // Index of the next available kernel input.
    unsigned int m_next_available_kernel_input_index;

    // Index of the next available kernel output.
    unsigned int m_next_available_kernel_output_index;

    // Physical location of the core.
    const tt_cxy_pair m_core_physical_location;

    // Logical location of the core.
    const tt_cxy_pair m_core_logical_location;

    // Memory layout of L1 data buffers section.
    std::unique_ptr<L1DataBuffersMemoryLayout> m_l1_data_buffers_memory;
};

}  // namespace pipegen2