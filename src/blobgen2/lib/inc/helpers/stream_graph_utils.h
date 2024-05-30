// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "overlay_blob/epoch_blob_data.h"

namespace pipegen2
{
class StreamGraphCollection;
}

namespace blobgen2
{

class SoCHelper;

using pipegen2::NcriscConfig;
using pipegen2::PhaseConfig;
using pipegen2::StreamGraphCollection;

// A group of static functions for reading StreamGraphCollection and extracting useful information from it.
class StreamGraphUtils
{
public:
    // Extracts chip ids from all configs. SocInfo is constructed using this number of chips.
    // Same is done in pipegen.
    static std::vector<size_t> get_chip_ids(const StreamGraphCollection& stream_graphs);

    // Transforms the StreamGraphCollection into a hierarchy of more convenient maps that hold PhaseConfigs and
    // NcriscConfigs.
    static EpochBlobData get_epoch_blob_data(const StreamGraphCollection& stream_graphs, const int epoch_num);

    // Collects a map of tile size and tile header buffer address per chip. These are common for all non ethernet cores
    // on a single chip, but all must be collected since the epoch struct requires this info.
    static std::map<size_t, std::map<uint32_t, uint32_t>> get_tile_size_and_address_per_chip(
        const StreamGraphCollection& stream_graphs, const SoCHelper& soc_helper);
};
}  // namespace blobgen2