// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <random>
#include <vector>

#include "model/tensor.hpp"
namespace verif {
namespace random {

// Seed manipulation
extern thread_local std::mt19937 rand_gen;
extern thread_local bool is_seed_initialized;
void tt_rnd_set_seed(int seed);
int32_t tt_gen_seed();

void log_assert_seed_uninitialized(const char* caller);

// Random api
int tt_rnd_int(int min, int max);
uint32_t tt_rnd_uint32t(uint32_t min, uint32_t max);

float tt_rnd_float(float min, float max);
bool tt_rnd_bin();
template <typename T>
T tt_rnd_pick(std::vector<T> items) {
    return items.at(tt_rnd_int(0, items.size() - 1));
}

// Tensor Randomization functions

template <typename T>
void randomize_normal(tt_tensor &tensor, T mean = 0, T stddev = 1);
template <typename T>
void randomize_normal(
    tt_tensor &tensor, T mean, T stddev, const std::pair<int, int> &r_bounds, const std::pair<int, int> &c_bounds);

template <typename T>
void randomize_uniform(tt_tensor &tensor, T lower_bound = 0, T upper_bound = 1);
template <typename T>
void randomize_uniform(
    tt_tensor &tensor,
    T lower_bound,
    T upper_bound,
    const std::pair<int, int> &r_bounds,
    const std::pair<int, int> &c_bounds);

void randomize_diagonal(tt_tensor &tensor, float mean = 0.0, float stddev = 0.25);
void randomize_manual_float(tt_tensor &tensor, int spread, int man_variance_bits);
void randomize_per_col_mask(
    tt_tensor &tensor, int spread, int man_variance_bits, vector<vector<vector<bool>>> &tensor_col_masks);
void init_to_xyz_values(tt_tensor &tensor);
void init_to_tile_id(tt_tensor &tensor, float step_size = 1.0, float base_val = 0.0);

template <typename T>
void set_number(tt_tensor &tensor, T num);
template <typename T>
void set_number(tt_tensor &tensor, T num, const std::pair<int, int> &r_bounds, const std::pair<int, int> &c_bounds);

float dword_to_float(uint32_t in);
void randomize_tile_per_col_mask(
    std::mt19937 &generator, tt_tile &tile, int spread, int man_variance_bits, vector<bool> &col_mask);

void generate_with_step(tt_tensor &tensor, float start, float increment);
}  // namespace random
}  // namespace verif
