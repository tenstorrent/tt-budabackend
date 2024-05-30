// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "helpers/stream_graph_utils.h"

#include "helpers/soc_helper.h"
#include "model/stream_graph/stream_graph_collection.h"
#include "model/stream_graph/stream_node.h"

namespace blobgen2
{

using pipegen2::StreamNode;

std::vector<size_t> StreamGraphUtils::get_chip_ids(const StreamGraphCollection& stream_graphs)
{
    std::set<size_t> chip_ids;

    for (const auto& stream_graph : stream_graphs.get_stream_graphs())
    {
        for (const std::unique_ptr<StreamNode>& stream : stream_graph->get_streams())
        {
            chip_ids.insert(stream->get_physical_location().chip);
        }
    }

    return std::vector<size_t>(chip_ids.begin(), chip_ids.end());
}

EpochBlobData StreamGraphUtils::get_epoch_blob_data(const StreamGraphCollection& stream_graphs, int epoch_num)
{
    EpochBlobData epoch_blob_data;

    for (const auto& stream_graph : stream_graphs.get_stream_graphs())
    {
        for (const std::unique_ptr<StreamNode>& stream : stream_graph->get_streams())
        {
            if (epoch_blob_data.cores.find(stream->get_physical_location()) == epoch_blob_data.cores.end())
            {
                epoch_blob_data.cores.emplace(stream->get_physical_location(), CoreBlobData{});
            }
            auto& core_blob_data = epoch_blob_data.cores.at(stream->get_physical_location());

            if (core_blob_data.streams.find(stream->get_stream_id()) == core_blob_data.streams.end())
            {
                core_blob_data.streams.emplace(
                    stream->get_stream_id(), StreamBlobData{.stream_id = stream->get_stream_id()});
            }
            auto& stream_blob_data = core_blob_data.streams.at(stream->get_stream_id());

            for (PhaseConfig& phase : stream->get_phase_configs())
            {
                stream_blob_data.phases.emplace(phase.phase_id, &phase);
            }

            stream_blob_data.ncriscs = stream->get_ncrisc_configs();
        }
    }

    return epoch_blob_data;
}

std::map<size_t, std::map<uint32_t, uint32_t>> StreamGraphUtils::get_tile_size_and_address_per_chip(
    const StreamGraphCollection& stream_graphs, const SoCHelper& soc_helper)
{
    std::map<size_t, std::map<uint32_t, uint32_t>> tile_size_and_address_per_chip;

    for (const auto& stream_graph : stream_graphs.get_stream_graphs())
    {
        for (const std::unique_ptr<StreamNode>& stream : stream_graph->get_streams())
        {
            size_t chip_id = stream->get_physical_location().chip;
            if (tile_size_and_address_per_chip.find(chip_id) == tile_size_and_address_per_chip.end())
            {
                tile_size_and_address_per_chip.emplace(chip_id, std::map<uint32_t, uint32_t>{});
            }
            tt_cxy_pair core_location = stream->get_physical_location();
            if (soc_helper.is_ethernet_core(core_location))
            {
                continue;
            }

            for (const PhaseConfig& phase : stream->get_phase_configs())
            {
                if (phase.config.get_msg_size().has_value())
                {
                    uint32_t tile_size = phase.config.get_msg_size().value();
                    uint32_t tile_header_addr = phase.config.get_msg_info_buf_addr().value();
                    tile_size_and_address_per_chip.at(chip_id).emplace(tile_size, tile_header_addr);
                }
            }
        }
    }

    return tile_size_and_address_per_chip;
}

}  // namespace blobgen2