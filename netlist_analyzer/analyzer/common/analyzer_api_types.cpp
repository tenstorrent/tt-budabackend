// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "analyzer_api_types.hpp"
namespace analyzer {

GridType get_grid_type(std::string string) {
    std::transform(string.begin(), string.end(), string.begin(), [](unsigned char c){ return std::tolower(c); });
    if (string == "op") {
        return GridType::Op;
    } else if (string == "queue") {
        return GridType::Queue;
    }else {
        return GridType::Invalid;
    }
}

}  // namespace analyzer
