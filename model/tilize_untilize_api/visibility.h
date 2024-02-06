// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#define TT_HIDDEN [[gnu::visibility("hidden")]]

// https://bugs.llvm.org/show_bug.cgi?id=29094
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0537r0.html
#ifdef __clang__
#define HIDDEN_TEMPLATE
#else
#define HIDDEN_TEMPLATE TT_HIDDEN
#endif
