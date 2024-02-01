// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/ncrisc_creators/ncrisc_creator.h"

#include <algorithm>
#include <limits>
#include <numeric>

#include "l1_address_map.h"

#include "graph_creator/stream_graph/ncrisc_creators/dram_scatter_offset_compressor.h"
#include "model/rational_graph/nodes/base_output_node.h"
#include "model/rational_graph/nodes/dram_embedding_table_input_node.h"
#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/nodes/dram_output_intermed_node.h"
#include "model/rational_graph/nodes/dram_output_node.h"
#include "model/rational_graph/nodes/packer_input_node.h"
#include "model/rational_graph/nodes/pcie_streaming_node.h"
#include "model/rational_graph/nodes/virtual_node.h"
#include "model/rational_graph/pipes/fork/dram_parallel_fork_pipe.h"
#include "pipegen2_constants.h"
#include "pipegen2_utils.h"

namespace pipegen2
{
    std::vector<NcriscConfig> NcriscCreator::configure_dram_ncrisc_reader(
        const DataFlowInfo& data_flow_info,
        const RGBasePipe* rg_pipe,
        const std::vector<int>& input_total_readers,
        const std::vector<int>& input_reader_index,
        const unsigned int max_dram_input_buffer_size_tiles,
        const SoCInfo* soc_info)
    {
        return configure_dram_ncrisc_reader(data_flow_info, rg_pipe, input_total_readers, input_reader_index, soc_info,
                                            max_dram_input_buffer_size_tiles, false /* prepare_offsets_for_tilizer */);
    }

    std::vector<NcriscConfig> NcriscCreator::configure_dram_tilizer_ncrisc_reader(
        const DataFlowInfo& data_flow_info,
        const RGBasePipe* rg_pipe,
        const std::vector<int>& input_total_readers,
        const std::vector<int>& input_reader_index,
        const unsigned int max_dram_input_buffer_size_tiles,
        const SoCInfo* soc_info)
    {
        return configure_dram_ncrisc_reader(data_flow_info, rg_pipe, input_total_readers, input_reader_index, soc_info,
                                            max_dram_input_buffer_size_tiles, true /* prepare_offsets_for_tilizer */);
    }

    std::vector<NcriscConfig> NcriscCreator::configure_dram_ncrisc_reader(
        const DataFlowInfo& data_flow_info,
        const RGBasePipe* rg_pipe,
        const std::vector<int>& input_total_readers,
        const std::vector<int>& input_reader_index,
        const SoCInfo* soc_info,
        const unsigned int max_dram_input_buffer_size_tiles,
        const bool prepare_offsets_for_tilizer)
    {
        std::vector<NcriscConfig> ncrisc_configs;

        const std::vector<const RGBaseNode*> unique_input_dram_nodes = rg_pipe->get_unique_input_nodes();

        // Read chunk size can be computed up front for all dram inputs for this pipe.
        const unsigned int dram_buf_read_chunk_size_tiles =
            calculate_dram_pipe_read_chunk_size(data_flow_info, unique_input_dram_nodes, max_dram_input_buffer_size_tiles);

        const unsigned int dram_scatter_chunk_size_tiles =
            calculate_dram_pipe_scatter_chunk_size(data_flow_info, rg_pipe);

        for (const RGBaseNode* input_node : unique_input_dram_nodes)
        {
            ncrisc_configs.emplace_back(create_dram_ncrisc_read_config(
                input_node, rg_pipe, soc_info, dram_buf_read_chunk_size_tiles, dram_scatter_chunk_size_tiles));
        }

        // Mapping from input node to the corresponding ncrisc config.
        // Has to be mapped after vector of configs is created, to be able to get stable pointers to vector elements.
        std::unordered_map<const RGBaseNode*, NcriscConfig*> input_dram_node_to_ncrisc_config;
        for (std::size_t i = 0; i < unique_input_dram_nodes.size(); ++i)
        {
            input_dram_node_to_ncrisc_config[unique_input_dram_nodes[i]] = &ncrisc_configs[i];
        }

        fill_dram_scatter_offsets(rg_pipe, input_dram_node_to_ncrisc_config, prepare_offsets_for_tilizer,
                                  ncrisc_configs);

        fill_dram_readers_properties(rg_pipe, input_total_readers, input_reader_index,
                                     input_dram_node_to_ncrisc_config);

        return ncrisc_configs;
    }

