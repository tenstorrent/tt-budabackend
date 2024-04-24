// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/rational_graph/rational_graph_creator.h"

#include <stack>
#include <stdexcept>

#include "device/resource_manager.h"
#include "device/tt_xy_pair.h"

#include "data_flow_calculator/pcie_flow_calculator.h"
#include "device/l1/l1_buffer.h"
#include "graph_creator/rational_graph/gather_optimization_manager.h"
#include "graph_creator/rational_graph/pg_subgraph_finder.h"
#include "model/rational_graph/nodes/dram_embedding_index_input_node.h"
#include "model/rational_graph/nodes/dram_embedding_table_input_node.h"
#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/nodes/dram_output_node.h"
#include "model/rational_graph/nodes/dram_output_intermed_node.h"
#include "model/rational_graph/nodes/ethernet_relay_node.h"
#include "model/rational_graph/nodes/non_shared_intermed_node.h"
#include "model/rational_graph/nodes/packer_input_node.h"
#include "model/rational_graph/nodes/pcie_streaming_node.h"
#include "model/rational_graph/nodes/relay_node.h"
#include "model/rational_graph/nodes/serial_fork_node.h"
#include "model/rational_graph/nodes/unpacker_output_node.h"
#include "model/rational_graph/nodes/virtual_node.h"
#include "model/rational_graph/nodes/shared_packer_intermed_node.h"
#include "model/rational_graph/pipes/direct/dram_output_intermed_pipe.h"
#include "model/rational_graph/pipes/direct/dram_prefetch_post_tm_unicast_pipe.h"
#include "model/rational_graph/pipes/direct/dram_tilizer_pipe.h"
#include "model/rational_graph/pipes/direct/dram_unicast_pipe.h"
#include "model/rational_graph/pipes/direct/ethernet_fw_relay_pipe.h"
#include "model/rational_graph/pipes/direct/ethernet_relay_pipe.h"
#include "model/rational_graph/pipes/direct/non_shared_intermed_pipe.h"
#include "model/rational_graph/pipes/direct/pcie_unicast_pipe.h"
#include "model/rational_graph/pipes/direct/relay_pipe.h"
#include "model/rational_graph/pipes/direct/unicast_pipe.h"
#include "model/rational_graph/pipes/direct/unicast_to_dram_pipe.h"
#include "model/rational_graph/pipes/direct/unicast_to_pcie_pipe.h"
#include "model/rational_graph/pipes/fork/dram_multicast_pipe.h"
#include "model/rational_graph/pipes/fork/dram_parallel_fork_pipe.h"
#include "model/rational_graph/pipes/fork/dram_prefetch_pre_tm_parallel_fork_pipe.h"
#include "model/rational_graph/pipes/fork/multicast_pipe.h"
#include "model/rational_graph/pipes/fork/padding_serial_fork_pipe.h"
#include "model/rational_graph/pipes/fork/parallel_fork_pipe.h"
#include "model/rational_graph/pipes/fork/pcie_multicast_pipe.h"
#include "model/rational_graph/pipes/fork/serial_fork_pipe.h"
#include "model/rational_graph/pipes/join/dormant_relay_pipe.h"
#include "model/rational_graph/pipes/join/dram_embedding_pipe.h"
#include "model/rational_graph/pipes/join/dram_gather_pipe.h"
#include "model/rational_graph/pipes/join/dram_prefetch_post_tm_gather_pipe.h"
#include "model/rational_graph/pipes/join/gather_pipe.h"
#include "model/rational_graph/pipes/join/gather_to_dram_pipe.h"
#include "model/rational_graph/pipes/join/gather_to_pcie_pipe.h"
#include "model/rational_graph/pipes/join/shared_packer_intermed_pipe.h"
#include "model/rational_graph/pipes/join/union_pipe.h"
#include "model/rational_graph/rational_graph.h"
#include "pipegen2_constants.h"
#include "pipegen2_exceptions.h"
#include "utils/logger.hpp"

namespace pipegen2
{
    std::vector<std::unique_ptr<RationalGraph>> RationalGraphCreator::create_rational_graphs(
        const PipeGraph& pipe_graph,
        ResourceManager* resource_manager)
    {
        PGSubgraphFinder pg_subgraph_finder;
        std::vector<PGSubgraph> pg_subgraphs = pg_subgraph_finder.find_subgraphs(pipe_graph);

        std::vector<std::unique_ptr<RationalGraph>> rational_graphs;
        for (const PGSubgraph& pg_subgraph : pg_subgraphs)
        {
            rational_graphs.push_back(create_rational_graph(&pg_subgraph, resource_manager));
        }

        manage_gather_optimizations(resource_manager, rational_graphs);

        allocate_kernel_input_index(resource_manager, rational_graphs);
        allocate_kernel_output_index(resource_manager, rational_graphs);

        return rational_graphs;
    }

    std::unique_ptr<RationalGraph> RationalGraphCreator::create_rational_graph(const PGSubgraph* pg_subgraph,
                                                                               ResourceManager* resource_manager)
    {
        std::vector<std::unique_ptr<RGBaseNode>> rational_graph_nodes = create_rational_graph_nodes(
            pg_subgraph->get_buffers(), resource_manager);

        std::vector<std::unique_ptr<RGBasePipe>> rational_graph_pipes;
        for (const PGPipe* subgraph_pipe : pg_subgraph->get_pipes())
        {
            create_rational_graph_pipes(subgraph_pipe, rational_graph_pipes, rational_graph_nodes,
                                        resource_manager->get_soc_info());
        }

        handle_forked_buffers(pg_subgraph->get_buffers(), rational_graph_pipes, rational_graph_nodes,
                              resource_manager->get_soc_info());

        create_relay_nodes(pg_subgraph->get_buffers(), rational_graph_nodes);

        std::unique_ptr<RationalGraph> rational_graph = std::make_unique<RationalGraph>(
            std::move(rational_graph_nodes),
            std::move(rational_graph_pipes),
            is_subgraph_doing_pcie_transfer(pg_subgraph));

        resource_manager->validate_rational_graph_resources(rational_graph.get());

        return rational_graph;
    }

    std::vector<std::unique_ptr<RGBaseNode>> RationalGraphCreator::create_rational_graph_nodes(
        const std::vector<const PGBuffer*>& subgraph_buffers,
        ResourceManager* resource_manager)
    {
        std::vector<std::unique_ptr<RGBaseNode>> rational_graph_nodes;

        m_node_from_buffer.clear();
        for (const PGBuffer* subgraph_buffer : subgraph_buffers)
        {
            rational_graph_nodes.push_back(create_rational_graph_node(subgraph_buffer, resource_manager));
            m_node_from_buffer[subgraph_buffer] = rational_graph_nodes.back().get();
        }

        return rational_graph_nodes;
    }

    std::unique_ptr<RGBaseNode> RationalGraphCreator::create_rational_graph_node(const PGBuffer* subgraph_buffer,
                                                                                 ResourceManager* resource_manager)
    {
        if (subgraph_buffer->is_intermediate_operand())
        {
            return create_intermed_node(subgraph_buffer, resource_manager);
        }
        else if (subgraph_buffer->has_no_input())
        {
            return create_input_node(subgraph_buffer, resource_manager);
        }
        else if (subgraph_buffer->has_no_outputs() || subgraph_buffer->is_embedding_index())
        {
            return create_output_node(subgraph_buffer, resource_manager);
        }
        else if (subgraph_buffer->is_relay())
        {
            return create_relay_node(subgraph_buffer, resource_manager->get_soc_info());
        }
        else
        {
            throw InvalidPipeGraphSpecificationException(
                "Unsupported input buffer type for buffer " +
                std::to_string(subgraph_buffer->get_id()) + ". This buffer was not recognized as either intermediate, "
                "input buffer, output buffer nor a relay buffer.",
                subgraph_buffer->get_logical_location());
        }
    }

    std::unique_ptr<BaseInputNode> RationalGraphCreator::create_input_node(const PGBuffer* input_buffer,
                                                                           ResourceManager* resource_manager)
    {
        const tt_cxy_pair physical_location = get_physical_location(input_buffer, resource_manager->get_soc_info());
        if (input_buffer->is_dram_input())
        {
            if (input_buffer->is_embedding_table() || input_buffer->is_hw_tilize())
            {
                return std::make_unique<DramEmbeddingTableInputNode>(
                    input_buffer->get_id(),
                    physical_location,
                    input_buffer->get_size_tiles(),
                    input_buffer->get_tile_size(),
                    input_buffer->get_num_epoch_tiles(),
                    input_buffer->get_num_tiles_per_input(),
                    input_buffer->get_operand_id(),
                    input_buffer->get_replicate_count(),
                    input_buffer->get_scatter_gather_num_tiles(),
                    input_buffer->get_num_queue_slots(),
                    input_buffer->get_dram_ram_flag(),
                    input_buffer->get_dram_address(),
                    input_buffer->get_dram_channel(),
                    input_buffer->get_dram_sub_channel(),
                    input_buffer->get_dram_io_flag_is_remote(),
                    input_buffer->get_embedding_table_core_c_div(),
                    input_buffer->get_embedding_table_row_size_per_core(),
                    input_buffer->get_untilized_output_c_dim(),
                    input_buffer->get_untilized_output_tile_dim_r(),
                    input_buffer->get_untilized_output_tile_dim_c(),
                    input_buffer->get_tilize_mblock_n_loop_num_rows(),
                    get_input_dram_buffer_type(input_buffer));
            }
            else if (input_buffer->is_embedding_index())
            {
                return std::make_unique<DramEmbeddingIndexInputNode>(
                    input_buffer->get_id(),
                    physical_location,
                    input_buffer->get_size_tiles(),
                    input_buffer->get_tile_size(),
                    input_buffer->get_num_epoch_tiles(),
                    input_buffer->get_num_tiles_per_input(),
                    input_buffer->get_operand_id(),
                    input_buffer->get_replicate_count(),
                    input_buffer->get_scatter_gather_num_tiles(),
                    input_buffer->get_num_queue_slots(),
                    input_buffer->get_dram_ram_flag(),
                    input_buffer->get_dram_address(),
                    input_buffer->get_dram_channel(),
                    input_buffer->get_dram_sub_channel(),
                    input_buffer->get_dram_io_flag_is_remote(),
                    input_buffer->get_embedding_indices_per_input(),
                    input_buffer->get_embedding_indices_per_tile(),
                    get_input_dram_buffer_type(input_buffer));
            }
            else
            {
                return std::make_unique<DramInputNode>(
                    input_buffer->get_id(),
                    physical_location,
                    input_buffer->get_size_tiles(),
                    input_buffer->get_tile_size(),
                    input_buffer->get_num_epoch_tiles(),
                    input_buffer->get_num_tiles_per_input(),
                    input_buffer->get_operand_id(),
                    input_buffer->get_replicate_count(),
                    input_buffer->get_scatter_gather_num_tiles(),
                    input_buffer->get_num_queue_slots(),
                    input_buffer->get_dram_ram_flag(),
                    input_buffer->get_dram_address(),
                    input_buffer->get_dram_channel(),
                    input_buffer->get_dram_sub_channel(),
                    input_buffer->get_dram_io_flag_is_remote(),
                    get_input_dram_buffer_type(input_buffer),
                    input_buffer->is_padding());
            }
        }
        else if (input_buffer->is_output_operand())
        {
            return std::make_unique<PackerInputNode>(
                input_buffer->get_id(),
                physical_location,
                input_buffer->get_size_tiles(),
                input_buffer->get_tile_size(),
                input_buffer->get_num_epoch_tiles(),
                input_buffer->get_num_tiles_per_input(),
                input_buffer->get_operand_id(),
                input_buffer->get_replicate_count(),
                input_buffer->get_scatter_gather_num_tiles(),
                input_buffer->get_moves_raw_data(),
                input_buffer->get_shared_space_buffer_id(),
                RGBaseNode::BlockingParams(input_buffer->get_mblock_m(),
                                           input_buffer->get_mblock_n(),
                                           input_buffer->get_mblock_k(),
                                           input_buffer->get_ublock_rt(),
                                           input_buffer->get_ublock_ct()),
                input_buffer->get_op_name());
        }
        else
        {
            throw InvalidPipeGraphSpecificationException(
                "Unsupported input buffer type for buffer " +
                std::to_string(input_buffer->get_id()) + ". This input buffer was not recognized as neither dram"
                "input buffer nor packer input buffer.",
                input_buffer->get_logical_location(),
                physical_location);
        }
    }

