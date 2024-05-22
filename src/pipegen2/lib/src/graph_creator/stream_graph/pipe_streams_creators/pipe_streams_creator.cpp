// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/pipe_streams_creator.h"

namespace pipegen2
{
PipeStreamsCreator::~PipeStreamsCreator() = default;

PipeStreamsCreator::PipeStreamsCreator(
    std::unique_ptr<NcriscCreator> ncrisc_creator,
    std::unique_ptr<StreamCreator> stream_creator,
    ResourceManager* resource_manager,
    std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
    m_ncrisc_creator(std::move(ncrisc_creator)),
    m_stream_creator(std::move(stream_creator)),
    m_resource_manager(resource_manager),
    m_virt_node_to_stream_node(virt_node_to_stream_node)
{
}

std::vector<std::unique_ptr<StreamNode>> PipeStreamsCreator::create_streams(
    const RGBasePipe* pipe, const DataFlowInfo& data_flow_info)
{
    // Step 1: Create streams.
    std::vector<std::unique_ptr<StreamNode>> created_streams = create_streams_internal(pipe, data_flow_info);

    // Step 2: Pass created streams to virtual nodes.
    assign_streams_to_pipe_outputs(pipe, created_streams);

    // Step 3: Assign common config to pipe outputs.
    assign_common_config_to_pipe_outputs(pipe);

    return created_streams;
}

void PipeStreamsCreator::assign_streams_to_pipe_outputs(
    const RGBasePipe* pipe, const std::vector<std::unique_ptr<StreamNode>>& created_streams)
{
    if (created_streams.empty())
    {
        // No streams created for this pipe.
        // TODO: in future refactor PSC to pass input streams forward if output node is virtual even when no streams
        // are created.
        return;
    }

    for (std::size_t output_index = 0; output_index < pipe->get_output_nodes().size(); ++output_index)
    {
        const RGBaseNode* output_node = pipe->get_output_nodes()[output_index];
        const VirtualNode* virtual_node = dynamic_cast<const VirtualNode*>(output_node);
        if (virtual_node)
        {
            assign_stream_node_to_virtual_node(
                virtual_node, get_stream_node_from_created_streams(created_streams, pipe, output_index));
        }
    }
}

StreamNode* PipeStreamsCreator::get_stream_node_from_created_streams(
    const std::vector<std::unique_ptr<StreamNode>>& created_streams,
    const RGBasePipe* pipe,
    const std::size_t output_index)
{
    // Here we are making 2 assumptions:
    // 1. Created streams order corresponds to the order of pipe outputs.
    // 2. Created streams that are outputs of the pipe are at the end of the created streams vector. PSCs that do
    //    not follow this assumption do not assign streams to pipe outputs.
    // TODO: make this explicit in the code with further refactoring.

    log_assert(created_streams.size() >= pipe->get_output_nodes().size(), "Not enough created streams");
    std::size_t stream_index = created_streams.size() - pipe->get_output_nodes().size() + output_index;

    log_assert(stream_index < created_streams.size(), "Index out of range");
    return created_streams[stream_index].get();
}

void PipeStreamsCreator::assign_stream_node_to_virtual_node(const VirtualNode* virt_node, StreamNode* stream_node)
{
    auto emplace_result = m_virt_node_to_stream_node->emplace(virt_node, StreamPhasesCommonConfig(stream_node));
    log_assert(emplace_result.second, "Virtual node already has a stream node");
}

void PipeStreamsCreator::assign_common_config_to_virtual_node_stream(
    const VirtualNode* virt_node, StreamConfig&& stream_node_static_config)
{
    auto it = m_virt_node_to_stream_node->find(virt_node);
    log_assert(it != m_virt_node_to_stream_node->end(), "Virtual node does not have a stream node assigned");
    it->second.set_common_config(std::move(stream_node_static_config));
}

const StreamPhasesCommonConfig& PipeStreamsCreator::get_stream_node_from_virtual_node(const VirtualNode* virt_node)
{
    auto it = m_virt_node_to_stream_node->find(virt_node);
    log_assert(it != m_virt_node_to_stream_node->end(), "Virtual node does not have a stream node assigned");
    return it->second;
}

StreamNode* PipeStreamsCreator::get_input_stream_node(const RGBasePipe* pipe)
{
    log_assert(pipe->get_inputs().size() == 1, "Expecting single input to the pipe");
    const RGBaseNode* input_node = pipe->get_inputs()[0].get_node();

    return get_input_stream_node(input_node);
}

StreamNode* PipeStreamsCreator::get_input_stream_node(const RGBaseNode* input_node)
{
    // Input to the pipe must be virtual node which is an output of another pipe.
    const VirtualNode* virtual_node = dynamic_cast<const VirtualNode*>(input_node);
    log_assert(virtual_node != nullptr, "Expecting virtual node at the pipe input");

    const StreamPhasesCommonConfig& stream_phases_common_config = get_stream_node_from_virtual_node(virtual_node);

    return stream_phases_common_config.get_stream_node();
}

const VirtualNode* PipeStreamsCreator::get_output_virtual_node(const RGBasePipe* pipe)
{
    const RGBaseNode* output_node = pipe->get_output_nodes()[0];
    const VirtualNode* virt_node = dynamic_cast<const VirtualNode*>(output_node);
    log_assert(virt_node != nullptr, "Expecting pipe output node to be encapsulated in a virtual node");

    return virt_node;
}
}  // namespace pipegen2