    void NcriscCreator::fill_dram_scatter_offsets(
        const RGBasePipe* rg_pipe,
        const std::unordered_map<const RGBaseNode*, NcriscConfig*>& input_dram_node_to_ncrisc_config,
        const bool prepare_offsets_for_tilizer,
        std::vector<NcriscConfig>& ncrisc_configs)
    {
        std::vector<unsigned long> merged_dram_scatter_offsets;
        if (prepare_offsets_for_tilizer)
        {
            fill_tilizer_dram_scatter_offsets(rg_pipe, merged_dram_scatter_offsets);
        }
        else
        {
            fill_regular_dram_scatter_offsets(rg_pipe, input_dram_node_to_ncrisc_config, merged_dram_scatter_offsets);
        }

        // Blobgen expects all offsets to be set only in the first NCRISC config.
        ncrisc_configs[0].dram_scatter_offsets_full_size = merged_dram_scatter_offsets.size();
        ncrisc_configs[0].dram_scatter_offsets = std::move(merged_dram_scatter_offsets);

        compress_dram_scatter_offsets(rg_pipe, prepare_offsets_for_tilizer, ncrisc_configs[0]);
    }

    void NcriscCreator::fill_tilizer_dram_scatter_offsets(
        const RGBasePipe* rg_pipe,
        std::vector<unsigned long>& merged_dram_scatter_offsets)
    {
        for (const PipeInput& pipe_input: rg_pipe->get_inputs())
        {
            if (merged_dram_scatter_offsets.empty() || pipe_input.get_offset() > merged_dram_scatter_offsets.back())
            {
                merged_dram_scatter_offsets.push_back(pipe_input.get_offset());
            }
        }

        const DramEmbeddingTableInputNode* dram_embedding_table =
            dynamic_cast<const DramEmbeddingTableInputNode*>(rg_pipe->get_inputs()[0].get_non_virtual_node());
        ASSERT(dram_embedding_table, "Expected DramEmbeddingTableInputNode as input node for tilizer pipe");

        // Check whether we can create an entire tile from the rows and if not pad with zero rows.
        unsigned int tile_dim_r = dram_embedding_table->get_untilized_input_tile_dim_r();
        unsigned int tile_dim_r_overflow =
            (merged_dram_scatter_offsets.size() * dram_embedding_table->get_scatter_gather_num_tiles()) % tile_dim_r;
        if (tile_dim_r_overflow > 0)
        {
            for (unsigned int i = 0; i < tile_dim_r - tile_dim_r_overflow; ++i)
            {
                merged_dram_scatter_offsets.push_back(c_invalid_dram_scatter_offset_flag);
            }
        }
    }

    void NcriscCreator::fill_regular_dram_scatter_offsets(
        const RGBasePipe* rg_pipe,
        const std::unordered_map<const RGBaseNode*, NcriscConfig*>& input_dram_node_to_ncrisc_config,
        std::vector<unsigned long>& merged_dram_scatter_offsets)
    {
        for (const PipeInput& pipe_input: rg_pipe->get_inputs())
        {
            const DramInputNode* input_dram_node =
                dynamic_cast<const DramInputNode*>(pipe_input.get_non_virtual_node());
            ASSERT(input_dram_node, "Dram pipe is expected to have dram nodes as inputs.");
            const NcriscConfig* ncrisc_config = input_dram_node_to_ncrisc_config.at(pipe_input.get_node());

            unsigned long dram_scatter_offset = ncrisc_config->dram_buf_noc_addr +
                                                pipe_input.get_offset() * ncrisc_config->msg_size;

            if (input_dram_node->is_padding())
            {
                dram_scatter_offset |= c_is_offset_padding_address_flag;
            }

            merged_dram_scatter_offsets.push_back(dram_scatter_offset);
        }
    }

    void NcriscCreator::compress_dram_scatter_offsets(
        const RGBasePipe* rg_pipe,
        const bool compress_offsets_for_tilizer,
        NcriscConfig& ncrisc_config)
    {
        const DramInputNode* dram_input_node =
            dynamic_cast<const DramInputNode*>(rg_pipe->get_inputs()[0].get_non_virtual_node());
        ASSERT(dram_input_node, "Dram pipe is expected to have dram nodes as inputs.");

        // Offsets are only compressed for the case of dram io. For the prefetch read case, offsets are not compressed.
        if (!dram_input_node->is_dram_io())
        {
            return;
        }

        DramScatterOffsetCompressor compressor(ncrisc_config.dram_scatter_offsets.value());

        if (compress_offsets_for_tilizer)
        {
            const DramEmbeddingTableInputNode* dram_embedding_table =
                dynamic_cast<const DramEmbeddingTableInputNode*>(dram_input_node);
            ASSERT(dram_embedding_table, "Expected DramEmbeddingTableInputNode as input node for tilizer pipe");

            ncrisc_config.dram_scatter_offsets = compressor.compress_dram_scatter_offsets_for_tilizer(
                    dram_embedding_table->get_untilized_input_c_dim(),
                    dram_embedding_table->get_tilize_mblock_n_loop_num_rows());
        }
        else
        {
            ncrisc_config.dram_scatter_offsets = compressor.compress_dram_scatter_offsets();
        }
    }

