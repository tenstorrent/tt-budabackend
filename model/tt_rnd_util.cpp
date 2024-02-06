// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <set>
#include <algorithm>

#include "model/tt_rnd_util.hpp"

namespace tt::test {

std::mt19937 rand_gen(0);
void tt_rnd_set_seed(int seed) { rand_gen.seed(seed); }

int tt_rnd_int(int min, int max) {
    log_assert((min <= max) ,  "min is greater than max!");
    std::uniform_int_distribution<int> distrib(min, max);
    return distrib(rand_gen);
}

float tt_rnd_float(float min, float max) {
    log_assert((min <= max) ,  "min is greater than max!");
    std::uniform_real_distribution<float> distrib(min, max);
    return distrib(rand_gen);
}

bool tt_rnd_bin() { return tt_rnd_int(0, 1); }

DataFormat tt_rnd_data_format(bool use_fp32) {
    std::vector<DataFormat> data_formats = {DataFormat::Float16, DataFormat::Float16_b, DataFormat::Bfp8_b, DataFormat::Bfp8};
    if (use_fp32) data_formats.push_back(DataFormat::Float32);
    int ret = tt_rnd_int(0, (int)data_formats.size() - 1);
    return data_formats[ret];
}

std::pair<DataFormat, DataFormat> tt_rnd_inout_formats() {
    DataFormat in, out;

    // Randomize input format
    in = tt_rnd_data_format(true);
    std::vector<DataFormat> a_formats = {DataFormat::Float16, DataFormat::Bfp8};

    // Always valid to convert to either Float16 A or B
    std::vector<DataFormat> out_formats = {DataFormat::Float32, DataFormat::Float16, DataFormat::Float16_b};
    bool is_a_format = std::find(a_formats.begin(), a_formats.end(), in) != a_formats.end();
    bool is_b_format = !is_a_format;

    // Only allow same exponent width flavour of Bfp8
    if (is_a_format || (in == DataFormat::Float32)) {
        out_formats.push_back(DataFormat::Bfp8);
    }
    if (is_b_format || (in == DataFormat::Float32)) {
        out_formats.push_back(DataFormat::Bfp8_b);
    }

    // Randomize output format
    int idx = tt_rnd_int(0, out_formats.size() - 1);
    out = out_formats.at(idx);
    return {in, out};
}

std::tuple<DataFormat, DataFormat, DataFormat> tt_rnd_bin_out_formats() {
    DataFormat in_0, in_1, out;

    // Randomize input format
    in_0 = tt_rnd_data_format();
    const std::vector<DataFormat> a_formats = {DataFormat::Float16, DataFormat::Bfp8};
    const std::vector<DataFormat> b_formats = {DataFormat::Float16_b, DataFormat::Bfp8_b};
    bool is_a_format = std::find(a_formats.begin(), a_formats.end(), in_0) != a_formats.end();

    // Always valid to convert to either Float16 A or B
    std::vector<DataFormat> out_formats = {DataFormat::Float16, DataFormat::Float16_b};

    const std::vector<DataFormat> *in_formats_ptr = &a_formats;

    // For output, only allow same exponent width flavour of Bfp8
    // For input, must match exp width
    if (is_a_format) {
        out_formats.push_back(DataFormat::Bfp8);

        in_formats_ptr = &a_formats;
    } else {
        out_formats.push_back(DataFormat::Bfp8_b);

        in_formats_ptr = &b_formats;
    }

    // Randomize output format
    int idx = tt_rnd_int(0, out_formats.size() - 1);
    out = out_formats.at(idx);

    idx = tt_rnd_int(0, in_formats_ptr->size() - 1);
    in_1 = in_formats_ptr->at(idx);

    return {in_0, in_1, out};
}

uint32_t tt_gen_seed(bool print_seed) {
    std::mt19937::result_type seed;

    std::random_device rd;
    seed = rd() ^ ((std::mt19937::result_type)std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count() +
                   (std::mt19937::result_type)std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::high_resolution_clock::now().time_since_epoch())
                       .count());

    if (print_seed)
        printf("generated seed = 0x%x\n", (uint32_t)seed);

    return ((uint32_t)seed);
}

}  // namespace tt::test
