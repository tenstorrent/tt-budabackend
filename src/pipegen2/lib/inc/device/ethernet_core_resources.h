// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

// clang-format off
#include "device/tt_xy_pair.h"

#include "core_resources.h"
#include "model/typedefs.h"
// clang-format on

namespace pipegen2
{

class EthernetCoreResources : public CoreResources
{
public:
    EthernetCoreResources(const tt_cxy_pair& core_physical_location);

    // Allocates stream with capability of transferring data over ethernet and returns its ID.
    StreamId allocate_ethernet_stream();

private:
    // Returns next available gather stream ID.
    StreamId get_next_available_gather_stream_id() override;

    // Returns next available multicast stream ID.
    StreamId get_next_available_multicast_stream_id() override;

    // Returns next available general purpose stream ID.
    StreamId get_next_available_general_purpose_stream_id() override;

    // Calculates total number of multicast streams available.
    unsigned int calculate_multicast_streams_count() const override;

    // Calculates total number of general purpose streams available.
    unsigned int calculate_general_purpose_streams_count() const;

    // Calculates total number of ethernet streams available.
    unsigned int calculate_ethernet_streams_count() const;

    // Returns total number of streams available capable of doing ethernet transfers.
    unsigned int calculate_ethernet_transfers_capable_streams_count() const;

    // Returns next available gather/multicast stream ID.
    StreamId get_next_available_gather_multicast_stream_id();

    // Returns next available ethernet stream ID;
    StreamId get_next_available_ethernet_stream_id();

    // Next available ethernet stream ID. Streams are allocated in the range [9, 11] and then [0, 3] as those have
    // ethernet capability as well. But first prioritize the range [9, 11] because streams in range [0, 3] can do
    // multicast as well.
    StreamId m_next_available_ethernet_stream_id;

    // ID of the next available gather/multicast stream.
    StreamId m_next_available_gather_multicast_stream_id;
};

}  // namespace pipegen2