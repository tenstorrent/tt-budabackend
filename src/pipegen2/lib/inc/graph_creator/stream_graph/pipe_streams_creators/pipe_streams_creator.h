// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

// clang-format off
#include "utils/logger.hpp"

#include "device/resource_manager.h"
#include "graph_creator/stream_graph/ncrisc_creators/ncrisc_creator.h"
#include "graph_creator/stream_graph/stream_creators/stream_creator.h"
#include "model/data_flow/data_flow_info.h"
#include "model/rational_graph/nodes/virtual_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "model/stream_graph/stream_node.h"
// clang-format on

namespace pipegen2
{
class PipeStreamsCreator
{
public:
    // Virtual destructor, necessary for base class.
    virtual ~PipeStreamsCreator();

    // Creates streams from the rational graph pipe.
    std::vector<std::unique_ptr<StreamNode>> create_streams(const RGBasePipe* pipe, const DataFlowInfo& data_flow_info);

protected:
    // Constructor, available only from derived classes for specific architecture.
    PipeStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node);

    // Creates streams from the rational graph pipe.
    virtual std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
        const RGBasePipe* pipe, const DataFlowInfo& data_flow_info) = 0;

    // For each virtual node at the pipe output, assigns a stream node to it. If node is not virtual, does nothing
    // as only virtual nodes may have stream nodes assigned to them. May be overridden by child classes if they
    // have different ways of assigning stream nodes to pipe outputs.
    virtual void assign_streams_to_pipe_outputs(
        const RGBasePipe* pipe, const std::vector<std::unique_ptr<StreamNode>>& created_streams);

    // For each stream node assigned to a virtual node, assigns the common phase config to the stream node. Default
    // implementation does nothing.
    virtual void assign_common_config_to_pipe_outputs(const RGBasePipe* pipe) {}

    // Returns stream node out of the created streams for the given output index. Various child classes may have
    // different ways of assigning stream nodes to pipe outputs. Default implementation assumes that the created
    // streams are assigned to pipe outputs in the same order as the pipe outputs are stored in the pipe.
    virtual StreamNode* get_stream_node_from_created_streams(
        const std::vector<std::unique_ptr<StreamNode>>& created_streams,
        const RGBasePipe* pipe,
        const std::size_t output_index);

    // Returns the stream node and its phases common config assigned to the virtual node.
    const StreamPhasesCommonConfig& get_stream_node_from_virtual_node(const VirtualNode* virt_node);

    // Assigns the stream node to the virtual node.
    void assign_stream_node_to_virtual_node(const VirtualNode* virt_node, StreamNode* stream_node);

    // Assigns common config to the virtual node.
    void assign_common_config_to_virtual_node_stream(
        const VirtualNode* virt_node, StreamConfig&& stream_node_static_config);

    // Returns a stream node of a virtual input node of the given pipe.
    StreamNode* get_input_stream_node(const RGBasePipe* pipe);

    // Returns a stream node from a given virtual input node.
    StreamNode* get_input_stream_node(const RGBaseNode* input_node);

    // Returns the pipe's output node at the given output index in its original (pre-encapsulation) type.
    template <typename NodeType>
    static const NodeType* get_output_node(const RGBasePipe* pipe, const std::size_t output_index = 0)
    {
        log_assert(output_index < pipe->get_output_nodes().size(), "Index out of range");
        const RGBaseNode* output_node = pipe->get_output_nodes()[output_index];

        // Check if the node is wrapped in a virtual node. In such case fetch the original node.
        output_node = VirtualNode::get_non_virtual_node(output_node);

        const NodeType* node = dynamic_cast<const NodeType*>(output_node);
        log_assert(node != nullptr, "Expecting {} at the pipe output", typeid(NodeType).name());

        return node;
    }

    // Returns the output node of the pipe encapsulated in a virtual node.
    const VirtualNode* get_output_virtual_node(const RGBasePipe* pipe);

    // Stream creator to use for creating streams.
    std::unique_ptr<StreamCreator> m_stream_creator;

    // Ncrisc creator to use for creating ncrisc configurations.
    std::unique_ptr<NcriscCreator> m_ncrisc_creator;

    // Pointer to the resource manager.
    ResourceManager* m_resource_manager;

    // Pointer to the map from virtual nodes to stream nodes and their static config. Each pipe that has a virtual
    // node at its output, will assign a stream node to that virtual node. Its static config will be used to create
    // dynamic config through phases for the stream node.
    std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* m_virt_node_to_stream_node;
};
}  // namespace pipegen2