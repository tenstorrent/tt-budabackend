// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tile.hpp"

#ifdef __x86_64__
#include <immintrin.h>
#endif

namespace tt {
    #ifdef __x86_64__
    inline void fast_transpose_4x4(const float *A, float *B, const int lda, const int ldb) {
        __m128 row1 = _mm_load_ps(&A[0*lda]);
        __m128 row2 = _mm_load_ps(&A[1*lda]);
        __m128 row3 = _mm_load_ps(&A[2*lda]);
        __m128 row4 = _mm_load_ps(&A[3*lda]);
        _MM_TRANSPOSE4_PS(row1, row2, row3, row4);
        _mm_store_ps(&B[0*ldb], row1);
        _mm_store_ps(&B[1*ldb], row2);
        _mm_store_ps(&B[2*ldb], row3);
        _mm_store_ps(&B[3*ldb], row4);
    }

    tt_tile tt_tile::transpose_xy() const
    {
        tt_tile tmp;
        tmp.data_format = data_format;
     
        for (int i=0; i<tt::constants::TILE_HEIGHT; i+=4) {
            for (int j=0; j<tt::constants::TILE_WIDTH; j+=4) {
                fast_transpose_4x4(&t_vector[i*tt::constants::TILE_WIDTH + j], &tmp.t_vector[j*tt::constants::TILE_HEIGHT + i], tt::constants::TILE_WIDTH, tt::constants::TILE_HEIGHT);
            }
        }
        return(tmp);
    }

