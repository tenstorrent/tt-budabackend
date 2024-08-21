// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tile_lib.hpp"
#ifdef __x86_64__
#include <immintrin.h>
#endif
#include <cmath>
#include <random>

#include "utils/logger.hpp"

// *** These are all Tile library operations meant to replace ones in tile.cpp,
// ***   they are all inplace and optimized to be as quick as possible
namespace tt::tile_lib {

// *** Tile by Tile Binary Operations *** //
namespace binary {
#ifdef __x86_64__
void add(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1) {
    __m256 row_vector_A;
    __m256 row_vector_B;
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i += 8) {
        row_vector_A = _mm256_load_ps(&input0.t_vector[i]);
        row_vector_B = _mm256_load_ps(&input1.t_vector[i]);
        _mm256_store_ps(&output_tile.t_vector[i], _mm256_add_ps(row_vector_A, row_vector_B));
    }
}
void subtract(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1) {
    __m256 row_vector_A;
    __m256 row_vector_B;
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i += 8) {
        row_vector_A = _mm256_load_ps(&input0.t_vector[i]);
        row_vector_B = _mm256_load_ps(&input1.t_vector[i]);
        _mm256_store_ps(&output_tile.t_vector[i], _mm256_sub_ps(row_vector_A, row_vector_B));
    }
}
void multiply(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1) {
    __m256 row_vector_A;
    __m256 row_vector_B;
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i += 8) {
        row_vector_A = _mm256_load_ps(&input0.t_vector[i]);
        row_vector_B = _mm256_load_ps(&input1.t_vector[i]);
        // Find all zeroes in A and set the corresponding idxs in B to 0.
        __m256i cmp_mask = _mm256_cmpeq_epi32(_mm256_castps_si256(row_vector_A), _mm256_set1_epi32(0));
        row_vector_B =
            _mm256_castsi256_ps(_mm256_blendv_epi8(_mm256_castps_si256(row_vector_B), _mm256_set1_epi32(0), cmp_mask));
        // Find all zeroes in B and set the corresponding idxs in A to 0.
        cmp_mask = _mm256_cmpeq_epi32(_mm256_castps_si256(row_vector_B), _mm256_set1_epi32(0));
        row_vector_A =
            _mm256_castsi256_ps(_mm256_blendv_epi8(_mm256_castps_si256(row_vector_A), _mm256_set1_epi32(0), cmp_mask));
        // Multiply
        _mm256_store_ps(&output_tile.t_vector[i], _mm256_mul_ps(row_vector_A, row_vector_B));
    }
}
void maximum(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1) {
    __m256 row_vector_A;
    __m256 row_vector_B;
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i += 8) {
        row_vector_A = _mm256_load_ps(&input0.t_vector[i]);
        row_vector_B = _mm256_load_ps(&input1.t_vector[i]);
        _mm256_store_ps(&output_tile.t_vector[i], _mm256_max_ps(row_vector_A, row_vector_B));
    }
}
#else
void add(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1) {
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i++) {
        output_tile.t_vector[i] = input0.t_vector[i] + input1.t_vector[i];
    }
}
void subtract(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1) {
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i++) {
        output_tile.t_vector[i] = input0.t_vector[i] - input1.t_vector[i];
    }
}
void multiply(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1) {
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i++) {
        // Set output to 0 if either input datum is 0.
        output_tile.t_vector[i] = (input0.t_vector[i] == 0 || input1.t_vector[i] == 0) ? 0 : input0.t_vector[i] * input1.t_vector[i];
    }
}
void maximum(tt::tt_tile& output_tile, const tt::tt_tile& input0, const tt::tt_tile& input1) {
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i++) {
        output_tile.t_vector[i] = input0.t_vector[i] > input1.t_vector[i] ? input0.t_vector[i] : input1.t_vector[i];
    }
}

#endif
}  // namespace binary

