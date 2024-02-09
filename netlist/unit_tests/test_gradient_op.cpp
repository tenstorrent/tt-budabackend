// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <stdexcept>
#include "device/tt_arch_types.h"
#include "gtest/gtest.h"
#include "netlist_op_info_types.hpp"
#include "netlist_parser.hpp"
#include "netlist_parser_mock.hpp"

tt_op_info get_gradient_op(const std::string& op_name, tt::ARCH arch) {
    netlist_unittests::netlist_parser_mock parser;
    parser.device_info.arch = arch;
    std::string op_fields_str = "{ type: " + op_name + ", gradient_op: true}";

    return parser.parse_op_fields(op_fields_str);
}

// Grayskull

TEST(GradientOpValidation, Gradient_Op_Grayskull_Matmul_Is_Valid)
{    
    tt_op_info op_info = get_gradient_op("matmul", tt::ARCH::GRAYSKULL);
    EXPECT_NO_THROW(verify_gradient_op(op_info));
}

TEST(GradientOpValidation, Gradient_Op_Grayskull_Datacopy_Is_Valid)
{
    tt_op_info op_info = get_gradient_op("datacopy", tt::ARCH::GRAYSKULL);
    EXPECT_NO_THROW(verify_gradient_op(op_info));
}

TEST(GradientOpValidation, Gradient_Op_Grayskull_Multiply_Is_Valid)
{
    tt_op_info op_info = get_gradient_op("multiply", tt::ARCH::GRAYSKULL);
    EXPECT_NO_THROW(verify_gradient_op(op_info));
}

TEST(GradientOpValidation, Gradient_Op_Grayskull_Add_Is_Invalid)
{
    tt_op_info op_info = get_gradient_op("add", tt::ARCH::GRAYSKULL);
    EXPECT_THROW(verify_gradient_op(op_info), std::runtime_error);
}

TEST(GradientOpValidation, Gradient_Op_Grayskull_Subtract_Is_Invalid)
{
    tt_op_info op_info = get_gradient_op("subtract", tt::ARCH::GRAYSKULL);
    EXPECT_THROW(verify_gradient_op(op_info), std::runtime_error);
}

TEST(GradientOpValidation, Gradient_Op_Grayskull_Sfpu_Is_Invalid)
{
    tt_op_info op_info = get_gradient_op("abs", tt::ARCH::GRAYSKULL);
    EXPECT_THROW(verify_gradient_op(op_info), std::runtime_error);
}

// Wormhole_b0

TEST(GradientOpValidation, Gradient_Op_Wormhole_B0_Matmul_Is_Valid)
{    
    tt_op_info op_info = get_gradient_op("matmul", tt::ARCH::WORMHOLE_B0);
    EXPECT_NO_THROW(verify_gradient_op(op_info));
}

TEST(GradientOpValidation, Gradient_Op_Wormhole_B0_Datacopy_Is_Valid)
{
    tt_op_info op_info = get_gradient_op("datacopy", tt::ARCH::WORMHOLE_B0);
    EXPECT_NO_THROW(verify_gradient_op(op_info));
}

TEST(GradientOpValidation, Gradient_Op_Wormhole_B0_Multiply_Is_Valid)
{
    tt_op_info op_info = get_gradient_op("multiply", tt::ARCH::WORMHOLE_B0);
    EXPECT_NO_THROW(verify_gradient_op(op_info));
}

TEST(GradientOpValidation, Gradient_Op_Wormhole_B0_Add_Is_Valid)
{
    tt_op_info op_info = get_gradient_op("add", tt::ARCH::WORMHOLE_B0);
    EXPECT_NO_THROW(verify_gradient_op(op_info));
}

TEST(GradientOpValidation, Gradient_Op_Wormhole_B0_Subtract_Is_Valid)
{
    tt_op_info op_info = get_gradient_op("subtract", tt::ARCH::WORMHOLE_B0);
    EXPECT_NO_THROW(verify_gradient_op(op_info));
}

TEST(GradientOpValidation, Gradient_Op_Wormhole_B0_Sfpu_Is_Valid)
{
    tt_op_info op_info = get_gradient_op("abs", tt::ARCH::WORMHOLE_B0);
    EXPECT_NO_THROW(verify_gradient_op(op_info));
}