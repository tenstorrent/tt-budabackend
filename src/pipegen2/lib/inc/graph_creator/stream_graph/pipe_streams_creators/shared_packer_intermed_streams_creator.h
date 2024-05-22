// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/rational_graph/nodes/shared_packer_intermed_node.h"
#include "pipe_streams_creator.h"

namespace pipegen2
{
class SharedPackerIntermedStreamsCreator : public PipeStreamsCreator
{
public:
    SharedPackerIntermedStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(
            std::move(ncrisc_creator), std::move(stream_creator), resource_manager, virt_node_to_stream_node)
    {
    }

    ~SharedPackerIntermedStreamsCreator() = default;

private:
    std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
        const RGBasePipe* pipe, const DataFlowInfo& data_flow_info) override;
};
}  // namespace pipegen2