// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "net2hlks_lib/net2hlks.h"

int main(int argc, char* argv[]) {

  try {
    
    if (argc != 3) {
      throw std::runtime_error("Usage: net2hlks <input netlist yaml> <output directory>\n");
    }
    Net2Hlks n2h(argv[1], argv[2]);
    n2h.output_hlks();
    
  } catch(const std::exception& e) {
    std::cout << std::string("\nERROR: ") + e.what() << "\n";
    std::cerr << std::string("\nERROR: ") + e.what() << "\n";
    return 1;
  } catch (...) {
    std::cout << "Error: unknown runtime error\n";
    std::cerr << "Error: unknown runtime error\n";
    return 1;
  }
  return 0;
}

