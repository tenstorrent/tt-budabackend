// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/pipe_graph_parser_internal.h"

#include <cstdint>
#include <fstream>
#include <gtest/gtest.h>
#include <ios>
#include <string>
#include <vector>


#include "mocks/model/pipe_graph/pg_buffer_mock.h"
#include "mocks/model/pipe_graph/pg_pipe_mock.h"
#include "model/pipe_graph/pg_buffer.h"
#include "model/pipe_graph/pg_pipe.h"
#include "model/typedefs.h"
#include "pipegen2_constants.h"
#include "pipegen2_exceptions.h"
#include "test_pipe_graph_parser_internal_utils.h"

using namespace pipegen2;
using namespace pipegen2::pipe_graph_parser_internal;
using namespace pipegen2::unit_test_utils;

/**********************************************************************************************************************
    Tests for function: parse_attribute
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseAttribute_InvalidYamlLine)
{
    std::string attr_name, attr_value;
    EXPECT_THROW(parse_attribute("", attr_name, attr_value), InvalidPipegenYamlException);
    EXPECT_THROW(parse_attribute("no delimiter", attr_name, attr_value), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseAttribute_NormalYamlLine)
{
    std::string expected_attr_name = "dram_addr";
    std::string expected_attr_value = "0x1915a660";
    std::string yaml_line = expected_attr_name + ": " + expected_attr_value;

    std::string attr_name, attr_value;
    parse_attribute(yaml_line, attr_name, attr_value);
    EXPECT_EQ(attr_name, expected_attr_name);
    EXPECT_EQ(attr_value, expected_attr_value);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseAttribute_MultiDelimiter)
{
    std::string expected_attr_name = "attr_name";
    std::string expected_attr_value = "vdwq:dwqgr:gergdsafew:wqgredbdf";
    std::string yaml_line = expected_attr_name + ": " + expected_attr_value;

    std::string attr_name, attr_value;
    parse_attribute(yaml_line, attr_name, attr_value);
    EXPECT_EQ(attr_name, expected_attr_name);
    EXPECT_EQ(attr_value, expected_attr_value);
}

/**********************************************************************************************************************
    Tests for function: parse_int_attribute_value
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseIntAttributeValue_InvalidAttrValue)
{
    EXPECT_THROW(parse_int_attribute_value(""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_int_attribute_value(","), InvalidPipegenYamlException);
    EXPECT_THROW(parse_int_attribute_value("."), InvalidPipegenYamlException);
    EXPECT_THROW(parse_int_attribute_value("str"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseIntAttributeValue_NormalAttrValue)
{
    EXPECT_EQ(parse_int_attribute_value("1"), 1);
    EXPECT_EQ(parse_int_attribute_value("-1"), -1);
    EXPECT_EQ(parse_int_attribute_value("0xfa3"), 4003);
}

/**********************************************************************************************************************
    Tests for function: parse_uint_attribute_value
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseUintAttributeValue_InvalidAttrValue)
{
    EXPECT_THROW(parse_uint_attribute_value(""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_uint_attribute_value(","), InvalidPipegenYamlException);
    EXPECT_THROW(parse_uint_attribute_value("."), InvalidPipegenYamlException);
    EXPECT_THROW(parse_uint_attribute_value("str"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseUintAttributeValue_NormalAttrValue)
{
    EXPECT_EQ(parse_uint_attribute_value("123456789"), (unsigned int)123456789);
    EXPECT_EQ(parse_uint_attribute_value("0xfa3"), (unsigned int)4003);
}

/**********************************************************************************************************************
    Tests for function: parse_ulong_attribute_value
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseUlongAttributeValue_InvalidAttrValue)
{
    EXPECT_THROW(parse_ulong_attribute_value(""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_ulong_attribute_value(","), InvalidPipegenYamlException);
    EXPECT_THROW(parse_ulong_attribute_value("."), InvalidPipegenYamlException);
    EXPECT_THROW(parse_ulong_attribute_value("str"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseUlongAttributeValue_NormalAttrValue)
{
    EXPECT_EQ(parse_ulong_attribute_value("123456789"), (std::uint64_t)123456789);
    EXPECT_EQ(parse_ulong_attribute_value("0x1915a660"), (std::uint64_t)420849248);
}

/**********************************************************************************************************************
    Tests for function: parse_buffer_operand_id
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseBufferOperandId_InvalidAttrValue)
{
    PGBuffer buffer;
    EXPECT_THROW(parse_buffer_operand_id(&buffer, ""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_buffer_operand_id(&buffer, "-1"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseBufferOperandId_NormalAttrValue)
{
    PGBuffer buffer;
    parse_buffer_operand_id(&buffer, "1");
    EXPECT_EQ(buffer.get_operand_id(), 1);
    parse_buffer_operand_id(&buffer, "0xfa3");
    EXPECT_EQ(buffer.get_operand_id(), 4003);
}

/**********************************************************************************************************************
    Tests for function: parse_buffer_chip_id
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseBufferChipId_InvalidAttrValue)
{
    PGBuffer buffer;
    EXPECT_THROW(parse_buffer_chip_id(&buffer, ""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_buffer_chip_id(&buffer, "[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_buffer_chip_id(&buffer, "[1, 2]"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseBufferChipId_NormalAttrValue)
{
    PGBuffer buffer;
    ChipId chip_id = 5;
    parse_buffer_chip_id(&buffer, "[" + std::to_string(chip_id) + "]");
    EXPECT_EQ(buffer.get_logical_location().chip, chip_id);
}

/**********************************************************************************************************************
    Tests for function: parse_buffer_location
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseBufferLocation_InvalidAttrValue)
{
    PGBuffer buffer;
    EXPECT_THROW(parse_buffer_location(&buffer, ""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_buffer_location(&buffer, "[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_buffer_location(&buffer, "[1]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_buffer_location(&buffer, "[1, 2, 3]"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseBufferLocation_NormalAttrValue)
{
    PGBuffer buffer;
    parse_buffer_location(&buffer, "[1, 2]");
    EXPECT_EQ(buffer.get_logical_location().y, 1);
    EXPECT_EQ(buffer.get_logical_location().x, 2);
}

/**********************************************************************************************************************
    Tests for function: parse_pipe_mcast_locations
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParsePipeMcastLocations_InvalidAttrValue)
{
    PGPipe pipe;
    EXPECT_THROW(parse_pipe_mcast_locations(&pipe, ""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_mcast_locations(&pipe, "[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_mcast_locations(&pipe, "[[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_mcast_locations(&pipe, "[1, 2]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_mcast_locations(&pipe, "[[1, 2]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_mcast_locations(&pipe, "[[1, 2, 3], [4, 5]]"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParsePipeMcastLocations_OneDimList)
{
    PGPipe pipe;
    parse_pipe_mcast_locations(&pipe, "[1, 2, 3]");
    ASSERT_EQ(pipe.get_mcast_core_logical_locations().size(), 1);
    EXPECT_EQ(pipe.get_mcast_core_logical_locations()[0].chip, 1);
    EXPECT_EQ(pipe.get_mcast_core_logical_locations()[0].y, 2);
    EXPECT_EQ(pipe.get_mcast_core_logical_locations()[0].x, 3);
}

TEST(Pipegen2_PipeGraphParserInternal, ParsePipeMcastLocations_TwoDimList)
{
    PGPipe pipe1;
    parse_pipe_mcast_locations(&pipe1, "[[1, 2, 3]]");
    ASSERT_EQ(pipe1.get_mcast_core_logical_locations().size(), 1);
    EXPECT_EQ(pipe1.get_mcast_core_logical_locations()[0].chip, 1);
    EXPECT_EQ(pipe1.get_mcast_core_logical_locations()[0].y, 2);
    EXPECT_EQ(pipe1.get_mcast_core_logical_locations()[0].x, 3);

    PGPipe pipe2;
    parse_pipe_mcast_locations(&pipe2, "[[1, 2, 3], [4, 5, 6]]");
    ASSERT_EQ(pipe2.get_mcast_core_logical_locations().size(), 2);
    EXPECT_EQ(pipe2.get_mcast_core_logical_locations()[0].chip, 1);
    EXPECT_EQ(pipe2.get_mcast_core_logical_locations()[0].y, 2);
    EXPECT_EQ(pipe2.get_mcast_core_logical_locations()[0].x, 3);
    EXPECT_EQ(pipe2.get_mcast_core_logical_locations()[1].chip, 4);
    EXPECT_EQ(pipe2.get_mcast_core_logical_locations()[1].y, 5);
    EXPECT_EQ(pipe2.get_mcast_core_logical_locations()[1].x, 6);
}

/**********************************************************************************************************************
    Tests for function: parse_pipe_inputs
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParsePipeInputs_InvalidListString)
{
    PGPipe pipe;
    EXPECT_THROW(parse_pipe_inputs(&pipe, ""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_inputs(&pipe, "[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_inputs(&pipe, "[,]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_inputs(&pipe, "[,,]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_inputs(&pipe, "1,2,3"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParsePipeInputs_OneElement)
{
    PGPipe pipe;
    parse_pipe_inputs(&pipe, "[1]");
    ASSERT_EQ(pipe.get_input_buffers_ids().size(), 1);
    EXPECT_EQ(pipe.get_input_buffers_ids()[0], 1);
}

TEST(Pipegen2_PipeGraphParserInternal, ParsePipeInputs_MultipleElements)
{
    PGPipe pipe;
    parse_pipe_inputs(&pipe, "[1, 2, 3]");
    ASSERT_EQ(pipe.get_input_buffers_ids().size(), 3);
    EXPECT_EQ(pipe.get_input_buffers_ids()[0], 1);
    EXPECT_EQ(pipe.get_input_buffers_ids()[1], 2);
    EXPECT_EQ(pipe.get_input_buffers_ids()[2], 3);
}

/**********************************************************************************************************************
    Tests for function: parse_pipe_outputs
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParsePipeOutputs_InvalidListString)
{
    PGPipe pipe;
    EXPECT_THROW(parse_pipe_outputs(&pipe, ""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_outputs(&pipe, "[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_outputs(&pipe, "[[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_outputs(&pipe, "[[],[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_pipe_outputs(&pipe, "1,2,3"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParsePipeOutputs_OneDimList)
{
    PGPipe pipe1;
    parse_pipe_outputs(&pipe1, "[1]");
    ASSERT_EQ(pipe1.get_output_buffers_ids().size(), 1);
    ASSERT_EQ(pipe1.get_output_buffers_ids()[0].size(), 1);
    EXPECT_EQ(pipe1.get_output_buffers_ids()[0][0], 1);

    PGPipe pipe2;
    parse_pipe_outputs(&pipe2, "[1, 2]");
    ASSERT_EQ(pipe2.get_output_buffers_ids().size(), 1);
    ASSERT_EQ(pipe2.get_output_buffers_ids()[0].size(), 2);
    EXPECT_EQ(pipe2.get_output_buffers_ids()[0][0], 1);
    EXPECT_EQ(pipe2.get_output_buffers_ids()[0][1], 2);
}

TEST(Pipegen2_PipeGraphParserInternal, ParsePipeOutputs_TwoDimList)
{
    PGPipe pipe1;
    parse_pipe_outputs(&pipe1, "[[1]]");
    ASSERT_EQ(pipe1.get_output_buffers_ids().size(), 1);
    ASSERT_EQ(pipe1.get_output_buffers_ids()[0].size(), 1);
    EXPECT_EQ(pipe1.get_output_buffers_ids()[0][0], 1);

    PGPipe pipe2;
    parse_pipe_outputs(&pipe2, "[[1], [2, 3]]");
    ASSERT_EQ(pipe2.get_output_buffers_ids().size(), 2);
    ASSERT_EQ(pipe2.get_output_buffers_ids()[0].size(), 1);
    EXPECT_EQ(pipe2.get_output_buffers_ids()[0][0], 1);
    ASSERT_EQ(pipe2.get_output_buffers_ids()[1].size(), 2);
    EXPECT_EQ(pipe2.get_output_buffers_ids()[1][0], 2);
    EXPECT_EQ(pipe2.get_output_buffers_ids()[1][1], 3);
}

/**********************************************************************************************************************
    Tests for function: parse_vector_of_ints
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfInts_InvalidListString)
{
    EXPECT_THROW(parse_vector_of_ints(""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ints("["), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ints("]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ints("[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ints("[,]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ints("[,,]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ints("[1,2,3"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ints("1,2,3]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ints("1,2,3"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfInts_OneElement)
{
    std::vector<int> ints_vec = parse_vector_of_ints("[1]");
    ASSERT_EQ(ints_vec.size(), 1);
    EXPECT_EQ(ints_vec[0], 1);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfInts_MultipleElements)
{
    std::vector<int> ints_vec = parse_vector_of_ints("[1,2,3]");
    ASSERT_EQ(ints_vec.size(), 3);
    EXPECT_EQ(ints_vec[0], 1);
    EXPECT_EQ(ints_vec[1], 2);
    EXPECT_EQ(ints_vec[2], 3);

    ints_vec = parse_vector_of_ints("[1, 2, 3]");
    ASSERT_EQ(ints_vec.size(), 3);
    EXPECT_EQ(ints_vec[0], 1);
    EXPECT_EQ(ints_vec[1], 2);
    EXPECT_EQ(ints_vec[2], 3);
}

/**********************************************************************************************************************
    Tests for function: parse_two_dim_vector_of_ints
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfInts_InvalidListString)
{
    EXPECT_THROW(parse_two_dim_vector_of_ints(""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[[,]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[[],]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[,[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[[],[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[[,],[,]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[1,2]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[[1,2,3]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ints("[1,2,3]]"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfInts_OneElement)
{
    std::vector<std::vector<int>> ints_vec = parse_two_dim_vector_of_ints("[[1]]");
    ASSERT_EQ(ints_vec.size(), 1);
    ASSERT_EQ(ints_vec[0].size(), 1);
    EXPECT_EQ(ints_vec[0][0], 1);

    ints_vec = parse_two_dim_vector_of_ints("[[1], [2]]");
    ASSERT_EQ(ints_vec.size(), 2);
    ASSERT_EQ(ints_vec[0].size(), 1);
    EXPECT_EQ(ints_vec[0][0], 1);
    ASSERT_EQ(ints_vec[1].size(), 1);
    EXPECT_EQ(ints_vec[1][0], 2);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfInts_MultipleElements)
{
    std::vector<std::vector<int>> ints_vec = parse_two_dim_vector_of_ints("[[1, 2, 3]]");
    ASSERT_EQ(ints_vec.size(), 1);
    ASSERT_EQ(ints_vec[0].size(), 3);
    EXPECT_EQ(ints_vec[0][0], 1);
    EXPECT_EQ(ints_vec[0][1], 2);
    EXPECT_EQ(ints_vec[0][2], 3);

    ints_vec = parse_two_dim_vector_of_ints("[[1, 2], [3, 4]]");
    ASSERT_EQ(ints_vec.size(), 2);
    ASSERT_EQ(ints_vec[0].size(), 2);
    EXPECT_EQ(ints_vec[0][0], 1);
    EXPECT_EQ(ints_vec[0][1], 2);
    ASSERT_EQ(ints_vec[1].size(), 2);
    EXPECT_EQ(ints_vec[1][0], 3);
    EXPECT_EQ(ints_vec[1][1], 4);
}

/**********************************************************************************************************************
    Tests for function: convert_strings_to_ints
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ConvertStringsToInts_NoStringValues)
{
    std::vector<int> int_values = convert_strings_to_ints(std::vector<std::string>());
    ASSERT_EQ(int_values.size(), 0);
}

TEST(Pipegen2_PipeGraphParserInternal, ConvertStringsToInts_NormalStringValues)
{
    std::vector<std::string> string_values = {"1", "2", "3"};
    std::vector<int> int_values = convert_strings_to_ints(string_values);
    ASSERT_EQ(int_values.size(), 3);
    EXPECT_EQ(int_values[0], 1);
    EXPECT_EQ(int_values[1], 2);
    EXPECT_EQ(int_values[2], 3);
}

/**********************************************************************************************************************
    Tests for function: parse_vector_of_ulongs
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfUlongs_InvalidListString)
{
    EXPECT_THROW(parse_vector_of_ulongs(""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ulongs("["), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ulongs("]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ulongs("[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ulongs("[,]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ulongs("[,,]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ulongs("[1,2,3"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ulongs("1,2,3]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_ulongs("1,2,3"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfUlongs_OneElement)
{
    std::vector<std::uint64_t> ulongs_vec = parse_vector_of_ulongs("[1]");
    ASSERT_EQ(ulongs_vec.size(), 1);
    EXPECT_EQ(ulongs_vec[0], 1);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfUlongs_MultipleElements)
{
    std::vector<std::uint64_t> ulongs_vec = parse_vector_of_ulongs("[1,2,3]");
    ASSERT_EQ(ulongs_vec.size(), 3);
    EXPECT_EQ(ulongs_vec[0], 1);
    EXPECT_EQ(ulongs_vec[1], 2);
    EXPECT_EQ(ulongs_vec[2], 3);

    ulongs_vec = parse_vector_of_ulongs("[1, 2, 3]");
    ASSERT_EQ(ulongs_vec.size(), 3);
    EXPECT_EQ(ulongs_vec[0], 1);
    EXPECT_EQ(ulongs_vec[1], 2);
    EXPECT_EQ(ulongs_vec[2], 3);
}

/**********************************************************************************************************************
    Tests for function: parse_two_dim_vector_of_ulongs
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfUlongs_InvalidListString)
{
    EXPECT_THROW(parse_two_dim_vector_of_ulongs(""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[[,]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[[],]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[,[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[[],[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[[,],[,]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[1,2]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[[1,2,3]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_ulongs("[1,2,3]]"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfUlongs_OneElement)
{
    std::vector<std::vector<std::uint64_t>> ulongs_vec = parse_two_dim_vector_of_ulongs("[[1]]");
    ASSERT_EQ(ulongs_vec.size(), 1);
    ASSERT_EQ(ulongs_vec[0].size(), 1);
    EXPECT_EQ(ulongs_vec[0][0], 1);

    ulongs_vec = parse_two_dim_vector_of_ulongs("[[1], [2]]");
    ASSERT_EQ(ulongs_vec.size(), 2);
    ASSERT_EQ(ulongs_vec[0].size(), 1);
    EXPECT_EQ(ulongs_vec[0][0], 1);
    ASSERT_EQ(ulongs_vec[1].size(), 1);
    EXPECT_EQ(ulongs_vec[1][0], 2);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfUlongs_MultipleElements)
{
    std::vector<std::vector<std::uint64_t>> ulongs_vec = parse_two_dim_vector_of_ulongs("[[1, 2, 3]]");
    ASSERT_EQ(ulongs_vec.size(), 1);
    ASSERT_EQ(ulongs_vec[0].size(), 3);
    EXPECT_EQ(ulongs_vec[0][0], 1);
    EXPECT_EQ(ulongs_vec[0][1], 2);
    EXPECT_EQ(ulongs_vec[0][2], 3);

    ulongs_vec = parse_two_dim_vector_of_ulongs("[[1, 2], [3, 4]]");
    ASSERT_EQ(ulongs_vec.size(), 2);
    ASSERT_EQ(ulongs_vec[0].size(), 2);
    EXPECT_EQ(ulongs_vec[0][0], 1);
    EXPECT_EQ(ulongs_vec[0][1], 2);
    ASSERT_EQ(ulongs_vec[1].size(), 2);
    EXPECT_EQ(ulongs_vec[1][0], 3);
    EXPECT_EQ(ulongs_vec[1][1], 4);
}

/**********************************************************************************************************************
    Tests for function: convert_strings_to_ulongs
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ConvertStringsToUlongs_NoStringValues)
{
    std::vector<std::uint64_t> ulong_values = convert_strings_to_ulongs(std::vector<std::string>());
    ASSERT_EQ(ulong_values.size(), 0);
}

TEST(Pipegen2_PipeGraphParserInternal, ConvertStringsToUlongs_NormalStringValues)
{
    std::vector<std::string> string_values = {"1", "2", "3"};
    std::vector<std::uint64_t> ulong_values = convert_strings_to_ulongs(string_values);
    ASSERT_EQ(ulong_values.size(), 3);
    EXPECT_EQ(ulong_values[0], 1);
    EXPECT_EQ(ulong_values[1], 2);
    EXPECT_EQ(ulong_values[2], 3);
}

/**********************************************************************************************************************
    Tests for function: parse_vector_of_strings
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfStrings_InvalidListString)
{
    EXPECT_THROW(parse_vector_of_strings(""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_strings("["), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_strings("]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_strings("[1,2,3"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_strings("1,2,3]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_vector_of_strings("1,2,3"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfStrings_NoElements)
{
    std::vector<std::string> strings_vec = parse_vector_of_strings("[]");
    ASSERT_EQ(strings_vec.size(), 1);
    EXPECT_EQ(strings_vec[0], "");
}

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfStrings_OneElement)
{
    std::vector<std::string> strings_vec = parse_vector_of_strings("[1]");
    ASSERT_EQ(strings_vec.size(), 1);
    EXPECT_EQ(strings_vec[0], "1");
}

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfStrings_MultipleElements)
{
    std::vector<std::string> strings_vec = parse_vector_of_strings("[1,2,3]");
    ASSERT_EQ(strings_vec.size(), 3);
    EXPECT_EQ(strings_vec[0], "1");
    EXPECT_EQ(strings_vec[1], "2");
    EXPECT_EQ(strings_vec[2], "3");

    strings_vec = parse_vector_of_strings("[1, 2, 3]");
    ASSERT_EQ(strings_vec.size(), 3);
    EXPECT_EQ(strings_vec[0], "1");
    EXPECT_EQ(strings_vec[1], "2");
    EXPECT_EQ(strings_vec[2], "3");
}

TEST(Pipegen2_PipeGraphParserInternal, ParseVectorOfStrings_EmptyElements)
{
    std::vector<std::string> strings_vec = parse_vector_of_strings("[,]");
    ASSERT_EQ(strings_vec.size(), 2);
    EXPECT_EQ(strings_vec[0], "");
    EXPECT_EQ(strings_vec[1], "");

    strings_vec = parse_vector_of_strings("[,,]");
    ASSERT_EQ(strings_vec.size(), 3);
    EXPECT_EQ(strings_vec[0], "");
    EXPECT_EQ(strings_vec[1], "");
    EXPECT_EQ(strings_vec[2], "");
}

/**********************************************************************************************************************
    Tests for function: parse_two_dim_vector_of_strings
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfStrings_InvalidListString)
{
    EXPECT_THROW(parse_two_dim_vector_of_strings(""), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_strings("[]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_strings("[[],]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_strings("[,[]]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_strings("[1,2]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_strings("[[1,2,3]"), InvalidPipegenYamlException);
    EXPECT_THROW(parse_two_dim_vector_of_strings("[1,2,3]]"), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfStrings_NoElements)
{
    std::vector<std::vector<std::string>> strings_vec = parse_two_dim_vector_of_strings("[[]]");
    ASSERT_EQ(strings_vec.size(), 1);
    ASSERT_EQ(strings_vec[0].size(), 1);
    EXPECT_EQ(strings_vec[0][0], "");

    strings_vec = parse_two_dim_vector_of_strings("[[],[]]");
    ASSERT_EQ(strings_vec.size(), 2);
    ASSERT_EQ(strings_vec[0].size(), 1);
    EXPECT_EQ(strings_vec[0][0], "");
    ASSERT_EQ(strings_vec[1].size(), 1);
    EXPECT_EQ(strings_vec[1][0], "");
}

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfStrings_OneElement)
{
    std::vector<std::vector<std::string>> strings_vec = parse_two_dim_vector_of_strings("[[1]]");
    ASSERT_EQ(strings_vec.size(), 1);
    ASSERT_EQ(strings_vec[0].size(), 1);
    EXPECT_EQ(strings_vec[0][0], "1");

    strings_vec = parse_two_dim_vector_of_strings("[[1], [2]]");
    ASSERT_EQ(strings_vec.size(), 2);
    ASSERT_EQ(strings_vec[0].size(), 1);
    EXPECT_EQ(strings_vec[0][0], "1");
    ASSERT_EQ(strings_vec[1].size(), 1);
    EXPECT_EQ(strings_vec[1][0], "2");
}

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfStrings_MultipleElements)
{
    std::vector<std::vector<std::string>> strings_vec = parse_two_dim_vector_of_strings("[[1, 2, 3]]");
    ASSERT_EQ(strings_vec.size(), 1);
    ASSERT_EQ(strings_vec[0].size(), 3);
    EXPECT_EQ(strings_vec[0][0], "1");
    EXPECT_EQ(strings_vec[0][1], "2");
    EXPECT_EQ(strings_vec[0][2], "3");

    strings_vec = parse_two_dim_vector_of_strings("[[1, 2], [3, 4]]");
    ASSERT_EQ(strings_vec.size(), 2);
    ASSERT_EQ(strings_vec[0].size(), 2);
    EXPECT_EQ(strings_vec[0][0], "1");
    EXPECT_EQ(strings_vec[0][1], "2");
    ASSERT_EQ(strings_vec[1].size(), 2);
    EXPECT_EQ(strings_vec[1][0], "3");
    EXPECT_EQ(strings_vec[1][1], "4");
}

TEST(Pipegen2_PipeGraphParserInternal, ParseTwoDimVectorOfStrings_EmptyElements)
{
    std::vector<std::vector<std::string>> strings_vec = parse_two_dim_vector_of_strings("[[,,]]");
    ASSERT_EQ(strings_vec.size(), 1);
    ASSERT_EQ(strings_vec[0].size(), 3);
    EXPECT_EQ(strings_vec[0][0], "");
    EXPECT_EQ(strings_vec[0][1], "");
    EXPECT_EQ(strings_vec[0][2], "");

    strings_vec = parse_two_dim_vector_of_strings("[[,], [,]]");
    ASSERT_EQ(strings_vec.size(), 2);
    ASSERT_EQ(strings_vec[0].size(), 2);
    EXPECT_EQ(strings_vec[0][0], "");
    EXPECT_EQ(strings_vec[0][1], "");
    ASSERT_EQ(strings_vec[1].size(), 2);
    EXPECT_EQ(strings_vec[1][0], "");
    EXPECT_EQ(strings_vec[1][1], "");
}

/**********************************************************************************************************************
    Tests for function: split_string
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, SplitString_EmptyString)
{
    std::vector<std::string> substrings = split_string("", ',');
    ASSERT_EQ(substrings.size(), 1);
    EXPECT_EQ(substrings[0], "");

    substrings = split_string(" ", ',');
    ASSERT_EQ(substrings.size(), 1);
    EXPECT_EQ(substrings[0], "");

    substrings = split_string("  ", ',');
    ASSERT_EQ(substrings.size(), 1);
    EXPECT_EQ(substrings[0], "");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitString_NormalString)
{
    std::vector<std::string> substrings = split_string("1,2,3", ',');
    ASSERT_EQ(substrings.size(), 3);
    EXPECT_EQ(substrings[0], "1");
    EXPECT_EQ(substrings[1], "2");
    EXPECT_EQ(substrings[2], "3");

    substrings = split_string("1, 2, 3", ',');
    ASSERT_EQ(substrings.size(), 3);
    EXPECT_EQ(substrings[0], "1");
    EXPECT_EQ(substrings[1], "2");
    EXPECT_EQ(substrings[2], "3");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitString_NoDelimiterString)
{
    std::string example_str("123");
    std::vector<std::string> substrings = split_string(example_str, ',');
    ASSERT_EQ(substrings.size(), 1);
    EXPECT_EQ(substrings[0], example_str);
}

TEST(Pipegen2_PipeGraphParserInternal, SplitString_DoubleDelimiterString)
{
    std::string example_str("1,,2");
    std::vector<std::string> substrings = split_string(example_str, ',');
    ASSERT_EQ(substrings.size(), 3);
    EXPECT_EQ(substrings[0], "1");
    EXPECT_EQ(substrings[1], "");
    EXPECT_EQ(substrings[2], "2");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitString_DelimiterBeginningString)
{
    std::string example_str(",1,2");
    std::vector<std::string> substrings = split_string(example_str, ',');
    ASSERT_EQ(substrings.size(), 3);
    EXPECT_EQ(substrings[0], "");
    EXPECT_EQ(substrings[1], "1");
    EXPECT_EQ(substrings[2], "2");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitString_DelimiterEndString)
{
    std::string example_str("1,2,");
    std::vector<std::string> substrings = split_string(example_str, ',');
    ASSERT_EQ(substrings.size(), 3);
    EXPECT_EQ(substrings[0], "1");
    EXPECT_EQ(substrings[1], "2");
    EXPECT_EQ(substrings[2], "");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitString_DelimiterBeginningEndString)
{
    std::string example_str(",1,2,");
    std::vector<std::string> substrings = split_string(example_str, ',');
    ASSERT_EQ(substrings.size(), 4);
    EXPECT_EQ(substrings[0], "");
    EXPECT_EQ(substrings[1], "1");
    EXPECT_EQ(substrings[2], "2");
    EXPECT_EQ(substrings[3], "");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitString_OnlyDelimitersString)
{
    std::vector<std::string> substrings = split_string(",", ',');
    ASSERT_EQ(substrings.size(), 2);
    EXPECT_EQ(substrings[0], "");
    EXPECT_EQ(substrings[1], "");

    substrings = split_string(",,", ',');
    ASSERT_EQ(substrings.size(), 3);
    EXPECT_EQ(substrings[0], "");
    EXPECT_EQ(substrings[1], "");
    EXPECT_EQ(substrings[2], "");
}

/**********************************************************************************************************************
    Tests for function: split_string_once
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, SplitStringOnce_EmptyString)
{
    std::vector<std::string> substrings = split_string_once("", ',');
    ASSERT_EQ(substrings.size(), 1);
    EXPECT_EQ(substrings[0], "");

    substrings = split_string_once(" ", ',');
    ASSERT_EQ(substrings.size(), 1);
    EXPECT_EQ(substrings[0], "");

    substrings = split_string_once("  ", ',');
    ASSERT_EQ(substrings.size(), 1);
    EXPECT_EQ(substrings[0], "");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitStringOnce_NormalString)
{
    std::vector<std::string> substrings = split_string_once("1,2,3", ',');
    ASSERT_EQ(substrings.size(), 2);
    EXPECT_EQ(substrings[0], "1");
    EXPECT_EQ(substrings[1], "2,3");

    substrings = split_string_once("1, 2, 3", ',');
    ASSERT_EQ(substrings.size(), 2);
    EXPECT_EQ(substrings[0], "1");
    EXPECT_EQ(substrings[1], "2, 3");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitStringOnce_NoDelimiterString)
{
    std::string example_str("123");
    std::vector<std::string> substrings = split_string_once(example_str, ',');
    ASSERT_EQ(substrings.size(), 1);
    EXPECT_EQ(substrings[0], example_str);
}

TEST(Pipegen2_PipeGraphParserInternal, SplitStringOnce_DoubleDelimiterString)
{
    std::string example_str("1,,2");
    std::vector<std::string> substrings = split_string_once(example_str, ',');
    ASSERT_EQ(substrings.size(), 2);
    EXPECT_EQ(substrings[0], "1");
    EXPECT_EQ(substrings[1], ",2");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitStringOnce_DelimiterBeginningString)
{
    std::string example_str(",1,2");
    std::vector<std::string> substrings = split_string_once(example_str, ',');
    ASSERT_EQ(substrings.size(), 2);
    EXPECT_EQ(substrings[0], "");
    EXPECT_EQ(substrings[1], "1,2");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitStringOnce_DelimiterEndString)
{
    std::string example_str("1,2,");
    std::vector<std::string> substrings = split_string_once(example_str, ',');
    ASSERT_EQ(substrings.size(), 2);
    EXPECT_EQ(substrings[0], "1");
    EXPECT_EQ(substrings[1], "2,");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitStringOnce_DelimiterBeginningEndString)
{
    std::string example_str(",1,2,");
    std::vector<std::string> substrings = split_string_once(example_str, ',');
    ASSERT_EQ(substrings.size(), 2);
    EXPECT_EQ(substrings[0], "");
    EXPECT_EQ(substrings[1], "1,2,");
}

TEST(Pipegen2_PipeGraphParserInternal, SplitStringOnce_OnlyDelimitersString)
{
    std::vector<std::string> substrings = split_string_once(",", ',');
    ASSERT_EQ(substrings.size(), 2);
    EXPECT_EQ(substrings[0], "");
    EXPECT_EQ(substrings[1], "");

    substrings = split_string_once(",,", ',');
    ASSERT_EQ(substrings.size(), 2);
    EXPECT_EQ(substrings[0], "");
    EXPECT_EQ(substrings[1], ",");
}

/**********************************************************************************************************************
    Tests for function: trim_string
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, TrimString_EmptyString)
{
    EXPECT_EQ(trim_string(""), "");
}

TEST(Pipegen2_PipeGraphParserInternal, TrimString_TrimmedString)
{
    std::string trimmed_str("test 123");
    EXPECT_EQ(trim_string(trimmed_str), trimmed_str);
}

TEST(Pipegen2_PipeGraphParserInternal, TrimString_NonTrimmedString)
{
    std::string trimmed_str("test 123");
    EXPECT_EQ(trim_string(" " + trimmed_str), trimmed_str);
    EXPECT_EQ(trim_string("    " + trimmed_str), trimmed_str);
    EXPECT_EQ(trim_string(trimmed_str + " "), trimmed_str);
    EXPECT_EQ(trim_string(trimmed_str + "     "), trimmed_str);
    EXPECT_EQ(trim_string(" " + trimmed_str + " "), trimmed_str);
    EXPECT_EQ(trim_string("     " + trimmed_str + "     "), trimmed_str);
}

/**********************************************************************************************************************
    Tests for function: string_starts_with
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, StringStartsWith_DoesStart)
{
    EXPECT_TRUE(string_starts_with("test", ""));
    EXPECT_TRUE(string_starts_with(" test", " "));
    EXPECT_TRUE(string_starts_with("test", "t"));
    EXPECT_TRUE(string_starts_with("test", "test"));
}

TEST(Pipegen2_PipeGraphParserInternal, StringStartsWith_DoesntStart)
{
    EXPECT_FALSE(string_starts_with("", "["));
    EXPECT_FALSE(string_starts_with("test", "a"));
    EXPECT_FALSE(string_starts_with("test", "est"));
}

/**********************************************************************************************************************
    Tests for function: parse_pipe
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParsePipe_ParsingPipeWithCorrectAttributes)
{
    PGPipeMock pipe_mock(
        116000000000, 1, 1, 7, NOC_ROUTE::NOC0, 2, NOC_ROUTE::NOC1, 2, 1, 0, 1, 0, 1, 24, {0, 0, 10}, {1, 1, 1}, {0, 0, 0},
        {109000000000, 112000000000}, {120000000000, 128000000000}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 147000000000});
    std::vector<std::string> yaml_lines = pipe_mock.to_json_list_of_strings_all_attributes();

    PipeGraph pipe_graph;
    parse_pipe(yaml_lines, pipe_graph);
    EXPECT_EQ(pipe_graph.get_pipes().size(), 1);

    PGPipe& pipe = *(pipe_graph.get_pipes()[0]);
    compare_pipe_attributes(pipe_mock, pipe);
}

TEST(Pipegen2_PipeGraphParserInternal, ParsePipe_DefaultValuesIdOnly)
{
    PGPipeMock pipe_mock(116000000000);
    std::vector<std::string> yaml_lines = pipe_mock.to_json_list_of_strings_id_only();

    PipeGraph pipe_graph;
    parse_pipe(yaml_lines, pipe_graph);
    EXPECT_EQ(pipe_graph.get_pipes().size(), 1);

    PGPipe& pipe = *(pipe_graph.get_pipes()[0]);
    compare_pipe_attributes(pipe_mock, pipe);
}

TEST(Pipegen2_PipeGraphParserInternal, ParsePipe_NoPipeInformation)
{
    constexpr NodeId pipe_id = 116000000000;
    std::vector<std::string> yaml_lines = {
        "pipe_"+ std::to_string(pipe_id) + ":"
    };
    PipeGraph pipe_graph;
    parse_pipe(yaml_lines, pipe_graph);
    EXPECT_EQ(pipe_graph.get_pipes().size(), 0);
}

/**********************************************************************************************************************
    Tests for function: parse_buffer
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseBuffer_Normal)
{  
    PGBufferMock buffer_mock(
        BufferType::kUnpacker, 111000000000, 1, 64, 12, {0}, {1, 11}, 2, 16, 12, 2080, 0, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 32, 32, 1, 4, 131000000000, 1, 1, 12, 0, PrefetchType::PRE_TM, 1, 4,
        1, 0, 1024, 192, 1, 32, 131072, 1, 1, 103424, 1);
    std::vector<std::string> yaml_lines = buffer_mock.to_json_list_of_strings_all_attributes();
    PipeGraph pipe_graph;
    parse_buffer(yaml_lines, pipe_graph);
    EXPECT_EQ(pipe_graph.get_buffers().size(), 1);
    
    PGBuffer& buffer = *(pipe_graph.get_buffers()[0]);
    compare_buffer_attributes(buffer, buffer_mock);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseBuffer_DefaultValues)
{
    PGBufferMock buffer_mock(111000000000);
    std::vector<std::string> yaml_lines = buffer_mock.to_json_list_of_strings_id_only();

    PipeGraph pipe_graph;
    parse_buffer(yaml_lines, pipe_graph);
    
    PGBuffer& buffer = *(pipe_graph.get_buffers()[0]);
    compare_buffer_attributes(buffer, buffer_mock);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseBuffer_NoBufferInformation)
{
    constexpr std::uint64_t uniqid = 111000000000;
    std::vector<std::string> yaml_lines = {
        "buffer_"+ std::to_string(uniqid) + ":",
    };
    PipeGraph pipe_graph;
    parse_buffer(yaml_lines, pipe_graph);
    EXPECT_EQ(pipe_graph.get_buffers().size(), 0);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseBuffer_SizeTilesGreaterThanLimitDRAMNoException)
{
    constexpr std::uint64_t uniqid = 111000000000;
    constexpr std::uint64_t size_tiles = constants::general_max_num_tiles_per_phase + 1;
    PipeGraph pipe_graph;
    std::vector<std::string> yaml_lines = {
        "uniqid: " + std::to_string(uniqid),
        "buffer_type: dram_io",
        "size_tiles: " + std::to_string(size_tiles),
        "dram_io_flag: 1",
        "q_slots: 1"
    };
    EXPECT_NO_THROW(parse_buffer(yaml_lines, pipe_graph));
}

TEST(Pipegen2_PipeGraphParserInternal, ParseBuffer_SizeTilesGreaterThanLimitUnpackerNoException)
{
    constexpr std::uint64_t uniqid = 111000000000;
    constexpr std::uint64_t size_tiles = constants::general_max_num_tiles_per_phase + 1;
    PipeGraph pipe_graph;
    std::vector<std::string> yaml_lines = {
        "uniqid: " + std::to_string(uniqid),
        "buffer_type: unpacker",
        "size_tiles: " + std::to_string(size_tiles),
        "dram_io_flag: 0"
    };
    EXPECT_NO_THROW(parse_buffer(yaml_lines, pipe_graph));
}

TEST(Pipegen2_PipeGraphParserInternal, ParseBuffer_SizeTilesGreaterThanLimitPackerException)
{
    constexpr std::uint64_t uniqid = 111000000000;
    constexpr std::uint64_t size_tiles = constants::general_max_num_tiles_per_phase + 1;
    PipeGraph pipe_graph;
    std::vector<std::string> yaml_lines = {
        "uniqid: " + std::to_string(uniqid),
        "buffer_type: packer",
        "size_tiles: " + std::to_string(size_tiles),
        "dram_io_flag: 0"
    };
    EXPECT_THROW(parse_buffer(yaml_lines, pipe_graph), InvalidPipeGraphSpecificationException);
}

/**********************************************************************************************************************
    Tests for function: parse_node
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseNode_InvalidName)
{
    constexpr std::uint64_t uniqid = 111000000000;
    std::vector<std::string> yaml_lines = {
        "node_"+ std::to_string(uniqid) + ":",
    };
    PipeGraph pipe_graph;
    EXPECT_THROW(parse_node(yaml_lines, pipe_graph), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseNode_ParseBuffer)
{
    constexpr std::uint64_t uniqid = 111000000000;
    std::vector<std::string> yaml_lines = {
        "buffer_"+ std::to_string(uniqid) + ":",
        "uniqid: " + std::to_string(uniqid),
    };
    PipeGraph pipe_graph;
    parse_node(yaml_lines, pipe_graph);
    EXPECT_EQ(pipe_graph.get_buffers().size(), 1);
    PGBuffer& buffer = *(pipe_graph.get_buffers()[0]);
    EXPECT_EQ(buffer.get_id(), uniqid);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseNode_ParsePipe)
{
    constexpr NodeId pipe_id = 116000000000;
    std::vector<std::string> yaml_lines = {
        "pipe_"+ std::to_string(pipe_id) + ":",
        "id: " + std::to_string(pipe_id)
    };
    PipeGraph pipe_graph;
    parse_node(yaml_lines, pipe_graph);
    EXPECT_EQ(pipe_graph.get_pipes().size(), 1);
    PGPipe& pipe = *(pipe_graph.get_pipes()[0]);
    EXPECT_EQ(pipe.get_id(), pipe_id);
}

/**********************************************************************************************************************
    Tests for function: parse_graph
**********************************************************************************************************************/