// *** Tile by Tile Unary Operations *** //
namespace unary {

// Per Float util functions
namespace utils {
float exp(const float& x) { return std::min(std::numeric_limits<float>::max(), std::exp(x)); }
float tanh(const float& x) { return std::tanh(x); }
float log(const float& x) {
    if (x == 0.0f) {
        // Account for -inf case, add epsilon
        return std::log(std::numeric_limits<float>::epsilon());
    } else {
        return std::log(x);
    }
}
float sigmoid(const float& x) { return 1.0 / (1.0 + utils::exp(-x)); }
float sqrt(const float& x) {
    if (std::isnan(x)) {
        log_fatal("sqrt input={} is NaN", x);
    } else if (x < 0.0f) {
        log_fatal("input to sqrt is negative, will result in a NaN");
    }
    return std::sqrt(x);
}
float square(const float& x) { return (x * x); }
float relu(const float& x, const ReluMode& relu_mode, const float& threshold = 0.0f) {
    if (relu_mode == ReluMode::Max) {
        return (x >= threshold) ? threshold : relu(x, ReluMode::Min);  // [0, threshold]
    } else {
        return (x >= threshold) ? x : 0.0f;  // [0, x]
    }
};
float gelu(const float& x) {
    return 0.5 * x * (1 + std::tanh((M_2_SQRTPI / M_SQRT2) * (x + 0.044715 * std::pow(x, 3))));
};
float lrelu(const float& x, const float& slope) {
    return (x > 0.0f) ? x : x * slope;  // [x*slope, x];
};
float sine(const float& x) { return std::sin(x); };
float cosine(const float& x) { return std::cos(x); };
float abs(const float& x) { return std::abs(x); };
float gelu_derivative(const float& x) {
    return 0.5 * (1 + std::tanh((M_2_SQRTPI / M_SQRT2) * (x + 0.044715 * std::pow(x, 3)))) +  // Intermediate0
           x * std::exp(-0.5 * x * x) * (0.5 * M_2_SQRTPI / M_SQRT2);                         // Intermediate1
}
float reciprocal(const float& x) {
    if (x == 0.0f) {
        return 1.0 / std::numeric_limits<float>::epsilon();
    }
    float value = 1.0 / x;
    if (std::isnan(value)) {
        log_fatal("reciprocal output={} is NaN", value);
    } else if (value == -std::numeric_limits<float>::infinity()) {
        log_fatal("reciprocal output={} is negative inf", value);
    }
    return value;
}
#ifdef __x86_64__
// Fast transpose not defined for ARM
inline void fast_transpose_4x4(const float* A, float* B, const int lda, const int ldb) {
    __m128 row1 = _mm_load_ps(&A[0 * lda]);
    __m128 row2 = _mm_load_ps(&A[1 * lda]);
    __m128 row3 = _mm_load_ps(&A[2 * lda]);
    __m128 row4 = _mm_load_ps(&A[3 * lda]);
    _MM_TRANSPOSE4_PS(row1, row2, row3, row4);
    _mm_store_ps(&B[0 * ldb], row1);
    _mm_store_ps(&B[1 * ldb], row2);
    _mm_store_ps(&B[2 * ldb], row3);
    _mm_store_ps(&B[3 * ldb], row4);
}
#endif
}  // namespace utils
void exp(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::exp(input0.t[r][c]);
        }
    }
}
void log(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::log(input0.t[r][c]);
        }
    }
}
void sigmoid(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::sigmoid(input0.t[r][c]);
        }
    }
}
void sqrt(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::sqrt(input0.t[r][c]);
        }
    }
}

void lrelu(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode, const float& slope) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::lrelu(input0.t[r][c], slope);
        }
    }
}
void relu_with_threshold(
    tt::tt_tile& output_tile,
    const tt::tt_tile& input0,
    const Dim& vector_mode,
    const ReluMode& mode,
    const float& threshold) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::relu(input0.t[r][c], mode, threshold);
        }
    }
}
void gelu(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::gelu(input0.t[r][c]);
        }
    }
}
void gelu_derivative(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::gelu_derivative(input0.t[r][c]);
        }
    }
}
void reciprocal(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::reciprocal(input0.t[r][c]);
        }
    }
}
void tanh(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::tanh(input0.t[r][c]);
        }
    }
}

void sine(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::sine(input0.t[r][c]);
        }
    }
}
void cosine(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::cosine(input0.t[r][c]);
        }
    }
}
void abs(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = utils::abs(input0.t[r][c]);
        }
    }
}

void vector_datacopy(
    tt::tt_tile& output_tile,
    const tt::tt_tile input0,
    const Dim& vector_type,
    const unsigned int input_index,
    const unsigned int output_index) {
    log_assert(
        (vector_type == Dim::R) or (vector_type == Dim::C),
        "Only a row vector or col vector can be copied");

    if (vector_type == Dim::R) {
        // Copying a row vector
        log_assert(
            input_index < tt::constants::TILE_HEIGHT,
            "input_index={} must be a row offset that is within bounds of TILE_HEIGHT={}",
            input_index,
            tt::constants::TILE_HEIGHT);
        log_assert(
            output_index < tt::constants::TILE_HEIGHT,
            "output_index={} must be a row offset that is within bounds of TILE_HEIGHT={}",
            output_index,
            tt::constants::TILE_HEIGHT);
        for (unsigned int c = 0; c < tt::constants::TILE_WIDTH; c++) {
            output_tile.t[output_index][c] = input0.t[input_index][c];
        }
    } else if (vector_type == Dim::C) {
        // Copying a col vector
        log_assert(
            input_index < tt::constants::TILE_WIDTH,
            "input_index={} must be a col offset that is within bounds of TILE_WIDTH={}",
            input_index,
            tt::constants::TILE_WIDTH);
        log_assert(
            output_index < tt::constants::TILE_WIDTH,
            "output_index={} must be a col offset that is within bounds of TILE_WIDTH={}",
            output_index,
            tt::constants::TILE_WIDTH);
        for (unsigned int r = 0; r < tt::constants::TILE_HEIGHT; r++) {
            output_tile.t[r][output_index] = input0.t[r][input_index];
        }
    }
}
#ifdef __x86_64__
void square(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c += 8) {
            __m256 row_vec = _mm256_loadu_ps(input0.t[r] + c);
            _mm256_storeu_ps(output_tile.t[r] + c, _mm256_mul_ps(row_vec, row_vec));
        }
    }
}
void relu(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;

    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c += 8) {
            __m256i input_vec = _mm256_castps_si256(_mm256_loadu_ps(input0.t[r] + c));
            __m256i cmp_mask = _mm256_cmpgt_epi32(input_vec, _mm256_setzero_si256());
            _mm256_storeu_ps(
                output_tile.t[r] + c,
                _mm256_castsi256_ps(_mm256_blendv_epi8(_mm256_setzero_si256(), input_vec, cmp_mask)));
        }
    }
}

