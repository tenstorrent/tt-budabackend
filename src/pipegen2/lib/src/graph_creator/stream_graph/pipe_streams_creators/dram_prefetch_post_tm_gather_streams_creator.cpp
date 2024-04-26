// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_prefetch_post_tm_gather_streams_creator.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_read_common_streams_creator.h"
#include "model/rational_graph/pipes/join/dram_prefetch_post_tm_gather_pipe.h"

namespace pipegen2
{

DramPrefetchPostTMGatherStreamsCreator::DramPrefetchPostTMGatherStreamsCreator(
    std::unique_ptr<NcriscCreator> ncrisc_creator,
    std::unique_ptr<StreamCreator> stream_creator,
    ResourceManager* resource_manager,
    std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
    PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                        virt_node_to_stream_node)
{
    m_dram_read_common_streams_creator = std::make_unique<DramReadCommonStreamsCreator>(m_stream_creator.get());
}

DramPrefetchPostTMGatherStreamsCreator::~DramPrefetchPostTMGatherStreamsCreator() = default;

std::vector<std::unique_ptr<StreamNode>> DramPrefetchPostTMGatherStreamsCreator::create_streams_internal(
    const RGBasePipe* pipe,
    const DataFlowInfo& data_flow_info)
{
    const auto dram_gather_pipe = dynamic_cast<const DramPrefetchPostTMGatherPipe*>(pipe);
    log_assert(dram_gather_pipe, "DramPrefetchPostTMGatherPipe invoked for inadequate pipe");
    
    std::vector<NcriscConfig> ncrisc_configs =
        m_ncrisc_creator->configure_prefetch_post_tm_dram_ncrisc_reader(
        data_flow_info,
        pipe,
        dram_gather_pipe->get_dram_input_total_readers(),
        dram_gather_pipe->get_dram_input_reader_index(),
        dram_gather_pipe->get_max_dram_input_buffer_size_tiles(),
        m_resource_manager->get_soc_info());
    log_assert(ncrisc_configs.size() > 1, "Expecting more than one NCRISC config for DramPrefetchPostTMGatherPipe pipe");
    
    std::vector<std::unique_ptr<StreamNode>> created_streams =
        m_dram_read_common_streams_creator->create_dram_prefetch_post_tm_input_stream(
            pipe,
            data_flow_info,
            dram_gather_pipe->get_max_dram_input_buffer_size_tiles(),
            std::move(ncrisc_configs));

    return created_streams;
}

} // namespace pipegen2