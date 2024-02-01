// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

enum class map_dims {t, rt, ct};

enum class OpClass {MatMul, EltwiseUnary, EltwiseBinary, Reduce, FusedOp, Nary, Buffer, Embedding, Tilizer, Depthwise};

inline bool divisible_either_direction(int a, int b) {
return ((a % b) == 0) || ((b % a) == 0);
}