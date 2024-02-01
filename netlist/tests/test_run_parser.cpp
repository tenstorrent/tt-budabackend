// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>

#include "netlist/netlist.hpp"
int main() {
    netlist_parser my_parser;

    std::cout << "Parser Sanity -- Parsing default file" << std::endl;
    my_parser.parse_file();
    std::cout << "Parser Sanity -- Done default file" << std::endl;

    std::cout << "Parser Sanity -- Parsing user specified file" << std::endl;
    my_parser.parse_file("default_netlist.yaml");
    std::cout << "Parser Sanity -- Done user specified file" << std::endl;

    std::cout << "Parser Sanity -- Parsing netlist string" << std::endl;
    std::ifstream netlist_fstream("default_netlist.yaml");
    std::stringstream netlist_ss;
    netlist_ss << netlist_fstream.rdbuf();
    my_parser.parse_string(netlist_ss.str());
    std::cout << "Parser Sanity -- Done netlist string" << std::endl;
}
