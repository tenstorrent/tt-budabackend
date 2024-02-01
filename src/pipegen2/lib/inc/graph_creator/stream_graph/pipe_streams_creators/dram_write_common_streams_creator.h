// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/soc_info.h"
#include "graph_creator/stream_graph/ncrisc_creators/ncrisc_creator.h"
#include "graph_creator/stream_graph/stream_creators/stream_creator.h"
#include "model/data_flow/data_flow_info.h"
#include "model/rational_graph/nodes/base_rg_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "model/stream_graph/ncrisc_config.h"

namespace pipegen2
{
    class DramWriteCommonStreamsCreator
    {
    public:
        DramWriteCommonStreamsCreator(StreamCreator* stream_creator, NcriscCreator* ncrisc_creator) :
            m_stream_creator(stream_creator),
            m_ncrisc_creator(ncrisc_creator)
        {
        }

        // Setups given packer stream for DRAM write. Configures untilization parameters if untilized output is
        // required.
        void setup_packer_stream_for_dram_write(const StreamPhasesCommonConfig& stream_phases_common_config,
                                                const std::size_t input_index,
                                                const RGBasePipe* pipe,
                                                const DataFlowInfo& data_flow_info,
                                                const SoCInfo* soc_info);

        void setup_packer_stream_for_pcie_write(const StreamPhasesCommonConfig& stream_phases_common_config,
                                                const RGBaseNode* input_node,
                                                const RGBasePipe* pipe,
                                                const DataFlowInfo& data_flow_info);

        void setup_relay_stream_for_pcie_write(StreamNode* relay_stream,
                                               const RGBasePipe* pipe,
                                               const DataFlowInfo& data_flow_info);

    private:
        // Setups given stream for DRAM or PCIe write.
        void setup_stream_for_dram_or_pcie_write(StreamNode* stream,
                                                 NcriscConfig&& ncrisc_config,
                                                 const bool is_remote_write,
                                                 const bool is_untilized_output);

        // Stream creator to use for creating streams.
        StreamCreator* m_stream_creator;

        // Ncrisc creator to use for creating ncrisc configurations.
        NcriscCreator* m_ncrisc_creator;
    };
}