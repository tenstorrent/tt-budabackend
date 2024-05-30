// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/stream_graph/ncrisc_config.h"

// Forward declarations so we don't have to include headers here.
namespace pipegen2
{
class PhaseConfig;
}

namespace blobgen2
{

class NOCHelper;
class PhaseConfigHelper;

// These are stated to avoid using a lot of "pipegen2::" throughout the code.
using pipegen2::NcriscConfig;
using pipegen2::PhaseConfig;
using pipegen2::PhaseId;

// Note that this is true only for grayskull, since its l1_mem::address_map::MAX_SIZE is 1MB.
// This is used only to calculate some destination on another chip when dram_streaming is enabled.
// I haven't found a case where this is enabled for non grayskull chip. For wormhole, it would probably be 21.
constexpr static unsigned int c_l1_size_log2 = 20;

// This helper is used with ncrisc configuration related calculations:
//  - It holds logic around default values for fields in NcriscConfig.
//  - Calculations made solely using fields from NcriscConfig, but are potentially used at more places in code and have
//  their own meaning.
//  - Calculations made using NcriscConfig and first PhaseConfig for a single stream.
//  - Values calculated based on all ncrisc configs for a single stream.
// TODO: Most of the code here has a potential to move to pipegen2
class NcriscConfigHelper
{
public:
    NcriscConfigHelper(const NcriscConfig& ncrisc) : ncrisc(ncrisc) {};

    bool has_dram_scatter_offsets() const { return ncrisc.dram_scatter_offsets.has_value(); }

    unsigned int reader_index() const { return ncrisc.reader_index.value_or(0); }

    unsigned int total_readers() const { return ncrisc.total_readers.value_or(1); }

    unsigned int get_dram_scatter_chunk_size_tiles() const { return ncrisc.dram_scatter_chunk_size_tiles.value_or(0); }

    bool get_dram_io() const { return ncrisc.dram_io.value_or(false); }

    bool get_dram_input() const { return ncrisc.dram_input.value_or(false); }

    bool get_dram_output() const { return ncrisc.dram_output.value_or(false); }

    bool get_dram_streaming() const { return ncrisc.dram_streaming.value_or(false); }

    bool get_dram_ram() const { return ncrisc.dram_ram.value_or(false); }

    unsigned int get_dram_scatter_offsets_full_size() const
    {
        return ncrisc.dram_scatter_offsets_full_size.value_or(0);
    }

    unsigned int get_dram_q_slot_size_tiles() const;

    unsigned int get_dram_q_slot_size_tiles(const PhaseConfigHelper& phase) const;

    unsigned int get_epoch_q_slots_remaining(
        const PhaseConfigHelper& phase, const std::map<PhaseId, PhaseConfig*>& phase_configs) const;

    unsigned int get_num_scatter_offsets(const PhaseConfigHelper& phase) const;

    unsigned int get_dram_q_slot_size_bytes() const;

    unsigned int get_epoch_q_slots_remaining_non_dram_scatter(
        const PhaseConfigHelper& phase, const std::map<PhaseId, PhaseConfig*>& phase_configs) const;

    unsigned int get_dram_q_slot_size_tiles_single_scatter(const PhaseConfigHelper& phase) const;

    unsigned int get_scatter_chunk_size_bytes() const;

    unsigned int get_stream_flags() const;

    uint64_t get_modified_dram_addr(
        const PhaseConfigHelper& phase, const NOCHelper& noc_helper, const uint64_t addr) const;

    uint64_t get_dram_buf_noc_addr(const PhaseConfigHelper& phase, const NOCHelper& noc_helper) const;

    std::vector<uint64_t> get_dram_scatter_offsets(const PhaseConfigHelper& phase, const NOCHelper& noc_helper) const;

    unsigned int get_data_chunk_size_tiles() const;

    unsigned int get_data_chunk_size_bytes(const PhaseConfigHelper& first_phase_helper) const;

    unsigned int get_dram_streaming_epoch_id(const PhaseId& first_phase_id) const;

    uint64_t get_dram_streaming_header_addr(
        const PhaseConfigHelper& first_phase_helper,
        const NOCHelper& noc_helper,
        const uint32_t desired_header_addr) const;

    static unsigned int get_dram_num_msgs(const std::vector<NcriscConfig>& ncriscs);

    static bool has_multi_readers(const std::vector<NcriscConfig>& ncriscs);

private:
    const NcriscConfig& ncrisc;
};

}  // namespace blobgen2
