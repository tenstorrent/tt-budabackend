// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/tt_xy_pair.h"

#include "core_resources.h"
#include "model/typedefs.h"

namespace pipegen2
{

class WorkerCoreResources : public CoreResources
{
public:
    // Virtual destructor, necessary for base class.
    virtual ~WorkerCoreResources() = default;

    // Allocates stream for transfer from the packer and returns its ID.
    StreamId allocate_packer_stream(int operand_id);

    // Allocates stream for transfer to the unpacker and returns its ID.
    StreamId allocate_unpacker_stream(int operand_id);

    // Allocates intermediate stream and returns its ID.
    StreamId allocate_intermed_stream(int operand_id);

    // Allocates stream with capability of multicasting directly from packer and returns its ID.
    StreamId allocate_packer_multicast_stream(int operand_id);

protected:
    // Constructor, protected from public.
    WorkerCoreResources(const tt_cxy_pair& core_physical_location, const tt_cxy_pair& core_logical_location);

    // Returns next available gather stream ID.
    virtual StreamId get_next_available_gather_stream_id() override = 0;

    // Returns next available multicast stream ID.
    virtual StreamId get_next_available_multicast_stream_id() override = 0;

    // Returns appropriate packer-multicast stream ID based on operand ID.
    virtual StreamId get_packer_multicast_stream_id(int operand_id) = 0;

    // Calculates total number of multicast streams available.
    virtual unsigned int calculate_multicast_streams_count() const override = 0;

    // Calculates total number of general purpose streams available.
    virtual unsigned int calculate_general_purpose_streams_count() const = 0;

private:
    // Returns next available general purpose stream ID.
    StreamId get_next_available_general_purpose_stream_id() override;
};

} // namespace pipegen2