// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>

#include "pipegen_parser.hpp"

int main() {
    pipegen_yaml_parser parser;
    parser.parse_file("pipegen.yaml");
    std::cout << "discoface" << std::endl;
}
