// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/parallel_fork_streams_creator.h"

#include "model/rational_graph/nodes/packer_input_node.h"

namespace pipegen2
{
    std::unique_ptr<StreamNode> ParallelForkStreamsCreator::create_sending_stream(
        const RGBaseNode* input_node,
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        // Input to the ParallelFork pipe must be packer input node.
        const PackerInputNode* packer_node = dynamic_cast<const PackerInputNode*>(input_node);
        ASSERT(packer_node, "Expecting packer input node at the ParallelFork pipe input");

        std::unique_ptr<StreamNode> packer_stream = std::make_unique<StreamNode>(
            StreamType::Packer,
            packer_node->get_physical_location(),
            packer_node->get_operand_id());

        m_stream_creator->configure_packer_to_noc_stream(packer_stream.get(), pipe, packer_node);

        return packer_stream;
    }
}