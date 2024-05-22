// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/padding_serial_fork_streams_creator.h"

#include "model/rational_graph/nodes/serial_fork_node.h"
#include "model/rational_graph/pipes/fork/padding_serial_fork_pipe.h"
#include "model/rational_graph/pipes/fork/serial_fork_pipe.h"

namespace pipegen2
{
std::vector<std::unique_ptr<StreamNode>> PaddingSerialForkStreamsCreator::create_streams_internal(
    const RGBasePipe* pipe, const DataFlowInfo& data_flow_info)
{
    std::vector<std::unique_ptr<StreamNode>> created_streams;

    const PaddingSerialForkPipe* serial_fork_pipe = dynamic_cast<const PaddingSerialForkPipe*>(pipe);
    log_assert(serial_fork_pipe, "Expecting serial fork pipe to be used in PaddingSerialForkStreamsCreator");

    create_padding_stream_per_output_core(serial_fork_pipe, created_streams);

    return created_streams;
}

void PaddingSerialForkStreamsCreator::assign_common_config_to_pipe_outputs(const RGBasePipe* pipe)
{
    for (const RGBaseNode* output_node : pipe->get_output_nodes())
    {
        const SerialForkNode* out_serial_fork_node = dynamic_cast<const SerialForkNode*>(output_node);
        log_assert(out_serial_fork_node != nullptr, "Expecting serial fork node at the PaddingSerialFork pipe output");

        const int scatter_index = out_serial_fork_node->get_scatter_index();

        StreamConfig common_phase_config;
        common_phase_config.set_scatter_idx(scatter_index);
        assign_common_config_to_virtual_node_stream(out_serial_fork_node, std::move(common_phase_config));
    }
}

StreamNode* PaddingSerialForkStreamsCreator::get_stream_node_from_created_streams(
    const std::vector<std::unique_ptr<StreamNode>>& created_streams,
    const RGBasePipe* pipe,
    const std::size_t output_index)
{
    log_assert(output_index < pipe->get_output_nodes().size(), "Index out of range");
    const RGBaseNode* output_node = pipe->get_output_nodes()[output_index];

    auto stream_it = std::find_if(
        created_streams.begin(),
        created_streams.end(),
        [output_node](const std::unique_ptr<StreamNode>& stream_node)
        { return stream_node->get_physical_location() == output_node->get_physical_location(); });

    log_assert(stream_it != created_streams.end(), "Did not find padding stream at the location of output node");

    return stream_it->get();
}

std::unordered_map<tt_cxy_pair, StreamNode*> PaddingSerialForkStreamsCreator::create_padding_stream_per_output_core(
    const PaddingSerialForkPipe* pipe, std::vector<std::unique_ptr<StreamNode>>& created_streams)
{
    std::unordered_map<tt_cxy_pair, StreamNode*> core_location_to_padding_stream;

    const std::vector<const RGBaseNode*> pipe_outputs = pipe->get_output_nodes();
    log_assert(!pipe_outputs.empty(), "Expecting at least one node at the PaddingSerialForkPipe output");

    StreamNode* base_padding_stream = get_input_stream_node(pipe);
    log_assert(base_padding_stream, "Base padding stream not found");

    for (const RGBaseNode* output_node : pipe_outputs)
    {
        const tt_cxy_pair& core_physical_location = output_node->get_physical_location();

        if (core_location_to_padding_stream.find(core_physical_location) != core_location_to_padding_stream.end())
        {
            continue;
        }

        created_streams.push_back(create_padding_stream_for_core(pipe, base_padding_stream, core_physical_location));
        core_location_to_padding_stream[core_physical_location] = created_streams.back().get();
    }

    // Base padding stream is only used as a "template" to create a padding stream for every output location.
    // After those streams are created, this stream is unnecessary so it deleted as it won't be connected to any
    // other stream.
    base_padding_stream->soft_delete();

    return core_location_to_padding_stream;
}

std::unique_ptr<StreamNode> PaddingSerialForkStreamsCreator::create_padding_stream_for_core(
    const PaddingSerialForkPipe* pipe, const StreamNode* base_padding_stream, const tt_cxy_pair& physical_location)
{
    std::unique_ptr<StreamNode> core_padding_stream =
        std::make_unique<StreamNode>(StreamType::Relay, physical_location);

    StreamConfig core_padding_stream_config = base_padding_stream->get_base_config();
    core_padding_stream_config.set_fork_streams({core_padding_stream.get()});
    core_padding_stream->set_base_config(std::move(core_padding_stream_config));

    std::vector<NcriscConfig> ncrisc_configs = base_padding_stream->get_ncrisc_configs();
    core_padding_stream->set_ncrisc_configs(std::move(ncrisc_configs));

    m_stream_creator->configure_scatter_order_sizes(pipe, core_padding_stream.get());

    return core_padding_stream;
}
}  // namespace pipegen2