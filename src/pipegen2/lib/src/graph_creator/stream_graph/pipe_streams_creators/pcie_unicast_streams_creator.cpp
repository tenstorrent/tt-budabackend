// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/pcie_unicast_streams_creator.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_read_common_streams_creator.h"
#include "model/stream_graph/ncrisc_config.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    PCIeUnicastStreamsCreator::PCIeUnicastStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                           virt_node_to_stream_node)
    {
        m_dram_read_common_streams_creator = std::make_unique<DramReadCommonStreamsCreator>(m_stream_creator.get());
    }

    PCIeUnicastStreamsCreator::~PCIeUnicastStreamsCreator() = default;

    std::vector<std::unique_ptr<StreamNode>> PCIeUnicastStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        NcriscConfig ncrisc_config =
            m_ncrisc_creator->configure_pcie_ncrisc_reader(data_flow_info, pipe, m_resource_manager->get_soc_info());

        // PCIe communication is handled by NCRISC, which is why we need to setup streams with NCRISC config
        // similar like we do for DRAM read.
        return m_dram_read_common_streams_creator->create_pcie_to_unpacker_streams(
            pipe, data_flow_info, std::move(ncrisc_config));
    }
}