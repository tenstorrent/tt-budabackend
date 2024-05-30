// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>

#include "epoch.h"
#include "overlay_blob/epoch_blob_data.h"
#include "overlay_blob/typedef.h"

namespace pipegen2
{
struct L1BufferAllocationInfo;
}

namespace blobgen2
{

class EpochAllocator;
class StreamInfo;
class SoCHelper;
class NOCHelper;

using pipegen2::L1BufferAllocationInfo;
using pipegen2::NcriscConfig;
using pipegen2::PhaseConfig;

// A group of static functions for building up epoch structs. These functions mostly call BlobSection's functions for
// the trivial part of filling structs. In addition, these functions care about allocating structs and preserving
// pointers at the right places. These functions also do a second pass of filling structs which can't be done in the
// first pass.
class EpochTBuilder
{
public:
    // Fills the epoch structs for all cores, from phase and ncrisc configs.
    static std::map<tt_cxy_pair, EpochAllocator> fill_epoch_structs(
        EpochBlobData& epoch_blob_data,
        const std::map<tt_cxy_pair, dram_perf_info_t>& dram_perf_info_map,
        const std::map<tt_cxy_pair, L1BufferAllocationInfo>& ncrisc_fallback_buffers,
        const std::map<size_t, std::map<uint32_t, uint32_t>>& tile_size_and_address_per_chip,
        const int epoch_num,
        const SoCHelper& soc_helper);

private:
    // Fills epoch_t structs for all cores.
    // They will be accessible through returned EpochAllocator map.
    static std::map<tt_cxy_pair, EpochAllocator> fill_epoch_t(
        EpochBlobData& epoch_blob_data,
        const std::map<tt_cxy_pair, dram_perf_info_t>& dram_perf_info_map,
        const std::map<tt_cxy_pair, L1BufferAllocationInfo>& ncrisc_fallback_buffers,
        const std::map<size_t, std::map<uint32_t, uint32_t>>& tile_size_and_address_per_chip,
        const int epoch_num,
        const SoCHelper& soc_helper);

    // Fills epoch_t struct, and all its underlying structs recursively.
    // Uses epoch_blob_filler for simple filling from phase and ncrisc configs, but then it
    // also creates other structures, such as epoch_stream_info_t, and fills pointers
    // to them within the epoch struct.
    static BlobPtr fill_epoch_t_single_core(
        EpochAllocator& epoch_allocator,
        const int epoch_num,
        const CoreBlobData& core_blob_data,
        const std::map<uint32_t, uint32_t>& tile_size_and_address,
        const std::optional<dram_perf_info_t> perf_info,
        const std::optional<L1BufferAllocationInfo>& ncrisc_fallback_buffer,
        const bool is_eth_core,
        const NOCHelper& noc_helper);

    // Similar to fill_epoch_t function, but for epoch_stream_info_t.
    static BlobPtr fill_epoch_stream_info_t(
        EpochAllocator& epoch_allocator, const StreamBlobData& stream_blob_data, const NOCHelper& noc_helper);

    // Similar to fill_epoch_t function, but for scatter_pipe_structs.
    static BlobPtr fill_scatter_pipe_structs(EpochAllocator& epoch_allocator, const StreamInfo& stream_info);

    // Similar to fill_epoch_t function, but for epoch_stream_dram_io_info_t.
    static BlobPtr fill_epoch_stream_dram_io_info_t(
        EpochAllocator& epoch_allocator, const StreamBlobData& stream_blob_data, const NOCHelper& noc_helper);

    // Similar to fill_epoch_t function, but for dram_io_state_t.
    static BlobPtr fill_dram_io_state_t(
        EpochAllocator& epoch_allocator, const StreamBlobData& stream_blob_data, const NOCHelper& noc_helper);

    // Similar to fill_epoch_t function, but for dram_io_scatter_state_t.
    static BlobPtr fill_dram_io_scatter_state_t(
        EpochAllocator& epoch_allocator,
        const PhaseConfig& first_phase,
        const NcriscConfig ncrisc_config,
        const NOCHelper& noc_helper);

    // Similar to fill_epoch_t function, but for dram_scatter offsets.
    static BlobPtr fill_dram_scatter_offsets(
        EpochAllocator& epoch_allocator,
        const PhaseConfig& first_phase,
        const NcriscConfig ncrisc_config,
        const NOCHelper& noc_helper,
        const int scatter_offsets_size);

    // Similar to fill_epoch_t function, but for dram_io_scatter_state_t.
    static epoch_stream_info_t* find_epoch_stream_info_t(
        EpochAllocator& epoch_allocator,
        const tt_cxy_pair core_location,
        const StreamId stream_id,
        const SoCHelper& soc_helper);

    // ------------------------------------ Second pass filling epoch structures --------------------------------------

    // Fills fork_idxs fields in stream_info structs. These point to other streams on the same core, so all streams have
    // to be filled first.
    static void fill_epoch_stream_info_t_fork_idxs_all_cores(
        std::map<tt_cxy_pair, EpochAllocator>& epoch_alocators,
        const EpochBlobData& epoch_blob_data,
        const SoCHelper& soc_helper);

    // Fills fork_idxs on a single core.
    static void fill_epoch_stream_info_t_fork_idxs(
        const epoch_stream_info_t_ptr (&active_streams)[NOC_NUM_STREAMS],
        EpochAllocator& epoch_allocator,
        const CoreBlobData& core_blob_data);

    // After all epoch structs are filled, we only now know the address where phase blob configurations start. This
    // function adds the needed absolute address to the relative offsets in the StreamInfo structs.
    static void update_blob_base_addrs(
        std::map<tt_cxy_pair, EpochAllocator>& epoch_allocators,
        const EpochBlobData& epoch_blob_data,
        const SoCHelper& soc_helper);

    // Fills the dram_streaming_header_addr field in the epoch structs. This field is the address where the phase
    // will stream to. This points to a stream on another core, so all stream_infos have to be filled first on all
    // cores.
    // This is used only for transferring data through MMIO, which is used only on grayskull.
    static void fill_dram_streaming_header_addr(
        std::map<tt_cxy_pair, EpochAllocator>& epoch_allocators,
        const EpochBlobData& epoch_blob_data,
        const SoCHelper& soc_helper);

    // Fills the specific config for intermediate buffer.
    static void fill_intermediates_stream_binary_instruction_list(
        std::map<tt_cxy_pair, EpochAllocator>& epoch_allocators, EpochBlobData& epoch_blob_data);
};
}  // namespace blobgen2