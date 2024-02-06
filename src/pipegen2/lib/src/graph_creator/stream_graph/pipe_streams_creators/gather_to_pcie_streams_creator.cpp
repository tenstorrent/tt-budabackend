// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/gather_to_pcie_streams_creator.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_write_common_streams_creator.h"

namespace pipegen2
{
    GatherToPCIeStreamsCreator::GatherToPCIeStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                           virt_node_to_stream_node)
    {
        m_dram_write_common_streams_creator =
            std::make_unique<DramWriteCommonStreamsCreator>(m_stream_creator.get(), m_ncrisc_creator.get());
    }

    GatherToPCIeStreamsCreator::~GatherToPCIeStreamsCreator() = default;

    std::vector<std::unique_ptr<StreamNode>> GatherToPCIeStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        // For gather from packers to PCIe core we don't setup each packer stream with NCRISC config to write to PCIe,
        // because then they would have to sync with each other and that would be too slow in PCIe transfer scenario
        // where buffers are typically small. Instead we setup one relay stream to write to PCIe core, and connect each
        // packer stream to that relay.
        // TODO: Setup DormantRelay pipe before GatherToPCIe, and then we can just update relay for DRAM write here.

        std::vector<std::unique_ptr<StreamNode>> created_streams;

        std::unique_ptr<StreamNode> relay_stream = create_relay_stream(pipe, data_flow_info);

        connect_inputs_to_relay_stream(pipe, data_flow_info, relay_stream.get());

        created_streams.push_back(std::move(relay_stream));

        return created_streams;
    }

    std::unique_ptr<StreamNode> GatherToPCIeStreamsCreator::create_relay_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        const std::vector<PhaseInfo> relay_phases = data_flow_info.get_edge_phases(pipe, pipe->get_output_nodes()[0]);

        std::unique_ptr<StreamNode> relay_stream = std::make_unique<StreamNode>(
            StreamType::Relay,
            pipe->get_physical_location(),
            relay_phases,
            data_flow_info.get_num_iterations_in_epoch(pipe),
            data_flow_info.get_max_num_tiles_per_phase(pipe->get_output_node()));

        m_stream_creator->configure_gather_to_pcie_relay_stream(relay_stream.get(), pipe);

        // PCIe communication is handled by NCRISC, which is why we need to setup streams with NCRISC config
        // similar like we do for DRAM write.
        m_dram_write_common_streams_creator->setup_relay_stream_for_pcie_write(
            relay_stream.get(), pipe, data_flow_info);

        // TODO: This should be properly set inside update_stream_for_dram_write() function.
        relay_stream->get_base_config().set_dram_output_no_push(false);

        return relay_stream;
    }

    void GatherToPCIeStreamsCreator::connect_inputs_to_relay_stream(const RGBasePipe* pipe,
                                                                    const DataFlowInfo& data_flow_info,
                                                                    StreamNode* relay_stream)
    {
        const std::vector<const RGBaseNode*> input_nodes = pipe->get_unique_input_nodes();

        for (const RGBaseNode* input_node: input_nodes)
        {
            const VirtualNode* virtual_node = dynamic_cast<const VirtualNode*>(input_node);
            ASSERT(virtual_node != nullptr, "Expecting virtual node at the GatherToPCIe pipe input");

            const StreamPhasesCommonConfig& stream_phases_common_config =
                get_stream_node_from_virtual_node(virtual_node);
            StreamNode* input_stream_node = stream_phases_common_config.get_stream_node();

            m_stream_creator->configure_phases_of_input_stream(
                stream_phases_common_config, input_node, pipe, {relay_stream}, data_flow_info);
        }
    }
}