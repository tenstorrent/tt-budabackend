// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <stdexcept>
#include "gtest/gtest.h"
#include "netlist_op_info_types.hpp"
#include "netlist_parser.hpp"
#include "netlist_parser_mock.hpp"

TEST(ReduceOpInvalid, F32DestInputsTypeA) {
    netlist_unittests::netlist_parser_mock parser;
    std::string op_fields_str =
        "{type: reduce, grid_loc: [0, 0], grid_size: [1, 1], inputs: [nop0], t: 2, mblock: [3, 1], ublock: [4, 1], "
        "in_df: [Bfp8], acc_df: Float32, "
        "intermed_df: Float32, out_df: Float32, buf_size_mb: 2, untilize_output: false, ublock_order: r, "
        "math_fidelity: LoFi, attributes: {dim: c, type: max, m_k: 6, u_kt: 2}}";
    tt_op_info op_info = parser.parse_op_fields(op_fields_str);

    EXPECT_THROW(verify(op_info), std::runtime_error);
}
