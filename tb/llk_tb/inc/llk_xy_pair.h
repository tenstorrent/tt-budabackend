// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <command_assembler/xy_pair.h>

#include <boost/functional/hash.hpp>
#include <boost/hana.hpp>
#include <cstddef>
#include <string>

namespace llk {

struct xy_pair {
    xy_pair() : x{}, y{} {}
    constexpr xy_pair(std::size_t x, std::size_t y) : x(x), y(y), first(x), second(y) {}
    template <typename Pair>
    explicit xy_pair(const Pair &p) : xy_pair(p.first, p.second) {}
    explicit xy_pair(const CommandAssembler::xy_pair &p) : xy_pair(p.x, p.y) {}

    std::size_t x;
    std::size_t y;
    std::size_t first;
    std::size_t second;

    template <class Function>
    void for_each(Function &&f) const;

    static xy_pair parse(const std::string &s);

    inline xy_pair operator+(const xy_pair &right) { return xy_pair(x + right.x, y + right.y); }

    std::string str() const { return "(x=" + std::to_string(x) + ",y=" + std::to_string(y) + ")"; }
    std::string str() { return "(x=" + std::to_string(x) + ",y=" + std::to_string(y) + ")"; }

    // TODO: Remove CA Translation
    operator CommandAssembler::xy_pair() const { return CommandAssembler::xy_pair(x, y); }
};

constexpr inline bool operator==(const xy_pair &a, const xy_pair &b) { return a.x == b.x && a.y == b.y; }

constexpr inline bool operator!=(const xy_pair &a, const xy_pair &b) { return !(a == b); }

constexpr inline bool operator<(const xy_pair &left, const xy_pair &right) {
    return (left.x < right.x || (left.x == right.x && left.y < right.y));
}

template <class Function>
void xy_pair::for_each(Function &&f) const {
    for (std::size_t i = 0; i < x; i++) {
        for (std::size_t j = 0; j < y; j++) {
            f(xy_pair(i, j));
        }
    }
}

std::string format_node(xy_pair xy);

xy_pair format_node(std::string str);

}  // namespace llk

namespace std {
template <>
struct hash<llk::xy_pair> {
    std::size_t operator()(llk::xy_pair const &o) const {
        std::size_t seed = 0;
        boost::hash_combine(seed, std::hash<std::size_t>{}(o.x));
        boost::hash_combine(seed, std::hash<std::size_t>{}(o.y));
        return seed;
    }
};
}  // namespace std