    tt_tile tt_tile::operator+(const tt_tile &rhs) const
    {
        tt_tile tmp;
        tmp.data_format = data_format;

        int i,j;
        __m256 row_vector_A;
        __m256 row_vector_B;

        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for (j = 0; j < tt::constants::TILE_WIDTH; j+=8) {
                row_vector_A = _mm256_load_ps(&t[i][j]);
                row_vector_B = _mm256_load_ps(&rhs.t[i][j]);
                _mm256_store_ps(&tmp.t[i][j], _mm256_add_ps(row_vector_A, row_vector_B));
            }
        }
        return(tmp);
    }

    tt_tile tt_tile::operator-(const tt_tile &rhs) const
    {
        tt_tile tmp;
        tmp.data_format = data_format;

        int i,j;
        __m256 row_vector_A;
        __m256 row_vector_B;

        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for (j = 0; j < tt::constants::TILE_WIDTH; j+=8) {
                row_vector_A = _mm256_load_ps(&t[i][j]);
                row_vector_B = _mm256_load_ps(&rhs.t[i][j]);
                _mm256_store_ps(&tmp.t[i][j], _mm256_sub_ps(row_vector_A, row_vector_B));
            }
        }
        return(tmp);
    }

    void tt_tile::matmul_with_partial(const tt_tile &rhs, tt_tile &result) const
    {
        int i,j,k;

        union alignas(32) {
            float t[tt::constants::TILE_HEIGHT][tt::constants::TILE_WIDTH];
            float t_vector[tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH];
            uint32_t t_u32[tt::constants::TILE_HEIGHT][tt::constants::TILE_WIDTH];
        } B_T;

        // Transpose matrix B makes it possible to do vectorized load
        for (i=0; i<tt::constants::TILE_HEIGHT; i+=4) {
            for (j=0; j<tt::constants::TILE_WIDTH; j+=4) {
                fast_transpose_4x4(&rhs.t_vector[i*tt::constants::TILE_WIDTH + j], &B_T.t_vector[j*tt::constants::TILE_HEIGHT + i], tt::constants::TILE_WIDTH, tt::constants::TILE_HEIGHT);
            }
        }

        __m256 row_vector_A, row_vector_B, int_prod;

        // Vectorized matrix multiplication with reduction on partial result
        for (i = 0; i < tt::constants::TILE_HEIGHT; i++) {
            for (j = 0; j < tt::constants::TILE_WIDTH; j++) {
                int_prod = _mm256_setzero_ps();
                for (k = 0; k < tt::constants::TILE_WIDTH; k+=8) {
                    row_vector_A = _mm256_load_ps(&t[i][k]);
                    row_vector_B = _mm256_load_ps(&B_T.t[j][k]);
                    int_prod = _mm256_fmadd_ps(row_vector_A, row_vector_B, int_prod);
                }
                // CPU implements reduction tree more efficiently than _mm256_hadd
                // 8-to-4 reduction
                __m128 hi_quad = _mm256_extractf128_ps(int_prod, 1);
                __m128 lo_quad = _mm256_castps256_ps128(int_prod);
                __m128 sum_quad = _mm_add_ps(lo_quad, hi_quad);
                // 4-to-2 reduction
                __m128 lo_dual = sum_quad;
                __m128 hi_dual = _mm_movehl_ps(sum_quad, sum_quad);
                __m128 sum_dual = _mm_add_ps(lo_dual, hi_dual);
                // 4-to-1 reduction
                __m128 lo = sum_dual;
                __m128 hi = _mm_shuffle_ps(sum_dual, sum_dual, 0x1);
                result.t[i][j] += _mm_cvtss_f32(_mm_add_ss(lo, hi));
            }
        }
    }

    tt_tile& tt_tile::operator+=(const tt_tile& rhs)
    {
        int i,j;
        __m256 row_vector_A;
        __m256 row_vector_B;

        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for (j = 0; j < tt::constants::TILE_WIDTH; j+=8) {
                row_vector_A = _mm256_load_ps(&t[i][j]);
                row_vector_B = _mm256_load_ps(&rhs.t[i][j]);
                _mm256_store_ps(&t[i][j], _mm256_add_ps(row_vector_A, row_vector_B));
            }
        }
        return(*this);
    }

    void tt_tile::operator = (float num)
    {
        int i,j;
        __m256 row_vector;
        uint32_t aligned_tile_width = floor(tile_width / 8) * 8;
        for(i=0;i < tile_height;++i)
        {
            for (j = 0; j < aligned_tile_width; j+=8) {
                row_vector = _mm256_broadcast_ss(&num);
                _mm256_store_ps(&t[i][j], row_vector);
            }
            for(j = aligned_tile_width; j < tile_width; j++) {
                t[i][j] = num;
            }
        }
    }

    void tt_tile::operator = (const tt_tile& rhs)
    {
        log_assert(rhs.data_format != DataFormat::Invalid, "invalid data format");
        data_format = rhs.data_format;

        int i,j;
        #ifdef __x86_64__
        __m256 row_vector;

        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for (j = 0; j < tt::constants::TILE_WIDTH; j+=8) {
                row_vector = _mm256_load_ps(&rhs.t[i][j]);
                _mm256_store_ps(&t[i][j], row_vector);
            }
        }
        #else
        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                this->t[i][j] = rhs.t[i][j];
            }
        }
        #endif
    }
    #else
    tt_tile tt_tile::transpose_xy() const
    {
        tt_tile tmp;
        tmp.data_format = data_format;
     
        uint32_t i,j;
        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                tmp.t[i][j] = t[j][i];
            }
        }
        return tmp;
    }

    tt_tile tt_tile::operator+(const tt_tile &rhs) const
    {
        tt_tile tmp;
        tmp.data_format = data_format;

        int i,j;

        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                tmp.t[i][j] = t[i][j] + rhs.t[i][j];
            }
        }
        return(tmp);
    }

    tt_tile tt_tile::operator-(const tt_tile &rhs) const
    {
        tt_tile tmp;
        tmp.data_format = data_format;

        int i,j;

        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                tmp.t[i][j] = t[i][j] - rhs.t[i][j];
            }
        }
        return(tmp);
    }

    void tt_tile::matmul_with_partial(const tt_tile &rhs, tt_tile &result) const
    {
        // Vanilla matmul ... no SIMD tricks used
        // Multiply and Accumulate done inside the function
        float acc;

        for (uint32_t i = 0; i < tt::constants::TILE_HEIGHT; ++i)
        {
            for (uint32_t j = 0; j < tt::constants::TILE_WIDTH; ++j)
            {
                acc = 0.0;                
                for(uint32_t k =0; k < tt::constants::TILE_WIDTH; ++k)
                {
                    acc += t[i][k] * rhs.t[k][j];
                }
                result.t[i][j] += acc;
            }
        }
    }

    tt_tile& tt_tile::operator+=(const tt_tile& rhs)
    {
        int i,j;
        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                this->t[i][j] = this->t[i][j] + rhs.t[i][j];
            }
        }
        return(*this);
    }

    void tt_tile::operator = (float num)
    {
        int i,j;
        uint32_t aligned_tile_width = floor(tile_width / 8) * 8;
        for(i=0;i < tile_height;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                this->t[i][j] = num;
            }
        }
    }

    void tt_tile::operator = (const tt_tile& rhs)
    {
        log_assert(rhs.data_format != DataFormat::Invalid, "invalid data format");
        data_format = rhs.data_format;

        int i,j;
        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                this->t[i][j] = rhs.t[i][j];
            }
        }
    }
    #endif
}