    void NcriscCreator::fill_dram_readers_properties(
        const RGBasePipe* rg_pipe,
        const std::vector<int>& input_total_readers,
        const std::vector<int>& input_reader_index,
        const std::unordered_map<const RGBaseNode*, NcriscConfig*>& input_dram_node_to_ncrisc_config)
    {
        const auto& pipe_inputs = rg_pipe->get_inputs();
        for (unsigned int i = 0; i < pipe_inputs.size(); ++i)
        {
            auto input_node = pipe_inputs[i].get_node();
            input_dram_node_to_ncrisc_config.at(input_node)->total_readers = input_total_readers[i];
            input_dram_node_to_ncrisc_config.at(input_node)->reader_index = input_reader_index[i];
        }
    }

    NcriscConfig NcriscCreator::configure_pcie_ncrisc_reader(
        const DataFlowInfo& data_flow_info,
        const RGBasePipe* rg_pipe,
        const SoCInfo* soc_info)
    {
        ASSERT(rg_pipe->get_inputs().size() == 1, "Expecting only a single PCIe streaming node at the input");
        const PCIeStreamingNode* pcie_streaming_node = dynamic_cast<const PCIeStreamingNode*>(
            rg_pipe->get_inputs().front().get_node());
        ASSERT(pcie_streaming_node, "Expecting a PCIe streaming node at the pipe input");

        const unsigned int num_msgs =
            pcie_streaming_node->get_num_epoch_tiles() / data_flow_info.get_num_iterations_in_epoch(rg_pipe);
        const unsigned int msg_size = pcie_streaming_node->get_tile_size();
        // PCIe NOC address depends on the stream's L1 buffer address, so we will set it later
        // after resource allocation is done.
        const unsigned long dram_buf_noc_addr = 0;
        const unsigned int dram_buf_size_tiles =
            pcie_streaming_node->get_num_queue_slots() * pcie_streaming_node->get_size_tiles();
        const unsigned int dram_buf_size_bytes = dram_buf_size_tiles * msg_size;

        NcriscConfig ncrisc_config = create_ncrisc_read_config(
            num_msgs,
            msg_size,
            dram_buf_noc_addr,
            dram_buf_size_tiles,
            dram_buf_size_bytes,
            pcie_streaming_node->get_num_queue_slots(),
            pcie_streaming_node->get_size_tiles() /* dram_buf_read_chunk_size_tiles */,
            false /* is_ram */,
            false /* is_padding */);

        ncrisc_config.dram_streaming = true;
        ncrisc_config.dram_streaming_downstream = pcie_streaming_node->is_streaming_downstream();

        return ncrisc_config;
    }

    NcriscConfig NcriscCreator::configure_prefetch_pre_tm_ncrisc_reader(
        const RGBasePipe* rg_pipe,
        const SoCInfo* soc_info,
        const DramInputNode* dram_input_node)
    {
        ASSERT(dram_input_node && dram_input_node->is_dram_prefetch_pre_tm(),
               "Expecting prefetch Pre-TM dram input node as input to the pipe");

        // Size of the entire DRAM buffer, i.e. the sum of size in tiles of all the scatter buffers which make up
        // this DRAM buffer.
        const unsigned int total_dram_buffer_size_tiles = dram_input_node->get_size_tiles() *
                                                          dram_input_node->get_num_queue_slots() *
                                                          dram_input_node->get_num_scatter_chunks();

        NcriscConfig ncrisc_config;
        ncrisc_config.dram_buf_noc_addr =
            get_dram_buffer_noc_address(dram_input_node, dram_input_node->get_physical_location().chip, soc_info);
        ncrisc_config.dram_buf_size_q_slots = dram_input_node->get_num_queue_slots();
        ncrisc_config.dram_buf_size_tiles = total_dram_buffer_size_tiles;
        ncrisc_config.dram_buf_size_bytes = total_dram_buffer_size_tiles * dram_input_node->get_tile_size();
        ncrisc_config.dram_buf_read_chunk_size_tiles = dram_input_node->get_size_tiles();
        ncrisc_config.msg_size = dram_input_node->get_tile_size();

        // TODO: Check if this field is actually irrelevant and if it is, remove it.
        ncrisc_config.num_msgs = total_dram_buffer_size_tiles;

        ncrisc_config.dram_input = true;
        ncrisc_config.dram_io = false;
        if (dram_input_node->get_is_ram())
        {
            ncrisc_config.dram_ram = dram_input_node->get_is_ram();
        }
        if (dram_input_node->is_padding())
        {
            ncrisc_config.dram_padding = true;
        }

        return ncrisc_config;
    }