    std::unique_ptr<BaseOutputNode> RationalGraphCreator::create_output_node(const PGBuffer* output_buffer,
                                                                             ResourceManager* resource_manager)
    {
        tt_cxy_pair physical_location = get_physical_location(output_buffer, resource_manager->get_soc_info());
        if (output_buffer->is_dram_output())
        {
            return std::make_unique<DramOutputNode>(
                output_buffer->get_id(),
                physical_location,
                output_buffer->get_size_tiles(),
                output_buffer->get_tile_size(),
                output_buffer->get_num_epoch_tiles(),
                output_buffer->get_num_tiles_per_input(),
                output_buffer->get_operand_id(),
                output_buffer->get_scatter_gather_num_tiles(),
                output_buffer->get_num_queue_slots(),
                output_buffer->get_dram_ram_flag(),
                output_buffer->get_dram_address(),
                output_buffer->get_dram_channel(),
                output_buffer->get_dram_sub_channel(),
                output_buffer->get_dram_io_flag_is_remote(),
                output_buffer->get_moves_raw_data(),
                output_buffer->get_untilized_output_r_dim(),
                output_buffer->get_untilized_output_c_dim(),
                output_buffer->get_untilized_output_z_dim(),
                output_buffer->get_untilized_output_full_r_dim(),
                output_buffer->get_untilized_output_full_c_dim(),
                output_buffer->get_untilized_output_tile_dim_r(),
                output_buffer->get_untilized_output_tile_dim_c());
        }
        else if (output_buffer->is_input_operand())
        {
            // Unpacker nodes have an optional attribute which indicates we should allocate additional memory for
            // overlay blob on core. Attibute specifies full overlay blob size, and if it exceeds default blob size,
            // that means we have to allocate extra space at the beginning of L1 data buffers space on core.
            const L1Buffer* extra_overlay_blob_space = resource_manager->allocate_l1_extra_overlay_blob_space(
                physical_location, output_buffer->get_overlay_blob_size());

            unsigned int extra_overlay_blob_space_in_bytes =
                extra_overlay_blob_space != nullptr ? extra_overlay_blob_space->get_size() : 0;

            return std::make_unique<UnpackerOutputNode>(
                output_buffer->get_id(),
                output_buffer->get_op_name(),
                physical_location,
                output_buffer->get_size_tiles(),
                output_buffer->get_tile_size(),
                output_buffer->get_num_epoch_tiles(),
                output_buffer->get_num_tiles_per_input(),
                output_buffer->get_operand_id(),
                output_buffer->get_tile_clear_granularity(),
                output_buffer->get_scatter_gather_num_tiles(),
                output_buffer->is_embedding_index(),
                extra_overlay_blob_space_in_bytes);
        }
        else
        {
            throw InvalidPipeGraphSpecificationException(
                "Unsupported output buffer type for buffer " +
                std::to_string(output_buffer->get_id()) + ". This input buffer was not recognized as neither dram"
                "output buffer nor unpacker output buffer.",
                output_buffer->get_logical_location(),
                physical_location);
        }
    }

    std::unique_ptr<BaseIntermedNode> RationalGraphCreator::create_intermed_node(const PGBuffer* intermed_buffer,
                                                                                 ResourceManager* resource_manager)
    {
        tt_cxy_pair physical_location = get_physical_location(intermed_buffer, resource_manager->get_soc_info());

        if (intermed_buffer->get_type() == BufferType::kGradientOp)
        {
            return std::make_unique<DramOutputIntermedNode>(
                intermed_buffer->get_id(),
                physical_location,
                intermed_buffer->get_size_tiles(),
                intermed_buffer->get_tile_size(),
                intermed_buffer->get_num_epoch_tiles(),
                intermed_buffer->get_operand_id(),
                intermed_buffer->get_tile_clear_granularity(),
                intermed_buffer->get_scatter_gather_num_tiles(),
                intermed_buffer->get_dram_ram_flag(),
                intermed_buffer->get_dram_address(),
                intermed_buffer->get_dram_channel(),
                intermed_buffer->get_dram_sub_channel(),
                intermed_buffer->get_dram_io_flag_is_remote(),
                RGBaseNode::BlockingParams(intermed_buffer->get_mblock_m(),
                                           intermed_buffer->get_mblock_n(),
                                           intermed_buffer->get_mblock_k(),
                                           intermed_buffer->get_ublock_rt(),
                                           intermed_buffer->get_ublock_ct()));
        }
        else if (intermed_buffer->get_type() == BufferType::kIntermediate)
        {
            if (intermed_buffer->shares_l1_space())
            {
                return std::make_unique<SharedPackerIntermedNode>(
                    intermed_buffer->get_id(),
                    physical_location,
                    intermed_buffer->get_size_tiles(),
                    intermed_buffer->get_tile_size(),
                    intermed_buffer->get_num_epoch_tiles(),
                    intermed_buffer->get_operand_id(),
                    intermed_buffer->get_tile_clear_granularity(),
                    intermed_buffer->get_scatter_gather_num_tiles(),
                    RGBaseNode::BlockingParams(intermed_buffer->get_mblock_m(),
                                               intermed_buffer->get_mblock_n(),
                                               intermed_buffer->get_mblock_k(),
                                               intermed_buffer->get_ublock_rt(),
                                               intermed_buffer->get_ublock_ct()));
            }
            else
            {
                return std::make_unique<NonSharedIntermedNode>(
                    intermed_buffer->get_id(),
                    physical_location,
                    intermed_buffer->get_size_tiles(),
                    intermed_buffer->get_tile_size(),
                    intermed_buffer->get_num_epoch_tiles(),
                    intermed_buffer->get_operand_id(),
                    intermed_buffer->get_tile_clear_granularity(),
                    intermed_buffer->get_scatter_gather_num_tiles(),
                    RGBaseNode::BlockingParams(intermed_buffer->get_mblock_m(),
                                               intermed_buffer->get_mblock_n(),
                                               intermed_buffer->get_mblock_k(),
                                               intermed_buffer->get_ublock_rt(),
                                               intermed_buffer->get_ublock_ct()));
            }
        }
        else
        {
            throw InvalidPipeGraphSpecificationException(
                "Unsupported intermed buffer type for buffer " +
                std::to_string(intermed_buffer->get_id()) + ". This buffer was not recognized as either gradient op "
                "(dram output intermediate buffer), intermediate buffer which shares L1 space with a packer buffer "
                "(matmul op) nor intermediate which doesn't share space with packer buffer (fused op).",
                intermed_buffer->get_logical_location(),
                physical_location);
        }
    }

    std::unique_ptr<RGBaseNode> RationalGraphCreator::create_relay_node(const PGBuffer* relay_buffer,
                                                                        const SoCInfo* soc_info)
    {
        std::unique_ptr<RelayNode> relay_node;

        if (relay_buffer->get_type() == BufferType::kEthernetRelay)
        {
            relay_node = std::make_unique<EthernetRelayNode>(
                relay_buffer->get_id(),
                get_physical_location(relay_buffer, soc_info),
                relay_buffer->get_size_tiles(),
                relay_buffer->get_tile_size(),
                relay_buffer->get_num_epoch_tiles(),
                relay_buffer->get_num_tiles_per_input(),
                relay_buffer->get_operand_id(),
                relay_buffer->get_scatter_gather_num_tiles(),
                relay_buffer->get_use_ethernet_fw_stream()
            );
        }
        else
        {
            relay_node = std::make_unique<RelayNode>(
                relay_buffer->get_id(),
                get_physical_location(relay_buffer, soc_info),
                relay_buffer->get_size_tiles(),
                relay_buffer->get_tile_size(),
                relay_buffer->get_num_epoch_tiles(),
                relay_buffer->get_num_tiles_per_input(),
                relay_buffer->get_operand_id(),
                relay_buffer->get_scatter_gather_num_tiles()
            );
        }

        return relay_node;
    }

