// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe_streams_creator.h"

namespace pipegen2
{
class BaseParallelForkPipeStreamsCreator : public PipeStreamsCreator
{
public:
    // Virtual destructor, necessary for base class.
    virtual ~BaseParallelForkPipeStreamsCreator();

protected:
    BaseParallelForkPipeStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(
            std::move(ncrisc_creator), std::move(stream_creator), resource_manager, virt_node_to_stream_node)
    {
    }

    std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
        const RGBasePipe* pipe, const DataFlowInfo& data_flow_info) override;

    // Returns stream that sends thata from the source buffer to a single destination.
    virtual std::unique_ptr<StreamNode> create_sending_stream(
        const RGBaseNode* input_node, const RGBasePipe* pipe, const DataFlowInfo& data_flow_info) = 0;
};
}  // namespace pipegen2