// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include "netlist_analyzer/analyzer/common/gridloc.hpp"
#include "netlist_analyzer/analyzer/common/shape.hpp"
namespace analyzer {

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &v) {
    os << "{";
    bool first = true;
    for (T const &elem : v) {
        if (!first) {
            os << ", ";
        }
        os << elem;
        first = false;
    }
    os << "}";
    return os;
}

enum class GridType {
    Op,
    Queue,
    Invalid
};

GridType get_grid_type(std::string string);
inline std::ostream& operator<<(std::ostream& os, const GridType& type) {
    switch (type) {
        case GridType::Op:
            os << "Op";
            break;
        case GridType::Queue:
            os <<  "Queue";
            break;
        case GridType::Invalid:
        default:
            os <<  "Invalid";
            break;
    }
    return os;
}

//! Grid Configuration
struct GridConfig {
    std::string name = "";
    GridType type = GridType::Invalid;
    std::string op_type = "invalid";
    GridLoc loc = {};
    Shape shape = {};
    std::vector<int> dram_channels = {};
    int estimated_cycles = -1;
    bool grid_transpose = false;
};
inline std::ostream& operator<<(std::ostream& os, const GridConfig& config) {
    os << "GridConfig{";
    os << ".name = " << config.name << ", ";
    os << ".type = " << config.type << ", ";
    os << ".op_type = " << config.op_type << ", ";
    os << ".loc = " << config.loc << ", ";
    os << ".shape = " << config.shape << ", ";
    os << ".dram_channels = " << config.dram_channels;
    os << "}";
    return os;
}

//! Edge Configuration
struct EdgeConfig {
    std::string name = "";
    std::string input = "";
    std::string output = "";
};
inline std::ostream& operator<<(std::ostream& os, const EdgeConfig& config) {
    os << "EdgeConfig{";
    os << ".name = " << config.name << ", ";
    os << ".input = " << config.input << ", ";
    os << ".output = " << config.output;
    os << "}";
    return os;
}

}  // namespace analyzer