    void RationalGraphCreator::create_rational_graph_pipes(
        const PGPipe* subgraph_pipe,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        const SoCInfo* soc_info)
    {
        std::map<std::vector<PGBuffer*>, RGBasePipe*> pg_pipe_dest_list_to_rg_pipe;
        const unsigned int number_of_scattered_pipes = subgraph_pipe->get_scatter_fanout();
        std::unordered_set<RGBasePipe*> created_rg_pipes_from_subgraph_pipe;
        for (unsigned int scatter_index = 0; scatter_index < number_of_scattered_pipes; ++scatter_index)
        {
            create_rational_graph_pipes(subgraph_pipe,
                                        scatter_index,
                                        &created_rg_pipes_from_subgraph_pipe,
                                        rational_graph_pipes,
                                        rational_graph_nodes,
                                        pg_pipe_dest_list_to_rg_pipe,
                                        soc_info);
        }

        if (subgraph_pipe->is_pipe_scattering())
        {
            for (RGBasePipe* rg_pipe : created_rg_pipes_from_subgraph_pipe)
            {
                rg_pipe->set_is_part_of_scattered_pipe(true);
            }

            create_serial_fork_nodes_for_pipe_inputs(subgraph_pipe,
                                                     created_rg_pipes_from_subgraph_pipe,
                                                     soc_info,
                                                     rational_graph_pipes,
                                                     rational_graph_nodes);
        }
    }

    void RationalGraphCreator::create_rational_graph_pipes(
        const PGPipe* subgraph_pipe,
        const unsigned int scatter_index,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe,
        const SoCInfo* soc_info)
    {
        const std::vector<PGPipe::Input>& pg_pipe_inputs = subgraph_pipe->get_inputs(scatter_index);
        const std::vector<PGBuffer*>& pg_pipe_outputs = subgraph_pipe->get_output_buffers()[scatter_index];

        const tt_cxy_pair& pipe_logical_location =
            subgraph_pipe->get_mcast_core_logical_locations()[scatter_index * subgraph_pipe->get_consumer_repeat()];
        const tt_cxy_pair& pipe_physical_location = get_physical_location(
            subgraph_pipe, pipe_logical_location, soc_info);

        const bool pipe_has_single_input = subgraph_pipe->has_single_buffer_input();
        const bool pipe_has_single_output = pg_pipe_outputs.size() == 1;
        const bool pipe_reads_from_dram_after_reblocking = subgraph_pipe->has_non_prefetch_pre_tm_dram_input();
        const bool pipe_writes_to_dram = pg_pipe_outputs[0]->is_dram_output();
        const bool pipe_is_join_intermed = subgraph_pipe->is_join_intermediate_pipe();
        const bool pipe_is_dram_embedding = pg_pipe_inputs[0].get_buffer()->is_embedding_table();
        const bool should_decompose_inter_tensix_pipe =
            subgraph_pipe->is_connecting_l1_buffers() &&
            !can_directly_connect_l1_source_to_l1_dest(subgraph_pipe, soc_info) &&
            !pipe_is_join_intermed;

        if (subgraph_pipe->is_mmio_pipe())
        {
            decompose_mmio_pipe(subgraph_pipe,
                                scatter_index,
                                pipe_logical_location,
                                created_rg_pipes_from_subgraph_pipe,
                                rational_graph_pipes,
                                rational_graph_nodes,
                                soc_info,
                                pg_pipe_dest_list_to_rg_pipe);
        }
        else if (should_decompose_inter_tensix_pipe)
        {
            decompose_inter_tensix_pipe(subgraph_pipe,
                                        scatter_index,
                                        created_rg_pipes_from_subgraph_pipe,
                                        pipe_physical_location,
                                        rational_graph_pipes,
                                        rational_graph_nodes,
                                        pg_pipe_dest_list_to_rg_pipe);
        }
        else if (pipe_has_single_input && pipe_has_single_output)
        {
            create_direct_pipe(subgraph_pipe,
                               scatter_index,
                               created_rg_pipes_from_subgraph_pipe,
                               pipe_physical_location,
                               rational_graph_pipes,
                               rational_graph_nodes);
        }
        else if ((pipe_has_single_input || (pipe_reads_from_dram_after_reblocking && !pipe_has_single_output)))
        {
            create_multicast_pipe(subgraph_pipe,
                                  scatter_index,
                                  pipe_physical_location,
                                  soc_info,
                                  created_rg_pipes_from_subgraph_pipe,
                                  rational_graph_pipes);
        }
        else if (pipe_reads_from_dram_after_reblocking ||
                 pipe_writes_to_dram ||
                 pipe_is_join_intermed ||
                 pipe_is_dram_embedding)
        {
            create_join_pipe(subgraph_pipe,
                             scatter_index,
                             created_rg_pipes_from_subgraph_pipe,
                             pipe_physical_location,
                             rational_graph_pipes,
                             rational_graph_nodes);
        }
        else
        {
            throw InvalidPipeGraphSpecificationException(
                "Unsupported pipe type for pipe " + std::to_string(subgraph_pipe->get_id()) +
                " and scatter index " + std::to_string(scatter_index) + ". This pipe was not recognized to be either" +
                " MMIO pipe, inter-tensix pipe, direct pipe, multicast pipe nor join pipe.",
                pipe_logical_location,
                pipe_physical_location);
        }
    }

    void RationalGraphCreator::create_direct_pipe(
        const PGPipe* subgraph_pipe,
        const unsigned int scatter_index,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        const tt_cxy_pair& pipe_physical_location,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes)
    {
        std::unique_ptr<RGBasePipe> direct_pipe;
        const std::vector<PGPipe::Input>& pg_pipe_inputs = subgraph_pipe->get_inputs(scatter_index);
        const std::vector<PGBuffer*>& pg_pipe_outputs = subgraph_pipe->get_output_buffers()[scatter_index];
        const PGBuffer* pg_pipe_output = pg_pipe_outputs[0];
        RGPipeProperties rg_pipe_properties = create_rg_pipe_properties_from_pg_pipe(subgraph_pipe);

        const bool is_tilizer_pipe = pg_pipe_inputs[0].get_buffer()->is_hw_tilize();

        if (is_tilizer_pipe)
        {
            direct_pipe = std::make_unique<DramTilizerPipe>(std::move(rg_pipe_properties),
                                                            subgraph_pipe->get_dram_pipe_total_readers(),
                                                            subgraph_pipe->get_dram_pipe_reader_index(),
                                                            subgraph_pipe->get_op_input_dram_io_buf_size_tiles(),
                                                            pipe_physical_location,
                                                            pg_pipe_output->get_tilize_row_col_offset());
        }
        else if (subgraph_pipe->is_dram_prefetch_post_tm())
        {
            direct_pipe = std::make_unique<DramPrefetchPostTMUnicastPipe>(std::move(rg_pipe_properties),
                                                            subgraph_pipe->get_dram_pipe_total_readers(),
                                                            subgraph_pipe->get_dram_pipe_reader_index(),
                                                            subgraph_pipe->get_op_input_dram_io_buf_size_tiles(),
                                                            pipe_physical_location);
        }
        else if (subgraph_pipe->has_non_prefetch_pre_tm_dram_input())
        {
            direct_pipe = std::make_unique<DramUnicastPipe>(std::move(rg_pipe_properties),
                                                            subgraph_pipe->get_dram_pipe_total_readers(),
                                                            subgraph_pipe->get_dram_pipe_reader_index(),
                                                            subgraph_pipe->get_op_input_dram_io_buf_size_tiles(),
                                                            pipe_physical_location);
        }
        else if (pg_pipe_output->is_dram_output())
        {
            direct_pipe = std::make_unique<UnicastToDramPipe>(std::move(rg_pipe_properties),
                                                              pipe_physical_location);
        }
        else if (subgraph_pipe->is_direct_intermediate_pipe())
        {
            if (pg_pipe_output->get_type() == BufferType::kGradientOp)
            {
                direct_pipe = std::make_unique<DramOutputIntermedPipe>(subgraph_pipe->get_id());
            }
            else if (pg_pipe_output->get_type() == BufferType::kIntermediate)
            {
                direct_pipe = std::make_unique<NonSharedIntermedPipe>(subgraph_pipe->get_id());
            }
            else
            {
                throw InvalidPipeGraphSpecificationException(
                    "Unsupported intermediate pipe " + std::to_string(subgraph_pipe->get_id()) +
                    " and scatter index " + std::to_string(scatter_index) + ". The pipe is unsuppored because it is" +
                    " categorized as direct intermediate pipe, but its output buffer was not recognized to be either" +
                    " gradient op nor the intermediate which doesn't share buffer with the packer (fused op).",
                    subgraph_pipe->get_mcast_core_logical_locations()[scatter_index],
                    pipe_physical_location);
            }
        }
        else if (pg_pipe_output->is_relay())
        {
            direct_pipe = create_relay_pipe(std::move(rg_pipe_properties),
                                            pipe_physical_location,
                                            pg_pipe_inputs[0].get_buffer(),
                                            pg_pipe_output);
        }
        else
        {
            direct_pipe = std::make_unique<UnicastPipe>(std::move(rg_pipe_properties), pipe_physical_location);
        }

        connect_rg_pipe_with_input_nodes(direct_pipe.get(), pg_pipe_inputs, scatter_index);
        connect_rg_pipe_with_output_node(direct_pipe.get(), pg_pipe_output);

        created_rg_pipes_from_subgraph_pipe->insert(direct_pipe.get());
        rational_graph_pipes.push_back(std::move(direct_pipe));
    }

    std::unique_ptr<RGBasePipe> RationalGraphCreator::create_relay_pipe(RGPipeProperties&& rg_pipe_properties,
                                                                       const tt_cxy_pair& pipe_physical_location,
                                                                       const PGBuffer* input_buffer,
                                                                       const PGBuffer* relay_buffer)
    {
        if (input_buffer->get_logical_location().chip != relay_buffer->get_logical_location().chip)
        {
            log_assert(input_buffer->get_type() == BufferType::kEthernetRelay &&
                       relay_buffer->get_type() == BufferType::kEthernetRelay,
                       "Expecting Ethernet relay buffers for cross-chip relay pipe");
            return create_ethernet_relay_pipe(std::move(rg_pipe_properties), pipe_physical_location,
                                              relay_buffer->get_use_ethernet_fw_stream());
        }
        else
        {
            return std::make_unique<RelayPipe>(std::move(rg_pipe_properties), pipe_physical_location);
        }
    }