    NcriscConfig NcriscCreator::configure_dram_ncrisc_writer(const DataFlowInfo& data_flow_info,
                                                             const RGBasePipe* rg_pipe,
                                                             const SoCInfo* soc_info)
    {
        const DramOutputNode* dram_output_node = dynamic_cast<const DramOutputNode*>(rg_pipe->get_output_node());
        ASSERT(dram_output_node, "Expecting a DRAM output node at the pipe output");
        const RGBaseNode* first_input_node = rg_pipe->get_inputs()[0].get_node();

        const unsigned int msg_size = dram_output_node->get_tile_size();
        const unsigned long dram_buf_noc_addr =
            get_dram_buffer_noc_address(dram_output_node, dram_output_node->get_physical_location().chip, soc_info);
        const unsigned int dram_buf_size_tiles =
            dram_output_node->get_num_queue_slots() * dram_output_node->get_size_tiles();
        const unsigned int dram_buf_size_bytes = dram_buf_size_tiles * msg_size;
        const unsigned int dram_buf_write_chunk_size_tiles =
            calculate_dram_buf_write_chunk_size_tiles(data_flow_info, rg_pipe, dram_output_node);

        NcriscConfig ncrisc_config = create_ncrisc_write_config(
            data_flow_info.get_edge_phases(first_input_node, rg_pipe),
            msg_size,
            dram_buf_noc_addr,
            dram_buf_size_tiles,
            dram_buf_size_bytes,
            dram_output_node->get_num_queue_slots(),
            dram_buf_write_chunk_size_tiles,
            dram_output_node->get_is_ram());

        // TODO: set dram_io to false when buffer is epilogue.
        ncrisc_config.dram_io = true;

        if (dram_output_node->is_untilized_output())
        {
            ncrisc_config.dram_buf_write_row_chunk_size_bytes = dram_output_node->get_datums_row_size_bytes();
        }

        return ncrisc_config;
    }

    NcriscConfig NcriscCreator::configure_pcie_ncrisc_writer(const DataFlowInfo& data_flow_info,
                                                             const RGBasePipe* rg_pipe)
    {
        const PCIeStreamingNode* pcie_streaming_node =
            dynamic_cast<const PCIeStreamingNode*>(rg_pipe->get_output_node());
        if (pcie_streaming_node == nullptr)
        {
            const VirtualNode* v_node = dynamic_cast<const VirtualNode*>(rg_pipe->get_output_node());
            ASSERT(v_node, "Expecting either a Virtual or PCIe streaming node at the PCIe write pipe output");
            ASSERT(v_node->has_output_pipes(),
                   "Expecting virtual node at the PCIe write pipe output to be connected to the union pipe");
            pcie_streaming_node = dynamic_cast<const PCIeStreamingNode*>(v_node->get_output_pipe()->get_output_node());
            ASSERT(pcie_streaming_node, "Expecting a PCIe streaming node at the union pipe output");
        }

        const unsigned int msg_size = pcie_streaming_node->get_tile_size();
        // PCIe NOC address depends on the stream's L1 buffer address, so we will set it later
        // after resource allocation is done.
        const unsigned long dram_buf_noc_addr = 0;
        const unsigned int dram_buf_size_tiles =
            pcie_streaming_node->get_num_queue_slots() * pcie_streaming_node->get_size_tiles();
        const unsigned int dram_buf_size_bytes = dram_buf_size_tiles * msg_size;

        NcriscConfig ncrisc_config = create_ncrisc_write_config(
            data_flow_info.get_edge_phases(rg_pipe, rg_pipe->get_output_node()),
            msg_size,
            dram_buf_noc_addr,
            dram_buf_size_tiles,
            dram_buf_size_bytes,
            pcie_streaming_node->get_num_queue_slots(),
            pcie_streaming_node->get_size_tiles() /* dram_buf_write_chunk_size_tiles */,
            false /* is_ram */);

        ncrisc_config.dram_streaming = true;
        ncrisc_config.dram_streaming_downstream = pcie_streaming_node->is_streaming_downstream();

        return ncrisc_config;
    }

    NcriscConfig NcriscCreator::configure_dram_output_intermed_ncrisc_writer(
        const DramOutputIntermedNode* intermed_node, const SoCInfo* soc_info)
    {
        const unsigned int msg_size = intermed_node->get_tile_size();
        const unsigned long dram_buf_noc_addr =
            get_dram_buffer_noc_address(intermed_node, intermed_node->get_physical_location().chip, soc_info);
        const unsigned int dram_buf_size_tiles = intermed_node->get_size_tiles();
        const unsigned int dram_buf_size_bytes = dram_buf_size_tiles * msg_size;

        NcriscConfig ncrisc_config = create_ncrisc_write_config(
            intermed_node->get_num_epoch_tiles(),
            msg_size,
            dram_buf_noc_addr,
            dram_buf_size_tiles,
            dram_buf_size_bytes,
            intermed_node->get_num_queue_slots(),
            intermed_node->get_size_tiles() /* dram_buf_write_chunk_size_tiles */,
            true /* is_ram */);

        ncrisc_config.dram_buf_read_chunk_size_tiles = intermed_node->get_size_tiles();

        // Default values, always like this in blob for dram output intermed streams.
        ncrisc_config.dram_input = true;
        ncrisc_config.dram_io = false;

        return ncrisc_config;
    }

