// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "verif_rand_utils.hpp"


#include <algorithm>
#include <chrono>
#include <set>
#include <type_traits>

#include "utils/logger.hpp"

thread_local std::mt19937 verif::random::rand_gen(0);
thread_local bool verif::random::is_seed_initialized(false);

void verif::random::tt_rnd_set_seed(int seed) {
    log_info(tt::LogVerif, "Setting Test Seed = {}", seed);
    rand_gen.seed(seed);
    is_seed_initialized = true;
}

int32_t verif::random::tt_gen_seed() {
    std::mt19937::result_type result;
    std::random_device rd;
    result = rd() ^ ((std::mt19937::result_type)std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count() +
                     (std::mt19937::result_type)std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::high_resolution_clock::now().time_since_epoch())
                         .count());
    int32_t seed = *((int32_t *)(&result));
    log_info(tt::LogVerif, "Generated Test Seed = {}", seed);
    return seed;
}

void verif::random::log_assert_seed_uninitialized(const char* caller) {
    log_assert(is_seed_initialized,
               "The tt_rnd_set_seed() has to be called in the thread that calls {}() "
               "to avoid unexpected values due to using the default seed in verif::random library.",
               caller);
}

int verif::random::tt_rnd_int(int min, int max) {
    log_assert((min <= max), "min is greater than max!");
    log_assert_seed_uninitialized(__func__);
    std::uniform_int_distribution<int> distrib(min, max);
    return distrib(rand_gen);
}

uint32_t verif::random::tt_rnd_uint32t(uint32_t min, uint32_t max) {
    log_assert((min <= max), "min is greater than max!");
    log_assert_seed_uninitialized(__func__);
    std::uniform_int_distribution<uint32_t> distrib(min, max);
    return distrib(rand_gen);
}

float verif::random::tt_rnd_float(float min, float max) {
    log_assert((min <= max), "min is greater than max!");
    log_assert_seed_uninitialized(__func__);
    std::uniform_real_distribution<float> distrib(min, max);
    return distrib(rand_gen);
}

float verif::random::dword_to_float(uint32_t in) {
    union {
        float output;  // assumes sizeof(float) == sizeof(int)
        uint32_t input;
    } data;
    data.input = in;
    return (data.output);
}

template <typename T>
void verif::random::randomize_normal(tt_tensor &tensor, T mean, T stddev) {
    randomize_normal(tensor, mean, stddev, {0, tt::constants::TILE_HEIGHT - 1}, {0, tt::constants::TILE_WIDTH - 1});
}

template <typename T>
void verif::random::randomize_normal(
    tt_tensor &tensor, T mean, T stddev, const std::pair<int, int> &r_bounds, const std::pair<int, int> &c_bounds) {
    std::mt19937 &generator = verif::random::rand_gen;
    std::normal_distribution<double> distribution(mean, stddev);
    log_assert_seed_uninitialized(__func__);

    tensor.reserve_tile_tensor();
    tensor.apply([mean, stddev, &generator, &distribution, &r_bounds, &c_bounds](tt_tile &tile) {
        for (int i = r_bounds.first; i <= r_bounds.second; ++i) {
            for (int j = c_bounds.first; j <= c_bounds.second; ++j) {
                if (std::is_floating_point<T>::value) {
                    tile.set(i, j, distribution(generator));
                } else {
                    T val = static_cast<T>(std::round(distribution(generator)));
                    tile.set(i, j, val);
                }
            }
        }
    });
}

template <class T>
void verif::random::randomize_uniform(tt_tensor &tensor, T lower_bound, T upper_bound) {
    randomize_uniform(tensor, lower_bound, upper_bound, {0, tt::constants::TILE_HEIGHT - 1}, {0, tt::constants::TILE_WIDTH - 1});
}

