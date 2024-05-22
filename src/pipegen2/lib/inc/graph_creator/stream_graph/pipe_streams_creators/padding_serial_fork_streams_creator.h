// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe_streams_creator.h"

namespace pipegen2
{
class PaddingSerialForkPipe;
class SerialForkPipe;

class PaddingSerialForkStreamsCreator : public PipeStreamsCreator
{
public:
    PaddingSerialForkStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(
            std::move(ncrisc_creator), std::move(stream_creator), resource_manager, virt_node_to_stream_node)
    {
    }

private:
    std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
        const RGBasePipe* pipe, const DataFlowInfo& data_flow_info) override;

    // Create a stream which reads from the padding table for every unique output location. This can be done since
    // every worker core has a padding table with all the necessary padding buffers allocated by the runtime.
    std::unordered_map<tt_cxy_pair, StreamNode*> create_padding_stream_per_output_core(
        const PaddingSerialForkPipe* pipe, std::vector<std::unique_ptr<StreamNode>>& created_streams);

    // Creates a padding stream on a given output location based on the base padding stream node.
    std::unique_ptr<StreamNode> create_padding_stream_for_core(
        const PaddingSerialForkPipe* pipe, const StreamNode* base_padding_stream, const tt_cxy_pair& physical_location);

    // Overrides base class method to get proper stream node from created streams. This is necessary because padding
    // serial fork creates a stream per output core and multiple output nodes can be mapped to the same core.
    StreamNode* get_stream_node_from_created_streams(
        const std::vector<std::unique_ptr<StreamNode>>& created_streams,
        const RGBasePipe* pipe,
        const std::size_t output_index) override;

    // Overrides base class method to assign scatter indices to pipe outputs.
    void assign_common_config_to_pipe_outputs(const RGBasePipe* pipe) override;
};
}  // namespace pipegen2