// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/pipe_graph_parser.h"

#include <algorithm>

#include "io/pipe_graph_parser_internal.h"

namespace pipegen2
{
    const std::string PipeGraphParser::s_buffer_prefix = "buffer_";
    const std::string PipeGraphParser::s_pipe_prefix = "pipe_";
    const std::string PipeGraphParser::s_delimiter_prefix = "--";

    PipeGraphParser::PipeGraphParser(const std::string& pipegen_yaml_path) :
        m_pipegen_yaml_stream(pipegen_yaml_path)
    {
    }

    void PipeGraphParser::parse_graph(PipeGraph& pipe_graph)
    {
        std::vector<std::string> yaml_lines;
        std::string current_line;

        while (std::getline(m_pipegen_yaml_stream, current_line))
        {
            if (!(string_starts_with(current_line, s_buffer_prefix) || string_starts_with(current_line, s_pipe_prefix)) && yaml_lines.empty())
            {
                // Skipping lines outside of buffer and pipe definitions.
                continue;
            }
            else if (string_starts_with(current_line, s_delimiter_prefix))
            {
                if (yaml_lines.empty())
                {
                    throw_parsing_error("Found empty graph node");
                }

                parse_node(yaml_lines, pipe_graph);
                yaml_lines.clear();
            }
            else
            {
                yaml_lines.push_back(current_line);
            }
        }

        if (!yaml_lines.empty())
        {
            parse_node(yaml_lines, pipe_graph);
        }
    }

    void PipeGraphParser::parse_node(const std::vector<std::string>& yaml_lines, PipeGraph& pipe_graph)
    {
        if (string_starts_with(yaml_lines[0], s_buffer_prefix))
        {
            parse_buffer(yaml_lines, pipe_graph);
        }
        else if (string_starts_with(yaml_lines[0], s_pipe_prefix))
        {
            parse_pipe(yaml_lines, pipe_graph);
        }
        else
        {
            throw_parsing_error("Found graph node other than buffer and pipe");
        }
    }

