// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "device/soc_info.h"
#include "model/pipe_graph/pg_buffer.h"
#include "model/pipe_graph/pg_pipe.h"
#include "model/typedefs.h"

namespace pipegen2
{
    // Parsing error message prefix.
    const std::string c_parsing_error_msg_prefix = "Error parsing pipegen graph yaml: ";

    // Throws parsing error with given message.
    void throw_parsing_error(const std::string& error_msg);

    // Parses attribute name and value from yaml line.
    void parse_attribute(const std::string& yaml_line, std::string& attr_name, std::string& attr_value);

    // Parses int attribute value.
    int parse_int_attribute_value(const std::string& attr_value);

    // Parses unsigned int attribute value.
    unsigned int parse_uint_attribute_value(const std::string& attr_value);

    // Parses unsigned long attribute value.
    std::uint64_t parse_ulong_attribute_value(const std::string& attr_value);

    // Parses operand id attribute of the buffer.
    void parse_buffer_operand_id(PGBuffer* buffer, const std::string& attr_value);

    // Parses chip id attribute of the buffer.
    void parse_buffer_chip_id(PGBuffer* buffer, const std::string& attr_value);

    // Parses buffer location.
    void parse_buffer_location(PGBuffer* buffer, const std::string& attr_value);

    // Parses pipe multicast cores locations.
    void parse_pipe_mcast_locations(PGPipe* pipe, const std::string& attr_value);

    // Parses list of pipe inputs.
    void parse_pipe_inputs(PGPipe* pipe, const std::string& attr_value);

    // Parses list of pipe outputs.
    void parse_pipe_outputs(PGPipe* pipe, const std::string& attr_value);

    // Parses list of pipe output padding buffers.
    void parse_pipe_output_padding_list(PGPipe* pipe, const std::string& attr_value);

    // Parses vector of ints from string: "[1, 2, 3]" -> {1, 2, 3}
    std::vector<int> parse_vector_of_ints(const std::string& str);

    // Parses two-dimensional vector of ints from string:
    // "[[1,2,3],[4,5,6],[7,8,9]]" -> {{1,2,3},{4,5,6},{7,8,9}}
    std::vector<std::vector<int>> parse_two_dim_vector_of_ints(const std::string& str);

    // Converts vector of strings to vector of ints.
    std::vector<int> convert_strings_to_ints(const std::vector<std::string>& string_values);

    // Converts vector of unsigned longs to vector of node ids.
    std::vector<NodeId> convert_ul_to_node_id_vector(const std::vector<std::uint64_t> ul_ids);

    // Parses vector of unsigned longs from string: "[1, 2, 3]" -> {1, 2, 3}
    std::vector<std::uint64_t> parse_vector_of_ulongs(const std::string& str);

    // Parses two-dimensional vector of unsigned longs from string:
    // "[[1,2,3],[4,5,6],[7,8,9]]" -> {{1,2,3},{4,5,6},{7,8,9}}
    std::vector<std::vector<std::uint64_t>> parse_two_dim_vector_of_ulongs(const std::string& str);

    // Converts vector of strings to vector of unsigned longs.
    std::vector<std::uint64_t> convert_strings_to_ulongs(const std::vector<std::string>& string_values);

    // Parses vector of strings from string: "[1, 2, 3]" -> {"1", "2", "3"}
    std::vector<std::string> parse_vector_of_strings(const std::string& str);

    // Parses two dimensional vector of strings from string:
    // "[[1,2,3],[4,5,6],[7,8,9]]" -> {{"1","2","3"},{"4","5","6"},{"7","8","9"}}
    std::vector<std::vector<std::string>> parse_two_dim_vector_of_strings(const std::string& str);

    // Splits string by delimiter on the first occurrence and returns trimmed substrings.
    std::vector<std::string> split_string_once(const std::string& str, const char delimiter);

    // Splits string by delimiter and returns trimmed substrings.
    std::vector<std::string> split_string(const std::string& str, const char delimiter);

    // Trims white space from start and end of the string.
    std::string trim_string(const std::string& str);

    // Checks if string starts with a given prefix.
    bool string_starts_with(const std::string& str, const std::string& prefix);

    // Converts buffer_type string to BufferType enum.
    BufferType parse_buffer_type_string(const std::string& s);
}
