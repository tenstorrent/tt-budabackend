// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_prefetch_pre_tm_parallel_fork_streams_creator.h"

#include "model/rational_graph/nodes/dram_input_node.h"

namespace pipegen2
{
    std::unique_ptr<StreamNode> DramPrefetchPreTMParallelForkStreamsCreator::create_sending_stream(
        const RGBaseNode* input_node,
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        // Input to the DramPrefetchPreTMParallelFork pipe must be dram input node with prefetch pre TM type.
        const DramInputNode* prefetch_input_node = dynamic_cast<const DramInputNode*>(input_node);
        log_assert(prefetch_input_node && prefetch_input_node->is_dram_prefetch_pre_tm(),
                   "Expecting prefetch Pre-TM DRAM input node at the DramPrefetchPreTMParalleFork pipe input");

        std::unique_ptr<StreamNode> prefetch_relay_stream = std::make_unique<StreamNode>(
            StreamType::Relay,
            prefetch_input_node->get_physical_location());

        NcriscConfig ncrisc_config = m_ncrisc_creator->configure_prefetch_pre_tm_ncrisc_reader(
            pipe, m_resource_manager->get_soc_info(), prefetch_input_node);

        m_stream_creator->configure_prefetch_pre_tm_relay_stream(
            prefetch_relay_stream.get(), prefetch_input_node, pipe, {ncrisc_config});

        return prefetch_relay_stream;
    }
}