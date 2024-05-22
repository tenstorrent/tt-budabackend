// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "base_parallel_fork_pipe_streams_creator.h"

namespace pipegen2
{
class ParallelForkStreamsCreator : public BaseParallelForkPipeStreamsCreator
{
public:
    ParallelForkStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        BaseParallelForkPipeStreamsCreator(
            std::move(ncrisc_creator), std::move(stream_creator), resource_manager, virt_node_to_stream_node)
    {
    }

private:
    // Returns stream that sends data from packer.
    std::unique_ptr<StreamNode> create_sending_stream(
        const RGBaseNode* input_node, const RGBasePipe* pipe, const DataFlowInfo& data_flow_info) override;
};
}  // namespace pipegen2