template <class T>
void verif::random::randomize_uniform(
    tt_tensor &tensor,
    T lower_bound,
    T upper_bound,
    const std::pair<int, int> &r_bounds,
    const std::pair<int, int> &c_bounds) {
    std::mt19937 &generator = verif::random::rand_gen;
    log_assert_seed_uninitialized(__func__);

    using DistributionType = typename std::conditional<
        std::is_floating_point<T>::value,
        std::uniform_real_distribution<T>,
        std::uniform_int_distribution<T>>::type;
    DistributionType distribution(lower_bound, upper_bound);

    tensor.reserve_tile_tensor();
    tensor.apply([lower_bound, upper_bound, &generator, &distribution, &r_bounds, &c_bounds](tt_tile &tile) {
        for (int i = r_bounds.first; i <= r_bounds.second; ++i) {
            for (int j = c_bounds.first; j <= c_bounds.second; ++j) {
                tile.set(i, j, distribution(generator));
            }
        }
    });
}

void verif::random::randomize_diagonal(tt_tensor &tensor, float mean, float stddev) {
    std::mt19937 &generator = verif::random::rand_gen;
    std::normal_distribution<float> distribution(mean, stddev);
    log_assert_seed_uninitialized(__func__);

    tensor.reserve_tile_tensor();
    tensor.apply([mean, stddev, &generator, &distribution](tt_tile &tile) {
        memset(tile.t, 0, sizeof(tile.t));
        for (int i = 0; i < tt::constants::TILE_HEIGHT; ++i) {
            tile.t[i][i] = distribution(generator);
        }
    });
}

void verif::random::randomize_manual_float(tt_tensor &tensor, int spread, int man_variance_bits) {
    std::mt19937 &generator = verif::random::rand_gen;
    log_assert_seed_uninitialized(__func__);

    tensor.reserve_tile_tensor();
    tensor.apply([spread, man_variance_bits, &generator](tt_tile &tile) {
        int man_max = (1 << man_variance_bits) - 1;
        uint32_t man_shift = 23 - man_variance_bits;
        std::uniform_int_distribution<uint32_t> exp_distribution(127 - spread, 127 + spread);
        // uniform_int_distribution<uint32_t> man_distribution(0,8388600);
        std::uniform_int_distribution<uint32_t> man_distribution(0, man_max);
        std::uniform_int_distribution<uint32_t> sign_distribution(0, 1);

        for (int i = 0; i < tt::constants::TILE_HEIGHT; ++i) {
            for (int j = 0; j < tt::constants::TILE_WIDTH; ++j) {
                uint32_t sign = sign_distribution(generator);
                uint32_t exp = exp_distribution(generator);
                uint32_t man = man_distribution(generator);
                log_assert((sign == 0) || (sign == 1), "Sign should always be 0/1");
                log_assert((exp >= (127 - spread)) && (exp <= (127 + spread)), "Exponent is invalid");
                log_assert((man < (man_max + 4)), "Mantissa is invalid");
                uint32_t num = (sign << 31) | (exp << 23) | (man << man_shift);
                tile.t[i][j] = dword_to_float(num);
            }
        }
    });
}
void verif::random::randomize_per_col_mask(
    tt_tensor &tensor, int spread, int man_variance_bits, vector<vector<vector<bool>>> &tensor_col_masks) {
    log_assert(tensor_col_masks.size() == tensor.getrt(), "tensor_col_masks.size() must match tensor columns");
    std::mt19937 &generator = verif::random::rand_gen;
    log_assert_seed_uninitialized(__func__);

    tensor.reserve_tile_tensor();
    // Not using 'apply' method here for readability
    for (int wi = 0; wi < tensor.getw(); ++wi) {
        for (int zi = 0; zi < tensor.getz(); ++zi) {
            for (int ri = 0; ri < tensor.getrt(); ++ri) {
                log_assert(tensor_col_masks[ri].size() == tensor.getct(), "tensor_col_masks[ri].size() must match tensor rows");
                for (int ci = 0; ci < tensor.getct(); ++ci) {
                    randomize_tile_per_col_mask(
                        generator,
                        tensor.tile_tensor[wi][zi][ri][ci],
                        spread,
                        man_variance_bits,
                        tensor_col_masks[ri][ci]);
                }
            }
        }
    }
}

