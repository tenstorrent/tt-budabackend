// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_output_intermed_streams_creator.h"

// clang-format off
#include "utils/logger.hpp"

#include "model/rational_graph/nodes/dram_output_intermed_node.h"
#include "model/rational_graph/pipes/direct/dram_output_intermed_pipe.h"
// clang-format on

namespace pipegen2
{
std::vector<std::unique_ptr<StreamNode>> DramOutputIntermedStreamsCreator::create_streams_internal(
    const RGBasePipe* pipe, const DataFlowInfo& data_flow_info)
{
    const DramOutputIntermedPipe* intermed_pipe = dynamic_cast<const DramOutputIntermedPipe*>(pipe);
    log_assert(intermed_pipe != nullptr, "Expecting DramOutputIntermedPipe in DramOutputIntermedStreamsCreator");

    const DramOutputIntermedNode* intermed_input_node =
        dynamic_cast<const DramOutputIntermedNode*>(pipe->get_inputs()[0].get_node());
    const DramOutputIntermedNode* intermed_output_node =
        dynamic_cast<const DramOutputIntermedNode*>(pipe->get_output_nodes()[0]);
    log_assert(intermed_input_node != nullptr, "Expecting DramOutputIntermedNode at DramOutputIntermedPipe input");
    log_assert(intermed_output_node != nullptr, "Expecting DramOutputIntermedNode at DramOutputIntermedPipe input");

    // New stream is created based solely on info from intermed_output_node.
    std::unique_ptr<StreamNode> intermed_stream = std::make_unique<StreamNode>(
        StreamType::Intermed, intermed_output_node->get_physical_location(), intermed_output_node->get_operand_id());

    add_ncrisc_config(intermed_stream.get(), intermed_output_node);

    m_stream_creator->configure_default_dram_output_intermed_stream_properties(intermed_stream.get());
    m_stream_creator->configure_intermed_stream_properties_from_node(intermed_stream.get(), intermed_output_node);
    intermed_stream->update_phases(
        data_flow_info.get_edge_phases(intermed_pipe, intermed_output_node),
        data_flow_info.get_max_num_tiles_per_phase(intermed_output_node));

    std::vector<std::unique_ptr<StreamNode>> created_streams;
    created_streams.push_back(std::move(intermed_stream));
    return created_streams;
}

void DramOutputIntermedStreamsCreator::add_ncrisc_config(
    StreamNode* stream, const DramOutputIntermedNode* intermed_node)
{
    NcriscConfig ncrisc_config = m_ncrisc_creator->configure_dram_output_intermed_ncrisc_writer(
        intermed_node, m_resource_manager->get_soc_info());
    stream->set_ncrisc_configs({ncrisc_config});
}
}  // namespace pipegen2