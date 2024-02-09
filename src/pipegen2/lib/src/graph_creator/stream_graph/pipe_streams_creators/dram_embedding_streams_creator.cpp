// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_embedding_streams_creator.h"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_read_common_streams_creator.h"
#include "model/rational_graph/nodes/dram_embedding_index_input_node.h"
#include "model/rational_graph/nodes/dram_embedding_table_input_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "model/rational_graph/pipes/join/dram_embedding_pipe.h"
#include "pipegen2_utils.h"

namespace pipegen2
{

    DramEmbeddingStreamsCreator::DramEmbeddingStreamsCreator(
        std::unique_ptr<NcriscCreator> ncrisc_creator,
        std::unique_ptr<StreamCreator> stream_creator,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
        PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager,
                           virt_node_to_stream_node)
    {
        m_dram_read_common_streams_creator = std::make_unique<DramReadCommonStreamsCreator>(m_stream_creator.get());
    }

    DramEmbeddingStreamsCreator::~DramEmbeddingStreamsCreator() = default;

    std::vector<std::unique_ptr<StreamNode>> DramEmbeddingStreamsCreator::create_streams_internal(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        const DramEmbeddingPipe* dram_embedding_pipe = dynamic_cast<const DramEmbeddingPipe*>(pipe);
        log_assert(dram_embedding_pipe, "DramEmbeddingStreamsCreator invoked for inadequate pipe");

        const DramEmbeddingTableInputNode* dram_embedding_table = dram_embedding_pipe->get_embedding_table();

        std::vector<NcriscConfig> ncrisc_configs =
            m_ncrisc_creator->configure_dram_ncrisc_reader(
                data_flow_info,
                pipe,
                dram_embedding_pipe->get_dram_input_total_readers(),
                dram_embedding_pipe->get_dram_input_reader_index(),
                dram_embedding_pipe->get_max_dram_input_buffer_size_tiles(),
                m_resource_manager->get_soc_info());
        log_assert(ncrisc_configs.size() == 1, "Expecting only one NCRISC config for DramEmbedding pipe");

        std::vector<std::unique_ptr<StreamNode>> created_streams =
            m_dram_read_common_streams_creator->create_dram_embedding_input_streams(
                pipe,
                dram_embedding_table,
                data_flow_info,
                dram_embedding_pipe->get_max_dram_input_buffer_size_tiles(),
                std::move(ncrisc_configs));
        log_assert(created_streams.size() == 1, "Expecting only one stream created for reading from DRAM embedding table");

        StreamConfig& base_config = created_streams[0]->get_base_config();

        const VirtualNode* embedding_index_v_node = dram_embedding_pipe->get_embedding_index_v_node();
        const DramEmbeddingIndexInputNode* embedding_index = dram_embedding_pipe->get_embedding_index();

        const StreamPhasesCommonConfig& stream_config = get_stream_node_from_virtual_node(embedding_index_v_node);
        base_config.set_scatter_list_stream_id(stream_config.get_stream_node());
        base_config.set_scatter_list_indicies_per_input(embedding_index->get_embedding_indices_per_input());
        base_config.set_scatter_list_indicies_per_tile(embedding_index->get_embedding_indices_per_tile());

        return created_streams;
    }
}