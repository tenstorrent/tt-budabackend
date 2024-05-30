// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "overlay_blob/epoch_blob_data.h"

namespace blobgen2
{

class SoCHelper;

using pipegen2::NcriscConfig;
using pipegen2::PhaseConfig;

// A group of static functions for filling up StreamInfo structs. These structs also contain byte vectors for
// phase configuration section of blobs.
class StreamInfoBuilder
{
public:
    // Fills the StreamInfo structs for all streams in the epoch.
    // These structs hold stream instructions per phase, which will end up directly in resulting blobs.
    static void fill_stream_infos(EpochBlobData& epoch_blob_data, const SoCHelper& soc_helper);

private:
    // Fills the StreamInfo struct for a single stream.
    static void fill_stream_info(
        StreamBlobData& stream_blob_data,
        EpochBlobData& epoch_blob_data,
        const SoCHelper& soc_helper,
        const tt_cxy_pair& core_id,
        const StreamId& stream_id,
        size_t& curr_blob_relative_offset);

    // Returns a map which holds the phase id increment to be written to the corresponding register for each phase.
    static std::map<PhaseId, size_t> get_num_inc_per_phase(const std::map<PhaseId, PhaseConfig*>& phase_configs);

    // Helper function used for appending a new PhaseOffsetsForSingleDest for this stream.
    static void append_phase_offset(
        StreamInfo& stream_info, const PhaseConfig& phase, const size_t curr_blob_relative_offset);

    // Fills header of the last phase of the stream.
    static void fill_header_of_last_phase(
        StreamBlobData& stream_blob_data,
        const PhaseConfig& phase,
        const std::map<uint32_t, PhaseId>& scatter_idx_to_phase_id,
        const std::map<PhaseId, size_t>& num_inc_per_phase);
};
}  // namespace blobgen2