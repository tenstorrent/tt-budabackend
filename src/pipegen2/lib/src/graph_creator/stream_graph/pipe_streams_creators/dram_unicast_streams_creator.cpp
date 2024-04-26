// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_unicast_streams_creator.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_read_common_streams_creator.h"
#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "model/rational_graph/pipes/direct/dram_unicast_pipe.h"
#include "utils/logger.hpp"

namespace pipegen2
{

    DramUnicastStreamsCreator::DramUnicastStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                           virt_node_to_stream_node)
    {
        m_dram_read_common_streams_creator = std::make_unique<DramReadCommonStreamsCreator>(m_stream_creator.get());
    }

    DramUnicastStreamsCreator::~DramUnicastStreamsCreator() = default;

    std::vector<std::unique_ptr<StreamNode>> DramUnicastStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        const DramUnicastPipe* dram_unicast_pipe = dynamic_cast<const DramUnicastPipe*>(pipe);
        log_assert(dram_unicast_pipe, "DramUnicastStreamsCreator invoked for inadequate pipe");

        std::vector<NcriscConfig> ncrisc_configs =
            m_ncrisc_creator->configure_dram_ncrisc_reader(
                data_flow_info,
                pipe,
                dram_unicast_pipe->get_dram_input_total_readers(),
                dram_unicast_pipe->get_dram_input_reader_index(),
                dram_unicast_pipe->get_max_dram_input_buffer_size_tiles(),
                m_resource_manager->get_soc_info());
        log_assert(ncrisc_configs.size() == 1, "Expecting exactly one NCRISC config for DramUnicast pipe");

        std::vector<std::unique_ptr<StreamNode>> created_streams =
            m_dram_read_common_streams_creator->create_dram_input_streams(
                pipe,
                data_flow_info,
                dram_unicast_pipe->get_max_dram_input_buffer_size_tiles(),
                std::move(ncrisc_configs));

        return created_streams;
    }
}