    std::unique_ptr<RGBasePipe> RationalGraphCreator::create_ethernet_relay_pipe(
        RGPipeProperties&& rg_pipe_properties,
        const tt_cxy_pair& pipe_physical_location,
        const bool is_fw_relay_pipe)
    {
        if (is_fw_relay_pipe)
        {
            return std::make_unique<EthernetFWRelayPipe>(std::move(rg_pipe_properties), pipe_physical_location);
        }
        else
        {
            return std::make_unique<EthernetRelayPipe>(std::move(rg_pipe_properties), pipe_physical_location);
        }
    }

    void RationalGraphCreator::create_multicast_pipe(
        const PGPipe* subgraph_pipe,
        const unsigned int scatter_index,
        const tt_cxy_pair& pipe_physical_location,
        const SoCInfo* soc_info,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes)
    {
        std::unique_ptr<RGBasePipe> fork_pipe;
        const std::vector<PGPipe::Input>& pg_pipe_inputs = subgraph_pipe->get_inputs(scatter_index);
        const std::vector<PGBuffer*>& pg_pipe_outputs = subgraph_pipe->get_output_buffers()[scatter_index];
        RGPipeProperties rg_pipe_properties = create_rg_pipe_properties_from_pg_pipe(subgraph_pipe);

        if (subgraph_pipe->has_non_prefetch_pre_tm_dram_input())
        {
            fork_pipe = std::make_unique<DramMulticastPipe>(
                std::move(rg_pipe_properties),
                subgraph_pipe->get_dram_pipe_total_readers(),
                subgraph_pipe->get_dram_pipe_reader_index(),
                subgraph_pipe->get_op_input_dram_io_buf_size_tiles(),
                pipe_physical_location);
        }
        else
        {
            fork_pipe = std::make_unique<MulticastPipe>(
                std::move(rg_pipe_properties),
                pipe_physical_location,
                is_packer_multicast_optimization_enabled(subgraph_pipe, soc_info));
        }

        connect_rg_pipe_with_input_nodes(fork_pipe.get(), pg_pipe_inputs, scatter_index);
        connect_rg_pipe_with_output_nodes(fork_pipe.get(), pg_pipe_outputs);

        created_rg_pipes_from_subgraph_pipe->insert(fork_pipe.get());
        rational_graph_pipes.push_back(std::move(fork_pipe));
    }

    void RationalGraphCreator::create_join_pipe(
        const PGPipe* subgraph_pipe,
        const unsigned int scatter_index,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        const tt_cxy_pair& pipe_physical_location,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes)
    {
        std::unique_ptr<RGBasePipe> join_pipe;
        const std::vector<PGPipe::Input>& pg_pipe_inputs = subgraph_pipe->get_inputs(scatter_index);
        const std::vector<PGBuffer*>& pg_pipe_outputs = subgraph_pipe->get_output_buffers()[scatter_index];
        RGPipeProperties rg_pipe_properties = create_rg_pipe_properties_from_pg_pipe(subgraph_pipe);

        const bool is_embedding_pipe = pg_pipe_inputs[0].get_buffer()->is_embedding_table();

        if (is_embedding_pipe)
        {
            // The embedding index is the last input of the embedding pipe. We need to wrap it into a virtual node and
            // then make a connection between embedding index pipe (a normal DramUnicast) and the new embedding pipe.
            const PGBuffer* embedding_index = pg_pipe_inputs.back().get_buffer();
            std::unique_ptr<VirtualNode> wrapped_embedding_index = wrap_into_virtual_node(embedding_index);
            join_pipe = std::make_unique<DramEmbeddingPipe>(std::move(rg_pipe_properties),
                                                            subgraph_pipe->get_dram_pipe_total_readers(),
                                                            subgraph_pipe->get_dram_pipe_reader_index(),
                                                            subgraph_pipe->get_op_input_dram_io_buf_size_tiles(),
                                                            pipe_physical_location);
            rational_graph_nodes.push_back(std::move(wrapped_embedding_index));
        }
        else if (subgraph_pipe->is_join_intermediate_pipe())
        {
            join_pipe = std::make_unique<SharedPackerIntermedPipe>(subgraph_pipe->get_id());
        }
        else if (subgraph_pipe->is_dram_prefetch_post_tm())
        {
            join_pipe = std::make_unique<DramPrefetchPostTMGatherPipe>(std::move(rg_pipe_properties),
                                                            subgraph_pipe->get_dram_pipe_total_readers(),
                                                            subgraph_pipe->get_dram_pipe_reader_index(),
                                                            subgraph_pipe->get_op_input_dram_io_buf_size_tiles(),
                                                            pipe_physical_location);
        }
        else if (subgraph_pipe->has_non_prefetch_pre_tm_dram_input())
        {
            join_pipe = std::make_unique<DramGatherPipe>(std::move(rg_pipe_properties),
                                                         subgraph_pipe->get_dram_pipe_total_readers(),
                                                         subgraph_pipe->get_dram_pipe_reader_index(),
                                                         subgraph_pipe->get_op_input_dram_io_buf_size_tiles(),
                                                         pipe_physical_location);
        }
        else
        {
            log_assert(pg_pipe_outputs[0]->is_dram_output(), "Expected dram output");
            join_pipe = std::make_unique<GatherToDramPipe>(std::move(rg_pipe_properties), pipe_physical_location);
        }

        created_rg_pipes_from_subgraph_pipe->insert(join_pipe.get());
        connect_rg_pipe_with_input_nodes(join_pipe.get(), pg_pipe_inputs, scatter_index);
        connect_rg_pipe_with_output_node(join_pipe.get(), pg_pipe_outputs[0]);
        rational_graph_pipes.push_back(std::move(join_pipe));
    }

    RGPipeProperties RationalGraphCreator::create_rg_pipe_properties_from_pg_pipe(const PGPipe* subgraph_pipe)
    {
        return RGPipeProperties(subgraph_pipe->get_id(),
                                subgraph_pipe->get_incoming_noc_id(),
                                subgraph_pipe->get_incoming_noc_vc(),
                                subgraph_pipe->get_outgoing_noc_id(),
                                subgraph_pipe->get_outgoing_noc_vc(),
                                subgraph_pipe->get_pipe_periodic_repeat(),
                                subgraph_pipe->get_consumer_repeat(),
                                subgraph_pipe->is_ethernet_pipe());
    }

    void RationalGraphCreator::decompose_inter_tensix_pipe(
        const PGPipe* subgraph_pipe,
        const unsigned int scatter_index,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        const tt_cxy_pair& pipe_physical_location,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe)
    {
        const std::vector<PGPipe::Input>& pg_pipe_inputs = subgraph_pipe->get_inputs(scatter_index);
        const std::vector<PGBuffer*>& pg_pipe_outputs = subgraph_pipe->get_output_buffers()[scatter_index];

        create_receiving_subgraph(subgraph_pipe,
                                  scatter_index,
                                  created_rg_pipes_from_subgraph_pipe,
                                  pg_pipe_inputs,
                                  pipe_physical_location,
                                  rational_graph_pipes,
                                  rational_graph_nodes);

        RGBaseNode* root_subgraph_output_node = rational_graph_nodes.back().get();

        RGBasePipe* sink_subgraph_input_pipe = get_sending_subgraph(subgraph_pipe,
                                                                    created_rg_pipes_from_subgraph_pipe,
                                                                    pg_pipe_inputs,
                                                                    pg_pipe_outputs,
                                                                    pipe_physical_location,
                                                                    rational_graph_pipes,
                                                                    rational_graph_nodes,
                                                                    pg_pipe_dest_list_to_rg_pipe);

        connect_rg_pipe_with_input_node(sink_subgraph_input_pipe, root_subgraph_output_node);
    }

    void RationalGraphCreator::create_receiving_subgraph(
        const PGPipe* subgraph_pipe,
        const unsigned int scatter_index,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        const std::vector<PGPipe::Input>& pg_pipe_inputs,
        const tt_cxy_pair& pipe_physical_location,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes)
    {
        std::unique_ptr<RGBasePipe> input_pipe;
        RGPipeProperties rg_pipe_properties = create_rg_pipe_properties_from_pg_pipe(subgraph_pipe);

        if (subgraph_pipe->has_single_buffer_input() || should_disable_gather_optimization_for_pipe(subgraph_pipe))
        {
            input_pipe = std::make_unique<DormantRelayPipe>(std::move(rg_pipe_properties), pipe_physical_location);
        }
        else
        {
            input_pipe = std::make_unique<GatherPipe>(std::move(rg_pipe_properties), pipe_physical_location);
        }

        // TODO: Check what would be the proper values for node parameters.
        // For now, pass tile size of the first input buffer, as it will be useful when inspecting the node.
        std::unique_ptr<VirtualNode> output_virtual_node = std::make_unique<VirtualNode>(
            pipe_physical_location,
            0 /* size_tiles */,
            pg_pipe_inputs[0].get_buffer()->get_tile_size(),
            0 /* num_epoch_tiles */,
            0 /* tiles_per_input */);

        connect_rg_pipe_with_input_nodes(input_pipe.get(), pg_pipe_inputs, scatter_index);
        input_pipe->add_output_node(output_virtual_node.get());
        output_virtual_node->set_input_pipe(input_pipe.get());

        created_rg_pipes_from_subgraph_pipe->insert(input_pipe.get());
        rational_graph_pipes.push_back(std::move(input_pipe));
        rational_graph_nodes.push_back(std::move(output_virtual_node));
    }

    bool RationalGraphCreator::should_disable_gather_optimization_for_pipe(const PGPipe* subgraph_pipe)
    {
        // Pipegen v1 doesn't create gather streams if consumer repeat is > 1. Instead, it creates a single relay
        // stream. Let's do the same for now. This stream may further be reconfigured to do mutlicast if needed.
        // TODO: evaluate if we could benefit from creating gather streams even if consumer repeat is > 1.
        return subgraph_pipe->has_consumer_duplicates() || subgraph_pipe->is_gather_optimization_disabled();
    }

