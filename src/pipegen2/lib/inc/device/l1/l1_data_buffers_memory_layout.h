// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <list>
#include <memory>
#include <string>

#include "device/tt_xy_pair.h"

namespace pipegen2
{

class L1Buffer;
class TileHeaderL1Buffer;
class StreamNode;

// Class modelling memory layout of core's L1 memory. Since some buffers are allocated at the beginning of data buffers
// space, and majority of others from the end to beginning in order in which allocation methods are called, two separate
// containers are used to model those two sections of memory in order to have efficient insert method.
class L1DataBuffersMemoryLayout
{
public:
    L1DataBuffersMemoryLayout(
        const tt_cxy_pair& core_physical_location,
        const tt_cxy_pair& core_logical_location,
        const unsigned int l1_data_buffers_space_start_address,
        const unsigned int l1_data_buffers_space_end_address,
        const unsigned int l1_predefined_tile_header_buffer_address);

    // Destructor, necessary for forward declarations of classes in smart pointer members.
    ~L1DataBuffersMemoryLayout();

    // Allocates tile header buffer for the given tile size and keeps track of that allocation. First call of this
    // function allocates tile buffer at a predefined address. Following calls will allocate extra memory chunk from end
    // of L1 data buffers space towards beginning in which buffer for provided tile size will be allocated. Returns
    // object representing that buffer.
    const L1Buffer* allocate_l1_tile_header_buffer(const unsigned int tile_size);

    // Allocates chunk of memory in L1's data buffers section used as stream buffer and keeps track of that allocation.
    // First THB on core is allocated at a predefined address, for every other THB extra memory is allocated from end of
    // L1 data buffers space towards beginning. Returns object representing that buffer.
    const L1Buffer* allocate_l1_stream_buffer(const StreamNode* stream_node, const unsigned int buffer_size);

    // Allocates chunk of memory in L1's data buffers section used as NCRISC fallback buffer and keeps track of that
    // allocation. Memory is allocated from end of L1 data buffers space towards beginning. Returns object representing
    // that buffer.
    const L1Buffer* allocate_l1_ncrisc_fallback_buffer(const unsigned int buffer_size);

    // Allocates extra chunk of space in L1 memory reserved for overlay blob and keeps track of that allocation. Memory
    // is allocated at the beginning of L1 data buffers space and can be allocated only once. Returns object
    // representing that memory chunk.
    const L1Buffer* allocate_l1_extra_overlay_blob_space(
        const unsigned int total_blob_size, const bool is_ethernet_core);

    // Checks if core is out of L1 memory for data buffers space and throws an exception if it is.
    void check_if_out_of_l1_data_buffers_memory();

    // Returns tile header buffer address for a given tile size. Asserts if such buffer was not allocated previously.
    unsigned int get_tile_header_buffer_address(const unsigned int tile_size);

    // Returns a string with formatted allocation info.
    std::string get_allocation_info() const;

    // Returns vector of all L1 buffers kept in this layout in rising order or buffer addresses.
    // TODO this is a temporary API to support L1 profiler from runtime. We don't want to couple runtime code to pipegen
    // objects, thus better API should be devised.
    std::vector<const L1Buffer*> get_all_allocated_buffers() const;

private:
    // Allocates memory for data buffer in L1 and returns the allocation address.
    // Memory is allocated from the end of L1 data buffers space towards beginning.
    unsigned int allocate_space_for_l1_data_buffer(const unsigned int size_in_bytes);

    // Calculates additional space in L1 to be used as extra overlay blob space.
    static unsigned int calculate_extra_blob_space_to_allocate(
        unsigned int blob_size_in_bytes, const bool is_ethernet_core);

    // Stores buffer in layout and returns pointer to buffer object.
    const L1Buffer* store_buffer(std::unique_ptr<L1Buffer>&& l1_buffer);

    // Physical location of core to which this L1 memory refers to.
    const tt_cxy_pair m_core_physical_location;

    // Physical location of core to which this L1 memory refers to.
    const tt_cxy_pair m_core_logical_location;

    // Starting address of the space in L1 memory reserved for data buffers.
    const unsigned int c_l1_data_buffers_space_start_address;

    // Ending address of the space in L1 memory reserved for data buffers.
    const unsigned int c_l1_data_buffers_space_end_address;

    // Predefined tile header buffer address in L1. First tile buffer is stored at it, while others are stored in data
    // buffers section.
    const unsigned int c_l1_predefined_tile_header_buffer_address;

    // Extra space in L1 memory in bytes, reserved for overlay blob.
    unsigned int m_l1_extra_overlay_blob_space;

    // Current address in L1 data buffers space available for allocation.
    unsigned int m_l1_current_data_buffers_space_address;

    // Models starting section of L1 data buffers space where, for now, only extra overlay blob space is allocated at
    // and only once per core. Addresses of buffers are increasing to the right and this section is growing to the
    // right also.
    std::list<std::unique_ptr<L1Buffer>> m_l1_data_buffers_start_section;

    // Other data buffers are stored from end towards beginning, in order in which they are created. Addresses of
    // buffers are increasing to the right, while this section is growing to the left by adding buffers.
    std::list<std::unique_ptr<L1Buffer>> m_l1_data_buffers_end_section;

    // Maps from tile size to it's corresponding tile header buffer.
    std::map<unsigned int, const TileHeaderL1Buffer*> m_allocated_tile_header_buffers;
};

}  // namespace pipegen2