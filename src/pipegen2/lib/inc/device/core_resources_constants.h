// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/typedefs.h"

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

} // namespace ethernet_core_resources_constants

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