    // TODO: merge this and fanin count into a same function and cache its result per pipe
    unsigned int NcriscCreator::calculate_dram_pipe_scatter_chunk_size(const DataFlowInfo& data_flow_info,
                                                                       const RGBasePipe* rg_pipe)
    {
        unsigned int pipe_scatter_chunk_size = 0;
        for (const PipeInput& pipe_input : rg_pipe->get_inputs())
        {
            unsigned int incoming_chunk_size = data_flow_info.get_tiles_to_send(pipe_input.get_node());
            pipe_scatter_chunk_size = std::gcd(incoming_chunk_size, pipe_scatter_chunk_size);
        }
        return pipe_scatter_chunk_size;
    }

    unsigned int NcriscCreator::calculate_dram_pipe_read_chunk_size(
        const DataFlowInfo& data_flow_info,
        const std::vector<const RGBaseNode*>& unique_input_dram_nodes,
        const unsigned int max_dram_input_buffer_size_tiles)
    {
        unsigned int read_chunk_size = std::numeric_limits<unsigned int>::max();

        for (const RGBaseNode* input_node : unique_input_dram_nodes)
        {
            const DramInputNode* dram_input_node =
                dynamic_cast<const DramInputNode*>(VirtualNode::get_non_virtual_node(input_node));
            ASSERT(dram_input_node, "Expected dram input node as input to the pipe");

            read_chunk_size = std::min(
                read_chunk_size,
                calculate_dram_input_read_chunk_size(data_flow_info, dram_input_node, max_dram_input_buffer_size_tiles));
        }

        return read_chunk_size;
    }

    unsigned int NcriscCreator::calculate_dram_input_read_chunk_size(const DataFlowInfo& data_flow_info,
                                                                     const DramInputNode* dram_input_node,
                                                                     const unsigned int max_dram_input_buffer_size_tiles)
    {
        // TODO:
        // Assert that this input is scatter
        // In old pipegen this chunk is calculated only for dram IO & dram prefetch scatter POST TM
        // which by any means must be scatter.

        // TODO: To get the same result as pipegen v1, we have calculate max tiles per phase in the same way.
        // This makes testing easier, otherwise we sometimes compute different chunk size. This affects streams that are
        // allocated later and testing becomes a pain. Once we get to the point where pipegen v2 works, we can change
        // this back to the initial (commented) version and test it on silicon.
        //
        // const unsigned int phase_tiles = data_flow_info.get_max_num_tiles_per_phase(dram_input_node);
        unsigned int clear_granularity = 0;

        const RGBasePipe* rg_pipe = dram_input_node->get_output_pipe();

        // Start from the pipe and go down the graph until reaching a buffer.
        get_clear_granularity_of_downstream_buffers(rg_pipe, nullptr /* rg_node */, &clear_granularity);
        const unsigned int phase_tiles =
            (constants::general_max_num_tiles_per_phase / clear_granularity) * clear_granularity;

        unsigned int chunk_size = phase_tiles;
        int min_chunk_size = std::numeric_limits<int>::max();

        const DramParallelForkPipe* dram_fork_pipe = dynamic_cast<const DramParallelForkPipe*>(rg_pipe);
        if (dram_fork_pipe)
        {
            // Computing read chunk size with respect to all forks of the dram input node is needed when we actually
            // employ fork streams for it. In case when we have multiple independent Ncrisc readers, we don't have to do
            // this. But, pipegen v1 computes in this way regardless of whether readers are independent and for testing
            // purposes we must obey to it.
            // TODO: After pipegen v2 reaches point of working for all the cases, we should inspect if we can
            // get rid of this complexity.
            for (const RGBaseNode* v_node : dram_fork_pipe->get_output_nodes())
            {
                const RGBasePipe* out_pipe = v_node->get_output_pipe();
                chunk_size = std::gcd(chunk_size, out_pipe->get_num_tiles_to_transfer(data_flow_info));
                min_chunk_size = std::min(min_chunk_size, out_pipe->get_min_num_tiles_to_transfer(data_flow_info));
            }
        }
        else
        {
            chunk_size = std::gcd(chunk_size, rg_pipe->get_num_tiles_to_transfer(data_flow_info));
            min_chunk_size = rg_pipe->get_min_num_tiles_to_transfer(data_flow_info);
        }

        chunk_size = std::lcm(chunk_size, min_chunk_size);

        const unsigned int tile_size = dram_input_node->get_tile_size();
        const unsigned int max_transfer_size_bytes =
            get_dram_read_max_transfer_size_bytes(tile_size, max_dram_input_buffer_size_tiles);

        chunk_size = get_transfer_chunk_size_tiles(
            chunk_size, min_chunk_size, tile_size, constants::dram_input_stream_max_pending_read_tiles,
            max_transfer_size_bytes);
        ASSERT(chunk_size > 0, "Failed to compute valid read_chunk_size_tiles");

        return chunk_size;
    }