// Debug for tracking tiles
void verif::random::init_to_xyz_values(tt_tensor &tensor) {
    tensor.reserve_tile_tensor();

    for (int wi = 0; wi < tensor.getw(); ++wi) {
        for (int zi = 0; zi < tensor.getz(); ++zi) {
            for (int ri = 0; ri < tensor.getrt(); ++ri) {
                for (int ci = 0; ci < tensor.getct(); ++ci) {
                    int z = wi * tensor.getz() + zi;

                    for (int i = 0; i < tt::constants::TILE_HEIGHT; ++i) {
                        for (int j = 0; j < tt::constants::TILE_WIDTH; ++j) {
                            tensor.tile_tensor[wi][zi][ri][ci].t[i][j] = i * tt::constants::TILE_WIDTH + j + z * 1000;
                        }
                    }
                }
            }
        }
    }
}
// Debug for tracking tiles
void verif::random::init_to_tile_id(tt_tensor &tensor, float step_size, float base_val) {
    tensor.reserve_tile_tensor();

    for (int wi = 0; wi < tensor.getw(); ++wi) {
        int w_offset = wi * tensor.getz() * tensor.getrt() * tensor.getct();
        for (int zi = 0; zi < tensor.getz(); ++zi) {
            int z_offset = zi * tensor.getrt() * tensor.getct();
            for (int ri = 0; ri < tensor.getrt(); ++ri) {
                int r_offset = ri * tensor.getct();
                for (int ci = 0; ci < tensor.getct(); ++ci) {
                    int unique_tile_id = w_offset + z_offset + r_offset + ci;
                    tensor.tile_tensor[wi][zi][ri][ci] = unique_tile_id * step_size + base_val;
                }
            }
        }
    }
}

template <typename T>
void verif::random::set_number(tt_tensor &tensor, T num) {
    set_number(tensor, num, {0, tt::constants::TILE_HEIGHT - 1}, {0, tt::constants::TILE_WIDTH - 1});
}

template <typename T>
void verif::random::set_number(
    tt_tensor &tensor, T num, const std::pair<int, int> &r_bounds, const std::pair<int, int> &c_bounds) {
    tensor.reserve_tile_tensor();
    tensor.apply([num, &r_bounds, &c_bounds](tt_tile &tile) {
        for (int i = r_bounds.first; i <= r_bounds.second; ++i) {
            for (int j = c_bounds.first; j <= c_bounds.second; ++j) {
                tile.set(i, j, num);
            }
        }
    });
}

void verif::random::randomize_tile_per_col_mask(
    std::mt19937 &generator, tt_tile &tile, int spread, int man_variance_bits, vector<bool> &col_mask) {
    // Entire column must have only 1 non-zero element
    // Allowed locations for a non-zero element are provided in the col_mask vector
    // Side-effect of the function is the update to the col_mask structure

    uint32_t man_max = (1 << man_variance_bits) - 1;
    uint32_t man_shift = 23 - man_variance_bits;
    std::uniform_int_distribution<uint32_t> exp_distribution(127 - spread, 127 + spread);
    // uniform_int_distribution<uint32_t> man_distribution(0,8388600);
    std::uniform_int_distribution<uint32_t> man_distribution(0, man_max);
    std::uniform_int_distribution<uint32_t> sign_distribution(0, 1);
    std::uniform_int_distribution<uint32_t> index_distribution(0, tt::constants::TILE_HEIGHT - 1);

    memset(tile.t, 0, sizeof(tile.t));

    std::vector<bool> all_false(tt::constants::TILE_HEIGHT, false);
    log_assert(col_mask.size() == tt::constants::TILE_HEIGHT, "col_mask.size() must match tile height");
    for (int j = 0; j < tt::constants::TILE_WIDTH; ++j) {
        // get number with random spec
        // Keep sampling until contitions are met
        uint32_t sign;
        uint32_t exp;
        uint32_t man;
        do {
            sign = sign_distribution(generator);
        } while ((sign != 0) && (sign != 1));
        do {
            exp = exp_distribution(generator);
        } while ((exp < (127 - spread)) || (exp > (127 + spread)));
        do {
            man = (man_max == 0) ? 0 : man_distribution(generator);
        } while (man > man_max);

        uint32_t num = (sign << 31) | (exp << 23) | (man << man_shift);
        float tmp_val = dword_to_float(num);

        if (col_mask != all_false) {
            bool row_index_allowed = false;
            int row_index;
            while (row_index_allowed == false) {
                row_index = index_distribution(generator);
                row_index_allowed = col_mask[row_index] && (row_index < tt::constants::TILE_HEIGHT) && (row_index >= 0);
            }
            tile.t[row_index][j] = tmp_val;
            col_mask[row_index] = false;
        }
    }

    // check if at most 1 number is non-zero for every column
    for (int j = 0; j < tt::constants::TILE_WIDTH; ++j) {
        bool found_non_zero = false;
        for (int i = 0; i < tt::constants::TILE_HEIGHT; ++i) {
            if (tile.t[i][j] != 0.0) {
                log_assert(!found_non_zero, "At most 1 number is non-zero for every column");
                found_non_zero = true;
            }
        }
    }
}

