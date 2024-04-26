// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <random>
#include <chrono>
#include <iostream>
#include <vector>

#include "common/base.hpp"

namespace tt::test {

extern std::mt19937 rand_gen;

void tt_rnd_set_seed(int seed);

int tt_rnd_int(int min, int max);

float tt_rnd_float(float min, float max);

bool tt_rnd_bin();

DataFormat tt_rnd_data_format(bool use_fp32 = false);

std::pair<DataFormat, DataFormat> tt_rnd_inout_formats();

std::tuple<DataFormat, DataFormat, DataFormat> tt_rnd_bin_out_formats();

template<typename T>
T tt_rnd_pick(std::vector<T> items) {
    return items.at(tt_rnd_int(0, items.size() - 1));
}

uint32_t tt_gen_seed(bool print_seed = true);
}  // namespace tt::test