    RGBasePipe* RationalGraphCreator::get_sending_subgraph(
        const PGPipe* subgraph_pipe,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        const std::vector<PGPipe::Input>& pg_pipe_inputs,
        const std::vector<PGBuffer*>& pg_pipe_outputs,
        const tt_cxy_pair& pipe_physical_location,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe)
    {
        const auto l1_sink_subgraph_it = pg_pipe_dest_list_to_rg_pipe.find(pg_pipe_outputs);
        if (l1_sink_subgraph_it != pg_pipe_dest_list_to_rg_pipe.end())
        {
            return l1_sink_subgraph_it->second;
        }

        create_sending_subgraph(subgraph_pipe,
                                created_rg_pipes_from_subgraph_pipe,
                                pg_pipe_inputs,
                                pg_pipe_outputs,
                                pipe_physical_location,
                                rational_graph_pipes,
                                rational_graph_nodes);

        RGBasePipe* l1_sink_graph_input_pipe = rational_graph_pipes.back().get();
        pg_pipe_dest_list_to_rg_pipe.emplace(pg_pipe_outputs, l1_sink_graph_input_pipe);

        return l1_sink_graph_input_pipe;
    }

    void RationalGraphCreator::create_sending_subgraph(
        const PGPipe* subgraph_pipe,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        const std::vector<PGPipe::Input>& pg_pipe_inputs,
        const std::vector<PGBuffer*>& pg_pipe_outputs,
        const tt_cxy_pair& pipe_physical_location,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes)
    {
        create_pipe_sending_to_destinations(subgraph_pipe,
                                            created_rg_pipes_from_subgraph_pipe,
                                            pg_pipe_inputs,
                                            pg_pipe_outputs,
                                            pipe_physical_location,
                                            rational_graph_pipes,
                                            rational_graph_nodes);

        if (subgraph_pipe->has_output_list_duplicates())
        {
            RGBasePipe* pipe_writing_to_l1 = rational_graph_pipes.back().get();

            std::unique_ptr<VirtualNode> union_output_node = std::make_unique<VirtualNode>(
                pipe_writing_to_l1->get_physical_location());
            connect_rg_pipe_with_input_node(pipe_writing_to_l1, union_output_node.get());

            create_union_pipe_for_rg_node(union_output_node.get(),
                                          created_rg_pipes_from_subgraph_pipe,
                                          rational_graph_pipes);
            rational_graph_nodes.push_back(std::move(union_output_node));
        }
    }

    void RationalGraphCreator::create_pipe_sending_to_destinations(
        const PGPipe* subgraph_pipe,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        const std::vector<PGPipe::Input>& pg_pipe_inputs,
        const std::vector<PGBuffer*>& pg_pipe_outputs,
        const tt_cxy_pair& pipe_physical_location,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes)
    {
        // pipe_periodic_repeat is important only to the Packer source subgraph, as it relates to inputs to the NxN pipe,
        // so we can set it to 1 in Multicast/Unicast/Relay pipes.
        RGPipeProperties derived_pipe_properties(
            subgraph_pipe->get_id(),
            subgraph_pipe->get_incoming_noc_id(),
            subgraph_pipe->get_incoming_noc_vc(),
            subgraph_pipe->get_outgoing_noc_id(),
            subgraph_pipe->get_outgoing_noc_vc(),
            1 /* pipe_periodic_repeat */,
            subgraph_pipe->get_consumer_repeat(),
            subgraph_pipe->get_ethernet_channel(),
            subgraph_pipe->is_ethernet_pipe());

        std::unique_ptr<RGBasePipe> derived_pipe;
        if (pg_pipe_outputs.size() > 1)
        {
            // N to N pipes are always multicasting, and multicast to DRAM is not supported.
            // Packer multicast optimization is disabled for decomposed inter tensix pipes.
            derived_pipe = std::make_unique<MulticastPipe>(std::move(derived_pipe_properties),
                                                           pipe_physical_location,
                                                           false /* packer_multicast_optimization_enabled */);
        }
        else if (pg_pipe_outputs[0]->is_relay())
        {
            derived_pipe_properties.set_incoming_noc_id(derived_pipe_properties.get_outgoing_noc_id());
            derived_pipe_properties.set_incoming_noc_vc(derived_pipe_properties.get_outgoing_noc_vc());
            derived_pipe = create_relay_pipe(std::move(derived_pipe_properties),
                                             pipe_physical_location,
                                             pg_pipe_inputs[0].get_buffer(),
                                             pg_pipe_outputs[0]);
        }
        else
        {
            derived_pipe_properties.set_incoming_noc_id(derived_pipe_properties.get_outgoing_noc_id());
            derived_pipe_properties.set_incoming_noc_vc(derived_pipe_properties.get_outgoing_noc_vc());
            derived_pipe = std::make_unique<UnicastPipe>(std::move(derived_pipe_properties), pipe_physical_location);
        }

        connect_rg_pipe_with_output_nodes(derived_pipe.get(), pg_pipe_outputs);

        created_rg_pipes_from_subgraph_pipe->insert(derived_pipe.get());
        rational_graph_pipes.push_back(std::move(derived_pipe));
    }

    void RationalGraphCreator::decompose_mmio_pipe(
        const PGPipe* mmio_pipe,
        const unsigned int scatter_index,
        const tt_cxy_pair& pipe_logical_location,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        const SoCInfo* soc_info,
        std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe)
    {
        const std::vector<PGPipe::Input>& mmio_pipe_inputs = mmio_pipe->get_inputs(scatter_index);
        const std::vector<PGBuffer*>& mmio_pipe_outputs = mmio_pipe->get_output_buffers()[scatter_index];

        const unsigned int tile_size = mmio_pipe_inputs[0].get_buffer()->get_tile_size();
        const unsigned int epoch_tiles = mmio_pipe_outputs[0]->get_num_epoch_tiles();
        unsigned int src_size_tiles =
            PCIeFlowCalculator::calculate_source_size_tiles(mmio_pipe_inputs, mmio_pipe_outputs, tile_size);
        unsigned int dest_size_tiles =
            PCIeFlowCalculator::calculate_destination_size_tiles(mmio_pipe_outputs, tile_size, src_size_tiles);

        std::unique_ptr<RGBasePipe> pipe_sending_to_pcie =
            create_pipe_sending_to_pcie(mmio_pipe, scatter_index, soc_info);
        bool new_pcie_streaming_node_created = connect_pipe_sending_to_pcie_to_streaming_node(
            pipe_sending_to_pcie.get(),
            tile_size,
            epoch_tiles,
            src_size_tiles,
            dest_size_tiles,
            mmio_pipe->is_mmio_pipe_downstream(),
            mmio_pipe_inputs[0].get_buffer()->get_logical_location().chip,
            mmio_pipe,
            mmio_pipe_outputs,
            created_rg_pipes_from_subgraph_pipe,
            rational_graph_nodes,
            rational_graph_pipes,
            pg_pipe_dest_list_to_rg_pipe,
            scatter_index);

        created_rg_pipes_from_subgraph_pipe->insert(pipe_sending_to_pcie.get());
        rational_graph_pipes.push_back(std::move(pipe_sending_to_pcie));

        if (!new_pcie_streaming_node_created)
        {
            // There already exists pipe reading from the existing PCIe streaming node.
            return;
        }

        std::unique_ptr<RGBasePipe> pipe_reading_from_pcie =
            create_pipe_reading_from_pcie(mmio_pipe, scatter_index, mmio_pipe_outputs, pipe_logical_location, soc_info);
        connect_pipe_reading_from_pcie_to_streaming_node(
            pipe_reading_from_pcie.get(), tile_size, epoch_tiles, src_size_tiles, dest_size_tiles,
            mmio_pipe->is_mmio_pipe_downstream(), mmio_pipe_outputs[0]->get_logical_location().chip,
            rational_graph_nodes, scatter_index, mmio_pipe->get_id());
        created_rg_pipes_from_subgraph_pipe->insert(pipe_reading_from_pcie.get());
        rational_graph_pipes.push_back(std::move(pipe_reading_from_pcie));
    }

    std::unique_ptr<RGBasePipe> RationalGraphCreator::create_pipe_sending_to_pcie(
        const PGPipe* mmio_pipe,
        const unsigned int scatter_index,
        const SoCInfo* soc_info)
    {
        const std::vector<PGPipe::Input>& mmio_pipe_inputs = mmio_pipe->get_inputs(scatter_index);

        tt_cxy_pair pipe_location = choose_location_of_pipe_sending_to_pcie(mmio_pipe_inputs, scatter_index, soc_info);

        std::unique_ptr<RGBasePipe> pipe_sending_to_pcie;

        // Unlike regular gather, PCIe transfer requires gather setup even in case of a single scattered input buffer.
        if (mmio_pipe_inputs.size() > 1 || mmio_pipe_inputs[0].get_buffer()->is_scatter())
        {
            pipe_sending_to_pcie = std::make_unique<GatherToPCIePipe>(
                RGPipeProperties(
                    mmio_pipe->get_id(),
                    mmio_pipe->get_incoming_noc_id(),
                    mmio_pipe->get_incoming_noc_vc(),
                    mmio_pipe->get_outgoing_noc_id(),
                    constants::mmio_dram_write_outgoing_noc_vc,
                    mmio_pipe->get_pipe_periodic_repeat(),
                    mmio_pipe->get_consumer_repeat()),
                pipe_location);
        }
        else
        {
            pipe_sending_to_pcie = std::make_unique<UnicastToPCIePipe>(
                RGPipeProperties(
                    mmio_pipe->get_id(),
                    mmio_pipe->get_incoming_noc_id(),
                    mmio_pipe->get_incoming_noc_vc(),
                    mmio_pipe->get_outgoing_noc_id(),
                    constants::mmio_dram_write_outgoing_noc_vc,
                    mmio_pipe->get_pipe_periodic_repeat(),
                    mmio_pipe->get_consumer_repeat()),
                pipe_location);
        }

        connect_rg_pipe_with_input_nodes(pipe_sending_to_pcie.get(), mmio_pipe_inputs, scatter_index);

        return pipe_sending_to_pcie;
    }