// Explicit template instantiations
template void verif::random::randomize_normal<float>(tt_tensor &, float, float);
template void verif::random::randomize_normal<double>(tt_tensor &, double, double);
template void verif::random::randomize_normal<int>(tt_tensor &, int, int);
template void verif::random::randomize_normal<uint32_t>(tt_tensor &, uint32_t, uint32_t);

template void verif::random::randomize_uniform<float>(tt_tensor &, float, float);
template void verif::random::randomize_uniform<double>(tt_tensor &, double, double);
template void verif::random::randomize_uniform<int>(tt_tensor &, int, int);
template void verif::random::randomize_uniform<uint32_t>(tt_tensor &, uint32_t, uint32_t);

template void verif::random::randomize_normal<float>(
    tt_tensor &, float, float, const std::pair<int, int> &, const std::pair<int, int> &);
template void verif::random::randomize_normal<double>(
    tt_tensor &, double, double, const std::pair<int, int> &, const std::pair<int, int> &);
template void verif::random::randomize_normal<int>(
    tt_tensor &, int, int, const std::pair<int, int> &, const std::pair<int, int> &);
template void verif::random::randomize_normal<uint32_t>(
    tt_tensor &, uint32_t, uint32_t, const std::pair<int, int> &, const std::pair<int, int> &);

template void verif::random::randomize_uniform<float>(
    tt_tensor &, float, float, const std::pair<int, int> &, const std::pair<int, int> &);
template void verif::random::randomize_uniform<double>(
    tt_tensor &, double, double, const std::pair<int, int> &, const std::pair<int, int> &);
template void verif::random::randomize_uniform<int>(
    tt_tensor &, int, int, const std::pair<int, int> &, const std::pair<int, int> &);
template void verif::random::randomize_uniform<uint32_t>(
    tt_tensor &, uint32_t, uint32_t, const std::pair<int, int> &, const std::pair<int, int> &);

template void verif::random::set_number<float>(tt_tensor &, float);
template void verif::random::set_number<double>(tt_tensor &, double);
template void verif::random::set_number<int>(tt_tensor &, int);
template void verif::random::set_number<uint32_t>(tt_tensor &, uint32_t);

template void verif::random::set_number<float>(
    tt_tensor &, float, const std::pair<int, int> &, const std::pair<int, int> &);
template void verif::random::set_number<double>(
    tt_tensor &, double, const std::pair<int, int> &, const std::pair<int, int> &);
template void verif::random::set_number<int>(
    tt_tensor &, int, const std::pair<int, int> &, const std::pair<int, int> &);
template void verif::random::set_number<uint32_t>(
    tt_tensor &, uint32_t, const std::pair<int, int> &, const std::pair<int, int> &);
