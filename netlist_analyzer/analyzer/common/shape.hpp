// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "utils/logger.hpp"

namespace analyzer {

struct Shape {
    int y = 0;
    int x = 0;

    Shape() {
        this->x = 0;
        this->y = 0;
    }
    Shape(const Shape& g) {
        this->x = g.x;
        this->y = g.y;
    };
    Shape(int y, int x) {
        this->x = x;
        this->y = y;
    };
    std::vector<int> to_vec() const { return {y, x}; }

    Shape from_vec(const std::vector<int>& v) { return Shape({v[0], v[1]}); }

    bool operator==(const Shape& o) const { return (y == o.y) && (x == o.x); }

    bool operator!=(const Shape& o) const { return !(*this == o); }

    int& operator[](int idx) {
        if (idx >= 2)
            log_fatal("Shape access oob");
        switch (idx) {
            case 0: return y;
            case 1: return x;
            default: return x;
        }
    }

    int volume() const { return y * x; }

    static uint32_t get_unit_element() { return 1; }
    static int get_rank() { return 2; }
};
inline std::ostream& operator<<(std::ostream& os, const Shape& shape) {
    os << "Shape{";
    os << ".y = " << shape.y << ", ";
    os << ".x = " << shape.x;
    os << "}";
    return os;
}

}  // namespace analyzer