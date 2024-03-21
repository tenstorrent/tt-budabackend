// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_parallel_fork_streams_creator.h"

#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/nodes/virtual_node.h"
#include "utils/logger.hpp"

namespace pipegen2
{

    DramParallelForkStreamsCreator::DramParallelForkStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                           virt_node_to_stream_node)
    {
    }

    DramParallelForkStreamsCreator::~DramParallelForkStreamsCreator() = default;

    std::vector<std::unique_ptr<StreamNode>> DramParallelForkStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        return {};
    }
}