    unsigned int NcriscCreator::get_dram_read_max_transfer_size_bytes(const unsigned int dram_input_node_tile_size,
                                                                      const unsigned int max_dram_input_buffer_size_tiles)
    {
        if (max_dram_input_buffer_size_tiles == 0)
        {
            return constants::dram_input_stream_max_pending_read_bytes;
        }

        return std::min(constants::dram_input_stream_max_pending_read_bytes,
                        max_dram_input_buffer_size_tiles * dram_input_node_tile_size);
    }

    void NcriscCreator::get_clear_granularity_of_downstream_buffers(const RGBasePipe* rg_pipe,
                                                                    const RGBaseNode* rg_node,
                                                                    unsigned int* clear_granularity)
    {
        if (rg_pipe)
        {
            for (const RGBaseNode* output_node : rg_pipe->get_output_nodes())
            {
                get_clear_granularity_of_downstream_buffers(nullptr /* rg_pipe */, output_node, clear_granularity);
            }
            return;
        }

        // If we have a virtual node, get its parent RG node. Otherwise, use the RG node directly. Ultimately, we want
        // to get to the output node. If cast to output node fails, we have a virtual node that is not associated with
        // an output node. In this case, we skip it.
        const VirtualNode* v_node = dynamic_cast<const VirtualNode*>(rg_node);
        const RGBaseNode* non_virt_node = v_node ? v_node->get_parent_rg_node() : rg_node;
        const BaseOutputNode* out_node = dynamic_cast<const BaseOutputNode*>(non_virt_node);

        if (!out_node)
        {
            ASSERT(v_node, "Expected virtual node in case when rg_node is not an output node");
            // Skip virtual nodes that are not associated with an output node.
            get_clear_granularity_of_downstream_buffers(
                v_node->get_output_pipe(), nullptr /* rg_node */, clear_granularity);
            return;
        }

        *clear_granularity = std::gcd(*clear_granularity, out_node->get_transfer_granularity());
    }

    NcriscConfig NcriscCreator::create_dram_ncrisc_read_config(const RGBaseNode* input_node,
                                                               const RGBasePipe* rg_pipe,
                                                               const SoCInfo* soc_info,
                                                               const unsigned int dram_buf_read_chunk_size_tiles,
                                                               const unsigned int dram_scatter_chunk_size_tiles)
    {
        const DramInputNode* dram_input_node =
            dynamic_cast<const DramInputNode*>(VirtualNode::get_non_virtual_node(input_node));
        ASSERT(dram_input_node, "Expected dram input node as input to the pipe");

        std::vector<unsigned int> input_offsets = rg_pipe->get_input_node_offsets(input_node);

        const unsigned int num_msgs = input_offsets.size() * dram_input_node->get_scatter_gather_num_tiles();
        const unsigned int msg_size = dram_input_node->get_tile_size();
        const unsigned long dram_buf_noc_addr =
            get_dram_buffer_noc_address(dram_input_node, dram_input_node->get_physical_location().chip, soc_info);
        const unsigned int dram_buf_size_tiles =
            dram_input_node->get_size_tiles() * dram_input_node->get_num_queue_slots();
        const unsigned int dram_buf_size_bytes = dram_buf_size_tiles * msg_size;

        NcriscConfig ncrisc_config = create_ncrisc_read_config(
            num_msgs,
            msg_size,
            dram_buf_noc_addr,
            dram_buf_size_tiles,
            dram_buf_size_bytes,
            dram_input_node->get_num_queue_slots(),
            dram_buf_read_chunk_size_tiles,
            dram_input_node->get_is_ram(),
            dram_input_node->is_padding());

        ncrisc_config.dram_scatter_chunk_size_tiles = dram_scatter_chunk_size_tiles;
        if (dram_input_node->is_dram_io())
        {
            ncrisc_config.dram_io = true;
        }

        return ncrisc_config;
    }

