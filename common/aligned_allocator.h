// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <cstddef>
#include <cstdlib>

template <class T, std::size_t Alignment>
struct AlignedAllocator
{
    typedef T value_type;
    static inline constexpr std::size_t alignment = Alignment;

    T* allocate(std::size_t n, const void * = nullptr)
    {
        return static_cast<T*>(
        // As of C++17, cstdlib declares aligned_alloc within std.
#ifndef __APPLE__
            std::
#endif
            aligned_alloc(alignment, n * sizeof(T)));
    }

    void deallocate(T *p, std::size_t n)
    {
        std::free(p);
    }

    // rebind is optional if the allocator is a template of the form Allocator<T, ...>,
    // yet g++ requires this.
    template <class U>
    struct rebind
    {
        typedef AlignedAllocator<U, alignment> other;
    };
};
