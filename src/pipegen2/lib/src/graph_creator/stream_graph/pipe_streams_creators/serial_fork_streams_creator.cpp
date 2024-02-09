// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/serial_fork_streams_creator.h"

#include "model/rational_graph/nodes/serial_fork_node.h"
#include "model/rational_graph/pipes/fork/serial_fork_pipe.h"

namespace pipegen2
{
    std::vector<std::unique_ptr<StreamNode>> SerialForkStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        const SerialForkPipe* serial_fork_pipe = dynamic_cast<const SerialForkPipe*>(pipe);
        log_assert(serial_fork_pipe, "Expecting serial fork pipe to be used in SerialForkStreamsCreator");

        StreamNode* stream_node = get_input_stream_node(pipe);
        log_assert(stream_node, "Input stream node for the SerialFork pipe not found");

        const std::vector<const RGBaseNode*>& pipe_outputs = pipe->get_output_nodes();
        m_stream_creator->configure_scatter_order_sizes(serial_fork_pipe, stream_node);

        return {};
    }

    void SerialForkStreamsCreator::assign_streams_to_pipe_outputs(
        const RGBasePipe* pipe,
        const std::vector<std::unique_ptr<StreamNode>>& created_streams)
    {
        StreamNode* stream_node = get_input_stream_node(pipe);
        log_assert(stream_node, "Input stream node for the SerialFork pipe not found");

        for (const RGBaseNode* output_node : pipe->get_output_nodes())
        {
            const SerialForkNode* out_serial_fork_node = dynamic_cast<const SerialForkNode*>(output_node);
            log_assert(out_serial_fork_node != nullptr, "Expecting serial fork node at the SerialFork pipe output");
            assign_stream_node_to_virtual_node(out_serial_fork_node, stream_node);
        }
    }

    void SerialForkStreamsCreator::assign_common_config_to_pipe_outputs(const RGBasePipe* pipe)
    {
        for (const RGBaseNode* output_node : pipe->get_output_nodes())
        {
            const SerialForkNode* out_serial_fork_node = dynamic_cast<const SerialForkNode*>(output_node);
            log_assert(out_serial_fork_node != nullptr, "Expecting serial fork node at the SerialFork pipe output");

            const int scatter_index = out_serial_fork_node->get_scatter_index();

            StreamConfig common_phase_config;
            common_phase_config.set_scatter_idx(scatter_index);
            assign_common_config_to_virtual_node_stream(out_serial_fork_node, std::move(common_phase_config));
        }
    }
}