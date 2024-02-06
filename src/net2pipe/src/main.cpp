// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "net2pipe.h"
#include "common/buda_soc_descriptor.h"

int main(int argc, char* argv[]) {

  tt::assert::register_segfault_handler();
  try {
    
    if (argc != 5 && argc != 6) {
      throw std::runtime_error("Usage: net2pipe <input netlist yaml> <output directory> <global epoch start> <soc descriptor yaml> <(optional)cluster descriptor file>\n");
    }
    
    bool has_explicit_cluster_description_file = argc == 6;
    const std::string &cluster_description_file_path = has_explicit_cluster_description_file ? argv[5] : "";
    Net2Pipe np(argv[1], argv[2], argv[3], argv[4], cluster_description_file_path);
    np.output_pipes();
    
  } catch(const std::exception& e) {
    n2p::Log() << std::string("\nERROR: ") + e.what() << "\n";
    std::cerr << std::string("\nERROR: ") + e.what() << "\n";
    return 1;
  } catch (...) {
    n2p::Log() << "Error: unknown runtime error\n";
    std::cerr << "Error: unknown runtime error\n";
    return 1;
  }
  return 0;
}

