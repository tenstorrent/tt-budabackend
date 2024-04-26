// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstring>

#include <immintrin.h>
#include <string>
#include <iomanip>


#include "tilecopy.h"
// Ignore ignored attributes warning related to using __m256i types in the std::function arguments
// Warning raised due to alignment attributes  (implictly defined in the intrinsic type) not respected by the template
// We can safely ignore this warning, since we explictly handle alignment when populating this function
#pragma GCC diagnostic ignored "-Wignored-attributes"
extern std::function<__m256i(const __m256i*)> simd_256_load;

namespace
{
template <std::size_t TileX, class Converter>
void tensor_to_row(const typename Converter::in_type * NO_PTR_ALIAS in, typename Converter::out_type * NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    for (std::size_t i = 0; i < TileX; i++) {
        *out++ = Converter::convert(in[i]);
    }
    
}
// Template Specializations for supported Data Formats and tile sizes
template<>
void tensor_to_row<32, RawUInt32_RawUInt32>(const std::uint32_t*  NO_PTR_ALIAS in, std::uint32_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    uint32_t num_blocks = copy_size_bytes / 32;
    __m256i* vout = reinterpret_cast<__m256i*>(out);
    const __m256i* vin = reinterpret_cast<const __m256i*>(in);

    for(uint32_t block = 0; block < num_blocks; block++) {
        _mm256_storeu_si256(vout + 0, simd_256_load(vin + block));
    }
}

template<>
void tensor_to_row<32, RawUInt16_RawUInt16>(const std::uint16_t* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    uint32_t num_blocks = copy_size_bytes / 32;
    __m256i* vout = reinterpret_cast<__m256i*>(out);
    const __m256i* vin = reinterpret_cast<const __m256i*>(in);
    for(uint32_t block = 0; block < num_blocks; block++) {
        _mm256_storeu_si256(vout + block, simd_256_load((vin + block)));
    }
}

template<>
void tensor_to_row<32, RawUInt8_RawUInt8>(const std::uint8_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    uint32_t num_blocks = copy_size_bytes / 32;
    __m256i* vout = reinterpret_cast<__m256i*>(out);
    const __m256i* vin = reinterpret_cast<const __m256i*>(in);
    for(uint32_t block = 0; block < num_blocks; block++) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(vout + block), simd_256_load(reinterpret_cast<const __m256i*>(vin + block)));
    }
}

template<>
void tensor_to_row<32, Fp32_Fp32>(const float*  NO_PTR_ALIAS in, float*  NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    uint32_t num_blocks = copy_size_bytes / 32;
    __m256i* vout = reinterpret_cast<__m256i*>(out);
    const __m256i* vin = reinterpret_cast<const __m256i*>(in);
    for(uint32_t block = 0; block < num_blocks; block++) {
        _mm256_storeu_si256(vout + 0, simd_256_load(vin + block));
    }
}

