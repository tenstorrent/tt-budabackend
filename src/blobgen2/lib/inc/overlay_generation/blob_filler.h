// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>

#include "overlay_blob/epoch_blob_data.h"

namespace blobgen2
{

class BlobData;
class EpochAllocator;
class PhaseBlobFiller;
class SoCHelper;

// A group of static functions used to fill byte vectors with epoch structures and phase configurations.
class BlobFiller
{
public:
    // Fill the blobs (essentially vectors of bytes) from the generated structures.
    static std::map<tt_cxy_pair, BlobData> fill_blobs(
        const std::map<tt_cxy_pair, EpochAllocator>& epoch_allocators,
        EpochBlobData& epoch_blob_data,
        const SoCHelper& soc_helper);

private:
    // Fill a single valid blob.
    static void fill_valid_blob(
        const tt_cxy_pair& core_id,
        BlobData& blob,
        const SoCHelper& soc_helper,
        const EpochAllocator& epoch_allocator,
        CoreBlobData& core_blob_data);

    // Fill all the epoch and epoch related structures to the blobs (vectors of bytes).
    static void fill_epoch_section(
        const EpochAllocator& epoch_allocator,
        BlobSection& epoch_section,
        const tt_cxy_pair& core_id,
        const CoreBlobData& core_blob_data,
        const SoCHelper& soc_helper);

    // Fill the infos related to register configuration per phase into the blob (vectors of bytes).
    static void fill_phase_blob_section(BlobSection& blob_space_section, CoreBlobData& core_blob_data);

    // Fill blobs for invalid cores which are not present in the StreamGraphCollection.
    static void fill_empty_blobs(
        std::map<tt_cxy_pair, BlobData>& blobs, const std::set<tt_cxy_pair>& valid_cores, const SoCHelper& soc_helper);

    // Fill a single invalid blob.
    static void fill_empty_blob(
        std::map<tt_cxy_pair, BlobData>& blobs, const tt_cxy_pair& core_id, const SoCHelper& soc_helper);
};
}  // namespace blobgen2