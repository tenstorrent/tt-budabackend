// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <string>
#include <iostream>

#include "opmodel.hpp"

// TODO: Should also account for dataformats, bfp8 etc
int eltwise_cycles(std::string op_type_str, int mb_r, int mb_c, int ub_r, int ub_c) {
    int cpt = -1;
    if ( op_type_str == "add" or
        op_type_str == "subtract") {
        cpt = 250;
    }
    else if( op_type_str == "datacopy" or
             op_type_str == "nop") {
        cpt = 150;
    }
    else if(op_type_str == "multiply") {
        cpt = 200;
    }
    else if( op_type_str == "exp") {
        cpt = 450; // approx from spreadsheet
    }
    else if(op_type_str == "reciprocal") {
        cpt = 950;
    }
    else if(op_type_str == "log") {
        cpt = 1425;
    }
    else if(op_type_str == "sqrt") {
        cpt = 250;
    }
    else if(op_type_str == "sig") {
        cpt = 225;
    }
    else if(op_type_str == "gelu") {
        cpt = 350;
    } else {
        //log_fatal(analyzer::common::LogAnalyzer, "No cycle model for type: '{}'", op_type_str);
        std::cout << "FATAL: No cycle model for type: " << op_type_str << std::endl;
        exit(-1);
    }
    const int total_tiles = mb_r * mb_c * ub_r * ub_c;
    return cpt * total_tiles;
}

// TODO: Should also account for dataformats, bfp8 etc
int matmul_cycles(int mb_r, int mb_c, int ub_r, int ub_c, int mk, int ukt) {
    const int datacopy_cpt = 16;
    const int matmul_cpt = 50;

    const int datacopy_tiles = mb_r * mb_c * ub_r * ub_c;
    const int datacopy_cycles = datacopy_cpt * datacopy_tiles;
    const int matmul_tiles = mb_r * mb_c * ub_r * ub_c * mk * ukt;
    const int matmul_cycles = matmul_cpt * matmul_tiles;
    return datacopy_cycles + matmul_cycles;
}

