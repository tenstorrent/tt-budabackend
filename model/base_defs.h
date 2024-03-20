// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iostream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

template <typename DTYPE>
void print_vector_like(const std::vector<DTYPE>& vec, std::ostream& out) {
    out << "{";
    if (vec.size() > 0) {
        out << vec[0];
        for (auto i = 1; i < vec.size(); ++i) {
            out << ", " << vec[i];
        }
    }
    out << "}";
}

template <typename K_, typename V_>
std::ostream& operator<<(std::ostream& out, const std::unordered_map<K_, V_>& m) {
    out << "{\n";
    for (const auto& [k, v] : m) {
        out << "{" << k << ": " << v << "}\n";
    }
    out << "}\n";
    return out;
}

namespace tt {
    template <typename T>
    std::ostream &operator<<(std::ostream &out, const std::vector<T> &vec) {
        print_vector_like(vec, out);
        return out;
    }

    struct cmd_result_t {
        bool success;
        std::string message;
    };

    bool run_command(const std::string &cmd, const std::string &log_file);
    cmd_result_t run_command(const std::string &cmd, const std::string &log_file, const std::string &err_file);
}