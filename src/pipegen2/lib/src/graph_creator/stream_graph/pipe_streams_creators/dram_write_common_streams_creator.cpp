// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_write_common_streams_creator.h"

#include "model/rational_graph/nodes/dram_output_node.h"
#include "model/stream_graph/stream_node.h"

namespace pipegen2
{
    void DramWriteCommonStreamsCreator::setup_packer_stream_for_dram_write(
        const StreamPhasesCommonConfig& stream_phases_common_config,
        const std::size_t input_index,
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const SoCInfo* soc_info)
    {
        const RGBaseNode* input_node = pipe->get_inputs()[input_index].get_node();

        const DramOutputNode* dram_output_node = dynamic_cast<const DramOutputNode*>(pipe->get_output_node());
        log_assert(dram_output_node, "Expecting DramOutputNode at the output of pipe writing to DRAM");

        NcriscConfig ncrisc_config = m_ncrisc_creator->configure_dram_ncrisc_writer(
            data_flow_info, pipe, soc_info);

        setup_stream_for_dram_or_pcie_write(stream_phases_common_config.get_stream_node(),
                                            std::move(ncrisc_config),
                                            dram_output_node->get_is_remote_io(),
                                            dram_output_node->is_untilized_output());

        m_stream_creator->configure_phases_of_input_stream(
            stream_phases_common_config, input_node, pipe, {} /* receiving_streams */, data_flow_info);

        if (dram_output_node->is_untilized_output())
        {
            m_stream_creator->configure_packer_stream_with_untilization_params(
                pipe, input_index /* packer_stride */, stream_phases_common_config.get_stream_node());
        }
    }

    void DramWriteCommonStreamsCreator::setup_packer_stream_for_pcie_write(
        const StreamPhasesCommonConfig& stream_phases_common_config,
        const RGBaseNode* input_node,
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        setup_relay_stream_for_pcie_write(stream_phases_common_config.get_stream_node(), pipe, data_flow_info);

        m_stream_creator->configure_phases_of_input_stream(
            stream_phases_common_config, input_node, pipe, {} /* receiving_streams */, data_flow_info);
    }

    void DramWriteCommonStreamsCreator::setup_relay_stream_for_pcie_write(
        StreamNode* relay_stream,
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        NcriscConfig ncrisc_config = m_ncrisc_creator->configure_pcie_ncrisc_writer(data_flow_info, pipe);

        setup_stream_for_dram_or_pcie_write(relay_stream,
                                            std::move(ncrisc_config),
                                            true /* is_remote_write */,
                                            false /* is_untilized_output */);
    }

    void DramWriteCommonStreamsCreator::setup_stream_for_dram_or_pcie_write(
        StreamNode* stream,
        NcriscConfig&& ncrisc_config,
        const bool is_remote_write,
        const bool is_untilized_output)
    {
        stream->set_ncrisc_configs({std::move(ncrisc_config)});

        m_stream_creator->update_stream_for_dram_write(stream, is_remote_write, is_untilized_output);
    }
}