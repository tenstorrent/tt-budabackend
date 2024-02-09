// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "utils/gtest_initializer.hpp"

int main(int argc, char **argv) {
  initialize_gtest(argc, argv);
  return RUN_ALL_TESTS();
}