    void PipeGraphParser::parse_buffer(const std::vector<std::string>& yaml_lines, PipeGraph& pipe_graph)
    {
        std::unique_ptr<PGBuffer> buffer = std::make_unique<PGBuffer>();

        for (std::size_t i = 1; i < yaml_lines.size(); ++i)
        {
            std::string attr_name, attr_value;
            parse_attribute(yaml_lines[i], attr_name, attr_value);

            if (attr_name == "buffer_type")
            {
                buffer->set_type(parse_buffer_type_string(attr_value));
            }
            else if (attr_name == "uniqid")
            {
                buffer->set_id(parse_ulong_attribute_value(attr_value));
            }
            else if (attr_name == "id")
            {
                parse_buffer_operand_id(buffer.get(), attr_value);
            }
            else if (attr_name == "epoch_tiles")
            {
                buffer->set_num_epoch_tiles(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "size_tiles")
            {
                buffer->set_size_tiles(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "tile_size")
            {
                buffer->set_tile_size(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "tiles_per_input") {
                buffer->set_num_tiles_per_input(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "scatter_gather_num_tiles")
            {
                buffer->set_scatter_gather_num_tiles(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "q_slots")
            {
                buffer->set_num_queue_slots(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "dram_io_flag")
            {
                buffer->set_dram_io_flag(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "dram_io_flag_is_remote")
            {
                buffer->set_dram_io_flag_is_remote(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "dram_buf_streaming")
            {
                buffer->set_dram_buf_streaming(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "dram_buf_flag")
            {
                buffer->set_dram_buf_flag(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "write_dram_buf_flag")
            {
                buffer->set_write_dram_buf_flag(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "dram_ram_flag")
            {
                buffer->set_dram_ram_flag(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "untilized_output")
            {
                buffer->set_moves_raw_data(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "untilized_output_full_r_dim")
            {
                buffer->set_untilized_output_full_r_dim(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "untilized_output_full_c_dim")
            {
                buffer->set_untilized_output_full_c_dim(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "untilized_output_r_dim")
            {
                buffer->set_untilized_output_r_dim(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "untilized_output_c_dim")
            {
                buffer->set_untilized_output_c_dim(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "untilized_output_z_dim")
            {
                buffer->set_untilized_output_z_dim(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "untilized_output_type_0_zdim")
            {
                buffer->set_untilized_output_type_0_zdim(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "untilized_output_type_1_zdim")
            {
                buffer->set_untilized_output_type_1_zdim(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "untilized_output_tile_dim_r")
            {
                unsigned int untilized_output_tile_dim_r = parse_uint_attribute_value(attr_value);
                // If tile size is not set, leave the default value.
                if (untilized_output_tile_dim_r > 0)
                {
                    buffer->set_untilized_output_tile_dim_r(untilized_output_tile_dim_r);
                }
            }
            else if (attr_name == "untilized_output_tile_dim_c")
            {
                unsigned int untilized_output_tile_dim_c = parse_uint_attribute_value(attr_value);
                // If tile size is not set, leave the default value.
                if (untilized_output_tile_dim_c > 0)
                {
                    buffer->set_untilized_output_tile_dim_c(untilized_output_tile_dim_c);
                }
            }
            else if (attr_name == "ublock_rt")
            {
                buffer->set_ublock_rt(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "ublock_ct")
            {
                buffer->set_ublock_ct(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "mblock_m")
            {
                buffer->set_mblock_m(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "mblock_n")
            {
                buffer->set_mblock_n(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "mblock_k")
            {
                buffer->set_mblock_k(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "tile_clear_granularity")
            {
                buffer->set_tile_clear_granularity(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "buffer_space_shared")
            {
                buffer->set_shared_space_buffer_id(parse_ulong_attribute_value(attr_value));
            }
            else if (attr_name == "producer_epoch_id")
            {
                buffer->set_producer_epoch_id(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "is_scatter")
            {
                buffer->set_is_scatter(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "replicate")
            {
                buffer->set_replicate_count(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "ethernet_chan")
            {
                buffer->set_ethernet_channel(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "dram_chan")
            {
                buffer->set_dram_channel(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "dram_sub_chan")
            {
                buffer->set_dram_sub_channel(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "dram_addr")
            {
                buffer->set_dram_address(parse_ulong_attribute_value(attr_value));
            }
            else if (attr_name == "chip_id")
            {
                parse_buffer_chip_id(buffer.get(), attr_value);
            }
            else if (attr_name == "core_coordinates")
            {
                parse_buffer_location(buffer.get(), attr_value);
            }
            else if (attr_name == "dram_prefetch_incoming_noc_id")
            {
                // All NOC IDs are assigned by net2pipe through the incoming/outgoing noc_id attributes of pipes.
                // The only case where we need to tag buffers with NOC ID is for prefetch buffers, which have no
                // explicit pipes for preloading.
                buffer->set_dram_prefetch_incoming_noc_id(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "prefetch_type")
            {
                buffer->set_prefetch_type(static_cast<PGBuffer::PrefetchType>(parse_int_attribute_value(attr_value)));
            }
            else if (attr_name == "embedding_table")
            {
                buffer->set_embedding_table(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "embedding_table_core_c_div")
            {
                buffer->set_embedding_table_core_c_div(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "embedding_table_row_size_per_core")
            {
                buffer->set_embedding_table_row_size_per_core(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "embedding_index")
            {
                buffer->set_embedding_index(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "embedding_indices_per_tile")
            {
                buffer->set_embedding_indices_per_tile(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "embedding_indices_per_input")
            {
                buffer->set_embedding_indices_per_input(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "hw_tilize")
            {
                buffer->set_hw_tilize(bool(parse_int_attribute_value(attr_value)));
            }
            else if (attr_name == "tilize_mblock_n_loop_num_rows")
            {
                buffer->set_tilize_mblock_n_loop_num_rows(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "tilize_row_col_offset")
            {
                buffer->set_tilize_row_col_offset(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "is_padding")
            {
                buffer->set_is_padding(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "use_ethernet_fw_stream")
            {
                buffer->set_use_ethernet_fw_stream(bool(parse_int_attribute_value(attr_value)));
            }
            else if (attr_name == "overlay_blob_size")
            {
                buffer->set_overlay_blob_size(parse_uint_attribute_value(attr_value));
            }
            else if (attr_name == "is_post_tm_relay_buf")
            {
                buffer->set_is_post_tm_relay_buf(bool(parse_int_attribute_value(attr_value)));
            }
        }

        pipe_graph.add_buffer(std::move(buffer));
    }

    void PipeGraphParser::parse_pipe(const std::vector<std::string>& yaml_lines, PipeGraph& pipe_graph)
    {
        std::unique_ptr<PGPipe> pipe = std::make_unique<PGPipe>();

        for (std::size_t i = 1; i < yaml_lines.size(); ++i)
        {
            std::string attr_name, attr_value;
            parse_attribute(yaml_lines[i], attr_name, attr_value);

            if (attr_name == "id")
            {
                pipe->set_id(parse_ulong_attribute_value(attr_value));
            }
            else if (attr_name == "pipe_periodic_repeat")
            {
                pipe->set_pipe_periodic_repeat(std::max((unsigned int)1, parse_uint_attribute_value(attr_value)));
            }
            else if (attr_name == "pipe_consumer_repeat")
            {
                pipe->set_consumer_repeat(std::max((unsigned int)1, parse_uint_attribute_value(attr_value)));
            }
            else if (attr_name == "ethernet_chan")
            {
                // Ethernet channel for pipes is written as signed integer from net2pipe.
                pipe->set_ethernet_channel(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "incoming_noc_id")
            {
                pipe->set_incoming_noc_id(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "incoming_vc")
            {
                pipe->set_incoming_noc_vc(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "outgoing_noc_id")
            {
                pipe->set_outgoing_noc_id(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "outgoing_vc")
            {
                pipe->set_outgoing_noc_vc(parse_int_attribute_value(attr_value));
            }
            else if (attr_name == "mmio_pipe")
            {
                pipe->set_is_mmio_pipe(parse_ulong_attribute_value(attr_value) != 0);
            }
            else if (attr_name == "mmio_pipe_downstream")
            {
                pipe->set_is_mmio_pipe_downstream(parse_ulong_attribute_value(attr_value) != 0);
            }
            else if (attr_name == "ethernet_pipe")
            {
                pipe->set_is_ethernet_pipe(parse_ulong_attribute_value(attr_value) != 0);
            }
            else if (attr_name == "dis_gather_opt")
            {
                pipe->set_gather_optimization_disabled(parse_ulong_attribute_value(attr_value) != 0);
            }
            else if (attr_name == "direct_mcast")
            {
                pipe->set_packer_multicast_optimization_enabled(parse_ulong_attribute_value(attr_value) != 0);
            }
            else if (attr_name == "op_input_dram_io_buf_size_tiles")
            {
                pipe->set_op_input_dram_io_buf_size_tiles(parse_ulong_attribute_value(attr_value));
            }
            else if (attr_name == "mcast_core_rc")
            {
                parse_pipe_mcast_locations(pipe.get(), attr_value);
            }
            else if (attr_name == "dram_pipe_total_readers")
            {
                pipe->set_dram_pipe_total_readers(parse_vector_of_ints(attr_value));
            }
            else if (attr_name == "dram_pipe_reader_index")
            {
                pipe->set_dram_pipe_reader_index(parse_vector_of_ints(attr_value));
            }
            else if (attr_name == "input_list")
            {
                parse_pipe_inputs(pipe.get(), attr_value);
            }
            else if (attr_name == "output_list")
            {
                parse_pipe_outputs(pipe.get(), attr_value);
            }
            else if (attr_name == "output_padding_list")
            {
                parse_pipe_output_padding_list(pipe.get(), attr_value);
            }
        }

        pipe_graph.add_pipe(std::move(pipe));
    }
}