template<>
void tensor_to_row<32, Fp32_Fp16b>(const float*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    uint32_t num_blocks = copy_size_bytes / 16;
    // VPSHUFB independently shuffles bytes in the top & bottom 128-bit halves of an AVX value.
    // We use it to collect the high 16 bits of each 32-bit float into the low 64-bits of each half.
    // i.e. from ABCDEFGHIJKLMNOPQRSTUVWXYZ123456 to 00000000CDGHKLOP00000000STWX1256
    // Then split the top half using VEXTRACTI128 and merge using VPUNPCKLQDQ.

    const __m256i shuffle_mask = _mm256_setr_epi8(2, 3, 6, 7, 10, 11, 14, 15,
                                                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                  2, 3, 6, 7, 10, 11, 14, 15,
                                                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    
    
    const __m256i* vin = reinterpret_cast<const __m256i*>(in);
    __m128i* vout = reinterpret_cast<__m128i*>(out);;

    for(int block = 0; block < num_blocks; block++){
        __m256i fp32_in = simd_256_load(vin+block);
        __m256i post_shuffle = _mm256_shuffle_epi8(fp32_in, shuffle_mask);
        __m128i post_shuffle_high = _mm256_extracti128_si256(post_shuffle, 1);
        __m256i packed = _mm256_unpacklo_epi64(post_shuffle, _mm256_castsi128_si256(post_shuffle_high));
        _mm_store_si128(vout+block, _mm256_castsi256_si128(packed));
    }
}

template<>
void tensor_to_row<32, Fp32_Fp16>(const float*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    uint32_t num_blocks = copy_size_bytes / 16;
    const __m256i mantissa_mask = _mm256_set1_epi32(0x007fffff);
    const __m256i exp_mask = _mm256_set1_epi32(0x7F800000);
    const __m256i sign_mask = _mm256_set1_epi32(0x80000000);

    const __m256i shuffle_order = _mm256_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    const __m256i permute_order = _mm256_setr_epi32(0, 1, 4, 5, 2, 33, 6, 7);

    
    const __m256i* vin = reinterpret_cast<const __m256i*>(in);
    __m128i* vout = reinterpret_cast<__m128i*>(out);

    for(int block = 0; block < num_blocks; block++){
        __m256i fp32_in = simd_256_load(vin + block);
        __m256i mantissa = _mm256_and_si256(fp32_in, mantissa_mask);
        __m256i exp = _mm256_sub_epi32(_mm256_srl_epi32(_mm256_and_si256(fp32_in, exp_mask), _mm_set_epi64x(0, 23)), _mm256_set1_epi32(127));
        __m256i sign = _mm256_srl_epi32(_mm256_and_si256(fp32_in, sign_mask), _mm_set_epi64x(0, 16));
        __m256i cmp_mask = _mm256_cmpgt_epi32(exp, _mm256_set1_epi32(-15));
        exp = _mm256_blendv_epi8(_mm256_set1_epi32(-15), exp, cmp_mask);
        mantissa = _mm256_blendv_epi8(_mm256_set1_epi32(0), mantissa, cmp_mask);
        cmp_mask = _mm256_cmpgt_epi32(exp, _mm256_set1_epi32(16));
        exp = _mm256_blendv_epi8(exp, _mm256_set1_epi32(16), cmp_mask);
        mantissa = _mm256_srl_epi32(_mm256_blendv_epi8(mantissa, mantissa_mask, cmp_mask), _mm_set_epi64x(0, 13));
        exp = _mm256_sll_epi32(_mm256_add_epi32(exp, _mm256_set1_epi32(15)), _mm_set_epi64x(0, 10));
        sign = _mm256_shuffle_epi8(_mm256_or_si256(_mm256_or_si256(sign, exp), mantissa), shuffle_order);
        _mm_store_si128(vout + block, _mm256_extractf128_si256(_mm256_permutevar8x32_epi32(sign, permute_order), 0));
    }
}
template<>
void tensor_to_row<32, Fp16b_Fp16b>(const std::uint16_t*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    const __m256i* vin = reinterpret_cast<const __m256i*>(in);
    __m256i* vout = reinterpret_cast<__m256i*>(out);
    uint32_t num_blocks = copy_size_bytes / 32;
    for(int block = 0; block < num_blocks; block++) {
        _mm256_storeu_si256(vout + block, simd_256_load(vin + block));
    }
}

template<>
void tensor_to_row<32, Fp16b_Fp16>(const std::uint16_t*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    uint32_t num_blocks = copy_size_bytes / 32;
    const __m256i mantissa_mask = _mm256_set1_epi16(0x7f);
    const __m256i exp_mask = _mm256_set1_epi16(0x7f80);
    const __m256i sign_mask = _mm256_set1_epi16(0x8000);

    
    const __m256i* vin = reinterpret_cast<const __m256i*>(in);
    __m256i* vout = reinterpret_cast<__m256i*>(out);
    
    for(int block = 0; block < num_blocks; block++){
        __m256i fp16b_in = simd_256_load(vin + block);
        __m256i mantissa = _mm256_and_si256(fp16b_in, mantissa_mask);
        __m256i exp = _mm256_sub_epi16(_mm256_srl_epi16(_mm256_and_si256(fp16b_in, exp_mask), _mm_set_epi64x(0, 7)), _mm256_set1_epi16(127));
        __m256i sign = _mm256_and_si256(fp16b_in, sign_mask);
        __m256i cmp_mask = _mm256_cmpgt_epi16(exp, _mm256_set1_epi16(-15));
        exp = _mm256_blendv_epi8(_mm256_set1_epi16(-15), exp, cmp_mask);
        mantissa = _mm256_blendv_epi8(_mm256_setzero_si256(), mantissa, cmp_mask);
        cmp_mask =  _mm256_cmpgt_epi16(exp, _mm256_set1_epi16(16));
        exp = _mm256_blendv_epi8(exp, _mm256_set1_epi16(16), cmp_mask);
        mantissa = _mm256_blendv_epi8(_mm256_sll_epi16(mantissa, _mm_set_epi64x(0, 3)), _mm256_set1_epi16(0x3ff), cmp_mask);
        exp = _mm256_sll_epi16(_mm256_add_epi16(exp, _mm256_set1_epi16(15)), _mm_set_epi64x(0, 10));
        sign = _mm256_or_si256(_mm256_or_si256(sign, exp), mantissa);
        _mm256_storeu_si256(vout + block, sign);
    }
}
template<>
void tensor_to_row<32, Fp16_Fp16b>(const std::uint16_t*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes)
{
    uint32_t num_blocks = copy_size_bytes / 32;

    const __m256i* vin = reinterpret_cast<const __m256i*>(in);
    __m256i* vout = reinterpret_cast<__m256i*>(out);

    for(int block = 0; block < num_blocks; block++){
        __m256i fp16_in = simd_256_load(vin + block);
        __m256i exp = _mm256_srl_epi16(_mm256_and_si256(fp16_in, _mm256_set1_epi16(0x7c00)), _mm_set_epi64x(0, 10));
        exp = _mm256_add_epi16(exp, _mm256_set1_epi16(112));
        exp = _mm256_blendv_epi8(exp, _mm256_set1_epi16(0), _mm256_cmpeq_epi16(exp, _mm256_set1_epi16(112)));
        exp = _mm256_sll_epi16(exp, _mm_set_epi64x(0, 7));
        __m256i man = _mm256_srl_epi16(_mm256_and_si256(fp16_in, _mm256_set1_epi16(0x3ff)), _mm_set_epi64x(0, 3));
        __m256i sign = _mm256_and_si256(fp16_in, _mm256_set1_epi16(0x8000));
        _mm256_storeu_si256(vout + block, _mm256_or_si256(sign, _mm256_or_si256(exp, man)));
    }
}
}

