// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "overlay_blob/blob_section.h"

using pipegen2::PhaseId;

namespace blobgen2
{

// This struct is used in FW to track stream's blobs for each destination.
// Scatter stream (product of serially forked pipe) has multiple destinations and for each destination we create
// separate blob. Each of these blobs must be ran from brisc and therefore it must know where to find blob.
struct PhaseOffsetsForSingleDest
{
    // Start address for each blob's first phase
    std::vector<uint32_t> offsets;

    // How many regs is configured in first phase of the blob
    std::vector<uint32_t> phase_num_cfg_regs;

    // Total number of unrolled iters, which directly matches number of blobs for a destination.
    int num_unroll_iter;
};

// This struct holds additional info needed per stream to generate the blob.
// This struct is first filled from the StreamGraph and then used to fill the epoch structs.
// It is possible that this could be removed by careful refactoring of the order of filling the epoch structs.
struct StreamInfo
{
    // Not allowed to be copied around.
    StreamInfo(StreamInfo&) = delete;

    StreamInfo(StreamInfo&&) = default;

    // Holds the blob section entries related to dummy phases.
    // Each dummy phase entry is held as a separate BlobSection, but they're all merged in the end.
    // For each MBlock buffer id, we hold a list of BlobSection entries.
    // Dummy phases are put at the end of the blob, i.e. they are last phases for a stream.
    std::vector<std::vector<BlobSection>> dummy_phase_stream_binary_instruction_list;

    // Holds the phase configuration blob section entries, specfically a list of them per each phase.
    std::map<PhaseId, std::vector<BlobSection>> stream_binary_instruction_list;

    // Holds the blob scatter info for each phase when scatter stream changes destination.
    std::map<PhaseId, PhaseOffsetsForSingleDest> phase_offsets;

    // Holds the header configuration for each phase.
    std::map<PhaseId, uint32_t> cfg_header_dw;

    // Relative blob start address. To be added to the absolute base address where blobs start.
    uint32_t blob_start_relative_offset;

    uint32_t blob_size;

    // Number of registers configured for the first phase.
    uint32_t start_phase_num_cfg_regs;

    size_t get_phase_blob_section_byte_size(const BlobSection& blob_section) const
    {
        // +1 at the end is to account for header.
        return 4 * (blob_section.dw_size() + 1);
    }

    size_t get_binary_instruction_size(const PhaseId phase_id) const
    {
        // [0] is to take 0th mblock, cause it doesn't matter which it is since they are identical.
        return get_phase_blob_section_byte_size(stream_binary_instruction_list.at(phase_id)[0]);
    }

    size_t get_dummy_phase_instructions_size(
        const bool has_dummy_phase_receiver, const bool has_dummy_phase_sender) const
    {
        size_t curr_blob_relative_offset = 0;

        // If it has a single dummy phase, take the size of the last item.
        // If it has both, take size of both of them, which is last two indexes.
        int index_from_last_to_take = (has_dummy_phase_receiver ? 1 : 0) + (has_dummy_phase_sender ? 1 : 0);

        while (index_from_last_to_take > 0)
        {
            size_t dummy_phase_instruction_index =
                dummy_phase_stream_binary_instruction_list[0].size() - index_from_last_to_take;
            curr_blob_relative_offset += get_phase_blob_section_byte_size(
                dummy_phase_stream_binary_instruction_list[0][dummy_phase_instruction_index]);

            index_from_last_to_take--;
        }

        return curr_blob_relative_offset;
    }
};

}  // namespace blobgen2
