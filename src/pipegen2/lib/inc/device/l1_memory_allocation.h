// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace pipegen2
{

// Base class modelling memory allocation on core's L1 memory.
class L1MemoryAllocation
{
public:
    L1MemoryAllocation(unsigned int size, unsigned int address) : 
        m_size(size), 
        m_address(address)
    {
    }

    // Destructor, necessary for forward declarations of classes in smart pointer members.
    virtual ~L1MemoryAllocation() = default;

    // Returns the name describing the allocation.
    virtual std::string allocation_name() const = 0;

    // Returns a string with formatted allocation info.
    virtual std::string allocation_info() const = 0;

    const unsigned int get_size() const { return m_size; }

    const unsigned int get_address() const { return m_address; }

private:
    // Size of the allocated memory.
    unsigned int m_size;

    // Starting address of the allocated memory.
    unsigned int m_address; 
};

} // namespace pipegen2