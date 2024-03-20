// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "gtest/gtest.h"
#include "netlist_fused_op_info_types.hpp"
#include "netlist_parser.hpp"
#include "netlist_parser_mock.hpp"

TEST(ScheduledOpInfoTest, inputs_missing) {
    netlist_unittests::netlist_parser_mock parser;
    tt_fused_op_info fused_op_info;
    std::string op_fields_str = "{type: matmul, pop_last: [intermed1], mblock: [2, 1], ublock: [2, 1], output: dest}";
    tt_scheduled_op_info scheduled_op_info = parser.parse_scheduled_op_fields("op1", op_fields_str);
    EXPECT_THROW(
        verify(scheduled_op_info, tt::ARCH::GRAYSKULL, fused_op_info, /*last op in schedule*/ true),
        std::runtime_error);
}

TEST(ScheduledOpInfoTest, matmul_mid_schedule) {
    netlist_unittests::netlist_parser_mock parser;
    tt_fused_op_info fused_op_info;
    std::string op_fields_str =
        "{type: matmul, inputs: [intermed0, input1], pop_last: [intermed1], mblock: [2, 1], ublock: [2, 1], output: "
        "intermed0}";
    tt_scheduled_op_info scheduled_op_info = parser.parse_scheduled_op_fields("matmul_op", op_fields_str);
    EXPECT_THROW(
        verify(scheduled_op_info, tt::ARCH::GRAYSKULL, fused_op_info, /*last op in schedule*/ false),
        std::runtime_error);
}

TEST(ScheduledOpInfoTest, reduce_f32_bad_input_type) {
    netlist_unittests::netlist_parser_mock parser;
    tt_fused_op_info fused_op_info;
    fused_op_info.accumulation_data_format = DataFormat::Float32;
    fused_op_info.input_data_formats.push_back(DataFormat::Bfp8);
    std::string scheduled_op_fields_str =
        "{type: reduce, inputs: [intermed0], pop: [intermed0], mblock: [2, 1], ublock: [2, 1], attributes: {type: max, "
        "dim: c, m_k: 4, u_kt: 2}, output: intermed0}";
    tt_scheduled_op_info scheduled_op_info = parser.parse_scheduled_op_fields("reduce_op", scheduled_op_fields_str);

    EXPECT_THROW(
        verify(scheduled_op_info, tt::ARCH::WORMHOLE_B0, fused_op_info, /*last op in schedule*/ true),
        std::runtime_error);
}