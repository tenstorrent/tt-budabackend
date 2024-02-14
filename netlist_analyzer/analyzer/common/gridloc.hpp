// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "utils/logger.hpp"
#include "vector"
#include <regex>

namespace analyzer {

struct GridLoc {
    int y = -1;
    int x = -1;

    GridLoc() {
        this->x = -1;
        this->y = -1;
    }
    GridLoc(const GridLoc& g) {
        this->x = g.x;
        this->y = g.y;
    };
    GridLoc(int y, int x) {
        this->x = x;
        this->y = y;
    };
    std::vector<int> to_vec() const { return {y, x}; }

    GridLoc from_vec(const std::vector<int>& v) { return GridLoc({v[0], v[1]}); }

    static GridLoc from_str(const std::string& str) {
        // read from coordinates separated by a dash, e.g. "1-2"
        int x_coord;
        int y_coord;
        std::regex expr("([0-9]+)[-,xX]([0-9]+)");
        std::smatch x_y_pair;

        if (std::regex_search(str, x_y_pair, expr)) {
            x_coord = std::stoi(x_y_pair[1]);
            y_coord = std::stoi(x_y_pair[2]);
        } else {
            throw std::runtime_error("Could not parse the core id: " + str);
        }

        return GridLoc(y_coord, x_coord);
    }

    bool operator==(const GridLoc& o) const { return (y == o.y) && (x == o.x); }

    bool operator!=(const GridLoc& o) const { return !(*this == o); }

    int& operator[](int idx) {
        if (idx >= 2)
            log_fatal("GridLoc access oob");
        switch (idx) {
            case 0: return y;
            case 1: return x;
            default: return x;
        }
    }

    static uint32_t get_unit_element() { return 1; }
    static int get_rank() { return 2; }
};

inline std::ostream& operator<<(std::ostream& os, const GridLoc& loc) {
    os << "GridLoc{";
    os << ".y = " << loc.y << ", ";
    os << ".x = " << loc.x;
    os << "}";
    return os;
}

}  // namespace analyzer

template<> struct std::hash<analyzer::GridLoc> {
    std::size_t operator()(analyzer::GridLoc const& s) const noexcept {
        std::size_t h1 = std::hash<int>{}(s.x);
        std::size_t h2 = std::hash<int>{}(s.y);
        return h1 ^ (h2 << 1); // or use boost::hash_combine (see Discussion) https://en.cppreference.com/w/Talk:cpp/utility/hash
    }
};
