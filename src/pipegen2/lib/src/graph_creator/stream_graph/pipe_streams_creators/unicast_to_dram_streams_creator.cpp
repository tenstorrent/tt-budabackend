// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/unicast_to_dram_streams_creator.h"

// clang-format off
#include "utils/logger.hpp"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_write_common_streams_creator.h"
// clang-format on

namespace pipegen2
{
UnicastToDramStreamsCreator::UnicastToDramStreamsCreator(
    std::unique_ptr<NcriscCreator> ncrisc_creator,
    std::unique_ptr<StreamCreator> stream_creator,
    ResourceManager* resource_manager,
    std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
    PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager, virt_node_to_stream_node)
{
    m_dram_write_common_streams_creator =
        std::make_unique<DramWriteCommonStreamsCreator>(m_stream_creator.get(), m_ncrisc_creator.get());
}

UnicastToDramStreamsCreator::~UnicastToDramStreamsCreator() = default;

std::vector<std::unique_ptr<StreamNode>> UnicastToDramStreamsCreator::create_streams_internal(
    const RGBasePipe* pipe, const DataFlowInfo& data_flow_info)
{
    const VirtualNode* virt_node = dynamic_cast<const VirtualNode*>(pipe->get_inputs()[0].get_node());
    log_assert(virt_node, "Expecting virtual node at the UnicastToDram pipe input");

    const StreamPhasesCommonConfig& stream_phases_common_config = get_stream_node_from_virtual_node(virt_node);

    m_dram_write_common_streams_creator->setup_packer_stream_for_dram_write(
        stream_phases_common_config, 0 /* input_index */, pipe, data_flow_info, m_resource_manager->get_soc_info());

    return {};
}
}  // namespace pipegen2