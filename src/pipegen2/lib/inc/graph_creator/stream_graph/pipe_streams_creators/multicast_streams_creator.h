// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe_streams_creator.h"

namespace pipegen2
{
class MulticastPipe;
class PackerInputNode;
class UnpackerOutputNode;

class MulticastStreamsCreator : public PipeStreamsCreator
{
public:
    MulticastStreamsCreator(
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
        const RGBasePipe* pipe, const DataFlowInfo& data_flow_info);

    // Checks whether separate multicast stream should be created.
    static bool should_create_multicast_stream(const StreamNode* input_stream, const MulticastPipe* multicast_pipe);

    // Checks if this multicast can be optimized as direct packer multicast.
    static bool is_direct_packer_multicast(const StreamNode* input_stream, const MulticastPipe* multicast_pipe);

    // Creates multicast stream.
    std::unique_ptr<StreamNode> create_multicast_stream(const RGBasePipe* pipe, const DataFlowInfo& data_flow_info);

    // Converts input stream to mutlicast stream.
    void convert_input_stream_to_multicast(const MulticastPipe* multicast_pipe, StreamNode* input_stream);

    // Creates stream that receives data to unpacker.
    std::vector<std::unique_ptr<StreamNode>> create_unpacker_receiving_streams(
        const RGBasePipe* pipe, const DataFlowInfo& data_flow_info);

    // TODO: Extract into common function once we make changes in RG structure and how pipe stream creators work.
    std::unique_ptr<StreamNode> create_unpacker_receiving_stream(
        const RGBasePipe* pipe, const std::size_t output_index, const DataFlowInfo& data_flow_info);
};
}  // namespace pipegen2