// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "pipe_streams_creator.h"

namespace pipegen2
{
    class DramReadCommonStreamsCreator;

    class PCIeMulticastStreamsCreator : public PipeStreamsCreator
    {
    public:
        PCIeMulticastStreamsCreator(
            std::unique_ptr<NcriscCreator> ncrisc_creator,
            std::unique_ptr<StreamCreator> stream_creator,
            ResourceManager* resource_manager,
            std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node);

        // Necessary for forward declarations of classes in smart pointer members.
        ~PCIeMulticastStreamsCreator();

    private:
        std::vector<std::unique_ptr<StreamNode>> create_streams_internal(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info) override;

        // PCIe communication is handled by NCRISC, which is why we need to setup streams with NCRISC config
        // similar like we do for DRAM read.
        std::unique_ptr<DramReadCommonStreamsCreator> m_dram_read_common_streams_creator;
    };
}