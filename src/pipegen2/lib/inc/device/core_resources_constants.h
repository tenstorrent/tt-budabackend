// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "eth_l1_address_map.h"
#include "l1_address_map.h"
#include "src/firmware/riscv/common/dram_stream_intf_constants.h"

#include "model/typedefs.h"
#include "pipegen2_constants.h"

namespace pipegen2
{
namespace core_resources_constants
{

// Maximum number of kernel inputs available per one core. Kernel input is index into `inputs` array field of
// epoch_t structure and since we want to keep this structure the same size for all core types this constant is
// universal and defined here in base class.
// TODO: Extract this constant from epoch.h (EPOCH_MAX_INPUTS) into some place where we can include it.
//       Including whole epoch.h in pipegen2 introduces bunch of compile errors and warnings.
constexpr unsigned int max_kernel_inputs_count = 24;

// Maximum number of kernel outputs available per one core. Kernel output is index into `outputs` array field of
// epoch_t structure and since we want to keep this structure the same size for all core types this constant is
// universal and defined here in base class.
// TODO: Extract this constant from epoch.h (EPOCH_MAX_OUTPUTS_ETH) into some place where we can include it.
//       Including whole epoch.h in pipegen2 introduces bunch of compile errors and warnings.
constexpr unsigned int max_kernel_outputs_count = 24;

// Forking factor (i.e. number of readers) of a scattered DRAM or PCIe buffer is limited to 255. Limit exists because
// variable which holds this info is 8-bit and is located in NCRISC L0 4KB DATA RAM.
constexpr unsigned int max_ncrisc_input_node_readers = 255;

// DRAM and PCIe read streams use data structures (queue descriptors) stored in NCRISC L0 to keep track of which part of
// queue we are reading from. Therefore we are limited by space due to how much of L0 has been designated for those
// structures.
constexpr unsigned int max_num_ncrisc_reading_streams = 8;

// DRAM and PCIe write streams also use queue descriptors stored in NCRISC L0 to keep track of which part of queue we
// are writing to. Similarly as above, we are limited by space in L0 designated for this.
constexpr unsigned int max_num_ncrisc_writing_streams = 8;

// A single DRAM IO stream can read from multiple DRAM or PCIe buffers. Total number of active (all but DRAM prefetch
// and DRAM output intermediates) DRAM or PCIe buffers a worker core accesses through all of its streams must be <= 40.
// Again, this limit comes from limitations of 4KB L0 DATA RAM in NCRISC where data structures to track NCRISC
// transfers are stored.
constexpr unsigned int max_num_active_buffers_accessed_through_l0 = MAX_L0_DRAM_Q_STATE_T;

// Memory in bytes one NCRISC FW struct used for keeping track of reading and writing to DRAM or PCIe buffers takes up.
constexpr unsigned int ncrisc_buffer_tracking_struct_size = EXPECTED_DRAM_Q_STATE_T_STRUCT_SIZE;

} // namespace core_resources_constants

namespace ethernet_core_resources_constants
{

// Number of NOC streams on ethernet cores. Ideally we should use ETH_NOC_NUM_STREAMS from
// noc_overlay_parameters.h, but we don't have it defined in grayskull lib.
constexpr StreamId ethernet_core_num_noc_streams = 32;

// Starting ID in the ethernet streams ID range.
constexpr StreamId ethernet_stream_id_range_start = 9;

// Ending ID in the ethernet streams ID range.
constexpr StreamId ethernet_stream_id_range_end = 11;

// Starting ID in the gather/multicast streams ID range.
constexpr StreamId gather_multicast_streams_id_range_start = 0;

// Ending ID in the gather/multicast streams ID range.
constexpr StreamId gather_multicast_streams_id_range_end = 3;

// Predefined tile header buffer address in L1.
constexpr unsigned int l1_predefined_tile_header_buffer_address =
    eth_l1_mem::address_map::OVERLAY_BLOB_BASE -
    128 /* cushion bytes */ -
    pipegen2::constants::tile_header_buffer_size_bytes;

} // namespace ethernet_core_resources_constants

namespace worker_core_resources_constants
{

// Predefined tile header buffer address in L1.
constexpr unsigned int l1_predefined_tile_header_buffer_address =
    l1_mem::address_map::OVERLAY_BLOB_BASE -
    128 /* cushion bytes */ -
    pipegen2::constants::tile_header_buffer_size_bytes;
}

namespace worker_core_resources_gs_constants
{

// Starting ID in the gather/multicast streams ID range.
// We are using same range of streams on Grayskull for gather and for multicast.
constexpr StreamId gather_multicast_streams_id_range_start = 0;

// Ending ID in the gather/multicast streams ID range.
// Stream 3 is reserved for fast tile clearing in Grayskull.
constexpr StreamId gather_multicast_streams_id_range_end = 2;

} // namespace worker_core_resources_gs_constants

namespace worker_core_resources_wh_constants
{

// Starting ID in the gather/multicast streams ID range.
// We are using same range of streams on Wormhole for gather and for multicast.
constexpr StreamId gather_multicast_streams_id_range_start = 0;

// Ending ID in the gather/multicast streams ID range.
// Streams 4-5 are not used because they have other functionality that might be
// required in the future, so they are reserved.
constexpr StreamId gather_multicast_streams_id_range_end = 3;

// First valid operand ID for which packer-multicast stream can be allocated.
constexpr unsigned int packer_multicast_stream_operand_id_range_start = 16;

// Last valid operand ID for which packer-multicast stream can be allocated.
constexpr unsigned int packer_multicast_stream_operand_id_range_end = 19;

} // namespace worker_core_resources_wh_constants

namespace worker_core_resources_bh_constants
{

// Starting ID in the gather/multicast streams ID range.
// We are using same range of streams on Blackhole for gather and for multicast.
constexpr StreamId gather_multicast_streams_id_range_start = 0;

// Ending ID in the gather/multicast streams ID range.
// Streams 4-5 are not used because they have other functionality that might be
// required in the future, so they are reserved.
constexpr StreamId gather_multicast_streams_id_range_end = 3;

// First valid operand ID for which packer-multicast stream can be allocated.
constexpr unsigned int packer_multicast_stream_operand_id_range_start = 16;

// Last valid operand ID for which packer-multicast stream can be allocated.
constexpr unsigned int packer_multicast_stream_operand_id_range_end = 19;

} // namespace worker_core_resources_bh_constants
} // namespace pipegen2