// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_inference.hpp"

using namespace inference;

int main(int argc, char** argv) {
    int return_code = 0;
    std::vector<std::string> input_args(argv, argv + argc);
    return_code = run(input_args);
    return return_code;
}
