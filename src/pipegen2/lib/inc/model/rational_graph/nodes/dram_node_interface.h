// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace pipegen2
{
// Base interface for all DRAM nodes.
class DramNodeInterface
{
public:
    virtual ~DramNodeInterface() {}

    virtual unsigned int get_num_queue_slots() const = 0;

    virtual unsigned long get_dram_address() const = 0;

    virtual unsigned int get_dram_channel() const = 0;

    virtual unsigned int get_dram_subchannel() const = 0;

    virtual bool get_is_ram() const = 0;

    virtual bool get_is_remote_io() const = 0;
};
}  // namespace pipegen2