    bool RationalGraphCreator::connect_pipe_sending_to_pcie_to_streaming_node(
        RGBasePipe* pipe_sending_to_pcie,
        const unsigned int tile_size,
        const unsigned int epoch_tiles,
        const unsigned int src_size_tiles,
        const unsigned int dest_size_tiles,
        const bool is_streaming_downstream,
        const ChipId chip_id,
        const PGPipe* mmio_pipe,
        const std::vector<PGBuffer*>& mmio_pipe_outputs,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe,
        const unsigned int scatter_index)
    {
        if (mmio_pipe->has_output_list_duplicates())
        {
            return connect_pipe_sending_to_pcie_to_union_pipe(pipe_sending_to_pcie,
                                                              tile_size,
                                                              epoch_tiles,
                                                              src_size_tiles,
                                                              dest_size_tiles,
                                                              is_streaming_downstream,
                                                              chip_id,
                                                              mmio_pipe,
                                                              mmio_pipe_outputs,
                                                              created_rg_pipes_from_subgraph_pipe,
                                                              rational_graph_nodes,
                                                              rational_graph_pipes,
                                                              pg_pipe_dest_list_to_rg_pipe,
                                                              scatter_index);
        }
        else
        {
            std::unique_ptr<PCIeStreamingNode> pcie_streaming_node = std::make_unique<PCIeStreamingNode>(
                chip_id, src_size_tiles, dest_size_tiles, tile_size, epoch_tiles, is_streaming_downstream,
                scatter_index, mmio_pipe->get_id());

            connect_rg_pipe_with_output_node(pipe_sending_to_pcie, pcie_streaming_node.get());
            rational_graph_nodes.push_back(std::move(pcie_streaming_node));

            return true;
        }
    }

    bool RationalGraphCreator::connect_pipe_sending_to_pcie_to_union_pipe(
        RGBasePipe* pipe_sending_to_pcie,
        const unsigned int tile_size,
        const unsigned int epoch_tiles,
        const unsigned int src_size_tiles,
        const unsigned int dest_size_tiles,
        const bool is_streaming_downstream,
        const ChipId chip_id,
        const PGPipe* mmio_pipe,
        const std::vector<PGBuffer*>& mmio_pipe_outputs,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe,
        const unsigned int scatter_index)
    {
        bool new_pcie_streaming_node_created = false;
        RGBasePipe* union_pipe_ptr = get_union_pipe_sending_to_pcie_streaming_node(
            tile_size,
            epoch_tiles,
            src_size_tiles,
            dest_size_tiles,
            is_streaming_downstream,
            chip_id,
            mmio_pipe,
            mmio_pipe_outputs,
            created_rg_pipes_from_subgraph_pipe,
            rational_graph_nodes,
            rational_graph_pipes,
            pg_pipe_dest_list_to_rg_pipe,
            new_pcie_streaming_node_created,
            scatter_index);

        std::unique_ptr<VirtualNode> input_virt_node = std::make_unique<VirtualNode>(
            pipe_sending_to_pcie->get_physical_location(),
            src_size_tiles,
            tile_size,
            epoch_tiles,
            src_size_tiles /* tiles_per_input */);

        connect_rg_pipe_with_input_node(union_pipe_ptr, input_virt_node.get());
        connect_rg_pipe_with_output_node(pipe_sending_to_pcie, input_virt_node.get());

        rational_graph_nodes.push_back(std::move(input_virt_node));

        return new_pcie_streaming_node_created;
    }

    RGBasePipe* RationalGraphCreator::get_union_pipe_sending_to_pcie_streaming_node(
        const unsigned int tile_size,
        const unsigned int epoch_tiles,
        const unsigned int src_size_tiles,
        const unsigned int dest_size_tiles,
        const bool is_streaming_downstream,
        const ChipId chip_id,
        const PGPipe* mmio_pipe,
        const std::vector<PGBuffer*>& mmio_pipe_outputs,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe,
        bool& new_pcie_streaming_node_created,
        const unsigned int scatter_index)
    {
        auto it = pg_pipe_dest_list_to_rg_pipe.find(mmio_pipe_outputs);
        if (it != pg_pipe_dest_list_to_rg_pipe.end())
        {
            new_pcie_streaming_node_created = false;
            return it->second;
        }

        std::unique_ptr<PCIeStreamingNode> pcie_streaming_node = std::make_unique<PCIeStreamingNode>(
            chip_id, src_size_tiles, dest_size_tiles, tile_size, epoch_tiles, is_streaming_downstream,
            scatter_index, mmio_pipe->get_id());

        create_union_pipe_for_rg_node(pcie_streaming_node.get(),
                                      created_rg_pipes_from_subgraph_pipe,
                                      rational_graph_pipes);
        rational_graph_nodes.push_back(std::move(pcie_streaming_node));
        new_pcie_streaming_node_created = true;

        RGBasePipe* union_pipe = rational_graph_pipes.back().get();
        pg_pipe_dest_list_to_rg_pipe.emplace(mmio_pipe_outputs, union_pipe);

        return union_pipe;
    }

    std::unique_ptr<RGBasePipe> RationalGraphCreator::create_pipe_reading_from_pcie(
        const PGPipe* mmio_pipe,
        const unsigned int scatter_index,
        const std::vector<PGBuffer*>& mmio_pipe_outputs,
        const tt_cxy_pair& mmio_pipe_logical_location,
        const SoCInfo* soc_info)
    {
        std::unique_ptr<RGBasePipe> pipe_reading_from_pcie;

        // Compute the location on the destination chip where the pipe reading from PCIe will be placed.
        tt_cxy_pair pipe_logical_location = tt_cxy_pair(mmio_pipe_outputs[0]->get_logical_location().chip,
                                                        mmio_pipe_logical_location.x, mmio_pipe_logical_location.y);
        tt_cxy_pair pipe_physical_location = get_physical_location(mmio_pipe, pipe_logical_location, soc_info);

        // Calculating pipe id in a same way as pipegen1 in order to easier match generated streams in tests.
        NodeId pipe_id = mmio_pipe->get_id() + 2 * (scatter_index + 1);

        // In order to balance the throughput we change the outgoing NOC VC after first scatter.
        const unsigned int outgoing_noc_vc =
            scatter_index == 0 ? mmio_pipe->get_outgoing_noc_vc() : constants::mmio_dram_write_outgoing_noc_vc;

        if (mmio_pipe_outputs.size() > 1)
        {
            pipe_reading_from_pcie =
                std::make_unique<PCIeMulticastPipe>(
                    RGPipeProperties(
                        pipe_id,
                        mmio_pipe->get_incoming_noc_id(),
                        mmio_pipe->get_incoming_noc_vc(),
                        mmio_pipe->get_outgoing_noc_id(),
                        outgoing_noc_vc,
                        1 /* periodic_repeat */,
                        // One receiving pipe is created for all scatter duplicates, with multiple sending pipes so
                        // there is no need to repeat output.
                        1 /* consumer_repeat */),
                    pipe_physical_location);
        }
        else
        {
            pipe_reading_from_pcie =
                std::make_unique<PCIeUnicastPipe>(
                    RGPipeProperties(
                        pipe_id,
                        mmio_pipe->get_incoming_noc_id(),
                        mmio_pipe->get_incoming_noc_vc(),
                        mmio_pipe->get_outgoing_noc_id(),
                        outgoing_noc_vc,
                        1 /* periodic_repeat */,
                        // One receiving pipe is created for all scatter duplicates, with multiple sending pipes so
                        // there is no need to repeat output.
                        1 /* consumer_repeat */),
                    pipe_physical_location);
        }

        connect_rg_pipe_with_output_nodes(pipe_reading_from_pcie.get(), mmio_pipe_outputs);

        return pipe_reading_from_pcie;
    }

    void RationalGraphCreator::connect_pipe_reading_from_pcie_to_streaming_node(
        RGBasePipe* pipe_reading_from_pcie,
        const unsigned int tile_size,
        const unsigned int epoch_tiles,
        const unsigned int src_size_tiles,
        const unsigned int dest_size_tiles,
        const bool is_streaming_downstream,
        const ChipId chip_id,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        const unsigned int scatter_index,
        const NodeId mmio_pipe_id)
    {
        std::unique_ptr<PCIeStreamingNode> pcie_streaming_node = std::make_unique<PCIeStreamingNode>(
            chip_id,
            src_size_tiles,
            dest_size_tiles,
            tile_size,
            epoch_tiles,
            is_streaming_downstream,
            scatter_index,
            mmio_pipe_id);

        pipe_reading_from_pcie->add_input_node(pcie_streaming_node.get());
        pcie_streaming_node->add_output_pipe(pipe_reading_from_pcie);

        rational_graph_nodes.push_back(std::move(pcie_streaming_node));
    }

    tt_cxy_pair RationalGraphCreator::choose_location_of_pipe_sending_to_pcie(
        const std::vector<PGPipe::Input>& mmio_pipe_inputs,
        const unsigned int scatter_index,
        const SoCInfo* soc_info)
    {
        // Goal of this code is to balance the load across two cores. Each pipe sending to PCIe for even scatter index
        // will be assigned location of the first input, and for each odd scatter index will be assigned location of
        // first other input with different location than the first input (or also the location of the first input if
        // none such other is found).

        // TODO: We should refactor this logic so that we first find locations for odd and even scatter indexes,
        //       and then assign the appropriate location for decompose of each scatter index instead of finding them
        //       all over again.

        const tt_cxy_pair& first_input_location =
            get_physical_location(mmio_pipe_inputs[0].get_buffer(), soc_info);

        if (scatter_index % 2 == 0)
        {
            return first_input_location;
        }

        for (std::size_t i = 1; i < mmio_pipe_inputs.size(); ++i)
        {
            const tt_cxy_pair& current_input_location =
                get_physical_location(mmio_pipe_inputs[i].get_buffer(), soc_info);
            if (current_input_location != first_input_location)
            {
                return current_input_location;
            }
        }

        return first_input_location;
    }

    void RationalGraphCreator::connect_rg_pipe_with_input_nodes(RGBasePipe* rg_pipe,
                                                                const std::vector<PGPipe::Input>& pg_pipe_inputs,
                                                                const unsigned int scatter_index)
    {
        for (const PGPipe::Input& pg_pipe_input : pg_pipe_inputs)
        {
            connect_rg_pipe_with_input_node(rg_pipe, pg_pipe_input, scatter_index);
        }
    }

    void RationalGraphCreator::connect_rg_pipe_with_input_node(RGBasePipe* rg_pipe,
                                                               const PGPipe::Input& pg_pipe_input,
                                                               const unsigned int scatter_index)
    {
        RGBaseNode* rg_node = m_node_from_buffer[pg_pipe_input.get_buffer()];

        m_rg_edge_to_scatter_index[rg_node][rg_pipe] = scatter_index;

        rg_pipe->add_input_node(rg_node, pg_pipe_input.get_offset());
        rg_node->add_output_pipe(rg_pipe);
    }