void dropout(
    tt::tt_tile& output_tile,
    const tt::tt_tile& input0,
    const Dim& vector_mode,
    const float& probability,
    const float& scale) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    std::mt19937 gen;
    std::bernoulli_distribution d(probability);
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c += 8) {
            // Vectorized version of (1 - d(gen)) * scale * input
            // Equivalent to d(gen) ? 0 : 1 * scale * input
            _mm256_storeu_ps(
                output_tile.t[r] + c,
                _mm256_mul_ps(
                    _mm256_mul_ps(
                        _mm256_sub_ps(
                            _mm256_set1_ps(1.0),
                            _mm256_set_ps(d(gen), d(gen), d(gen), d(gen), d(gen), d(gen), d(gen), d(gen))),
                        _mm256_loadu_ps(input0.t[r] + c)),
                    _mm256_set1_ps(scale)));
        }
    }
}
void power(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode, const int& exponent) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c += 8) {
            _mm256_storeu_ps(output_tile.t[r] + c, _mm256_set1_ps(1.0));
            for (int e = 0; e < exponent; e++) {
                _mm256_storeu_ps(
                    output_tile.t[r] + c,
                    _mm256_mul_ps(_mm256_loadu_ps(input0.t[r] + c), _mm256_loadu_ps(output_tile.t[r] + c)));
            }
        }
    }
}
void transpose_xy(tt::tt_tile& output_tile, const tt::tt_tile& input0) {
    log_assert(
        tt::constants::TILE_HEIGHT == tt::constants::TILE_WIDTH,
        "Tile Height must match tile width when doing transpose xy");
    // Chunk input and output into 4x4 blocks and perform fast transpose and copy
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT; i += 4) {
        for (unsigned int j = 0; j < tt::constants::TILE_WIDTH; j += 4) {
            utils::fast_transpose_4x4(
                &input0.t_vector[i * tt::constants::TILE_WIDTH + j],
                &output_tile.t_vector[j * tt::constants::TILE_HEIGHT + i],
                tt::constants::TILE_WIDTH,
                tt::constants::TILE_HEIGHT);
        }
    }
}
void datacopy(tt::tt_tile& output_tile, const tt::tt_tile& input0) {
    __m256 in;
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i += 8) {
        in = _mm256_load_ps(&input0.t_vector[i]);
        _mm256_store_ps(&output_tile.t_vector[i], in);
    }
}

#else
void square(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = input0.t[r][c] * input0.t[r][c];
        }
    }
}

void relu(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;

    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = (input0.t[r][c] < 0) ? 0 : input0.t[r][c];
        }
    }
}

void dropout(
    tt::tt_tile& output_tile,
    const tt::tt_tile& input0,
    const Dim& vector_mode,
    const float& probability,
    const float& scale) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    std::mt19937 gen;
    std::bernoulli_distribution d(probability);
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c += 8) {
            output_tile.t[r][c] = d(gen) ? 0 : input0.t[r][c] * scale;
        }
    }
}
void power(tt::tt_tile& output_tile, const tt::tt_tile& input0, const Dim& vector_mode, const int& exponent) {
    int rows_to_process = (vector_mode == Dim::R) ? 4 : tt::constants::TILE_HEIGHT;
    int cols_to_process = (vector_mode == Dim::C) ? tt::constants::TILE_WIDTH / 2 : tt::constants::TILE_WIDTH;
    for (int r = 0; r < rows_to_process; r++) {
        for (int c = 0; c < cols_to_process; c++) {
            output_tile.t[r][c] = 1;
            for (int e=0;e<exponent;e++) {
                output_tile.t[r][c] *= input0.t[r][c];
            }    
        }
    }
}
void transpose_xy(tt::tt_tile& output_tile, const tt::tt_tile& input0) {
    log_assert(
        tt::constants::TILE_HEIGHT == tt::constants::TILE_WIDTH,
        "Tile Height must match tile width when doing transpose xy");
    for (uint32_t r = 0; r < tt::constants::TILE_HEIGHT; r++) {
        for (uint32_t c = 0; c < tt::constants::TILE_WIDTH; c++) {
            output_tile.t[r][c] = input0.t[c][r];
        }
    }
}

void datacopy(tt::tt_tile& output_tile, const tt::tt_tile& input0) {
    for (unsigned int i = 0; i < tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH; i += 1) {
        output_tile.t_vector[i] = input0.t_vector[i];
    }
}

#endif
}  // namespace unary

}  // namespace tt::tile_lib
