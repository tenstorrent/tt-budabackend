// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "test_args.h"

namespace tests {

enum class LLK_TEST_TYPE {
    ELTWISE_UNARY,
    ELTWISE_BINARY,
    ELTWISE_NARY,
    MATMUL,
    MATMUL_LARGE,
    MATMUL_LARGE_ADD,
    HLKC_TEST,
    TILE_SYNC,
    REDUCE,
    MULTI_OUT,
    MULTI_IN,
    MULTI_IN_ACC,
    TILIZE,
    UNTILIZE,
    SFPI
};

extern std::unordered_map<std::string, LLK_TEST_TYPE> LLK_TEST_TYPE_NAME;
std::vector<std::string> get_all_tests();

bool test_main(std::string test_name, tests::TestArgs args);

}  // namespace tests
