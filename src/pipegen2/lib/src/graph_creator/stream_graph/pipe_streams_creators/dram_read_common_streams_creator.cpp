// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/pipe_streams_creators/dram_read_common_streams_creator.h"

#include <numeric>

#include "logger.hpp"

#include "model/rational_graph/nodes/dram_embedding_index_input_node.h"
#include "model/rational_graph/nodes/dram_embedding_table_input_node.h"
#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/nodes/pcie_streaming_node.h"
#include "model/rational_graph/nodes/relay_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "model/rational_graph/nodes/virtual_node.h"
#include "pipegen2_constants.h"

namespace pipegen2
{

    std::vector<std::unique_ptr<StreamNode>> DramReadCommonStreamsCreator::create_dram_prefetch_post_tm_input_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const unsigned int max_dram_input_buffer_size_tiles,
        std::vector<NcriscConfig>&& ncrisc_configs)
    {
        const UnpackerOutputNode* unpacker_node = get_unpacker_output_node(pipe);
        std::vector<std::unique_ptr<StreamNode>> created_streams;
        
        if (unpacker_node)
        {
            created_streams.push_back(create_dram_prefetch_post_tm_to_unpacker_stream(pipe, 
                                                                                      unpacker_node, 
                                                                                      data_flow_info, 
                                                                                      std::move(ncrisc_configs)));
        }
        else
        {
            created_streams.push_back(create_dram_relay_stream(pipe, 
                                                               data_flow_info, 
                                                               ncrisc_configs[0], 
                                                               true /* is_dram_prefetch_post_tm */));
        }

        const DramInputNode* dram_input_node = pipe->get_first_dram_input_node();
        unsigned int max_num_tiles_per_phase = data_flow_info.get_max_num_tiles_per_phase(pipe->get_output_node());
        configure_dram_receiving_stream(
            created_streams[0].get(), std::move(ncrisc_configs), pipe, dram_input_node, data_flow_info,
            false /* should_scale_up_stream */, max_dram_input_buffer_size_tiles, max_num_tiles_per_phase);
        
        return created_streams;
    }

    std::vector<std::unique_ptr<StreamNode>> DramReadCommonStreamsCreator::create_dram_input_streams(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const unsigned int max_dram_input_buffer_size_tiles,
        std::vector<NcriscConfig>&& ncrisc_configs)
    {
        const DramInputNode* dram_input_node = pipe->get_first_dram_input_node();

        const UnpackerOutputNode* unpacker_node = get_unpacker_output_node(pipe);

        std::vector<std::unique_ptr<StreamNode>> created_streams = create_dram_or_pcie_to_unpacker_streams(
            pipe, ncrisc_configs[0], dram_input_node->is_dram_prefetch_post_tm(), max_dram_input_buffer_size_tiles,
            unpacker_node, data_flow_info);

        // Whether we can use single stream to transfer data from DRAM to unpacker.
        const bool can_use_single_unpacker_stream = created_streams.size() == 1 && unpacker_node;
        const bool dram_input_node_is_embedding_index =
            dynamic_cast<const DramEmbeddingIndexInputNode*>(dram_input_node) != nullptr;
        const bool should_scale_up_stream = can_use_single_unpacker_stream ?
            dram_input_node->is_dram_io() && !dram_input_node_is_embedding_index :
            true;

        unsigned int max_num_tiles_per_phase = get_max_num_tiles_per_phase(
            pipe, can_use_single_unpacker_stream, data_flow_info, dram_input_node->get_tiles_per_input());

        configure_dram_receiving_stream(
            created_streams[0].get(), std::move(ncrisc_configs), pipe, dram_input_node, data_flow_info,
            should_scale_up_stream, max_dram_input_buffer_size_tiles, max_num_tiles_per_phase);

        if (created_streams.size() > 1)
        {
            log_assert(unpacker_node != nullptr, "Only dram to unpacker connection may induce multiple streams");
            configure_relay_to_unpacker_stream(created_streams[1].get(), pipe, dram_input_node, unpacker_node,
                                               data_flow_info, max_num_tiles_per_phase);
            connect_dram_or_pcie_relay_to_unpacker_streams(created_streams);
        }

        return created_streams;
    }

    std::vector<std::unique_ptr<StreamNode>> DramReadCommonStreamsCreator::create_dram_embedding_input_streams(
        const RGBasePipe* pipe,
        const DramEmbeddingTableInputNode* dram_embedding_table,
        const DataFlowInfo& data_flow_info,
        const unsigned int max_dram_input_buffer_size_tiles,
        std::vector<NcriscConfig>&& ncrisc_configs)
    {
        std::vector<std::unique_ptr<StreamNode>> created_streams = create_dram_input_streams(
            pipe, data_flow_info, max_dram_input_buffer_size_tiles, std::move(ncrisc_configs));
        log_assert(
            created_streams.size() == 1, "Expecting only one stream created for reading from DRAM embedding table");

        StreamConfig& base_config = created_streams[0]->get_base_config();

        // Force receiver endpoint for stream reading from DRAM embedding table, as we don't employ an extra local
        // stream for tile clearing in this case regardless of the chip architecture.
        base_config.opt_set_local_receiver(std::nullopt);
        base_config.opt_set_local_receiver_tile_clearing(std::nullopt);
        base_config.set_receiver_endpoint(true);

        // Row shift represents how many bytes stream will read from a single embedding table row.
        base_config.set_dram_embeddings_row_shift(dram_embedding_table->get_tile_size() *
                                                  dram_embedding_table->get_untilized_input_c_dim() *
                                                  dram_embedding_table->get_embedding_table_core_c_div());

        // Ncrisc config's read chunk size tiles represents how many embedding's tiles are read in one chunk. These
        // tiles are flat and each is represented by a single row of scalars. Divide this number by the number of rows
        // in a normal tile to get the number of normal tiles read in one chunk.
        const NcriscConfig& ncrisc_config = created_streams[0]->get_base_ncrisc_config();
        unsigned int tile_dim_r = dram_embedding_table->get_untilized_input_tile_dim_r();
        const unsigned int chunk_size_tiles =
            (ncrisc_config.dram_buf_read_chunk_size_tiles.value() + tile_dim_r - 1) / tile_dim_r;

        // Row tiles represents how many normal (tilized) tiles are read in one chunk from embedding table on the core.
        base_config.set_dram_embeddings_row_tiles(chunk_size_tiles *
                                                  dram_embedding_table->get_embedding_table_row_size_per_core() /
                                                  dram_embedding_table->get_untilized_input_c_dim());

        base_config.set_c_dim_size(dram_embedding_table->get_untilized_input_c_dim());

        return created_streams;
    }

    std::vector<std::unique_ptr<StreamNode>> DramReadCommonStreamsCreator::create_pcie_to_unpacker_streams(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        NcriscConfig&& ncrisc_config)
    {
        log_assert(pipe->get_inputs().size() == 1, "Expecting only a single PCIe streaming node at the input");
        const PCIeStreamingNode* pcie_streaming_node = dynamic_cast<const PCIeStreamingNode*>(
            pipe->get_inputs().front().get_node());
        log_assert(pcie_streaming_node, "Expecting a PCIe streaming node at the pipe input");

        const UnpackerOutputNode* unpacker_node = get_unpacker_output_node(pipe);
        log_assert(unpacker_node != nullptr, "Expecting unpacker node at the PCIe pipe output");

        std::vector<std::unique_ptr<StreamNode>> created_streams = create_dram_or_pcie_to_unpacker_streams(
            pipe, ncrisc_config, false /* is_dram_prefetch_post_tm */, 0 /* max_dram_input_buffer_size_tiles */, unpacker_node,
            data_flow_info);

        const bool can_use_single_unpacker_stream = created_streams.size() == 1;
        const bool should_scale_up_stream = !can_use_single_unpacker_stream;

        unsigned int max_num_tiles_per_phase = get_max_num_tiles_per_phase(
            pipe, can_use_single_unpacker_stream, data_flow_info, pcie_streaming_node->get_tiles_per_input());

        configure_pcie_receiving_stream(created_streams[0].get(), std::move(ncrisc_config), pipe, pcie_streaming_node,
                                        data_flow_info, should_scale_up_stream, max_num_tiles_per_phase);

        if (!can_use_single_unpacker_stream)
        {
            configure_relay_to_unpacker_stream(created_streams[1].get(), pipe, pcie_streaming_node, unpacker_node,
                                               data_flow_info, max_num_tiles_per_phase);
            connect_dram_or_pcie_relay_to_unpacker_streams(created_streams);
        }

        return created_streams;
    }

    std::unique_ptr<StreamNode> DramReadCommonStreamsCreator::create_dram_multicast_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const unsigned int max_dram_input_buffer_size_tiles,
        std::vector<NcriscConfig>&& ncrisc_configs,
        unsigned int& max_num_tiles_per_phase)
    {
        const DramInputNode* dram_input_node = pipe->get_first_dram_input_node();

        std::unique_ptr<StreamNode> multicast_stream =
            create_dram_or_pcie_multicast_stream(pipe, ncrisc_configs[0], dram_input_node->is_dram_prefetch_post_tm());

        max_num_tiles_per_phase = data_flow_info.get_max_num_tiles_per_phase(pipe->get_output_nodes()[0]);

        configure_dram_receiving_stream(
            multicast_stream.get(), std::move(ncrisc_configs), pipe, dram_input_node, data_flow_info,
            true /* should_scale_up_stream */, max_dram_input_buffer_size_tiles, max_num_tiles_per_phase);

        return multicast_stream;
    }

    std::unique_ptr<StreamNode> DramReadCommonStreamsCreator::create_pcie_multicast_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        NcriscConfig&& ncrisc_config,
        unsigned int& max_num_tiles_per_phase)
    {
        log_assert(pipe->get_inputs().size() == 1, "Expecting only a single PCIe streaming node at the input");
        const PCIeStreamingNode* pcie_streaming_node = dynamic_cast<const PCIeStreamingNode*>(
            pipe->get_inputs().front().get_node());
        log_assert(pcie_streaming_node, "Expecting a PCIe streaming node at the pipe input");

        std::unique_ptr<StreamNode> multicast_stream =
            create_dram_or_pcie_multicast_stream(pipe, ncrisc_config, false /* is_dram_prefetch_post_tm */);

        max_num_tiles_per_phase = data_flow_info.get_max_num_tiles_per_phase(pipe->get_output_nodes()[0]);

        configure_pcie_receiving_stream(multicast_stream.get(), std::move(ncrisc_config), pipe, pcie_streaming_node,
                                        data_flow_info, true /* should_scale_up_stream */, max_num_tiles_per_phase);

        return multicast_stream;
    }

    std::vector<std::unique_ptr<StreamNode>> DramReadCommonStreamsCreator::create_multicast_unpacker_streams(
            StreamNode* multicast_stream,
            const RGBasePipe* pipe,
            const RGBaseNode* input_node,
            const DataFlowInfo& data_flow_info,
            const unsigned int max_num_tiles_per_phase)
    {
        std::vector<std::unique_ptr<StreamNode>> unpacker_streams;

        for (std::size_t i = 0; i < pipe->get_output_nodes().size(); ++i)
        {
            const UnpackerOutputNode* unpacker_node = get_unpacker_output_node(pipe, i);
            log_assert(unpacker_node != nullptr,
                       "Expecting unpacker node at the DramMulticast or PCIeMulticast pipe output");

            unpacker_streams.push_back(
                create_multicast_unpacker_stream(
                    pipe, input_node, unpacker_node, data_flow_info, max_num_tiles_per_phase));
        }

        m_stream_creator->configure_multicast_streams_src_and_dest(multicast_stream, unpacker_streams);

        return unpacker_streams;
    }

    void DramReadCommonStreamsCreator::update_stream_with_merged_decomposed_phases(
        StreamNode* stream_node,
        const RGBaseNode* input_node,
        const std::vector<PhaseInfo>& original_phases,
        const DataFlowInfo& data_flow_info,
        const unsigned int num_iterations_in_epoch,
        const unsigned int max_num_tiles_per_phase)
    {
        // Decompose all the given phases into smaller phases because data flow calculator may have stiched some phases
        // together. This has to be done in order to further merge phases.
        const unsigned int decomposed_phase_max_size =
            std::min(max_num_tiles_per_phase, data_flow_info.get_tiles_to_send(input_node));
        const std::vector<PhaseInfo> decomposed_phases = m_stream_creator->decompose_phases(original_phases,
                                                                                            decomposed_phase_max_size);

        const std::vector<PhaseInfo> merged_phases = m_stream_creator->merge_phases(decomposed_phases,
                                                                                    max_num_tiles_per_phase);

        stream_node->update_phases(merged_phases, max_num_tiles_per_phase);
        stream_node->get_base_config().set_num_iters_in_epoch(num_iterations_in_epoch);
    }

    const UnpackerOutputNode* DramReadCommonStreamsCreator::get_unpacker_output_node(const RGBasePipe* pipe,
                                                                                     const std::size_t output_index)
    {
        log_assert(output_index < pipe->get_output_nodes().size(), "Index out of range");

        const RGBaseNode* output_node = pipe->get_output_nodes()[output_index];
        const VirtualNode* virtual_node = dynamic_cast<const VirtualNode*>(output_node);
        if (virtual_node != nullptr)
        {
            output_node = virtual_node->get_parent_rg_node();
        }

        const UnpackerOutputNode* unpacker_node = dynamic_cast<const UnpackerOutputNode*>(output_node);

        if (!unpacker_node)
        {
            log_assert(dynamic_cast<const RelayNode*>(output_node) != nullptr,
                       "Expecting unpacker or relay node at the dram pipe output");
        }

        return unpacker_node;
    }

    std::vector<std::unique_ptr<StreamNode>> DramReadCommonStreamsCreator::create_dram_or_pcie_to_unpacker_streams(
        const RGBasePipe* pipe,
        const NcriscConfig& base_ncrisc_config,
        const bool is_dram_prefetch_post_tm,
        const unsigned int max_dram_input_buffer_size_tiles,
        const UnpackerOutputNode* unpacker_node,
        const DataFlowInfo& data_flow_info)
    {
        std::vector<std::unique_ptr<StreamNode>> created_streams;

        const bool can_use_single_unpacker_stream =
            unpacker_node && m_stream_creator->can_configure_dram_to_unpacker_stream(
                pipe, base_ncrisc_config, unpacker_node, max_dram_input_buffer_size_tiles);

        if (can_use_single_unpacker_stream)
        {
            created_streams.push_back(
                create_single_dram_to_unpacker_stream(pipe, base_ncrisc_config, is_dram_prefetch_post_tm, unpacker_node));
        }
        else if (!unpacker_node)
        {
            created_streams.push_back(create_dram_relay_stream(pipe, data_flow_info, base_ncrisc_config, is_dram_prefetch_post_tm));
        }
        else
        {
            create_dram_to_unpacker_streams_with_relay(
                pipe, data_flow_info, base_ncrisc_config, is_dram_prefetch_post_tm, unpacker_node, created_streams);
        }

        return created_streams;
    }

    std::unique_ptr<StreamNode> DramReadCommonStreamsCreator::create_dram_or_pcie_multicast_stream(
        const RGBasePipe* pipe,
        const NcriscConfig& base_ncrisc_config,
        const bool is_dram_prefetch_post_tm)
    {
        std::unique_ptr<StreamNode> multicast_stream = std::make_unique<StreamNode>(
            StreamType::Multicast,
            pipe->get_physical_location());

        // TODO: Can we use configure dram relay stream?
        m_stream_creator->configure_dram_multicast_stream(
            pipe, is_dram_prefetch_post_tm, base_ncrisc_config, multicast_stream.get());

        return multicast_stream;
    }

    std::unique_ptr<StreamNode> DramReadCommonStreamsCreator::create_single_dram_to_unpacker_stream(
        const RGBasePipe* pipe,
        const NcriscConfig& base_ncrisc_config,
        const bool is_dram_prefetch_post_tm,
        const UnpackerOutputNode* unpacker_node)
    {
        std::unique_ptr<StreamNode> dram_to_unpacker_stream = std::make_unique<StreamNode>(
            StreamType::Unpacker,
            unpacker_node->get_physical_location(),
            unpacker_node->get_operand_id());

        m_stream_creator->configure_dram_to_unpacker_stream(
            pipe, base_ncrisc_config, unpacker_node, dram_to_unpacker_stream.get());

        return dram_to_unpacker_stream;
    }

    void DramReadCommonStreamsCreator::create_dram_to_unpacker_streams_with_relay(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const NcriscConfig& base_ncrisc_config,
        const bool is_dram_prefetch_post_tm,
        const UnpackerOutputNode* unpacker_node,
        std::vector<std::unique_ptr<StreamNode>>& created_streams)
    {
        std::unique_ptr<StreamNode> dram_relay_stream = create_dram_relay_stream(
            pipe, data_flow_info, base_ncrisc_config, is_dram_prefetch_post_tm);
        std::unique_ptr<StreamNode> relay_to_unpacker_stream = create_relay_to_unpacker_stream(pipe, unpacker_node);

        created_streams.push_back(std::move(dram_relay_stream));
        created_streams.push_back(std::move(relay_to_unpacker_stream));
    }

    std::unique_ptr<StreamNode> DramReadCommonStreamsCreator::create_dram_relay_stream(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info,
        const NcriscConfig& ncrisc_config,
        const bool is_dram_prefetch_post_tm)
    {
        std::unique_ptr<StreamNode> dram_relay_stream = std::make_unique<StreamNode>(
            StreamType::Relay,
            pipe->get_physical_location());

        m_stream_creator->configure_dram_relay_stream(
            pipe, data_flow_info, ncrisc_config, is_dram_prefetch_post_tm, dram_relay_stream.get());

        return dram_relay_stream;
    }

    std::unique_ptr<StreamNode> DramReadCommonStreamsCreator::create_dram_prefetch_post_tm_to_unpacker_stream(
                                                                            const RGBasePipe* pipe,
                                                                            const UnpackerOutputNode* unpacker_node,
                                                                            const DataFlowInfo& data_flow_info,
                                                                            std::vector<NcriscConfig>&& ncrisc_configs)
    {
        std::unique_ptr<StreamNode> unpacker_stream = std::make_unique<StreamNode>(
                                                                                StreamType::Unpacker,
                                                                                unpacker_node->get_physical_location(),
                                                                                unpacker_node->get_operand_id());

        m_stream_creator->configure_dram_to_unpacker_prefetch_post_tm_stream(
            pipe, data_flow_info, ncrisc_configs[0], unpacker_node, unpacker_stream.get());

        return unpacker_stream;
    }

    std::unique_ptr<StreamNode> DramReadCommonStreamsCreator::create_relay_to_unpacker_stream(
        const RGBasePipe* pipe,
        const UnpackerOutputNode* unpacker_node)
    {
        std::unique_ptr<StreamNode> unpacker_stream = std::make_unique<StreamNode>(
            StreamType::Unpacker,
            unpacker_node->get_physical_location(),
            unpacker_node->get_operand_id());

        m_stream_creator->configure_noc_to_unpacker_stream(unpacker_node, unpacker_stream.get());

        return unpacker_stream;
    }

    unsigned int DramReadCommonStreamsCreator::get_max_num_tiles_per_phase(
        const RGBasePipe* pipe,
        bool can_use_single_unpacker_stream,
        const DataFlowInfo& data_flow_info,
        const unsigned int tiles_per_input)
    {
        // If a single stream can be used, recalculate max_num_tiles_per_phase with respect to unpacker's buffer size,
        // instead of tile clear granularity which is used by default. This is done to mimic pipegen_v1 calculation,
        // even though the initial calculated value will prevent deadlocks in unpacker.
        // TODO: In future, after verifying current solution, try skipping this step and find out consequences.

        // Output node to be used for data flow info queries.
        const RGBaseNode* output_node = pipe->get_output_nodes()[0];
        bool should_recalculate =
            can_use_single_unpacker_stream &&
            data_flow_info.get_tiles_to_send(output_node) > constants::general_max_num_tiles_per_phase;

        if (should_recalculate)
        {
            return recalculate_max_num_tiles_per_phase(
                tiles_per_input, output_node->get_size_tiles(), data_flow_info.get_num_iterations_in_epoch(pipe));
        }
        else
        {
            return data_flow_info.get_max_num_tiles_per_phase(output_node);
        }
    }

    void DramReadCommonStreamsCreator::configure_dram_receiving_stream(
        StreamNode* dram_receiving_stream,
        std::vector<NcriscConfig>&& ncrisc_configs,
        const RGBasePipe* pipe,
        const DramInputNode* dram_input_node,
        const DataFlowInfo& data_flow_info,
        const bool should_scale_up_stream,
        const unsigned int max_dram_input_buffer_size_tiles,
        unsigned int& max_num_tiles_per_phase)
    {
        if (should_scale_up_stream)
        {
            scale_up_dram_receiving_stream(dram_receiving_stream,
                                           pipe,
                                           data_flow_info,
                                           max_num_tiles_per_phase,
                                           dram_input_node->get_tile_size(),
                                           max_dram_input_buffer_size_tiles);
        }

        configure_dram_or_pcie_receiving_stream(
            dram_receiving_stream, std::move(ncrisc_configs), pipe, dram_input_node, data_flow_info,
            max_num_tiles_per_phase);

        check_dram_input_buffer_size_constraints(dram_receiving_stream, get_unpacker_output_node(pipe), 
                                                 max_dram_input_buffer_size_tiles, dram_input_node->get_tile_size());
    }

    void DramReadCommonStreamsCreator::configure_pcie_receiving_stream(
        StreamNode* pcie_receiving_stream,
        NcriscConfig&& ncrisc_config,
        const RGBasePipe* pipe,
        const PCIeStreamingNode* pcie_streaming_node,
        const DataFlowInfo& data_flow_info,
        const bool should_scale_up_stream,
        unsigned int& max_num_tiles_per_phase)
    {
        if (should_scale_up_stream)
        {
            scale_up_pcie_receiving_stream(pcie_receiving_stream, pcie_streaming_node);
        }

        configure_dram_or_pcie_receiving_stream(
            pcie_receiving_stream, {std::move(ncrisc_config)}, pipe, pcie_streaming_node, data_flow_info,
            max_num_tiles_per_phase);
    }

    void DramReadCommonStreamsCreator::configure_dram_or_pcie_receiving_stream(
        StreamNode* dram_or_pcie_receiving_stream,
        std::vector<NcriscConfig>&& ncrisc_configs,
        const RGBasePipe* pipe,
        const RGBaseNode* input_node,
        const DataFlowInfo& data_flow_info,
        unsigned int& max_num_tiles_per_phase)
    {
        dram_or_pcie_receiving_stream->set_ncrisc_configs(std::move(ncrisc_configs));

        // We must recalculate max_num_tiles_per_phase to ensure phase tiles count divisibility by stream's buffer size.
        // This will enable us to have transfers aligned with buffer read pointers, and thus we will be able to read new
        // tiles from DRAM without waiting for phase to end.
        unsigned int rec_stream_buffer_size_tiles =
                dram_or_pcie_receiving_stream->get_base_config().get_buffer_size().value() /
                dram_or_pcie_receiving_stream->get_base_config().get_msg_size().value();

        const unsigned int num_iterations_in_epoch = data_flow_info.get_num_iterations_in_epoch(pipe);

        const RGBaseNode* output_node = pipe->get_output_nodes().front();
        if (data_flow_info.get_tiles_to_send(output_node) > constants::general_max_num_tiles_per_phase)
        {
            // When we cross 2K limit it is guaranteed to have transfer split in multiple phases, and we must ensure
            // divisibility by subtree divisor (consume granularity at the receiving end of data flow graph).
            rec_stream_buffer_size_tiles = std::lcm(rec_stream_buffer_size_tiles,
                                                    data_flow_info.get_subtree_divisor(pipe));
        }
        max_num_tiles_per_phase = recalculate_max_num_tiles_per_phase(input_node->get_tiles_per_input(),
                                                                      rec_stream_buffer_size_tiles,
                                                                      num_iterations_in_epoch);

        const std::vector<PhaseInfo> input_phases = get_flattened_input_phases(pipe, data_flow_info);
        log_assert(input_phases.size() > 0, "Expecting at least one phase to configure");

        update_stream_with_merged_decomposed_phases(
            dram_or_pcie_receiving_stream, input_node, input_phases, data_flow_info, num_iterations_in_epoch,
            max_num_tiles_per_phase);
    }

    void DramReadCommonStreamsCreator::scale_up_dram_receiving_stream(StreamNode* stream,
                                                                      const RGBasePipe* pipe,
                                                                      const DataFlowInfo& data_flow_info,
                                                                      unsigned int max_num_tiles_per_phase,
                                                                      unsigned int dram_input_node_tile_size_bytes,
                                                                      unsigned int max_dram_input_buffer_size_tiles)
    {
        const unsigned int base_buffer_size_bytes = stream->get_base_config().get_buffer_size().value();
            
        const unsigned int max_num_tiles_per_phase_bytes = max_num_tiles_per_phase * dram_input_node_tile_size_bytes;

        unsigned int scale_factor = 1;

        if (max_dram_input_buffer_size_tiles > 0)
        {
            // Heuristics taken from legacy code - first, try to scale dram stream buffer all the way up to max dram
            // buffer size (since this field is provided by the frontend, we need to ensure that scale factor is at
            // least 1). Next, ensure divisibility between the upscaled buffer size and max_num_tiles_per_phase_bytes.
            // TODO: We should probably report a warning if FE provided too small max_dram_input_buffer_size_tiles,
            // in which case scale_factor would be 0.
            scale_factor = std::max(
                1u, max_dram_input_buffer_size_tiles * dram_input_node_tile_size_bytes / base_buffer_size_bytes);
            while (scale_factor > 1)
            {
                if (max_num_tiles_per_phase_bytes % (scale_factor * base_buffer_size_bytes) == 0)
                {
                    break;
                }
                --scale_factor;
            }
        }
        else
        {
            // Note that min_buffer_size in the legacy pipegen is set using constant
            // DRAM_INPUT_STREAM_MAX_PENDING_READ_BYTES. This does not mean that it is bound with that value, it is just
            // arbitrarily picked up threshold. Therefore we define another constant here.
            // This logic mimics heuristics from the legacy code, but it doesn't mean it is correct way to size the
            // buffer, but we know that it will bump up the perf.
            // TODO: Find out the logic behind this heuristic and document it here.
            const unsigned int min_buffer_size = pipe->is_ethernet_pipe() ?
                constants::eth_dram_rec_stream_min_size_bytes : constants::dram_rec_stream_min_size_bytes;
            while ((scale_factor + 1) * base_buffer_size_bytes < min_buffer_size)
            {
                if (scale_factor * base_buffer_size_bytes >= min_buffer_size / 2 &&
                    max_num_tiles_per_phase_bytes % (scale_factor * base_buffer_size_bytes) == 0)
                {
                    break;
                }
                ++scale_factor;
            }
        }

        const unsigned int pipe_reduce_mem_usage_threshold =
            m_stream_creator->calculate_pipe_reduce_mem_usage_threshold(
                max_dram_input_buffer_size_tiles, pipe->is_ethernet_pipe(), dram_input_node_tile_size_bytes);

        if (scale_factor == 1 && (2 * base_buffer_size_bytes <= pipe_reduce_mem_usage_threshold) &&
            max_num_tiles_per_phase_bytes % (2 * base_buffer_size_bytes) == 0)
        {
            scale_factor = 2;
        }

        stream->get_base_config().set_buffer_size(base_buffer_size_bytes * scale_factor);
    }

    void DramReadCommonStreamsCreator::scale_up_pcie_receiving_stream(StreamNode* stream,
                                                                      const PCIeStreamingNode* pcie_streaming_node)
    {
        const unsigned int scale_factor = pcie_streaming_node->get_num_queue_slots();
        const unsigned int base_buffer_size_bytes = stream->get_base_config().get_buffer_size().value();

        stream->get_base_config().set_buffer_size(base_buffer_size_bytes * scale_factor);
    }

    unsigned int DramReadCommonStreamsCreator::recalculate_max_num_tiles_per_phase(
        unsigned int tiles_per_input,
        unsigned int buffer_size_tiles,
        unsigned int num_iters_in_epoch)
    {
        // In this special case, pipegen1 defaults to 2048.
        // TODO: investigate when this happens and if something like return std::lcm(tiles_per_input, subtree_divisor)
        // makes more sense in that case.
        if (num_iters_in_epoch == 1 && buffer_size_tiles > constants::general_max_num_tiles_per_phase)
        {
            return constants::general_max_num_tiles_per_phase;
        }

        unsigned int num_divisor_tiles = buffer_size_tiles;

        if (tiles_per_input <= constants::general_max_num_tiles_per_phase)
        {
            unsigned int lcm_with_tiles_per_input = std::lcm(num_divisor_tiles, tiles_per_input);
            if (lcm_with_tiles_per_input > constants::general_max_num_tiles_per_phase)
            {
                // TODO: investigate when and why this happens and what can be done to mitigate it.
                log_warning(tt::LogPipegen2,
                            "Pipegen cannot ensure divisibility with num_tiles_per_input, fork-join paths may hang");
            }
            else
            {
                num_divisor_tiles = lcm_with_tiles_per_input;
            }
        }

        unsigned int recalculated_max_num_tiles_per_phase =
            (constants::general_max_num_tiles_per_phase / num_divisor_tiles) * num_divisor_tiles;

        log_assert(recalculated_max_num_tiles_per_phase > 0 &&
                   recalculated_max_num_tiles_per_phase <= constants::general_max_num_tiles_per_phase,
                   "Phases cannot be split to satisfy divisibility");

        return recalculated_max_num_tiles_per_phase;
    }

    std::vector<PhaseInfo> DramReadCommonStreamsCreator::get_flattened_input_phases(
        const RGBasePipe* pipe,
        const DataFlowInfo& data_flow_info)
    {
        // Flatten all input phases into a single vector.
        std::vector<PhaseInfo> input_phases;
        for (const RGBaseNode* source_buffer: pipe->get_unique_input_nodes())
        {
            const std::vector<PhaseInfo>& current_edge_phases = data_flow_info.get_edge_phases(source_buffer, pipe);
            input_phases.insert(input_phases.end(), current_edge_phases.begin(), current_edge_phases.end());
        }

        // Sort phases by phase offset in ascending order.
        std::sort(input_phases.begin(), input_phases.end(), [](const PhaseInfo& left, const PhaseInfo& right)
        {
            return left.phase_offset < right.phase_offset;
        });

        return input_phases;
    }

    void DramReadCommonStreamsCreator::configure_relay_to_unpacker_stream(
        StreamNode* relay_to_unpacker_stream,
        const RGBasePipe* pipe,
        const RGBaseNode* input_node,
        const UnpackerOutputNode* unpacker_node,
        const DataFlowInfo& data_flow_info,
        const unsigned int max_num_tiles_per_phase)
    {
        const std::vector<PhaseInfo>& unpacker_phases = data_flow_info.get_edge_phases(pipe, unpacker_node);

        const unsigned int num_iterations = data_flow_info.get_num_iterations_in_epoch(pipe);

        update_stream_with_merged_decomposed_phases(
            relay_to_unpacker_stream, input_node, unpacker_phases, data_flow_info, num_iterations,
            max_num_tiles_per_phase);
    }

    void DramReadCommonStreamsCreator::connect_dram_or_pcie_relay_to_unpacker_streams(
        const std::vector<std::unique_ptr<StreamNode>>& created_streams)
    {
        StreamNode* dram_or_pcie_receiving_stream = created_streams[0].get();
        StreamNode* unpacker_stream = created_streams[1].get();

        m_stream_creator->configure_stream_src_and_dest(dram_or_pcie_receiving_stream,
                                                        nullptr /* source_stream */,
                                                        unpacker_stream);
        m_stream_creator->configure_stream_src_and_dest(unpacker_stream,
                                                        dram_or_pcie_receiving_stream,
                                                        nullptr /* destination_stream */);
    }

    std::unique_ptr<StreamNode> DramReadCommonStreamsCreator::create_multicast_unpacker_stream(
            const RGBasePipe* pipe,
            const RGBaseNode* input_node,
            const UnpackerOutputNode* unpacker_node,
            const DataFlowInfo& data_flow_info,
            const unsigned int max_num_tiles_per_phase)
    {
        std::unique_ptr<StreamNode> unpacker_stream = std::make_unique<StreamNode>(
            StreamType::Unpacker,
            unpacker_node->get_physical_location(),
            unpacker_node->get_operand_id());

        m_stream_creator->configure_noc_to_unpacker_stream(unpacker_node, unpacker_stream.get());

        const std::vector<PhaseInfo>& phases_info = data_flow_info.get_edge_phases(pipe, pipe->get_output_nodes()[0]);
        const unsigned int num_iterations_in_epoch = data_flow_info.get_num_iterations_in_epoch(pipe);

        update_stream_with_merged_decomposed_phases(unpacker_stream.get(), input_node, phases_info, data_flow_info,
                                                    num_iterations_in_epoch, max_num_tiles_per_phase);

        return unpacker_stream;
    }

    void DramReadCommonStreamsCreator::check_dram_input_buffer_size_constraints(
        const StreamNode* stream_node, 
        const UnpackerOutputNode* unpacker_node,
        unsigned int max_dram_input_buffer_size_tiles,
        unsigned int dram_input_node_tile_size_bytes)
    {
        if (max_dram_input_buffer_size_tiles == 0)
        {
            return;
        }

        unsigned int stream_node_buf_size_tiles = stream_node->get_base_config().get_buffer_size().value() /
                                                  dram_input_node_tile_size_bytes;

        if (stream_node->get_stream_type() == StreamType::Relay)
        {
            log_assert(stream_node_buf_size_tiles <= max_dram_input_buffer_size_tiles,
                       "DRAM relay input stream {} allocates a buffer of {} tiles, which is bigger than "
                       "the limit of {} tiles", stream_node->get_stream_id(),
                       stream_node_buf_size_tiles, max_dram_input_buffer_size_tiles);
        }
        else if (stream_node->get_stream_type() == StreamType::Unpacker)
        {
            unsigned int unpacker_node_buf_size_tiles = unpacker_node->get_size_tiles();
            unsigned int max_buf_size_tiles = std::max(max_dram_input_buffer_size_tiles,
                                                       unpacker_node_buf_size_tiles);
            
            log_assert(stream_node_buf_size_tiles <= max_buf_size_tiles,
                       "DRAM unpacker input stream {} allocates a buffer of {} tiles, which is bigger than both "
                       "the unpacker buffer size in tiles {} and PyBuda limit of {} tiles",
                       stream_node->get_stream_id(), stream_node_buf_size_tiles, unpacker_node_buf_size_tiles,
                       max_dram_input_buffer_size_tiles);
        }
    }

}