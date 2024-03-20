// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/unicast_to_pcie_streams_creator.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_write_common_streams_creator.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    UnicastToPCIeStreamsCreator::UnicastToPCIeStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                           virt_node_to_stream_node)
    {
        m_dram_write_common_streams_creator =
            std::make_unique<DramWriteCommonStreamsCreator>(m_stream_creator.get(), m_ncrisc_creator.get());
    }

    UnicastToPCIeStreamsCreator::~UnicastToPCIeStreamsCreator() = default;

    std::vector<std::unique_ptr<StreamNode>> UnicastToPCIeStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        const RGBaseNode* input_node = pipe->get_inputs()[0].get_node();

        const VirtualNode* virt_node = dynamic_cast<const VirtualNode*>(input_node);
        log_assert(virt_node, "Expecting virtual node at the UnicastToPCIe pipe input");

        const StreamPhasesCommonConfig& stream_phases_common_config = get_stream_node_from_virtual_node(virt_node);

        // PCIe communication is handled by NCRISC, which is why we need to setup streams with NCRISC config
        // similar like we do for DRAM write.
        m_dram_write_common_streams_creator->setup_packer_stream_for_pcie_write(
            stream_phases_common_config, input_node, pipe, data_flow_info);

        return {};
    }
}