    void RationalGraphCreator::connect_rg_pipe_with_input_node(RGBasePipe* rg_pipe, RGBaseNode* rg_node)
    {
        rg_pipe->add_input_node(rg_node);
        rg_node->add_output_pipe(rg_pipe);
    }

    void RationalGraphCreator::connect_rg_pipe_with_output_nodes(RGBasePipe* rg_pipe,
                                                                 const std::vector<PGBuffer*>& pg_pipe_outputs)
    {
        for (const PGBuffer* pg_pipe_output : pg_pipe_outputs)
        {
            connect_rg_pipe_with_output_node(rg_pipe, pg_pipe_output);
        }
    }

    void RationalGraphCreator::connect_rg_pipe_with_output_node(RGBasePipe* rg_pipe, const PGBuffer* pg_pipe_output)
    {
        RGBaseNode* rg_node = m_node_from_buffer[pg_pipe_output];
        connect_rg_pipe_with_output_node(rg_pipe, rg_node);
    }

    void RationalGraphCreator::connect_rg_pipe_with_output_node(RGBasePipe* rg_pipe, RGBaseNode* rg_node)
    {
        rg_pipe->add_output_node(rg_node);
        rg_node->set_input_pipe(rg_pipe);
    }

    void RationalGraphCreator::create_union_pipe_for_rg_node(
        RGBaseNode* union_pipe_output_node,
        std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes)
    {
        std::unique_ptr<UnionPipe> union_pipe = std::make_unique<UnionPipe>(
            union_pipe_output_node->get_physical_location());

        connect_rg_pipe_with_output_node(union_pipe.get(), union_pipe_output_node);

        created_rg_pipes_from_subgraph_pipe->insert(union_pipe.get());
        rational_graph_pipes.push_back(std::move(union_pipe));
    }

    std::unique_ptr<VirtualNode> RationalGraphCreator::wrap_into_virtual_node(const PGBuffer* pg_buffer)
    {
        RGBaseNode* rg_node = m_node_from_buffer.at(pg_buffer);
        log_assert(rg_node != nullptr, "Expected node to be created for buffer");
        std::unique_ptr<VirtualNode> virtual_node = std::make_unique<VirtualNode>(rg_node);
        m_node_from_buffer[pg_buffer] = virtual_node.get();

        RGBasePipe* input_pipe = rg_node->get_input_pipe();
        if (input_pipe)
        {
            // Disconnect the input pipe from the node and connect it to the virtual node.
            input_pipe->replace_output_node(rg_node, virtual_node.get());
            virtual_node->set_input_pipe(input_pipe);
            rg_node->set_input_pipe(nullptr);
        }

        for (RGBasePipe* output_pipe : rg_node->get_output_pipes())
        {
            // Disconnect the output pipe from the node and connect it to the virtual node.
            output_pipe->replace_input_node(rg_node, virtual_node.get());
            virtual_node->add_output_pipe(output_pipe);
            rg_node->remove_output_pipe(output_pipe);
        }

        return virtual_node;
    }

    void RationalGraphCreator::handle_forked_buffers(
        const std::vector<const PGBuffer*>& subgraph_buffers,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
        const SoCInfo* soc_info)
    {
        std::vector<const PGBuffer*> forked_buffers = find_forked_buffers(subgraph_buffers);

        for (const PGBuffer* forked_buffer : forked_buffers)
        {
            RGBaseNode* rg_node = m_node_from_buffer[forked_buffer];
            create_parallel_fork_for_node(forked_buffer, soc_info, rg_node, rational_graph_pipes, rational_graph_nodes);
        }
    }

    std::vector<const PGBuffer*> RationalGraphCreator::find_forked_buffers(
        const std::vector<const PGBuffer*>& subgraph_buffers)
    {
        std::vector<const PGBuffer*> forked_buffers;

        for (const PGBuffer* buffer : subgraph_buffers)
        {
            // Count also every packer or dram buffer with prefetch type Pre-TM as a forked buffer, to ensure creating a
            // parallel fork for it.
            if (buffer->is_forked() || buffer->is_packer() || buffer->is_dram_prefetch_pre_tm())
            {
                forked_buffers.push_back(buffer);
            }
        }

        return forked_buffers;
    }

    void RationalGraphCreator::create_parallel_fork_for_node(
        const PGBuffer* pg_buffer,
        const SoCInfo* soc_info,
        RGBaseNode* rg_node,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes)
    {
        const tt_cxy_pair& physical_location = get_physical_location(pg_buffer, soc_info);

        std::unique_ptr<ForkPipe> fork_pipe;
        if (pg_buffer->is_dram_input())
        {
            fork_pipe = create_dram_parallel_fork_pipe_for_node(pg_buffer, physical_location);
        }
        else
        {
            fork_pipe = std::make_unique<ParallelForkPipe>(physical_location);
        }

        std::unordered_set<RGBasePipe*> forked_pipes = rg_node->get_output_pipes_set();
        reconnect_forked_node(rg_node, std::move(fork_pipe), forked_pipes, rational_graph_pipes,
                              rational_graph_nodes, false /* is_serial_fork */);

        if (is_buffer_forking_to_optimized_packer_multicast(pg_buffer, soc_info))
        {
            // Fork path containing optimized packer multicast pipe has to be on the first fork index.
            ensure_optimized_packer_multicast_is_first_fork(rational_graph_pipes.back().get());
        }
    }

    std::unique_ptr<ForkPipe> RationalGraphCreator::create_dram_parallel_fork_pipe_for_node(
        const PGBuffer* pg_buffer,
        const tt_cxy_pair& physical_location)
    {
        if (pg_buffer->is_dram_prefetch_pre_tm())
        {
            return std::make_unique<DramPrefetchPreTMParallelForkPipe>(physical_location);
        }
        else
        {
            return std::make_unique<DramParallelForkPipe>(physical_location);
        }
    }

    void RationalGraphCreator::create_serial_fork_nodes_for_pipe_inputs(
        const PGPipe* subgraph_pipe,
        const std::unordered_set<RGBasePipe*>& created_rg_pipes_from_subgraph_pipe,
        const SoCInfo* soc_info,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes)
    {
        const unsigned int scatter_fanout = subgraph_pipe->get_scatter_fanout();
        const unsigned int scatter_padding_fanout = subgraph_pipe->get_padding_scatter_fanout();

        for (const PGBuffer* input_buffer : subgraph_pipe->get_unique_input_buffers_including_output_padding())
        {
            RGBaseNode* rg_node = m_node_from_buffer[input_buffer];
            create_serial_fork_for_node(subgraph_pipe,
                                        created_rg_pipes_from_subgraph_pipe,
                                        rg_node,
                                        get_physical_location(input_buffer, soc_info),
                                        scatter_fanout,
                                        scatter_padding_fanout,
                                        input_buffer->is_padding(),
                                        rational_graph_pipes,
                                        rational_graph_nodes);
        }
    }

    void RationalGraphCreator::create_serial_fork_for_node(
        const PGPipe* subgraph_pipe,
        const std::unordered_set<RGBasePipe*>& created_rg_pipes_from_subgraph_pipe,
        RGBaseNode* rg_node,
        const tt_cxy_pair& physical_location,
        const unsigned int scatter_fanout,
        const unsigned int scatter_padding_fanout,
        const bool is_output_padding,
        std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
        std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes)
    {
        std::unique_ptr<ForkPipe> fork_pipe = create_serial_fork_pipe_for_node(
            rg_node, subgraph_pipe, physical_location, scatter_fanout, scatter_padding_fanout, is_output_padding);

        reconnect_forked_node(rg_node, std::move(fork_pipe), created_rg_pipes_from_subgraph_pipe, rational_graph_pipes,
                              rational_graph_nodes, true /* is_serial_fork */);
    }

    std::unique_ptr<ForkPipe> RationalGraphCreator::create_serial_fork_pipe_for_node(
        const RGBaseNode* rg_node,
        const PGPipe* subgraph_pipe,
        const tt_cxy_pair& physical_location,
        const unsigned int scatter_fanout,
        const unsigned int scatter_padding_fanout,
        const bool is_output_padding)
    {
        RGPipeProperties rg_pipe_properties(subgraph_pipe->get_id(),
                                            subgraph_pipe->get_incoming_noc_id(),
                                            subgraph_pipe->get_incoming_noc_vc(),
                                            subgraph_pipe->get_outgoing_noc_id(),
                                            subgraph_pipe->get_outgoing_noc_vc());

        std::unique_ptr<ForkPipe> fork_pipe;
        if (is_output_padding)
        {
            fork_pipe = std::make_unique<PaddingSerialForkPipe>(
                std::move(rg_pipe_properties), physical_location, scatter_fanout, scatter_padding_fanout);
        }
        else
        {
            fork_pipe = std::make_unique<SerialForkPipe>(
                std::move(rg_pipe_properties), physical_location, scatter_fanout, scatter_padding_fanout);
        }

        return fork_pipe;
    }

    void RationalGraphCreator::reconnect_forked_node(RGBaseNode* rg_node,
                                                     std::unique_ptr<ForkPipe> fork_pipe,
                                                     const std::unordered_set<RGBasePipe*>& rg_pipes_behind_fork,
                                                     std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
                                                     std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
                                                     const bool is_serial_fork)
    {
        fork_pipe->set_input_node(rg_node);

        for (RGBasePipe* output_pipe : rg_node->get_output_pipes())
        {
            if (rg_pipes_behind_fork.find(output_pipe) == rg_pipes_behind_fork.end())
            {
                continue;
            }

            std::unique_ptr<VirtualNode> virtual_node = create_fork_node_for_rg_edge(
                rg_node, output_pipe, is_serial_fork);

            virtual_node->set_input_pipe(fork_pipe.get());
            fork_pipe->add_output_node(virtual_node.get());

            virtual_node->add_output_pipe(output_pipe);
            output_pipe->replace_input_node(rg_node, virtual_node.get());

            rational_graph_nodes.push_back(std::move(virtual_node));
        }

        for (RGBasePipe* rg_pipe : rg_pipes_behind_fork)
        {
            rg_node->remove_output_pipe(rg_pipe);
        }
        rg_node->add_output_pipe(fork_pipe.get());

        rational_graph_pipes.push_back(std::move(fork_pipe));
    }

