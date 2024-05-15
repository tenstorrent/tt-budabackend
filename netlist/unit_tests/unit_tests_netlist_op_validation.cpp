// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "gtest/gtest.h"
#include "netlist/netlist_parser.hpp"
#include "netlist_op_info_types.hpp"

tt_op_info create_single_input_splice_op(
    tt::tt_grid_shape const& grid_size, tt_dim_info const& dims, tt_splice_info const& attributes, SpliceMode mode) {
    return tt_op_info{
        .type = "splice",
        .grid_size = grid_size,
        .output_dim = dims,
        .attributes = tt_op_attributes{.op_type = "splice", .splice_infos = {attributes}, .splice_mode = mode},
        .input_dims = {dims}};
}

TEST(NetlistParserOpValidation, SpliceUblockOrT_IndexEqual0_Valid) {
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 1, 1}, tt_splice_info{.index = 0, .length = 1, .stride = 1}, SpliceMode::Ublock);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 1, 1}, tt_splice_info{.index = 0, .length = 1, .stride = 1}, SpliceMode::T);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
}

TEST(NetlistParserOpValidation, SpliceUblockOrT_IndexLastValue_Valid) {
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 2, 2, 4, 4}, tt_splice_info{.index = 15, .length = 1, .stride = 1}, SpliceMode::Ublock);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {64, 1, 1, 1, 1}, tt_splice_info{.index = 63, .length = 1, .stride = 1}, SpliceMode::T);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
}

TEST(NetlistParserOpValidation, SpliceUblockOrT_IndexLessThan0_Invalid) {
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 1, 1}, tt_splice_info{.index = -1, .length = 1, .stride = 1}, SpliceMode::Ublock);
        ASSERT_THROW(netlist_parser::validation::validate_splice_op_attributes(splice, 0), std::runtime_error);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 1, 1}, tt_splice_info{.index = -1, .length = 1, .stride = 1}, SpliceMode::T);
        ASSERT_THROW(netlist_parser::validation::validate_splice_op_attributes(splice, 0), std::runtime_error);
    }
}

TEST(NetlistParserOpValidation, SpliceUblockOrT_IndexLengthStrideInBounds_Valid) {
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 4, 4}, tt_splice_info{.index = 0, .length = 16, .stride = 1}, SpliceMode::Ublock);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 4, 4}, tt_splice_info{.index = 0, .length = 8, .stride = 2}, SpliceMode::Ublock);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {64, 1, 1, 1, 1}, tt_splice_info{.index = 0, .length = 64, .stride = 1}, SpliceMode::T);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {64, 1, 1, 1, 1}, tt_splice_info{.index = 0, .length = 32, .stride = 2}, SpliceMode::T);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
}

TEST(NetlistParserOpValidation, SpliceUblockOrT_LengthLessEqual0_Invalid) {
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 1, 1}, tt_splice_info{.index = 0, .length = 0, .stride = 1}, SpliceMode::Ublock);
        ASSERT_THROW(netlist_parser::validation::validate_splice_op_attributes(splice, 0), std::runtime_error);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 1, 1}, tt_splice_info{.index = 0, .length = -1, .stride = 1}, SpliceMode::Ublock);
        ASSERT_THROW(netlist_parser::validation::validate_splice_op_attributes(splice, 0), std::runtime_error);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 1, 1}, tt_splice_info{.index = 0, .length = 0, .stride = 1}, SpliceMode::T);
        ASSERT_THROW(netlist_parser::validation::validate_splice_op_attributes(splice, 0), std::runtime_error);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 1, 1}, tt_splice_info{.index = 0, .length = -1, .stride = 1}, SpliceMode::T);
        ASSERT_THROW(netlist_parser::validation::validate_splice_op_attributes(splice, 0), std::runtime_error);
    }
}

TEST(NetlistParserOpValidation, SpliceUblockOrT_IndexLengthStridePastEnd_Valid) {
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 4, 4}, tt_splice_info{.index = 0, .length = 17, .stride = 1}, SpliceMode::Ublock);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 4, 4}, tt_splice_info{.index = 1, .length = 9, .stride = 2}, SpliceMode::Ublock);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {1, 1, 1, 4, 4}, tt_splice_info{.index = 1, .length = 2, .stride = 16}, SpliceMode::Ublock);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {64, 1, 1, 1, 1}, tt_splice_info{.index = 0, .length = 63, .stride = 1}, SpliceMode::T);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {64, 1, 1, 1, 1}, tt_splice_info{.index = 1, .length = 2, .stride = 64}, SpliceMode::T);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
    {
        tt_op_info const& splice = create_single_input_splice_op(
            {1, 1}, {64, 1, 1, 1, 1}, tt_splice_info{.index = 1, .length = 32, .stride = 2}, SpliceMode::T);
        netlist_parser::validation::validate_splice_op_attributes(splice, 0);
    }
}