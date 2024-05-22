// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe_streams_creator.h"

namespace pipegen2
{
class UnionStreamsCreator : public PipeStreamsCreator
{
public:
    UnionStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(
            std::move(ncrisc_creator), std::move(stream_creator), resource_manager, virt_node_to_stream_node)
    {
    }

private:
    // Overrides base class method to create streams for union pipe. Union pipe does not create its own streams so
    // this is a no-op.
    std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
        const RGBasePipe* pipe, const DataFlowInfo& data_flow_info) override;

    // Overrides base class method to assign streams to pipe outputs. This is done differently for union because it
    // does not create its own streams but it reuses input streams. Also, it unifies all input streams into a single
    // stream. The rest of the streams are deleted. The union stream is then forwarded to the virtual output node.
    void assign_streams_to_pipe_outputs(
        const RGBasePipe* pipe, const std::vector<std::unique_ptr<StreamNode>>& created_streams) override;

    // Unifies all pipe input streams into a single stream by merging their phases.
    StreamNode* unify_input_streams(const RGBasePipe* pipe);

    // Forwards the configured union stream to pipe's output virtual node.
    void forward_union_stream_to_virtual_output(const RGBasePipe* pipe, StreamNode* union_stream);

    // Unifies given input stream with the union stream in a way that all the phases from the input streams are
    // moved to the union stream.
    void unify_input_stream_with_union_stream(StreamNode* input_stream, StreamNode* union_stream);

    // Updates the input stream's source stream to know have union stream as dest instead of the input stream.
    void update_stream_sources_dest_field(StreamNode* input_stream, StreamNode* union_stream);
};
}  // namespace pipegen2