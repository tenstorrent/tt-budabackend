// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/non_shared_intermed_streams_creator.h"

#include "model/rational_graph/pipes/direct/non_shared_intermed_pipe.h"

#include "pipegen2_utils.h"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamNode>> NonSharedIntermedStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        const NonSharedIntermedPipe* intermed_pipe = dynamic_cast<const NonSharedIntermedPipe*>(pipe);
        log_assert(intermed_pipe != nullptr, "Expecting NonSharedIntermedPipe in NonSharedIntermedStreamsCreator");

        const NonSharedIntermedNode* intermed_input_node = dynamic_cast<const NonSharedIntermedNode*>(
            pipe->get_inputs()[0].get_node());
        const NonSharedIntermedNode* intermed_output_node = dynamic_cast<const NonSharedIntermedNode*>(
            pipe->get_output_nodes()[0]);
        log_assert(intermed_input_node != nullptr, "Expecting NonSharedIntermedNode at NonSharedIntermedPipe input");
        log_assert(intermed_output_node != nullptr, "Expecting NonSharedIntermedNode at NonSharedIntermedPipe input");

        // New stream is created based solely on info from intermed_output_node.
        std::unique_ptr<StreamNode> intermed_stream = std::make_unique<StreamNode>(
            StreamType::Intermed,
            intermed_output_node->get_physical_location(),
            intermed_output_node->get_operand_id());

        m_stream_creator->configure_default_intermed_stream_properties(intermed_stream.get());
        m_stream_creator->configure_intermed_stream_properties_from_node(intermed_stream.get(), intermed_output_node);
        intermed_stream->update_phases(data_flow_info.get_edge_phases(intermed_pipe, intermed_output_node),
                                       data_flow_info.get_max_num_tiles_per_phase(intermed_output_node));

        std::vector<std::unique_ptr<StreamNode>> created_streams;
        created_streams.push_back(std::move(intermed_stream));
        return created_streams;
    }
}