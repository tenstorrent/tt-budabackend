// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/gather_to_dram_streams_creator.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_write_common_streams_creator.h"
#include "utils/logger.hpp"

namespace pipegen2
{
    GatherToDramStreamsCreator::GatherToDramStreamsCreator(
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

    GatherToDramStreamsCreator::~GatherToDramStreamsCreator() = default;

    std::vector<std::unique_ptr<StreamNode>> GatherToDramStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        const std::vector<PipeInput>& pipe_inputs = pipe->get_inputs();

        for (std::size_t input_index = 0; input_index < pipe_inputs.size(); ++input_index)
        {
            const VirtualNode* virt_node = dynamic_cast<const VirtualNode*>(pipe_inputs[input_index].get_node());
            log_assert(virt_node, "Expecting virtual node at the GatherToDram pipe input");

            const StreamPhasesCommonConfig& stream_phases_common_config = get_stream_node_from_virtual_node(virt_node);

            m_dram_write_common_streams_creator->setup_packer_stream_for_dram_write(
                stream_phases_common_config, input_index, pipe, data_flow_info, m_resource_manager->get_soc_info());
        }

        return {};
    }
}