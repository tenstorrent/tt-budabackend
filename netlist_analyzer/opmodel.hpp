// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>

// These models unfortunately capture the input performance from noc to L1 and output from L1 to the extra drainer ops

// TODO: Should also account for dataformats, bfp8 etc
int eltwise_cycles(std::string op_type_str, int mb_r, int mb_c, int ub_r, int ub_c);

// TODO: Should also account for dataformats, bfp8 etc
int matmul_cycles(int mb_r, int mb_c, int ub_r, int ub_c, int mk, int ukt);

