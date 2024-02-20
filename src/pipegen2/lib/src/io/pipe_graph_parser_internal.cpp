// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/pipe_graph_parser_internal.h"

#include <iostream>

#include "device/tt_xy_pair.h"
#include "model/typedefs.h"
#include "pipegen2_exceptions.h"

namespace pipegen2
{
namespace pipe_graph_parser_internal
{

void parse_pipe(const std::vector<std::string>& yaml_lines, PipeGraph& pipe_graph)
{
    std::unique_ptr<PGPipe> pipe = std::make_unique<PGPipe>();

    if (yaml_lines.size() < 2)
    {
        return;
    }

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

void parse_buffer(const std::vector<std::string>& yaml_lines, PipeGraph& pipe_graph)
{
    std::unique_ptr<PGBuffer> buffer = std::make_unique<PGBuffer>();

    if (yaml_lines.size() < 2)
    {
        return;
    }

    for (std::size_t i = 1; i < yaml_lines.size(); ++i)
    {
        std::string attr_name, attr_value;
        parse_attribute(yaml_lines[i], attr_name, attr_value);

        if (attr_name == "md_op_name")
        {
            buffer->set_op_name(attr_value);
        }
        else if (attr_name == "buffer_type")
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
            buffer->set_prefetch_type(static_cast<PrefetchType>(parse_int_attribute_value(attr_value)));
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

void parse_node(const std::vector<std::string>& yaml_lines, PipeGraph& pipe_graph)
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

void parse_graph(PipeGraph& pipe_graph, std::istream& input_stream)
{
    std::vector<std::string> yaml_lines;
    std::string current_line;

    while (std::getline(input_stream, current_line))
    {
        if (!string_starts_with(current_line, s_buffer_prefix) &&
            !string_starts_with(current_line, s_pipe_prefix) &&
            yaml_lines.empty())
        {
            // Skipping comments, newlines or delimiters.
            if (string_starts_with(current_line, s_comment_prefix) ||
                string_starts_with(current_line, s_graph_name_prefix) ||
                string_starts_with(current_line, s_delimiter_prefix) ||
                current_line.empty())
            {
                continue;
            }
            else
            {
                throw_parsing_error("Found invalid line");
            }
        }
        else if (string_starts_with(current_line, s_delimiter_prefix))
        {
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

void throw_parsing_error(const std::string& error_msg)
{
    throw InvalidPipegenYamlFormatException(c_parsing_error_msg_prefix + error_msg);
}

void parse_attribute(const std::string& yaml_line, std::string& attr_name, std::string& attr_value)
{
    const char attribute_delimiter = ':';

    // Split on the first colon, thus allowing to have more colons in the attribute value.
    std::vector<std::string> line_parts = split_string_once(yaml_line, attribute_delimiter);
    if (line_parts.size() != 2)
    {
        throw_parsing_error("Expecting at least one colon in the attribute line: " + yaml_line);
    }

    attr_name = line_parts[0];
    attr_value = line_parts[1];
}

int parse_int_attribute_value(const std::string& attr_value)
{
    try
    {
        return std::stoi(attr_value, 0, 0);
    }
    catch (...)
    {
        throw_parsing_error("Found invalid integer value");
        return 0;
    }
}

unsigned int parse_uint_attribute_value(const std::string& attr_value)
{
    try
    {
        return (unsigned int)std::stoul(attr_value, 0, 0);
    }
    catch (...)
    {
        throw_parsing_error("Found invalid unsigned integer value");
        return 0;
    }
}

std::uint64_t parse_ulong_attribute_value(const std::string& attr_value)
{
    try
    {
        return std::stoull(attr_value, 0, 0);
    }
    catch (...)
    {
        throw_parsing_error("Found invalid unsigned long value");
        return 0;
    }
}

void parse_buffer_operand_id(PGBuffer* buffer, const std::string& attr_value)
{
    buffer->set_operand_id(parse_int_attribute_value(attr_value));
    if (buffer->get_operand_id() < 0)
    {
        throw_parsing_error("Operand ID is invalid");
    }
}

void parse_buffer_chip_id(PGBuffer* buffer, const std::string& attr_value)
{
    std::vector<int> chip_ids = parse_vector_of_ints(attr_value);

    // Currently chip id is a single number.
    if (chip_ids.size() != 1)
    {
        throw_parsing_error("Chip Id is invalid");
    }

    buffer->set_chip_id(chip_ids[0]);
}

void parse_buffer_location(PGBuffer* buffer, const std::string& attr_value)
{
    std::vector<int> core_coordinates = parse_vector_of_ints(attr_value);

    if (core_coordinates.size() != 2)
    {
        throw_parsing_error("Buffer core coordinates are invalid");
    }

    int core_y = core_coordinates[0];
    buffer->set_logical_core_y((std::size_t)core_y);
    int core_x = core_coordinates[1];
    buffer->set_logical_core_x((std::size_t)core_x);
}

void parse_pipe_mcast_locations(PGPipe* pipe, const std::string& attr_value)
{
    std::vector<std::vector<int>> locations_coords;

    if (string_starts_with(attr_value, "[["))
    {
        locations_coords = parse_two_dim_vector_of_ints(attr_value);
    }
    else
    {
        locations_coords.push_back(parse_vector_of_ints(attr_value));
    }

    for (const std::vector<int>& location_coords: locations_coords)
    {
        if (location_coords.size() != 3)
        {
            throw_parsing_error("Pipe multicast core location coordinates are invalid");
        }

        pipe->add_mcast_core_logical_location(tt_cxy_pair(static_cast<ChipId>(location_coords[0]),
                                                            (std::size_t)location_coords[2],
                                                            (std::size_t)location_coords[1]));
    }
}

void parse_pipe_inputs(PGPipe* pipe, const std::string& attr_value)
{
    pipe->set_input_buffers_ids(convert_ul_to_node_id_vector(parse_vector_of_ulongs(attr_value)));
}

void parse_pipe_output_padding_list(PGPipe* pipe, const std::string& attr_value)
{
    pipe->set_output_padding_buffers_ids(convert_ul_to_node_id_vector(parse_vector_of_ulongs(attr_value)));
}

void parse_pipe_outputs(PGPipe* pipe, const std::string& attr_value)
{
    std::vector<std::vector<NodeId>> outputNodesIds;

    if (string_starts_with(attr_value, "[["))
    {
        std::vector<std::vector<std::uint64_t>> ul_ids_vec = parse_two_dim_vector_of_ulongs(attr_value);
        for (const std::vector<std::uint64_t>& ul_ids : ul_ids_vec)
        {
            outputNodesIds.push_back(convert_ul_to_node_id_vector(ul_ids));
        }
    }
    else
    {
        outputNodesIds.push_back(convert_ul_to_node_id_vector(parse_vector_of_ulongs(attr_value)));
    }

    pipe->set_output_buffers_ids(outputNodesIds);
}

std::vector<int> parse_vector_of_ints(const std::string& str)
{
    return convert_strings_to_ints(parse_vector_of_strings(str));
}

std::vector<std::vector<int>> parse_two_dim_vector_of_ints(const std::string& str)
{
    std::vector<std::vector<std::string>> two_dim_list_of_strings = parse_two_dim_vector_of_strings(str);

    std::vector<std::vector<int>> two_dim_list_of_ints;

    for (const std::vector<std::string>& list_of_strings: two_dim_list_of_strings)
    {
        two_dim_list_of_ints.push_back(convert_strings_to_ints(list_of_strings));
    }

    return two_dim_list_of_ints;
}

std::vector<int> convert_strings_to_ints(const std::vector<std::string>& string_values)
{
    std::vector<int> int_values;

    for (const std::string& str_val: string_values)
    {
        int_values.push_back(parse_int_attribute_value(str_val));
    }

    return int_values;
}

std::vector<NodeId> convert_ul_to_node_id_vector(const std::vector<std::uint64_t> ul_ids)
{
    std::vector<NodeId> node_ids;
    for (std::uint64_t id : ul_ids)
    {
        node_ids.push_back((NodeId)id);
    }

    return node_ids;
}

std::vector<std::uint64_t> parse_vector_of_ulongs(const std::string& str)
{
    return convert_strings_to_ulongs(parse_vector_of_strings(str));
}

std::vector<std::vector<std::uint64_t>> parse_two_dim_vector_of_ulongs(const std::string& str)
{
    std::vector<std::vector<std::string>> two_dim_list_of_strings = parse_two_dim_vector_of_strings(str);

    std::vector<std::vector<std::uint64_t>> two_dim_list_of_ulongs;

    for (const std::vector<std::string>& list_of_strings: two_dim_list_of_strings)
    {
        two_dim_list_of_ulongs.push_back(convert_strings_to_ulongs(list_of_strings));
    }

    return two_dim_list_of_ulongs;
}

std::vector<std::uint64_t> convert_strings_to_ulongs(const std::vector<std::string>& string_values)
{
    std::vector<std::uint64_t> ulong_values;

    for (const std::string& str_val: string_values)
    {
        ulong_values.push_back(parse_ulong_attribute_value(str_val));
    }

    return ulong_values;
}

std::vector<std::string> parse_vector_of_strings(const std::string& str)
{
    if (str.size() < 2 || str.front() != '[' || str.back() != ']')
    {
        throw_parsing_error("Found improperly formatted list attribute");
    }

    return split_string(str.substr(1, str.size() - 2), ',');
}

std::vector<std::vector<std::string>> parse_two_dim_vector_of_strings(const std::string& str)
{
    if (str.size() < 4 || str.front() != '[' || str[1] != '[' || str[str.size() - 2] != ']' || str.back() != ']')
    {
        throw_parsing_error("Found improperly formatted two-dimensional list attribute");
    }

    std::vector<std::vector<std::string>> two_dim_list_of_strings;

    // Splitting on right brackets first:
    // "[[1,2,3],[4,5,6],[7,8,9]]" -> {"[1,2,3" , ",[4,5,6" , ",[7,8,9"}
    std::vector<std::string> strs_between_rbrackets = split_string(str.substr(1, str.size() - 3), ']');
    for (const std::string& str_between_rbrackets: strs_between_rbrackets)
    {
        // Splitting on left bracket now:
        // "[1,2,3" -> {"" , "1,2,3"}
        // ",[4,5,6" -> {"," , "4,5,6"}
        std::vector<std::string> strs_around_lbracket = split_string(str_between_rbrackets, '[');

        if (strs_around_lbracket.size() != 2)
        {
            throw_parsing_error("Found improperly formatted two-dimensional list attribute");
        }

        // Finally, splitting list of numbers on the comma.
        // "1,2,3" -> {"1" , "2" , "3"}
        two_dim_list_of_strings.push_back(split_string(strs_around_lbracket[1], ','));
    }

    return two_dim_list_of_strings;
}

std::vector<std::string> split_string_once(const std::string& str, const char delimiter)
{
    std::vector<std::string> substrings;

    std::size_t delimiter_pos = str.find_first_of(delimiter);
    if (delimiter_pos == std::string::npos)
    {
        substrings.push_back(trim_string(str));
    }
    else
    {
        substrings.push_back(trim_string(str.substr(0, delimiter_pos)));
        substrings.push_back(trim_string(str.substr(delimiter_pos + 1, str.size() - delimiter_pos - 1)));
    }

    return substrings;
}

std::vector<std::string> split_string(const std::string& str, const char delimiter)
{
    std::vector<std::string> substrings;

    std::size_t start_pos = 0;
    std::size_t delimiter_pos = str.find_first_of(delimiter);
    while (delimiter_pos != std::string::npos)
    {
        substrings.push_back(trim_string(str.substr(start_pos, delimiter_pos - start_pos)));

        start_pos = delimiter_pos + 1;
        delimiter_pos = str.find_first_of(delimiter, start_pos);
    }

    if (str.empty() || str.back() == delimiter)
    {
        substrings.push_back("");
    }
    else if (start_pos <= str.size() - 1)
    {
        substrings.push_back(trim_string(str.substr(start_pos, str.size() - start_pos)));
    }

    return substrings;
}

std::string trim_string(const std::string& str)
{
    const std::string whitespaces = " \t\f\v\n\r";
    std::size_t start_pos = str.find_first_not_of(whitespaces);
    std::size_t end_pos = str.find_last_not_of(whitespaces);

    if (start_pos == std::string::npos || end_pos == std::string::npos)
    {
        return "";
    }
    else if (start_pos == 0 && end_pos == str.size() - 1)
    {
        return str;
    }

    return str.substr(start_pos, end_pos - start_pos + 1);
}

bool string_starts_with(const std::string& str, const std::string& prefix)
{
    return str.rfind(prefix, 0) == 0;
}

BufferType parse_buffer_type_string(const std::string& s)
{
    if (s == "dram_prolog")
    {
        return BufferType::kDramProlog;
    }
    else if (s == "dram_epilog")
    {
        return BufferType::kDramEpilog;
    }
    else if (s == "gradient_op")
    {
        return BufferType::kGradientOp;
    }
    else if (s == "intermediate")
    {
        return BufferType::kIntermediate;
    }
    else if (s == "packer")
    {
        return BufferType::kPacker;
    }
    else if (s == "unpacker")
    {
        return BufferType::kUnpacker;
    }
    else if (s == "dram_io")
    {
        return BufferType::kDramIO;
    }
    else if (s == "relay")
    {
        return BufferType::kRelay;
    }
    else if (s == "ethernet_relay")
    {
        return BufferType::kEthernetRelay;
    }
    else if (s == "prolog_relay")
    {
        return BufferType::kPrologRelay;
    }
    else
    {
        return BufferType::kUnknown;
    }
}

} // namespace pipe_graph_parser_internal
} // namespace pipegen2