    NcriscConfig NcriscCreator::create_ncrisc_read_config(const unsigned int num_msgs,
                                                          const unsigned int msg_size,
                                                          const unsigned long dram_buf_noc_addr,
                                                          const unsigned int dram_buf_size_tiles,
                                                          const unsigned int dram_buf_size_bytes,
                                                          const unsigned int dram_buf_size_q_slots,
                                                          const unsigned int dram_buf_read_chunk_size_tiles,
                                                          const bool is_ram,
                                                          const bool is_padding)
    {
        NcriscConfig ncrisc_config;

        ncrisc_config.num_msgs = num_msgs;
        ncrisc_config.msg_size = msg_size;
        ncrisc_config.dram_buf_noc_addr = dram_buf_noc_addr;
        ncrisc_config.dram_buf_size_tiles = dram_buf_size_tiles;
        ncrisc_config.dram_buf_size_bytes = dram_buf_size_bytes;
        ncrisc_config.dram_buf_size_q_slots = dram_buf_size_q_slots;
        ncrisc_config.dram_buf_read_chunk_size_tiles = dram_buf_read_chunk_size_tiles;
        ncrisc_config.dram_input = true;
        if (is_ram)
        {
            ncrisc_config.dram_ram = true;
        }
        if (is_padding)
        {
            ncrisc_config.dram_padding = true;
        }

        // Default values when dram input is not forked.
        ncrisc_config.reader_index = 0;
        ncrisc_config.total_readers = 1;

        return ncrisc_config;
    }

    NcriscConfig NcriscCreator::create_ncrisc_write_config(const std::vector<PhaseInfo>& phases,
                                                           const unsigned int msg_size,
                                                           const unsigned long dram_buf_noc_addr,
                                                           const unsigned int dram_buf_size_tiles,
                                                           const unsigned int dram_buf_size_bytes,
                                                           const unsigned int dram_buf_size_q_slots,
                                                           const unsigned int dram_buf_write_chunk_size_tiles,
                                                           const bool is_ram)
    {
        // All edge phases from any input node to this pipe have same num_msgs. They are stored in edge phases from pipe
        // to dram output node as well, but instead of accumulating them and dividing by num of input nodes, we can just
        // take num_msgs from any input node -> rg_pipe edge and sum them up.
        unsigned int num_msgs = 0;
        for (const PhaseInfo& phase_info : phases)
        {
            num_msgs += phase_info.num_msgs;
        }

        return create_ncrisc_write_config(num_msgs, msg_size, dram_buf_noc_addr, dram_buf_size_tiles,
                                          dram_buf_size_bytes, dram_buf_size_q_slots, dram_buf_write_chunk_size_tiles,
                                          is_ram);
    }

    NcriscConfig NcriscCreator::create_ncrisc_write_config(const unsigned int num_msgs,
                                                           const unsigned int msg_size,
                                                           const unsigned long dram_buf_noc_addr,
                                                           const unsigned int dram_buf_size_tiles,
                                                           const unsigned int dram_buf_size_bytes,
                                                           const unsigned int dram_buf_size_q_slots,
                                                           const unsigned int dram_buf_write_chunk_size_tiles,
                                                           const bool is_ram)
    {
        NcriscConfig ncrisc_config;

        ncrisc_config.num_msgs = num_msgs;
        ncrisc_config.msg_size = msg_size;
        ncrisc_config.dram_buf_noc_addr = dram_buf_noc_addr;
        ncrisc_config.dram_buf_size_tiles = dram_buf_size_tiles;
        ncrisc_config.dram_buf_size_bytes = dram_buf_size_bytes;
        ncrisc_config.dram_buf_size_q_slots = dram_buf_size_q_slots;
        ncrisc_config.dram_buf_write_chunk_size_tiles = dram_buf_write_chunk_size_tiles;
        ncrisc_config.dram_output = true;
        if (is_ram)
        {
            ncrisc_config.dram_ram = true;
        }

        return ncrisc_config;
    }

