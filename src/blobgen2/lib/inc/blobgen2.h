// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <memory>

#include "model/typedefs.h"
#include "overlay_blob/typedef.h"

namespace pipegen2
{
struct L1BufferAllocationInfo;
class StreamGraphCollection;
}  // namespace pipegen2

namespace blobgen2
{

class BlobData;
class SoCHelper;

using pipegen2::L1BufferAllocationInfo;
using pipegen2::PhaseId;
using pipegen2::StreamGraphCollection;

// Class used to generate overlay binaries from the provided StreamGraphCollection.
// For more information on how the outputted overlay blobs look like, see docs/overlay.md
class Blobgen2
{
public:
    // Generates the overlay blobs from the provided StreamGraphCollection.
    // If perf info is non-zero, PerfManager is used to generated values which will be written to epoch perf arrays.
    // If debug is set to true, the blobs will be outputted with a corresponding text describing firmware structure
    // fields and registers that the values in the blob go to. This will create non functional blobs, but is highly
    // useful for debugging.
    static void create_and_output_blobs(
        std::unique_ptr<StreamGraphCollection> stream_graphs,
        const std::string& soc_descriptor_yaml_path,
        const uint32_t dram_perf_info_arguments,
        const int epoch_num,
        const std::string& output_dir,
        const bool dump_debug_info = false);

    // A variant of previous function, used when dram_perf_info is directly read from blob.yaml.
    static void create_and_output_blobs(
        std::unique_ptr<StreamGraphCollection> stream_graphs,
        const std::string& soc_descriptor_yaml_path,
        const std::map<tt_cxy_pair, dram_perf_info_t>& dram_perf_info,
        const int epoch_num,
        const std::string& output_dir,
        const bool dump_debug_info = false);

private:
    // Main function, called from both public entry points. Glues other overlay_generation components together.
    static void create_and_output_blobs_internal(
        std::unique_ptr<StreamGraphCollection> stream_graphs,
        const SoCHelper& soc_helper,
        const std::map<tt_cxy_pair, dram_perf_info_t>& dram_perf_info,
        const int epoch_num,
        const std::string& output_dir,
        const bool dump_debug_info);

    // Constructs a PerfManager and extracts vectors of dram_perf_info_t from it, needed by firmware.
    static std::map<tt_cxy_pair, dram_perf_info_t> get_perf_info_from_manager(
        const uint32_t perf_info, const SoCHelper& soc_helper);

    // Writes blobs to the output directory.
    // Blobs are written as hex files, one for each core on each chip for this epoch.
    // The hex files contain two sections, each of them starting with the L1 address that
    // this memory should be copied to, and the subsequent array of bytes as ASCI hexadecimal chars.
    // The array of byte has 4 bytes (8 hex chars) in each line.
    static void output_blobs(
        const std::map<tt_cxy_pair, BlobData>& blobs,
        const std::string& output_dir,
        const int epoch_num,
        const bool dump_debug_info);
};

}  // namespace blobgen2
