// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "device/tt_xy_pair.h"

namespace pipegen2
{

// Base class modelling memory allocation on core's L1 memory.
class L1Buffer
{
public:
    L1Buffer(const tt_cxy_pair& core_physical_location, const unsigned int address, const unsigned int size) :
        m_address(address), m_size(size)
    {
    }

    virtual ~L1Buffer() = default;

    const unsigned int get_address() const { return m_address; }

    const unsigned int get_size() const { return m_size; }

    // Returns string representation (name) of the buffer.
    virtual std::string get_name() const = 0;

    // Returns a string with formatted allocation info.
    virtual std::string get_allocation_info() const = 0;

private:
    // Physical coordinates of core on which this buffer is stored.
    const tt_cxy_pair m_core_physical_location;

    // Starting address of the allocated memory.
    const unsigned int m_address;

    // Size of the allocated memory.
    const unsigned int m_size;
};

}  // namespace pipegen2