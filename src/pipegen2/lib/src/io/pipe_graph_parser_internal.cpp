// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/pipe_graph_parser_internal.h"

#include <stdexcept>

#include "device/tt_xy_pair.h"

namespace pipegen2
{
    void throw_parsing_error(const std::string& error_msg)
    {
        throw std::runtime_error(c_parsing_error_msg_prefix + error_msg);
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
}
