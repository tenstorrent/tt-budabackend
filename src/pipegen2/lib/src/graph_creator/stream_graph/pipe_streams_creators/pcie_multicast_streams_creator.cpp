// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/pcie_multicast_streams_creator.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_read_common_streams_creator.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"

namespace pipegen2
{
    PCIeMulticastStreamsCreator::PCIeMulticastStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                           virt_node_to_stream_node)
    {
        m_dram_read_common_streams_creator = std::make_unique<DramReadCommonStreamsCreator>(m_stream_creator.get());
    }

    PCIeMulticastStreamsCreator::~PCIeMulticastStreamsCreator() = default;

    std::vector<std::unique_ptr<StreamNode>> PCIeMulticastStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        std::vector<std::unique_ptr<StreamNode>> created_streams;

        NcriscConfig ncrisc_config =
            m_ncrisc_creator->configure_pcie_ncrisc_reader(data_flow_info, pipe, m_resource_manager->get_soc_info());

        unsigned int max_tiles_per_phase;
        std::unique_ptr<StreamNode> multicast_stream =
            m_dram_read_common_streams_creator->create_pcie_multicast_stream(
                pipe, data_flow_info, std::move(ncrisc_config), max_tiles_per_phase);

        m_stream_creator->set_common_stream_properties_from_pipe(
            multicast_stream.get(), pipe, false /* incoming_stream */);

        std::vector<std::unique_ptr<StreamNode>> unpacker_streams =
            m_dram_read_common_streams_creator->create_multicast_unpacker_streams(
                multicast_stream.get(), pipe, pipe->get_inputs().front().get_node(), data_flow_info,
                max_tiles_per_phase);

        created_streams.push_back(std::move(multicast_stream));
        std::move(unpacker_streams.begin(), unpacker_streams.end(), std::back_inserter(created_streams));

        return created_streams;
    }
}