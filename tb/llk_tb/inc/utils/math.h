// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <type_traits>

namespace ttl {
template <typename A, typename B>
using common_signed_t = std::conditional_t<
    std::is_unsigned_v<A> && std::is_unsigned_v<B>,
    std::common_type_t<A, B>,
    std::common_type_t<std::make_signed_t<A>, std::make_signed_t<B>>>;
//! Generic ceiling division code.
template <typename Dividend, typename Divisor>
constexpr common_signed_t<Dividend, Divisor> div_ceil(Dividend x, Divisor y) {
    if constexpr (std::is_unsigned_v<Dividend> && std::is_unsigned_v<Divisor>) {
        // quotient is always positive
        return x / y + (x % y != 0);  // uint / uint
    } else if constexpr (std::is_signed_v<Dividend> && std::is_unsigned_v<Divisor>) {
        auto sy = static_cast<std::make_signed_t<Divisor>>(y);
        bool quotientPositive = x >= 0;
        return x / sy + (x % sy != 0 && quotientPositive);  // int / uint
    } else if constexpr (std::is_unsigned_v<Dividend> && std::is_signed_v<Divisor>) {
        auto sx = static_cast<std::make_signed_t<Dividend>>(x);
        bool quotientPositive = y >= 0;
        return sx / y + (sx % y != 0 && quotientPositive);  // uint / int
    } else {
        bool quotientPositive = (y >= 0) == (x >= 0);
        return x / y + (x % y != 0 && quotientPositive);  // int / int
    }
}
uint32_t convert_float_to_1_8_7(float input);
}  // namespace ttl