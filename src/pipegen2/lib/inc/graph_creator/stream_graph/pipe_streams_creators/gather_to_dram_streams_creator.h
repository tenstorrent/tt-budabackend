// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe_streams_creator.h"

namespace pipegen2
{
    class DramWriteCommonStreamsCreator;

    class GatherToDramStreamsCreator : public PipeStreamsCreator
    {
    public:
        GatherToDramStreamsCreator(
            std::unique_ptr<NcriscCreator> ncrisc_creator,
            std::unique_ptr<StreamCreator> stream_creator,
            ResourceManager* resource_manager,
            std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node);

        // Necessary for forward declarations of classes in smart pointer members.
        ~GatherToDramStreamsCreator();

    private:
        std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info) override;

        // Instance of creator used for common setup of all dram write streams.
        std::unique_ptr<DramWriteCommonStreamsCreator> m_dram_write_common_streams_creator;
    };
}