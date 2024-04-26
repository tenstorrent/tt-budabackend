// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "op_model.hpp"

int main(int argc, char* argv[]) {

  try {    
    if (argc < 3) {
      throw std::runtime_error("Usage: op_model <arch_name> <op_type> [attributes]");
    }

    std::string arch_name = argv[1];
    std::string op_name = argv[2];

    if (argc == 4) {
        std::string param_name = argv[3];    
        uint32_t param = tt::OpModel::get_op_param({.type=op_name, .arch=arch_name}, param_name);
        std::cout << "OpModel::get_op_param(" << op_name << ", " << param_name << ") = " << param << std::endl;
    } else {
        tt::tt_op_model_desc op_desc = {.type = op_name, .arch = arch_name};
        uint32_t cycles = tt::OpModel::get_op_cycles(op_desc);
        std::cout << "OpModel::get_op_cycles(" << op_name << ") = " << cycles << std::endl;
    }
  } catch(const std::exception& e) {
    std::cout << e.what() << "\n";
    std::cerr << e.what() << "\n";
    return 1;
  } catch (...) {
    std::cout << "Error: unknown runtime error\n";
    std::cerr << "Error: unknown runtime error\n";
    return 1;
  }
  return 0;
}
