// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_prefetch_post_tm_multicast_streams_creator.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_read_common_streams_creator.h"
#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/pipes/fork/dram_prefetch_post_tm_multicast_pipe.h"
#include "pipegen2_utils.h"


namespace pipegen2
{

DramPrefetchPostTMMulticastStreamsCreator::DramPrefetchPostTMMulticastStreamsCreator(
    std::unique_ptr<NcriscCreator> ncrisc_creator,
    std::unique_ptr<StreamCreator> stream_creator,
    ResourceManager* resource_manager,
    std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
    PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                        virt_node_to_stream_node)
{
    m_dram_read_common_streams_creator = std::make_unique<DramReadCommonStreamsCreator>(m_stream_creator.get());
}

DramPrefetchPostTMMulticastStreamsCreator::~DramPrefetchPostTMMulticastStreamsCreator() = default;

std::vector<std::unique_ptr<StreamNode>> DramPrefetchPostTMMulticastStreamsCreator::create_streams_internal(
    const RGBasePipe* pipe,
    const DataFlowInfo& data_flow_info)
{
    std::vector<std::unique_ptr<StreamNode>> created_streams;

    const DramPrefetchPostTMMulticastPipe* dram_multicast_pipe = 
                                                        dynamic_cast<const DramPrefetchPostTMMulticastPipe*>(pipe);
    log_assert(dram_multicast_pipe, "DramPrefetchPostTMMulticastStreamsCreator invoked for inadequate pipe");

    const DramInputNode* dram_input_node = dram_multicast_pipe->get_first_dram_input_node();
    log_assert(dram_input_node != nullptr, "Expecting DRAM input node at the pipe input");

    std::vector<NcriscConfig> ncrisc_configs =
        m_ncrisc_creator->configure_prefetch_post_tm_dram_ncrisc_reader(
            data_flow_info,
            pipe,
            dram_multicast_pipe->get_dram_input_total_readers(),
            dram_multicast_pipe->get_dram_input_reader_index(),
            dram_multicast_pipe->get_max_dram_input_buffer_size_tiles(),
            m_resource_manager->get_soc_info());

    unsigned int max_tiles_per_phase;
    std::unique_ptr<StreamNode> multicast_stream =
        m_dram_read_common_streams_creator->create_dram_multicast_stream(
            pipe,
            data_flow_info,
            dram_multicast_pipe->get_max_dram_input_buffer_size_tiles(),
            std::move(ncrisc_configs), max_tiles_per_phase);

    m_stream_creator->set_common_stream_properties_from_pipe(
        multicast_stream.get(), pipe, false /* incoming_stream */);

    std::vector<std::unique_ptr<StreamNode>> unpacker_streams =
        m_dram_read_common_streams_creator->create_multicast_unpacker_streams(
            multicast_stream.get(), pipe, dram_input_node, data_flow_info, max_tiles_per_phase);

    created_streams.push_back(std::move(multicast_stream));
    std::move(unpacker_streams.begin(), unpacker_streams.end(), std::back_inserter(created_streams));

    return created_streams;
}

} // namespace pipegen2