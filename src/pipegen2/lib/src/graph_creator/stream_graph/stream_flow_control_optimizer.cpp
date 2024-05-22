// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/stream_flow_control_optimizer.h"

#include "graph_creator/stream_graph/stream_graph_creator.h"

namespace pipegen2
{
void StreamFlowControlOptimizer::optimize_stream_graph_flow_control(StreamGraph* stream_graph)
{
    for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
    {
        // We enable optimization on source->dest pairs, so skip streams that don't have a dest.
        if (!stream_node->get_phase_config(0).get_dest().has_value())
        {
            continue;
        }

        optimize_stream_flow_control(stream_node.get());
    }
}

void StreamFlowControlOptimizer::optimize_stream_flow_control(StreamNode* stream_node)
{
    // Message size in bytes is same for all phases.
    const unsigned int msg_size_bytes = stream_node->get_base_config().get_msg_size().value();

    // This is the default value of the flag, it will be updated in the loop. It's important to start from this
    // value since the flag might not be defined for each phase config.
    bool next_phase_dest_change = true;
    unsigned int sum_num_msgs = 0;
    std::size_t start_accumulating_from = 0;

    // Algorithm is as follows:
    // - Iterate over all phases of the stream.
    // - Accumulate the number of messages in the current phase. We are interested in HW phase, in other words
    //   phases that are consecutive and have next_phase_dest_change set to false are considered as one HW
    //   phase.
    // - If the accumulated number of messages is less than the size of the destination buffer, set the flag
    //   dest_data_buf_no_flow_ctrl to true on source.
    // - If the accumulated number of messages is greater than the size of the destination buffer, set the flag
    //   dest_data_buf_no_flow_ctrl to false on source.
    // - Identify phases on the destination stream node that correspond to the HW phase on the source stream and
    //   set data_buf_no_flow_ctrl accordingly.
    for (std::size_t i = 0; i < stream_node->get_num_phases(); ++i)
    {
        StreamConfig& cur_phase_config = stream_node->get_phase_config(i);
        sum_num_msgs += cur_phase_config.get_num_msgs().value();

        // Whether we're crossing to a new stream HW phase. It is guaranteed that last phase will have
        // next_phase_dest_change set to true, so we don't have to worry about it after exiting the loop.
        next_phase_dest_change = cur_phase_config.get_next_phase_dest_change().value_or(next_phase_dest_change);
        if (!next_phase_dest_change)
        {
            continue;
        }

        StreamNode* cur_dest = cur_phase_config.get_dest().value().front();
        // In case of multiple dests in single phase, we assume that all dests have the same buffer size.
        unsigned int cur_dest_buffer_size_bytes = cur_dest->get_base_config().get_buffer_size().value();

        // Before we cross to a new phase, we want to set no_flow_control flag for the previous range of phases.
        // Check if we can fit all messages in the destination buffer.
        const bool no_flow_control = sum_num_msgs * msg_size_bytes <= cur_dest_buffer_size_bytes;
        set_no_flow_control(stream_node, start_accumulating_from, no_flow_control);

        start_accumulating_from = i + 1;
        sum_num_msgs = 0;
    }
}

void StreamFlowControlOptimizer::set_no_flow_control(
    StreamNode* source_stream, const std::size_t start_phase_idx, const bool no_flow_control)
{
    PhaseConfig& source_phase_config = source_stream->get_phase_configs()[start_phase_idx];
    source_phase_config.config.set_dest_data_buf_no_flow_ctrl(no_flow_control);

    for (StreamNode* dest : source_phase_config.config.get_dest().value())
    {
        // Find the phase config in the dest stream node that corresponds to the start_phase_id. Set
        // data_buf_no_flow_ctrl to given value for all phase configs up to the phase where next_phase_dest_change is
        // set to true.
        auto dest_phase_iter =
            StreamGraphCreator::find_phase_config_by_phase_id(dest->get_phase_configs(), source_phase_config.phase_id);
        dest_phase_iter->config.set_data_buf_no_flow_ctrl(no_flow_control);
    }
}
}  // namespace pipegen2