TEST(Pipegen2_PipeGraphParserInternal, ParseGraph_ParsePipeAndBuffer)
{
    constexpr std::uint64_t buffer_id = 106000000000;
    constexpr std::uint64_t pipe_id = 109000000000;
    const std::string buffer_type = "unpacker";
    std::string graph_data = "graph_name: test_op\n";
    graph_data += "---\n";
    graph_data += "buffer_" + std::to_string(buffer_id) + ":\n";
    graph_data += "md_op_name: target_op0\n";
    graph_data += "buffer_type: " + buffer_type + "\n";
    graph_data += "id: 0\n";
    graph_data += "uniqid: " + std::to_string(buffer_id) + "\n";
    graph_data += "---\n";
    graph_data += "pipe_" + std::to_string(pipe_id) + ":\n";
    graph_data += "id: " + std::to_string(pipe_id) + "\n";

    std::stringstream string_stream(graph_data);
    PipeGraph pipe_graph;
    parse_graph(pipe_graph, string_stream);

    EXPECT_EQ(pipe_graph.get_pipes().size(), 1);
    EXPECT_EQ(pipe_graph.get_buffers().size(), 1);

    PGPipe& pipe = *(pipe_graph.get_pipes()[0]); 
    PGBuffer& buffer = *(pipe_graph.get_buffers()[0]);
    EXPECT_EQ(pipe.get_id(), pipe_id);
    EXPECT_EQ(buffer.get_id(), buffer_id);
    EXPECT_EQ(buffer.get_type(), BufferType::kUnpacker);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseGraph_CommentSuccess)
{
    constexpr std::uint64_t buffer_id = 106000000000;
    const std::string buffer_type = "unpacker";
    std::string graph_data = "graph_name: test_op\n";
    graph_data += "---\n";
    graph_data += "#ThisIsAComment\n";
    graph_data += "buffer_" + std::to_string(buffer_id) + ":\n";
    graph_data += "md_op_name: target_op0\n";
    graph_data += "buffer_type: " + buffer_type + "\n";
    graph_data += "id: 0\n";
    graph_data += "uniqid: " + std::to_string(buffer_id) + "\n";

    std::stringstream string_stream(graph_data);
    PipeGraph pipe_graph;
    parse_graph(pipe_graph, string_stream);

    EXPECT_EQ(pipe_graph.get_pipes().size(), 0);
    EXPECT_EQ(pipe_graph.get_buffers().size(), 1);

    PGBuffer& buffer = *(pipe_graph.get_buffers()[0]);
    EXPECT_EQ(buffer.get_id(), buffer_id);
    EXPECT_EQ(buffer.get_type(), BufferType::kUnpacker);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseGraph_WrongBufferFormat)
{
    constexpr std::uint64_t buffer_id = 106000000000;
    const std::string buffer_type = "unpacker";
    std::string graph_data = "graph_name: test_op\n";
    graph_data += "---\n";
    graph_data += "buffer_" + std::to_string(buffer_id) + ":\n";
    graph_data += "md_op_name: target_op0\n";
    graph_data += "buffer_type: " + buffer_type + "\n";
    graph_data += "id: 0\n";
    graph_data += "ThisIsANonStandardLine\n";
    graph_data += "uniqid: " + std::to_string(buffer_id) + "\n";

    std::stringstream string_stream(graph_data);
    PipeGraph pipe_graph;
    EXPECT_THROW(parse_graph(pipe_graph, string_stream), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseGraph_WrongPipegenYamlFormat)
{
    constexpr std::uint64_t buffer_id = 106000000000;
    const std::string buffer_type = "unpacker";
    std::string graph_data = "graph_name: test_op\n";
    graph_data += "---\n";
    graph_data += "ThisIsANonStandardLine\n";
    graph_data += "buffer_" + std::to_string(buffer_id) + ":\n";
    graph_data += "md_op_name: target_op0\n";
    graph_data += "buffer_type: " + buffer_type + "\n";
    graph_data += "id: 0\n";
    graph_data += "uniqid: " + std::to_string(buffer_id) + "\n";

    std::stringstream string_stream(graph_data);
    PipeGraph pipe_graph;
    EXPECT_THROW(parse_graph(pipe_graph, string_stream), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseGraph_NonStandardLinesAtEnd)
{
    constexpr std::uint64_t buffer_id = 106000000000;
    const std::string buffer_type = "unpacker";
    std::string graph_data = "graph_name: test_op\n";
    graph_data += "---\n";
    graph_data += "buffer_" + std::to_string(buffer_id) + ":\n";
    graph_data += "md_op_name: target_op0\n";
    graph_data += "buffer_type: " + buffer_type + "\n";
    graph_data += "id: 0\n";
    graph_data += "uniqid: " + std::to_string(buffer_id) + "\n";
    graph_data += "ThisIsANonStandardLine\n";

    std::stringstream string_stream(graph_data);
    PipeGraph pipe_graph;
    EXPECT_THROW(parse_graph(pipe_graph, string_stream), InvalidPipegenYamlException);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseGraph_EmptyGraph)
{
    std::string graph_data = "graph_name: test_op\n";
    PipeGraph pipe_graph;
    std::stringstream string_stream(graph_data);
    parse_graph(pipe_graph, string_stream);
    EXPECT_EQ(pipe_graph.get_pipes().size(), 0);
    EXPECT_EQ(pipe_graph.get_buffers().size(), 0);
}

TEST(Pipegen2_PipeGraphParserInternal, ParseGraph_InputStreamFail)
{
    PipeGraph pipe_graph;
    std::ifstream input_stream("");
    input_stream.setstate(std::ios::failbit);
    verify_throws_exception_with_message<InvalidPipegenYamlException>([&]() { parse_graph(pipe_graph, input_stream); },
                                                                      "No such file.*");
}