    std::unique_ptr<VirtualNode> RationalGraphCreator::create_fork_node_for_rg_edge(const RGBaseNode* input_node,
                                                                                    const RGBasePipe* output_pipe,
                                                                                    const bool is_serial_fork)
    {
        std::unique_ptr<VirtualNode> virtual_node;
        if (is_serial_fork)
        {
            const unsigned int scatter_index = m_rg_edge_to_scatter_index.at(input_node).at(output_pipe);
            const tt_cxy_pair& physical_location = output_pipe->get_physical_location();
            virtual_node = std::make_unique<SerialForkNode>(input_node, physical_location, scatter_index);
        }
        else
        {
            virtual_node = std::make_unique<VirtualNode>(input_node);
        }

        return virtual_node;
    }

    bool RationalGraphCreator::is_buffer_forking_to_optimized_packer_multicast(const PGBuffer* buffer,
                                                                               const SoCInfo* soc_info)
    {
        if (!buffer->is_packer())
        {
            return false;
        }

        for (const PGPipe* output_pipe : buffer->get_output_pipes())
        {
            if (is_packer_multicast_optimization_enabled(output_pipe, soc_info))
            {
                return true;
            }
        }

        return false;
    }

    void RationalGraphCreator::ensure_optimized_packer_multicast_is_first_fork(RGBasePipe* fork_pipe)
    {
        if (fork_pipe->get_output_nodes().size() < 2)
        {
            return;
        }

        bool found_optimized_packer_multicast_fork =
            is_fork_path_containing_optimized_packer_multicast(fork_pipe->get_output_nodes()[0]);

        for (std::size_t i = 1; i < fork_pipe->get_output_nodes().size(); ++i)
        {
            if (!is_fork_path_containing_optimized_packer_multicast(fork_pipe->get_output_nodes()[i]))
            {
                continue;
            }

            log_assert(!found_optimized_packer_multicast_fork,
                       "Only one fork path from packer containing optimized packer multicast is allowed");

            fork_pipe->swap_output_nodes(0, i);

            found_optimized_packer_multicast_fork = true;
        }
    }

    bool RationalGraphCreator::is_fork_path_containing_optimized_packer_multicast(const RGBaseNode* forked_node)
    {
        std::stack<const RGBaseNode*> fork_path_nodes;
        fork_path_nodes.push(forked_node);

        while (!fork_path_nodes.empty())
        {
            const RGBaseNode* current_node = fork_path_nodes.top();
            fork_path_nodes.pop();

            for (const RGBasePipe* output_pipe : current_node->get_output_pipes())
            {
                const MulticastPipe* multicast_pipe = dynamic_cast<const MulticastPipe*>(output_pipe);
                if (multicast_pipe && multicast_pipe->is_packer_multicast_optimization_enabled())
                {
                    return true;
                }

                for (const RGBaseNode* output_node : output_pipe->get_output_nodes())
                {
                    fork_path_nodes.push(output_node);
                }
            }
        }

        return false;
    }

    bool RationalGraphCreator::is_packer_multicast_optimization_enabled(const PGPipe* subgraph_pipe,
                                                                        const SoCInfo* soc_info)
    {
        // Direct packer multicast optimization is not yet supported in Grayskull due to not enough firmware space.
        return subgraph_pipe->is_packer_multicast_optimization_enabled() &&
               soc_info->get_device_arch() != tt::ARCH::GRAYSKULL;
    }

    bool RationalGraphCreator::can_directly_connect_l1_source_to_l1_dest(const PGPipe* subgraph_pipe,
                                                                         const SoCInfo* soc_info)
    {
        return subgraph_pipe->has_single_buffer_input() &&
               (!subgraph_pipe->has_consumer_duplicates() ||
                is_packer_multicast_optimization_enabled(subgraph_pipe, soc_info));
    }

    DramInputNodeType RationalGraphCreator::get_input_dram_buffer_type(const PGBuffer* input_dram_buffer)
    {
        if (input_dram_buffer->is_dram())
        {
            return DramInputNodeType::DramIO;
        }
        else if (input_dram_buffer->is_dram_prefetch_post_tm())
        {
            return DramInputNodeType::DramPrefetchPostTM;
        }
        else if (input_dram_buffer->is_dram_prefetch_pre_tm())
        {
            return DramInputNodeType::DramPrefetchPreTM;
        }
        else
        {
            throw InvalidPipeGraphSpecificationException(
                " Unsupported dram input buffer type for buffer " +
                std::to_string(input_dram_buffer->get_id()) + ". This dram buffer was not recognized as either dram io "
                "buffer, prefetch Pre-TM nor prefetch Post-TM buffer",
                input_dram_buffer->get_logical_location());
        }
    };

    tt_cxy_pair RationalGraphCreator::get_physical_location(const PGBuffer* pg_buffer, const SoCInfo* soc_info)
    {
        const tt_cxy_pair& logical_location = pg_buffer->get_logical_location();

        if (PipeGraph::is_unmapped_location(logical_location))
        {
            return logical_location;
        }
        else if (pg_buffer->get_ethernet_channel() >= 0)
        {
            return soc_info->get_ethernet_channel_core_physical_location(
                logical_location.chip, pg_buffer->get_ethernet_channel());
        }
        else
        {
            return soc_info->convert_logical_to_physical_worker_core_coords(logical_location);
        }
    }

    tt_cxy_pair RationalGraphCreator::get_physical_location(const PGPipe* pg_pipe,
                                                            const tt_cxy_pair& pipe_logical_location,
                                                            const SoCInfo* soc_info)
    {
        if (PipeGraph::is_unmapped_location(pipe_logical_location))
        {
            return pipe_logical_location;
        }
        else if (pg_pipe->get_ethernet_channel() >= 0)
        {
            return soc_info->get_ethernet_channel_core_physical_location(
                pipe_logical_location.chip, pg_pipe->get_ethernet_channel());
        }
        else if (pg_pipe->is_ethernet_pipe())
        {
            // If pipe is ethernet pipe, then in case of harvested chip translate its coordinates to harvested range. If
            // chip is not harvested, then use its coordinates as they are. This function will do it automatically.
            return soc_info->convert_unharvested_core_location_to_harvested(pipe_logical_location);
        }
        else
        {
            return soc_info->convert_logical_to_physical_worker_core_coords(pipe_logical_location);
        }
    }

    bool RationalGraphCreator::is_subgraph_doing_pcie_transfer(const PGSubgraph* pg_subgraph)
    {
        for (const PGPipe* pg_pipe : pg_subgraph->get_pipes())
        {
            if (pg_pipe->is_mmio_pipe())
            {
                return true;
            }
        }

        return false;
    }

    void RationalGraphCreator::create_relay_nodes(const std::vector<const PGBuffer*>& subgraph_buffers,
                                                  std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes)
    {
        for (const PGBuffer* pg_buffer : subgraph_buffers)
        {
            if (pg_buffer->is_relay())
            {
                std::unique_ptr<VirtualNode> wrapped_relay_node = wrap_into_virtual_node(pg_buffer);
                rational_graph_nodes.push_back(std::move(wrapped_relay_node));
            }
        }
    }

    void RationalGraphCreator::manage_gather_optimizations(const ResourceManager* resource_manager,
                                                           std::vector<std::unique_ptr<RationalGraph>>& rational_graphs)
    {
        GatherOptimizationManager gather_optimization_manager;
        gather_optimization_manager.process_rational_graphs(resource_manager, rational_graphs);
    }

    void RationalGraphCreator::allocate_kernel_input_index(
        ResourceManager* resource_manager,
        std::vector<std::unique_ptr<RationalGraph>>& rational_graphs)
    {
        std::vector<BaseIntermedNode*> intermed_kernel_inputs;

        for (const std::unique_ptr<RationalGraph>& rational_graph : rational_graphs)
        {
            for (const std::unique_ptr<RGBaseNode>& rg_node : rational_graph->get_nodes())
            {
                UnpackerOutputNode* unpacker_node = dynamic_cast<UnpackerOutputNode*>(rg_node.get());
                if (unpacker_node)
                {
                    unpacker_node->set_kernel_input_index(
                        resource_manager->allocate_kernel_input(unpacker_node->get_physical_location()));
                    continue;
                }

                BaseIntermedNode* intermed_node = dynamic_cast<BaseIntermedNode*>(rg_node.get());
                if (intermed_node && !is_dummy_intermediate_node(intermed_node))
                {
                    intermed_kernel_inputs.push_back(intermed_node);
                }
            }
        }

        for (BaseIntermedNode* intermed_node : intermed_kernel_inputs)
        {
            intermed_node->set_kernel_input_index(
                resource_manager->allocate_kernel_input(intermed_node->get_physical_location()));
        }
    }

    void RationalGraphCreator::allocate_kernel_output_index(
        ResourceManager* resource_manager,
        std::vector<std::unique_ptr<RationalGraph>>& rational_graphs)
    {
        std::vector<BaseIntermedNode*> intermed_kernel_outputs;

        for (const std::unique_ptr<RationalGraph>& rational_graph : rational_graphs)
        {
            for (const std::unique_ptr<RGBaseNode>& rg_node : rational_graph->get_nodes())
            {
                PackerInputNode* packer_node = dynamic_cast<PackerInputNode*>(rg_node.get());
                if (packer_node)
                {
                    packer_node->set_kernel_output_index(
                        resource_manager->allocate_kernel_output(packer_node->get_physical_location()));
                    continue;
                }

                BaseIntermedNode* intermed_node = dynamic_cast<BaseIntermedNode*>(rg_node.get());
                if (intermed_node && !is_dummy_intermediate_node(intermed_node))
                {
                    intermed_kernel_outputs.push_back(intermed_node);
                }
            }
        }

        for (BaseIntermedNode* intermed_node : intermed_kernel_outputs)
        {
            intermed_node->set_kernel_output_index(
                resource_manager->allocate_kernel_output(intermed_node->get_physical_location()));
        }
    }

    bool RationalGraphCreator::is_dummy_intermediate_node(const BaseIntermedNode* intermed_node)
    {
       return intermed_node->get_input_pipe() == nullptr;
    }
}