    unsigned int NcriscCreator::calculate_dram_buf_write_chunk_size_tiles(const DataFlowInfo& data_flow_info,
                                                                          const RGBasePipe* rg_pipe,
                                                                          const DramOutputNode* dram_output_node)
    {
        unsigned int inputs_chunk_size = 0;

        const RGBaseNode* input_node = rg_pipe->get_inputs()[0].get_non_virtual_node();
        const PackerInputNode* packer_input_node = dynamic_cast<const PackerInputNode*>(input_node);

        // PackerInputNode might be scattered so we have to take care of that case.
        if (packer_input_node && packer_input_node->is_scatter())
        {
            const RGBaseNode* pipe_virtual_input_node = rg_pipe->get_inputs()[0].get_node();

            // We'll calculate GCD of sending chunk sizes over all pipe inputs.
            for (const PhaseInfo& phase_info : data_flow_info.get_edge_phases(pipe_virtual_input_node, rg_pipe))
            {
                inputs_chunk_size = std::gcd(inputs_chunk_size, phase_info.num_msgs);
            }
        }
        else
        {
            inputs_chunk_size = std::gcd(input_node->get_scatter_gather_num_tiles(), input_node->get_size_tiles());
        }

        unsigned int min_write_chunk_size_tiles = 1;
        unsigned int write_chunk_size_tiles = 0;
        unsigned int max_transfer_size_bytes = constants::dram_output_stream_max_write_issue_bytes;

        if (dram_output_node->is_untilized_output())
        {
            ASSERT(packer_input_node, "Expected packer input node as input to the pipe when untilizing data");
            write_chunk_size_tiles = dram_output_node->get_untilized_output_r_dim() *
                                     dram_output_node->get_untilized_output_c_dim() *
                                     dram_output_node->get_untilized_output_z_dim();
            write_chunk_size_tiles = std::gcd(write_chunk_size_tiles, inputs_chunk_size);
            for (const auto& phase_info : data_flow_info.get_edge_phases(rg_pipe, dram_output_node))
            {
                write_chunk_size_tiles = std::gcd(write_chunk_size_tiles, phase_info.num_msgs);
            }
            min_write_chunk_size_tiles = packer_input_node->get_blocking_params().get_mblock_n() *
                                         packer_input_node->get_blocking_params().get_ublock_rt() *
                                         packer_input_node->get_blocking_params().get_ublock_ct();
            write_chunk_size_tiles = std::lcm(write_chunk_size_tiles, min_write_chunk_size_tiles);

            // TODO: find out why we use L1's MAX_SIZE in this case. Can we set this to some other value?
            max_transfer_size_bytes = l1_mem::address_map::MAX_SIZE;
        }
        else
        {
            write_chunk_size_tiles = std::gcd(dram_output_node->get_size_tiles(), inputs_chunk_size);
        }

        const unsigned int tile_size = dram_output_node->get_tile_size();
        write_chunk_size_tiles = get_transfer_chunk_size_tiles(write_chunk_size_tiles,
                                                               min_write_chunk_size_tiles,
                                                               tile_size,
                                                               constants::dram_output_stream_max_write_issue_tiles,
                                                               max_transfer_size_bytes);
        ASSERT(write_chunk_size_tiles > 0, "Failed to compute valid write_chunk_size_tiles");

        return write_chunk_size_tiles;
    }

    unsigned int NcriscCreator::get_transfer_chunk_size_tiles(unsigned int transfer_chunk_size_tiles,
                                                              const unsigned int min_transfer_chunk_size_tiles,
                                                              const unsigned int tile_size_bytes,
                                                              const unsigned int max_transfer_size_tiles,
                                                              const unsigned int max_transfer_size_bytes)
    {
        if (!is_transfer_chunk_size_within_limits(
                transfer_chunk_size_tiles, tile_size_bytes, max_transfer_size_tiles, max_transfer_size_bytes))
        {
            for (unsigned int div = 2; div <= transfer_chunk_size_tiles; ++div)
            {
                if (transfer_chunk_size_tiles % div != 0)
                {
                    continue;
                }
                unsigned int new_chunk_size = transfer_chunk_size_tiles / div;
                if (new_chunk_size % min_transfer_chunk_size_tiles == 0 &&
                    is_transfer_chunk_size_within_limits(
                        new_chunk_size, tile_size_bytes, max_transfer_size_tiles, max_transfer_size_bytes))
                {
                    transfer_chunk_size_tiles = new_chunk_size;
                    break;
                }
            }
        }
        if (!is_transfer_chunk_size_within_limits(
                transfer_chunk_size_tiles, tile_size_bytes, max_transfer_size_tiles, max_transfer_size_bytes))
        {
            transfer_chunk_size_tiles = min_transfer_chunk_size_tiles;
        }

        return transfer_chunk_size_tiles;
    }

    bool NcriscCreator::is_transfer_chunk_size_within_limits(const unsigned int transfer_chunk_size_tiles,
                                                             const unsigned int tile_size_bytes,
                                                             const unsigned int max_transfer_size_tiles,
                                                             const unsigned int max_transfer_size_bytes)
    {
        return transfer_chunk_size_tiles <= max_transfer_size_tiles &&
               transfer_chunk_size_tiles * tile_size_bytes <= max_transfer_size_bytes;
    }

    unsigned long NcriscCreator::get_dram_buffer_noc_address(const DramNodeInterface* dram_node, ChipId chip_id,
                                                             const SoCInfo* soc_info)
    {
        // TODO: Nowadays remote_io should never be true for DRAM nodes, only for PCIe streaming nodes.
        //       That is legacy path that was used before, and net2pipe should never set it today.
        //       We should investigate to confirm, and then remove this logic if it is not needed.
        return dram_node->get_is_remote_io() ?
            soc_info->get_buffer_noc_address_through_pcie(dram_node->get_dram_address(), chip_id) :
            soc_info->get_dram_buffer_noc_address(dram_node->get_dram_address(), chip_id, dram_node->get_dram_channel(),
                                                  dram_node->get_dram_subchannel());
    }
}