template<std::size_t Tile_X, class Converter>
void row_copy(const typename Converter::in_type * NO_PTR_ALIAS in, typename Converter::out_type * NO_PTR_ALIAS out, uint32_t copy_size_bytes) {
    tensor_to_row<Tile_X, Converter>(in, out, copy_size_bytes);
}

template void row_copy<32, RawUInt32_RawUInt32>(const std::uint32_t*  NO_PTR_ALIAS in, std::uint32_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, RawUInt16_RawUInt16>(const std::uint16_t*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, RawUInt8_RawUInt8>(const std::uint8_t*  NO_PTR_ALIAS in, std::uint8_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp32_Fp32>(const float*  NO_PTR_ALIAS in, float*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp32_Fp16b>(const float*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp32_Fp16>(const float*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp16b_Fp16b>(const std::uint16_t*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp16b_Fp16>(const std::uint16_t*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp16_Fp16b>(const std::uint16_t*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);

template void row_copy<32, Fp32_Bfp8_all>(const float* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp16b_Bfp8_all>(const std::uint16_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp16b_Fp32>(const std::uint16_t* NO_PTR_ALIAS in, float* NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp16_Bfp8>(const std::uint16_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp16_Bfp8b>(const std::uint16_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Fp16_Fp32>(const std::uint16_t* NO_PTR_ALIAS in, float* NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Int8_Int8>(const std::int8_t*  NO_PTR_ALIAS in, std::int8_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);
template void row_copy<32, Int8_Bfp8>(const std::uint8_t*  NO_PTR_ALIAS in, std::uint8_t*  NO_PTR_ALIAS out, uint32_t copy_size_bytes);