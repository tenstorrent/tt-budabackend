// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_tilizer_streams_creator.h"

// clang-format off
#include "utils/logger.hpp"

#include "graph_creator/stream_graph/pipe_streams_creators/dram_read_common_streams_creator.h"
#include "model/rational_graph/nodes/dram_embedding_table_input_node.h"
#include "model/rational_graph/pipes/direct/dram_tilizer_pipe.h"
// clang-format on

namespace pipegen2
{
DramTilizerStreamsCreator::DramTilizerStreamsCreator(
    std::unique_ptr<NcriscCreator> ncrisc_creator,
    std::unique_ptr<StreamCreator> stream_creator,
    ResourceManager* resource_manager,
    std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node) :
    PipeStreamsCreator(std::move(ncrisc_creator), std::move(stream_creator), resource_manager, virt_node_to_stream_node)
{
    m_dram_read_common_streams_creator = std::make_unique<DramReadCommonStreamsCreator>(m_stream_creator.get());
}

DramTilizerStreamsCreator::~DramTilizerStreamsCreator() = default;

std::vector<std::unique_ptr<StreamNode>> DramTilizerStreamsCreator::create_streams_internal(
    const RGBasePipe* pipe, const DataFlowInfo& data_flow_info)
{
    const DramTilizerPipe* dram_tilizer_pipe = dynamic_cast<const DramTilizerPipe*>(pipe);
    log_assert(dram_tilizer_pipe, "DramTilizerStreamsCreator invoked for inadequate pipe");

    const DramEmbeddingTableInputNode* dram_embedding_table = dram_tilizer_pipe->get_embedding_table();

    std::vector<NcriscConfig> ncrisc_configs = m_ncrisc_creator->configure_dram_tilizer_ncrisc_reader(
        data_flow_info,
        pipe,
        dram_tilizer_pipe->get_dram_input_total_readers(),
        dram_tilizer_pipe->get_dram_input_reader_index(),
        dram_tilizer_pipe->get_max_dram_input_buffer_size_tiles(),
        m_resource_manager->get_soc_info());
    log_assert(ncrisc_configs.size() == 1, "Expecting only one NCRISC config for DramTilizer pipe");

    std::vector<std::unique_ptr<StreamNode>> created_streams =
        m_dram_read_common_streams_creator->create_dram_embedding_input_streams(
            pipe,
            dram_embedding_table,
            data_flow_info,
            dram_tilizer_pipe->get_max_dram_input_buffer_size_tiles(),
            std::move(ncrisc_configs));
    log_assert(created_streams.size() == 1, "Expecting only one stream created for reading from DRAM embedding table");

    StreamConfig& base_config = created_streams[0]->get_base_config();
    base_config.set_hw_tilize(true);
    base_config.set_c_dim_loop_num_rows(dram_embedding_table->get_tilize_mblock_n_loop_num_rows());
    base_config.set_tile_dim_c(dram_embedding_table->get_untilized_input_tile_dim_c());
    base_config.set_tile_dim_r(dram_embedding_table->get_untilized_input_tile_dim_r());
    base_config.set_tilize_row_col_offset(dram_tilizer_pipe->get_tilize_row_col_offset());

    return created_streams;
}
}  // namespace pipegen2