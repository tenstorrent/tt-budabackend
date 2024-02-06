// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/worker_core_resources.h"

namespace pipegen2
{

class WorkerCoreResourcesGS : public WorkerCoreResources
{
public:
    WorkerCoreResourcesGS(const tt_cxy_pair& core_physical_location);

private:
    // Returns next available gather stream ID.
    StreamId get_next_available_gather_stream_id() override;

    // Returns next available multicast stream ID.
    StreamId get_next_available_multicast_stream_id() override;

    // Returns appropriate packer-multicast stream ID based on operand ID.
    StreamId get_packer_multicast_stream_id(int operand_id) override;

    // Calculates total number of multicast streams available.
    unsigned int calculate_multicast_streams_count() const override;

    // Returns next available gather/multicast stream ID.
    StreamId get_next_available_gather_multicast_stream_id();

    // ID of the next available gather/multicast stream.
    StreamId m_next_available_gather_multicast_stream_id;
};

} // namespace pipegen2