// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "llk_xy_pair.h"

#include <regex>
#include <stdexcept>
#include <string>

using namespace std;
using namespace llk;

xy_pair xy_pair::parse(const std::string &s) {
    regex parser("\\s*(\\d+)\\D(\\d+)\\s*");

    smatch results;
    if (!regex_match(s, results, parser))
        throw runtime_error("Could not parse grid size, expecting format XxY.");

    return xy_pair(stoul(results[1]), stoul(results[2]));
}

std::string llk::format_node(xy_pair xy) { return to_string(xy.x) + "-" + to_string(xy.y); }

xy_pair llk::format_node(std::string str) {
    xy_pair xy;
    std::regex expr("([0-9]+)[-,xX]([0-9]+)");
    std::smatch x_y_pair;

    if (std::regex_search(str, x_y_pair, expr)) {
        xy.x = std::stoi(x_y_pair[1]);
        xy.y = std::stoi(x_y_pair[2]);
    } else {
        throw runtime_error("Could not parse the core id: " + str);
    }

    return xy;
}
