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
    
    // 1: Netlist File Path
    // 2: Output Directory
    //    Tells net2pipe where to dump logs and generated files (pipegen.yaml)
    // 3: Global Epoch Start
    // 4: Soc Descriptor List File Path
    //    The path of a file that lists the soc descriptor paths for each soc
    // 5: Cluster Descriptor File (optional)

    auto const& netlist_file_path = argv[1];
    auto const& output_directory = argv[2];
    auto const& global_epoch_start = argv[3];
    auto const& soc_descriptor_list_file_path = argv[4];
    bool has_explicit_cluster_description_file = argc == 6;
    const std::string &cluster_description_file_path = has_explicit_cluster_description_file ? argv[5] : "";
    Net2Pipe np(netlist_file_path, output_directory, global_epoch_start, soc_descriptor_list_file_path, cluster_description_file_path);
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

