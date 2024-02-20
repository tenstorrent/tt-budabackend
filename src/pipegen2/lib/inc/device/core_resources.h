// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>
#include <set>

#include "device/tt_xy_pair.h"

#include "device/l1_memory_allocation.h"
#include "model/stream_graph/stream_node.h"
#include "model/typedefs.h"

namespace pipegen2
{

class L1MemoryAllocation;
class StreamNode;

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

    // Allocates extra space in L1 memory, reserved for message info buffer tile headers. Memory is allocated at the
    // beginning of L1 data buffers space.
    void allocate_l1_extra_tile_headers_space(unsigned int num_extra_tile_headers);

    // Allocates extra space in L1 memory, reserved for overlay blob. Memory is allocated at the beginning of L1
    // data buffers space.
    void allocate_l1_extra_overlay_blob_space(unsigned int size_in_bytes);

    // Allocates memory for data buffer in L1 and returns the allocation address.
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    unsigned int allocate_l1_data_buffer(unsigned int size_in_bytes);

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

    // Returns all memory allocation on the L1 memory of the core.
    const std::vector<std::unique_ptr<L1MemoryAllocation>>& get_all_memory_allocations() const 
    { 
        return m_memory_allocations; 
    }

    // Adds stream allocation to the core resource tracker.
    void track_stream_buffer_allocation(const StreamNode* stream_node, 
                                        const unsigned int buffer_size, 
                                        const unsigned int buffer_address);

    // Checks if core is out of L1 memory for data buffers space, and throws the error if it is.
    void check_if_out_of_l1_data_buffers_memory();

    // Gets logical location of the core tied to this core resource object.
    const tt_cxy_pair& get_logical_location() const { return m_core_logical_location; }

    // Sets the name of the op placed on this core.
    void set_op_name(const std::string& op_name) { m_op_name = op_name; }

    // Gets the name of the op placed on this core.
    const std::string get_op_name() const { return m_op_name != "" ? m_op_name : "undefined"; }

protected:
    // Constructor, protected from public.
    CoreResources(const tt_cxy_pair& core_physical_location,
                  const tt_cxy_pair& core_logical_location,
                  StreamId extra_streams_id_range_start,
                  StreamId extra_streams_id_range_end,
                  int l1_data_buffers_space_start_address,
                  int l1_data_buffers_space_end_address);

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
    // Returns a string represenations of the L1 memory allocations on the core.
    std::string allocations_to_string() const;

    // Starting ID in the extra streams ID range.
    // Extra streams are used for general purpose, for example as gather input streams.
    const StreamId c_extra_streams_id_range_start;

    // Ending ID in the extra streams ID range.
    const StreamId c_extra_streams_id_range_end;

    // Starting address of the space in L1 memory reserved for data buffers.
    const int c_l1_data_buffers_space_start_address;

    // Ending address of the space in L1 memory reserved for data buffers.
    const int c_l1_data_buffers_space_end_address;

    // Extra space in L1 memory in bytes, reserved for message info buffer tile headers
    int m_l1_extra_tile_headers_space;

    // Extra space in L1 memory in bytes, reserved for overlay blob.
    int m_l1_extra_overlay_blob_space;

    // Current address in L1 data buffers space available for allocation.
    int m_l1_current_data_buffers_space_address;

    // Index of the next available kernel input.
    unsigned int m_next_available_kernel_input_index;

    // Index of the next available kernel output.
    unsigned int m_next_available_kernel_output_index;

    // Physical location of the core.
    const tt_cxy_pair m_core_physical_location;

    // Logical location of the core.
    const tt_cxy_pair m_core_logical_location;

    // Vector of all the memory allocations on the core.
    std::vector<std::unique_ptr<L1MemoryAllocation>> m_memory_allocations;

    // Name of the op placed on the core. If such name does not exist, the string is empty.
    std::string m_op_name = "";
};

} // namespace pipegen2