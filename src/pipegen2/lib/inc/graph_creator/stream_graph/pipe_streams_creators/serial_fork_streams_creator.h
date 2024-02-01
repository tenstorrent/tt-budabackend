// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe_streams_creator.h"

namespace pipegen2
{
    class SerialForkStreamsCreator : public PipeStreamsCreator
    {
    public:
        SerialForkStreamsCreator(
            std::unique_ptr<NcriscCreator> ncrisc_creator,
            std::unique_ptr<StreamCreator> stream_creator,
            ResourceManager* resource_manager,
            std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
            PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                               virt_node_to_stream_node)
        {
        }

    private:
        std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info) override;

        // Overrides base class method to assign streams to pipe outputs. This is done differently for serial fork
        // because it does not create its own streams but it reuses input streams.
        void assign_streams_to_pipe_outputs(const RGBasePipe* pipe,
                                            const std::vector<std::unique_ptr<StreamNode>>& created_streams) override;

        // Overrides base class method to assign scatter indices to pipe outputs.
        void assign_common_config_to_pipe_outputs(const RGBasePipe* pipe) override;
    };
}