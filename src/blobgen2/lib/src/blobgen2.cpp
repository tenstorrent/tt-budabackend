// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "blobgen2.h"

#include "device/perf_info_manager.h"
#include "helpers/soc_helper.h"
#include "helpers/stream_graph_utils.h"
#include "model/stream_graph/stream_config.h"
#include "model/stream_graph/stream_graph_collection.h"
#include "overlay_blob/stream_info.h"
#include "overlay_generation/blob_filler.h"
#include "overlay_generation/epoch_allocator.h"
#include "overlay_generation/epoch_t_builder.h"
#include "overlay_generation/stream_info_builder.h"

namespace blobgen2
{

using pipegen2::PerfInfoManager;

void Blobgen2::create_and_output_blobs(
    std::unique_ptr<StreamGraphCollection> stream_graphs,
    const std::string& soc_descriptor_yaml_path,
    const uint32_t dram_perf_info_arguments,
    const int epoch_num,
    const std::string& output_dir,
    const bool dump_debug_info)
{
    SoCHelper soc_helper(soc_descriptor_yaml_path, StreamGraphUtils::get_chip_ids(*stream_graphs));

    create_and_output_blobs_internal(
        std::move(stream_graphs),
        soc_helper,
        get_perf_info_from_manager(dram_perf_info_arguments, soc_helper),
        epoch_num,
        output_dir,
        dump_debug_info);
}

void Blobgen2::create_and_output_blobs(
    std::unique_ptr<StreamGraphCollection> stream_graphs,
    const std::string& soc_descriptor_yaml_path,
    const std::map<tt_cxy_pair, dram_perf_info_t>& dram_perf_info,
    const int epoch_num,
    const std::string& output_dir,
    const bool dump_debug_info)
{
    SoCHelper soc_helper(soc_descriptor_yaml_path, StreamGraphUtils::get_chip_ids(*stream_graphs));

    create_and_output_blobs_internal(
        std::move(stream_graphs), soc_helper, dram_perf_info, epoch_num, output_dir, dump_debug_info);
}

void Blobgen2::create_and_output_blobs_internal(
    std::unique_ptr<StreamGraphCollection> stream_graphs,
    const SoCHelper& soc_helper,
    const std::map<tt_cxy_pair, dram_perf_info_t>& dram_perf_info,
    const int epoch_num,
    const std::string& output_dir,
    const bool dump_debug_info)
{
    EpochBlobData epoch_blob_data = StreamGraphUtils::get_epoch_blob_data(*stream_graphs, epoch_num);

    std::map<size_t, std::map<uint32_t, uint32_t>> tile_size_and_address_per_chip =
        StreamGraphUtils::get_tile_size_and_address_per_chip(*stream_graphs, soc_helper);

    const std::map<tt_cxy_pair, L1BufferAllocationInfo>& ncrisc_fallback_buffers =
        stream_graphs->get_ncrisc_fallback_buffers_allocations_per_core();

    StreamInfoBuilder::fill_stream_infos(epoch_blob_data, soc_helper);

    std::map<tt_cxy_pair, EpochAllocator> epoch_allocators = EpochTBuilder::fill_epoch_structs(
        epoch_blob_data,
        dram_perf_info,
        ncrisc_fallback_buffers,
        tile_size_and_address_per_chip,
        epoch_num,
        soc_helper);

    std::map<tt_cxy_pair, BlobData> blobs =
        BlobFiller::fill_blobs(epoch_allocators, epoch_blob_data, soc_helper, epoch_num);

    output_blobs(blobs, output_dir, epoch_num, dump_debug_info);
}

std::map<tt_cxy_pair, dram_perf_info_t> Blobgen2::get_perf_info_from_manager(
    const uint32_t perf_info, const SoCHelper& soc_helper)
{
    // TODO: Move out PerfInfoManager into a separate component.
    PerfInfoManager perf_info_manager(perf_info, soc_helper.get_soc_info());
    perf_info_manager.calculate_dram_perf_info();

    std::map<tt_cxy_pair, dram_perf_info_t> dram_perf_info;

    if (perf_info_manager.is_perf_dump_enabled())
    {
        std::map<tt_cxy_pair, std::vector<uint64_t>> workers_to_noc_addr_info =
            perf_info_manager.get_dram_perf_buf_noc_addr_info();
        std::map<tt_cxy_pair, std::vector<uint64_t>> workers_to_max_req_info =
            perf_info_manager.get_dram_perf_buf_max_req_info();

        for (auto& [core_id, noc_addr_info] : workers_to_noc_addr_info)
        {
            log_assert(
                noc_addr_info.size() == l1_mem::address_map::PERF_NUM_THREADS,
                "Expected to get exactly PERF_NUM_THREADS values from get_dram_perf_buf_noc_addr_info.");
            std::array<uint64_t, l1_mem::address_map::PERF_NUM_THREADS> noc_addr_info_arr;
            std::copy(noc_addr_info.begin(), noc_addr_info.end(), noc_addr_info_arr.begin());

            std::vector<uint64_t>& max_req_info = workers_to_max_req_info.at(core_id);
            log_assert(
                max_req_info.size() == l1_mem::address_map::PERF_NUM_THREADS,
                "Expected to get exactly PERF_NUM_THREADS values from get_dram_perf_buf_max_req_info.");
            std::array<uint16_t, l1_mem::address_map::PERF_NUM_THREADS> max_req_info_arr;
            std::copy(max_req_info.begin(), max_req_info.end(), max_req_info_arr.begin());

            dram_perf_info.emplace(core_id, dram_perf_info_t{noc_addr_info_arr, max_req_info_arr});
        }
    }

    return dram_perf_info;
}

void Blobgen2::output_blobs(
    const std::map<tt_cxy_pair, BlobData>& blobs,
    const std::string& output_dir,
    const int epoch_num,
    const bool dump_debug_info)
{
    for (const auto& blob_it : blobs)
    {
        auto location = blob_it.first;
        auto& blob = blob_it.second;

        blob.print_out(output_dir, location, epoch_num, dump_debug_info);
    }
}

}  // namespace blobgen2