// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/shared_packer_intermed_streams_creator.h"

#include "model/rational_graph/pipes/join/shared_packer_intermed_pipe.h"

#include "pipegen2_utils.h"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamNode>> SharedPackerIntermedStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        const SharedPackerIntermedPipe* intermed_pipe = dynamic_cast<const SharedPackerIntermedPipe*>(pipe);
        log_assert(
            intermed_pipe != nullptr, "Expecting SharedPackerIntermedPipe in SharedPackerIntermedStreamsCreator");

        const VirtualNode* virtual_input_node = dynamic_cast<const VirtualNode*>(
            intermed_pipe->get_shared_packer_virtual_node().get_node());
        const SharedPackerIntermedNode* intermed_input_node = dynamic_cast<const SharedPackerIntermedNode*>(
            intermed_pipe->get_inputs()[0].get_node());
        const SharedPackerIntermedNode* intermed_output_node = dynamic_cast<const SharedPackerIntermedNode*>(
            intermed_pipe->get_output_nodes()[0]);
        log_assert(virtual_input_node != nullptr, "Expecting VirtualNode at SharedPackerIntermedPipe input");
        log_assert(
            intermed_input_node != nullptr, "Expecting SharedPackerIntermedNode at SharedPackerIntermedPipe input");
        log_assert(
            intermed_output_node != nullptr, "Expecting SharedPackerIntermedNode at SharedPackerIntermedPipe output");

        // Repurpose inherited packer stream as intermed stream.
        const StreamPhasesCommonConfig& common_config = get_stream_node_from_virtual_node(virtual_input_node);
        StreamNode* packer_stream = common_config.get_stream_node();

        m_stream_creator->convert_packer_to_intermed_stream(packer_stream, intermed_output_node);
        m_stream_creator->configure_default_intermed_stream_properties(packer_stream);
        m_stream_creator->configure_intermed_stream_properties_from_node(packer_stream, intermed_output_node);
        packer_stream->update_phases(data_flow_info.get_edge_phases(intermed_pipe, intermed_output_node),
                                     data_flow_info.get_max_num_tiles_per_phase(intermed_output_node));

        return {};
    }
}