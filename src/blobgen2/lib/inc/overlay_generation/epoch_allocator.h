// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <map>

#include "epoch.h"
#include "helpers/noc_helper.h"

namespace blobgen2
{

// Class used to track allocation of epoch related structures in the blob on L1. Holds the mapping between
// the pointers used in current process and pointers in the blob.
class EpochAllocator
{
public:
    EpochAllocator(BlobPtr start_address) : m_curr_address(start_address) {}

    // Don't allow copying this structure around.
    // Move constructor has to be explicitly defined when copy constructor is deleted.
    EpochAllocator(EpochAllocator&) = delete;
    EpochAllocator(EpochAllocator&&) = default;

    BlobPtr get_current_address() const { return m_curr_address; }

    // When current structs reached some memory address, but we need to be aligned for the next struct, this function
    // will pad the memory pointer by the required amount.
    void pad_to_noc_alignment() { m_curr_address = get_noc_alignment_padding_bytes(m_curr_address); }

// We always use T[] type inside unique_ptr. This is done for simpler code.
// Note that they will both return a pointer to the first or only element of the array.
// For each of the types we define a mapping between location in L1 memory, and in the current process' memory.
#define ADD_STRUCT_ALLOCATOR_AND_GETTER(T)                            \
private:                                                              \
    std::map<BlobPtr, std::unique_ptr<T[]>> blob_ptrs_to_##T;         \
                                                                      \
public:                                                               \
    std::pair<BlobPtr, T*> new_##T(const size_t arr_size = 1)         \
    {                                                                 \
        BlobPtr blob_ptr = m_curr_address;                            \
        blob_ptrs_to_##T[blob_ptr] = std::make_unique<T[]>(arr_size); \
        m_curr_address += arr_size * sizeof(T);                       \
        return {blob_ptr, blob_ptrs_to_##T[blob_ptr].get()};          \
    }                                                                 \
                                                                      \
    T* get_##T(const BlobPtr blob_ptr) const { return blob_ptrs_to_##T.at(blob_ptr).get(); }

    ADD_STRUCT_ALLOCATOR_AND_GETTER(uint8_t)
    ADD_STRUCT_ALLOCATOR_AND_GETTER(tt_uint64_t)
    ADD_STRUCT_ALLOCATOR_AND_GETTER(epoch_t)
    ADD_STRUCT_ALLOCATOR_AND_GETTER(epoch_stream_info_t)
    ADD_STRUCT_ALLOCATOR_AND_GETTER(epoch_stream_dram_io_info_t)
    ADD_STRUCT_ALLOCATOR_AND_GETTER(dram_io_state_t)
    ADD_STRUCT_ALLOCATOR_AND_GETTER(dram_io_scatter_state_t)
    ADD_STRUCT_ALLOCATOR_AND_GETTER(scatter_pipe_state_t)
    ADD_STRUCT_ALLOCATOR_AND_GETTER(scatter_pipe_blob_t)

#undef ADD_STRUCT_ALLOCATOR_AND_GETTER

private:
    BlobPtr m_curr_address;
};

}  // namespace blobgen2
