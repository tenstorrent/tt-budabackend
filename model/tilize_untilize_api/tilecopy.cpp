// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cstring>

#include <immintrin.h>
#include <string>
#include <iomanip>


#include "tilecopy.h"

// Asserts that p is has (at least) a given alignment, must be a power of 2 and you must use the returned pointer.
#define ASSUME_ALIGNED(p, alignment) static_cast<decltype(p)>(__builtin_assume_aligned((p), (alignment)))

#define PREFETCH(addr) _mm_prefetch((addr), _MM_HINT_T0)
// Ignore ignored attributes warning related to using __m256i types in the std::function arguments
// Warning raised due to alignment attributes  (implictly defined in the intrinsic type) not respected by the template
// We can safely ignore this warning, since we explictly handle alignment when populating this function

#pragma GCC diagnostic ignored "-Wignored-attributes"
extern std::function<__m256i(const __m256i*)> simd_256_load;
extern std::function<__m128i( __m128i*)> simd_128_load;
namespace
{
template <typename ActualDataType, typename DataType>
void print_intrinsic_vals(ActualDataType input, int num_elements, std::string msg = "") {
    DataType* input_cast = reinterpret_cast<DataType*> (&input);
    std::cout << msg << std::endl;
    for (int i = 0; i < num_elements; i++) {
        std::cout << (float)(input[i]) << " ";
    }
    std::cout << std::endl;
}

template<class T> inline void print_intrinsic_vals(const __m256i & value, std::string msg = "")
{
    const size_t n = sizeof(__m256i) / sizeof(T);
    T buffer[n];
    _mm256_storeu_si256((__m256i*)buffer, value);
    std::cout << msg << std::endl;
    for (int i = 0; i < n; i++)
        std::cout << buffer[i] << " ";
    std::cout << std::endl;
}

template<class T> inline void print_intrinsic_vals_hex(const __m256i & value, std::string msg = "")
{
    const size_t n = sizeof(__m256i) / sizeof(T);
    T buffer[n];
    _mm256_storeu_si256((__m256i*)buffer, value);
    std::cout << msg << std::endl;
    for (int i = 0; i < n; i++)
        std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << buffer[i] << " ";
    std::cout << std::endl;
}

template<class T, class T_print> inline void print_intrinsic_vals(const __m256i & value, std::string msg = "")
{
    const size_t n = sizeof(__m256i) / sizeof(T);
    T buffer[n];
    _mm256_storeu_si256((__m256i*)buffer, value);
    std::cout << msg << std::endl;
    for (int i = 0; i < n; i++)
        std::cout << (T_print)buffer[i] << " ";
    std::cout << std::endl;
}

template<class T, class T_print> inline void print_intrinsic_vals_hex(const __m256i & value, std::string msg = "")
{
    const size_t n = sizeof(__m256i) / sizeof(T);
    T buffer[n];
    _mm256_storeu_si256((__m256i*)buffer, value);
    std::cout << msg << std::endl;
    for (int i = 0; i < n; i++)
        std::cout << "0x" << std::hex << std::setw(8) << std::setfill('0') << (T_print)buffer[i] << " ";
    std::cout << std::endl;
}

// Linearize a single tile, converting data as we go.
template <std::size_t TileX, std::size_t TileY, class Converter, bool DataShuffling = false>
typename Converter::out_type *
tensor_to_tile(const typename Converter::in_type * NO_PTR_ALIAS in,
               typename Converter::out_type * NO_PTR_ALIAS out,
               std::uint_fast32_t tensor_row_pitch,
               void** = nullptr, bool = false, int = 0, uint32_t = 0, uint32_t quad_height = 16, bool = false)
{
    for (std::size_t j = 0; j < quad_height; j++) {
        for (std::size_t i = 0; i < TileX; i++) {
            *out++ = Converter::convert(in[i]);
        }
        in += tensor_row_pitch / sizeof(*in);
    }

    return out;
}

template<>
std::uint32_t* tensor_to_tile<32, 32, RawUInt32_RawUInt32, false>(const std::uint32_t*  NO_PTR_ALIAS in,
               std::uint32_t*  NO_PTR_ALIAS out,
               std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t, bool)
{
    
    PREFETCH(in);
    PREFETCH(in + 32);
    PREFETCH(in + 64);
    PREFETCH(in + 96);
    PREFETCH(in + 128);
    __m256i* vout = reinterpret_cast<__m256i*>(out);
    for(std::size_t j = 0; j < 32; j++){
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        PREFETCH(in + 5*32);
        _mm256_storeu_si256(vout + 0, simd_256_load(vin + 0));
        _mm256_storeu_si256(vout + 1, simd_256_load(vin + 1));
        _mm256_storeu_si256(vout + 2, simd_256_load(vin + 2));
        _mm256_storeu_si256(vout + 3, simd_256_load(vin + 3));
        vout += 4;
        in += 32;
    }
    return out;
}

template<>
std::uint16_t* tensor_to_tile<32, 32, RawUInt16_RawUInt16, false>(const std::uint16_t* NO_PTR_ALIAS in, 
              std::uint16_t* NO_PTR_ALIAS out,
              std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t, bool)
{

    PREFETCH(in);
    PREFETCH(in + 32);
    PREFETCH(in + 64);
    PREFETCH(in + 96);
    PREFETCH(in + 128);
    
    __m256i* vout = reinterpret_cast<__m256i*>(out);
    for(std::size_t j = 0; j < 32; j++){
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        PREFETCH(in + 5*32);
        _mm256_storeu_si256(vout + 0, simd_256_load((vin + 0)));
        _mm256_storeu_si256(vout + 1, simd_256_load((vin + 1)));
        vout += 2;
        in += 32;
    }
    return out;
}

template<>
std::uint8_t* tensor_to_tile<32, 32, RawUInt8_RawUInt8, false>(const std::uint8_t* NO_PTR_ALIAS in,
            std::uint8_t* NO_PTR_ALIAS out,
            std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t, bool)
{
    PREFETCH(in);
    PREFETCH(in + 32);
    PREFETCH(in + 64);
    PREFETCH(in + 96);
    PREFETCH(in + 128);

    __m256i* vout = reinterpret_cast<__m256i*>(out);
    for(std::size_t j = 0; j < 32; j++){
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        PREFETCH(in + 5*32);
        _mm256_storeu_si256(vout, simd_256_load(vin));
        vout++;
        in += 32;
    }
    return out;
}

template <>
float* tensor_to_tile<16, 16, Fp32_Fp32, false>(const float* NO_PTR_ALIAS in, float* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t quad_height, bool)
{
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    const auto prefetch_offset = 5 * row_stride;

    __m256i* vout = reinterpret_cast<__m256i*>(out);
    for (std::size_t j = 0; j < quad_height; j++) {
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);

        PREFETCH(in + prefetch_offset);

        _mm256_storeu_si256(vout+0, simd_256_load(vin+0));
        _mm256_storeu_si256(vout+1, simd_256_load(vin+1));

        vout += 2;
        in += row_stride;
    }

    return out + quad_height*16;
}

template<>

std::int8_t* tensor_to_tile<16, 16, Int8_Int8, false>(const std::int8_t* NO_PTR_ALIAS in,
              std::int8_t* NO_PTR_ALIAS out,
              std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t quad_height, bool)
{
    const __m256i sign_mask = _mm256_set1_epi8(0x80);
    const __m256i range_min = _mm256_set1_epi8(-127);
    const __m256i offset = _mm256_set1_epi8(0x01);
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    for(std::size_t j = 0; j < 8; j++) {
        __m256i input_data = _mm256_insertf128_si256(_mm256_castsi128_si256(_mm_loadu_si128((__m128i*)(in))), _mm_loadu_si128((__m128i*)(in + row_stride)), 1);
        input_data = _mm256_max_epi8(input_data, range_min); // Bottom saturate host side values, since sign | mag is smaller range than 2's complement
        __m256i is_negative = _mm256_and_si256(input_data, sign_mask);
        __m256i negative_input = ~input_data;
        negative_input = _mm256_or_si256(_mm256_add_epi8(negative_input, offset), is_negative);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), _mm256_blendv_epi8(input_data, negative_input, is_negative));
        out += 32; 
        in += 2 * row_stride;
    }
    return out;
}
template <>
std::uint16_t* tensor_to_tile<16, 16, Fp16b_Fp16b, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t quad_height, bool)
{
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;

    for (std::size_t j = 0; j < quad_height; j++) {
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        __m256i* vout = reinterpret_cast<__m256i*>(out);
        PREFETCH(in + prefetch_offset);

        _mm256_storeu_si256(vout, simd_256_load(vin));

        in += row_stride;
        out += 16;
    }

    return out;
}

template<>
float* tensor_to_tile<16, 16, Fp16b_Fp32, false>(const std::uint16_t* NO_PTR_ALIAS in, float* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t quad_height, bool){
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;

    __m256i* vout = reinterpret_cast<__m256i*>(out);
    for(std::size_t j = 0; j < quad_height; j++){
        const __m128i* vin = reinterpret_cast<const __m128i*>(in);
        PREFETCH(in + prefetch_offset);
        __m128i fp16b_in0 = _mm_loadu_si128(vin);
        __m128i fp16b_in1 = _mm_loadu_si128(vin + 1);
        _mm256_storeu_si256(vout, _mm256_sll_epi32(_mm256_cvtepu16_epi32(fp16b_in0), _mm_set_epi64x(0, 16)));
        _mm256_storeu_si256(vout + 1, _mm256_sll_epi32(_mm256_cvtepu16_epi32(fp16b_in1), _mm_set_epi64x(0, 16)));
        in += row_stride;
        vout += 2;
    }
    return out + quad_height*16;
}

template<>
float* tensor_to_tile<16, 16, Fp16_Fp32, false>(const std::uint16_t* NO_PTR_ALIAS in, float* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t quad_height, bool){
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    __m256i* vout = reinterpret_cast<__m256i*>(out);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;

    for(std::size_t j = 0; j < quad_height; j++){
        const __m128i* vin = reinterpret_cast<const __m128i*>(in);
        PREFETCH(in + prefetch_offset);
        
        for(int block = 0; block < 2; block++){
            __m256i fp16_in = _mm256_cvtepu16_epi32(_mm_loadu_si128(vin + block));
            __m256i sign = _mm256_sll_epi32(_mm256_and_si256(fp16_in, _mm256_set1_epi32(0x8000)), _mm_set_epi64x(0, 16));
            __m256i man = _mm256_sll_epi32(_mm256_and_si256(fp16_in, _mm256_set1_epi32(0x3ff)), _mm_set_epi64x(0, 13));
            __m256i exp = _mm256_srl_epi32(_mm256_and_si256(fp16_in, _mm256_set1_epi32(0x7c00)), _mm_set_epi64x(0, 10));

            __m256i cmp_mask = _mm256_cmpeq_epi32(exp, _mm256_setzero_si256());
            exp = _mm256_sll_epi32(_mm256_blendv_epi8(_mm256_add_epi32(exp, _mm256_set1_epi32(112)), _mm256_setzero_si256(), cmp_mask), _mm_set_epi64x(0, 23));
            _mm256_storeu_si256(vout + block, _mm256_or_si256(_mm256_or_si256(sign, exp), man));
        }
        in += row_stride;
        vout += 2;
    }
    return out + quad_height*16;
}

template <>
std::uint16_t* tensor_to_tile<16, 16, Fp16b_Fp16, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t quad_height, bool)
{
    const auto row_stride = tensor_row_pitch / sizeof(*in);
    const __m256i mantissa_mask = _mm256_set1_epi16(0x7f);
    const __m256i exp_mask = _mm256_set1_epi16(0x7f80);
    const __m256i sign_mask = _mm256_set1_epi16(0x8000);
    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;

    for(std::size_t j = 0; j < quad_height; j++){
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        __m256i* vout = reinterpret_cast<__m256i*>(out);
        PREFETCH(in + prefetch_offset);

        __m256i fp16b_in = simd_256_load(vin);
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
        _mm256_storeu_si256(vout, sign);
        out += 16;
        in += row_stride;
    }
    return out;
}

template<>
std::uint16_t* tensor_to_tile<16, 16, Fp16_Fp16b, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t quad_height, bool)
{
    const auto row_stride = tensor_row_pitch / sizeof(*in);
    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;

    for(std::size_t j = 0; j < quad_height; j++){
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        __m256i* vout = reinterpret_cast<__m256i*>(out);
        PREFETCH(in + prefetch_offset);
        __m256i fp16_in = simd_256_load(vin);
        __m256i exp = _mm256_srl_epi16(_mm256_and_si256(fp16_in, _mm256_set1_epi16(0x7c00)), _mm_set_epi64x(0, 10));
        exp = _mm256_add_epi16(exp, _mm256_set1_epi16(112));
        exp = _mm256_blendv_epi8(exp, _mm256_set1_epi16(0), _mm256_cmpeq_epi16(exp, _mm256_set1_epi16(112)));
        exp = _mm256_sll_epi16(exp, _mm_set_epi64x(0, 7));
        __m256i man = _mm256_srl_epi16(_mm256_and_si256(fp16_in, _mm256_set1_epi16(0x3ff)), _mm_set_epi64x(0, 3));
        __m256i sign = _mm256_and_si256(fp16_in, _mm256_set1_epi16(0x8000));
        _mm256_storeu_si256(vout, _mm256_or_si256(sign, _mm256_or_si256(exp, man)));
        
        out += 16;
        in += row_stride;
    }
    return out;
}

template<>
std::uint16_t* tensor_to_tile<16, 16, Fp32_Fp16, false>(const float* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t quad_height, bool)
{
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;

    const __m256i mantissa_mask = _mm256_set1_epi32(0x007fffff);
    const __m256i exp_mask = _mm256_set1_epi32(0x7F800000);
    const __m256i sign_mask = _mm256_set1_epi32(0x80000000);

    const __m256i shuffle_order = _mm256_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    const __m256i permute_order = _mm256_setr_epi32(0, 1, 4, 5, 2, 33, 6, 7);

    for(std::size_t j = 0; j < quad_height; j++){
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        __m128i* vout = reinterpret_cast<__m128i*>(out);
        PREFETCH(in + prefetch_offset);

        for(int block = 0; block < 2; block++){
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
        out += 16;
        in += row_stride;
    }
    return out;
}

template <>
std::uint16_t* tensor_to_tile<16, 16, Fp32_Fp16b, false>(const float* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t quad_height, bool)
{
    // VPSHUFB independently shuffles bytes in the top & bottom 128-bit halves of an AVX value.
    // We use it to collect the high 16 bits of each 32-bit float into the low 64-bits of each half.
    // i.e. from ABCDEFGHIJKLMNOPQRSTUVWXYZ123456 to 00000000CDGHKLOP00000000STWX1256
    // Then split the top half using VEXTRACTI128 and merge using VPUNPCKLQDQ.

    const __m256i shuffle_mask = _mm256_setr_epi8(2, 3, 6, 7, 10, 11, 14, 15,
                                                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                  2, 3, 6, 7, 10, 11, 14, 15,
                                                  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    const auto prefetch_offset = 4 * row_stride;

    for (std::size_t j = 0; j < quad_height; j++) {
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        __m128i* vout = reinterpret_cast<__m128i*>(out);
 
        PREFETCH(in + prefetch_offset);
        __m256i fp32_in = simd_256_load(vin+0);
        __m256i post_shuffle = _mm256_shuffle_epi8(fp32_in, shuffle_mask);
        __m128i post_shuffle_high = _mm256_extracti128_si256(post_shuffle, 1);
        __m256i packed = _mm256_unpacklo_epi64(post_shuffle, _mm256_castsi128_si256(post_shuffle_high));
        _mm_store_si128(vout+0, _mm256_castsi256_si128(packed));

        fp32_in = simd_256_load(vin+1);
        post_shuffle = _mm256_shuffle_epi8(fp32_in, shuffle_mask);
        post_shuffle_high = _mm256_extracti128_si256(post_shuffle, 1);
        packed = _mm256_unpacklo_epi64(post_shuffle, _mm256_castsi128_si256(post_shuffle_high));
        _mm_store_si128(vout+1, _mm256_castsi256_si128(packed));
        in += row_stride;
    
        out += 16;
        
    }

    return out;
}

template <>
std::uint8_t* tensor_to_tile<16, 16, Fp32_Bfp8_all, false>(const float* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void** out_exp, bool conversion_to_a_format, int, uint32_t, uint32_t quad_height, bool truncate_bfp_mantissa)
{
    // AS: This converter can be used in 2 modes. If conversion_to_a_format = true, this function is a Fp32 -> Bfp8 converter. By default its an Fp32 -> Bfp8b converter.
    // This reduces code reuse, as both conversions are very similar.

    // Since input is fp32 and the registers we can use are 256b, we have to repeat exponent, mantissa, and sign extraction twice for each row
    // FP32 -> BFP8B requires the following steps:
    // 1. Extract the exponent bits {bits[30:23]} of all values in a single row and find the maximum value among those
    // 2. Extract the mantissa bits and add an explicit 1 as MSB and shift by 1 again to reserve MSB of each mantissa for the sign bit: {0-1-bits[22:17]}
    //    If (sign-exponent) == 0, we don't add the explicit one.
    // 3. Shift to right, each mantissa values by the diff of max-exponent and the corresponding exponent
    // 4. Extract the sign bits (MSB of each fp32 value) and append that to each corresponding shifted mantissa values

    // FP32 -> BFP8 requires the following steps:
    // 1. Extract the exponent bits {bits[30:23]} of all values in a single row
    // 2. Rebias and saturate the exponents and mantissa values for each datum
    // 3. Find the maximum value among the rebiased exponents
    // 4. Extract the mantissa bits and add an explicit 1 as MSB and shift by 1 again to reserve MSB of each mantissa for the sign bit: {0-1-bits[22:17]}
    //    If (sign-exponent) == 0, we don't add the explicit one.
    // 5. Shift to right, each mantissa values by the diff of max-exponent and the corresponding exponent
    // 6. Extract the sign bits (MSB of each fp32 value) and append that to each corresponding shifted mantissa values
    //std::cout << "pushing data "

    const __m256i exponent_shift_offset_0 = _mm256_setr_epi32(4, 5, 6, 7, 0, 1, 2, 3);
    const __m256i exponent_shift_offset_1 = _mm256_setr_epi32(2, 3, 0, 1, 6, 7, 4, 5);
    const __m256i exponent_shift_offset_2 = _mm256_setr_epi32(1, 0, 3, 2, 5, 4, 7, 6);
    const __m256i zero_compare            = _mm256_setr_epi32(0, 0, 0, 0, 0, 0, 0, 0);

    const __m256i explicit_mantissa_one_mask = _mm256_setr_epi32(0x800000, 0x800000, 0x800000, 0x800000,
                                                    0x800000, 0x800000, 0x800000, 0x800000);


    const __m256i sign_bit_mask =               _mm256_setr_epi32(0x80000000, 0x80000000, 0x80000000, 0x80000000,
                                                    0x80000000, 0x80000000, 0x80000000, 0x80000000);

    
    const __m256i mantissa_mask =               _mm256_setr_epi32(0x007F0000, 0x007F0000, 0x007F0000, 0x007F0000,
                                                    0x007F0000, 0x007F0000, 0x007F0000, 0x007F0000);
    
    const __m256i lower_8b_mask =               _mm256_setr_epi32(0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF,
                                                    0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF);

    const __m256i shuffle_mask_mantissa =       _mm256_setr_epi8(0, 4, 8, 12, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0, 4, 8, 12, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    const auto prefetch_offset = 4 * row_stride;

    for (std::size_t j = 0; j < quad_height; j++) {
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        __m128i* vout = reinterpret_cast<__m128i*>(out);
        PREFETCH(in + prefetch_offset);

        __m256i fp32_in_0 = simd_256_load(vin);
        __m256i fp32_in_1 = simd_256_load(vin+1);

        /////////////////////////////////////////////////////////
        // Extract sign bits - First 8 float values or row
        /////////////////////////////////////////////////////////

        __m256i sign_bits = _mm256_and_si256(fp32_in_0, sign_bit_mask);
        // 8, 8-bit values each {sign-0000000}
        // Shift all 32bit values by 8, such that sign bit will be placed at bit[7]. We will do bitwise `or` on this and mantissa later
        __m256i sign_shifted_0 = _mm256_srli_epi32(sign_bits, 24);

        /////////////////////////////////////////////////////////
        // Extract sign bits - second 8 float values or row
        /////////////////////////////////////////////////////////

        sign_bits = _mm256_and_si256(fp32_in_1, sign_bit_mask);
        // 8, 8-bit values each {sign-0000000}
        // Shift all 32bit values by 8, such that sign bit will be placed at bit[7]. We will do bitwise `or` on this and mantissa later
        __m256i sign_shifted_1 = _mm256_srli_epi32(sign_bits, 24);

        /////////////////////////////////////////////////////////
        // Extract Exponent bits - First 8 float values or row
        /////////////////////////////////////////////////////////

        __m256i fp32_in_shifted_exponent_0 = _mm256_srli_epi32(fp32_in_0, 23);

        /////////////////////////////////////////////////////////
        // Extract Exponent bits - Second 8 float values or row
        /////////////////////////////////////////////////////////

        // Shift every 32bit float to left by 1 so that we can extract exponents by taking the upper 8-bits
        __m256i fp32_in_shifted_exponent_1 = _mm256_srli_epi32(fp32_in_1, 23);

        //////////////////////////////////////////////////////////////////////////////////
        // Rebiasing the exponent and saturating the exponent and mantissa (only for conversions from Fp32 -> Bfp8)
        //////////////////////////////////////////////////////////////////////////////////

        // rebias exponent for Bfp8_a formats
        
        if(conversion_to_a_format){
            fp32_in_shifted_exponent_0 = _mm256_sub_epi32(_mm256_and_si256(fp32_in_shifted_exponent_0, _mm256_set1_epi32(0xff)), _mm256_set1_epi32(112));
            fp32_in_shifted_exponent_1 = _mm256_sub_epi32(_mm256_and_si256(fp32_in_shifted_exponent_1, _mm256_set1_epi32(0xff)), _mm256_set1_epi32(112));

            // saturate exp and mantissa for Bfp8_a formats
            __m256i cmp_mask = _mm256_cmpgt_epi32(fp32_in_shifted_exponent_0, _mm256_set1_epi32(31));

            fp32_in_shifted_exponent_0 = _mm256_blendv_epi8(fp32_in_shifted_exponent_0, _mm256_set1_epi32(31), cmp_mask);
            fp32_in_0 = _mm256_blendv_epi8(fp32_in_0, _mm256_or_si256(fp32_in_0, _mm256_set1_epi32(0x007fffff)), cmp_mask);
            cmp_mask = _mm256_or_si256(_mm256_cmpgt_epi32(fp32_in_shifted_exponent_0, _mm256_setzero_si256()), _mm256_cmpeq_epi32(fp32_in_shifted_exponent_0, _mm256_setzero_si256()));
            fp32_in_shifted_exponent_0 = _mm256_blendv_epi8(_mm256_setzero_si256(), fp32_in_shifted_exponent_0, cmp_mask);
            fp32_in_0 = _mm256_blendv_epi8(_mm256_and_si256(fp32_in_0, _mm256_set1_epi32(0xFF800000)), fp32_in_0, cmp_mask);


            cmp_mask = _mm256_cmpgt_epi32(fp32_in_shifted_exponent_1, _mm256_set1_epi32(31));
            fp32_in_shifted_exponent_1 = _mm256_blendv_epi8(fp32_in_shifted_exponent_1, _mm256_set1_epi32(31), cmp_mask); //if(exp > 31) exp = 31
            fp32_in_1 = _mm256_blendv_epi8(fp32_in_1, _mm256_or_si256(fp32_in_1, _mm256_set1_epi32(0x007fffff)), cmp_mask); // if(exp > 31) man = 0x7fffff
            cmp_mask = _mm256_or_si256(_mm256_cmpgt_epi32(fp32_in_shifted_exponent_1, _mm256_setzero_si256()), _mm256_cmpeq_epi32(fp32_in_shifted_exponent_1, _mm256_setzero_si256())); 
            fp32_in_shifted_exponent_1 = _mm256_blendv_epi8(_mm256_setzero_si256(), fp32_in_shifted_exponent_1, cmp_mask); // if(exp < 0) exp = 0
            fp32_in_1 = _mm256_blendv_epi8(_mm256_and_si256(fp32_in_1, _mm256_set1_epi32(0xFF800000)), fp32_in_1, cmp_mask); //if (exp < 0) man = 0
        }

        /////////////////////////////////////////////////////////
        // Reduction tree for finding the maximum exponent
        /////////////////////////////////////////////////////////

        // A reduction tree for finding the max value between all the exponents
        // Take the max between first 8 exponents and the second set of 8 exponents
        // Results: 0005000600070008000E000F000G000H
        __m256i exponent_max = _mm256_max_epu8(fp32_in_shifted_exponent_0, fp32_in_shifted_exponent_1);
        // Swap the lower 32 bits of the two 128-bit halves
        // We don't need the second 128-bit half after the next max
        // Results: 000E000F000G000H0005000600070008
        __m256i exponent_shift = _mm256_permutevar8x32_epi32(exponent_max, exponent_shift_offset_0);
        // Take the max again
        // Results: 000E000F000G000H000E000F000G000H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // Shuffle such that first 32 bits are shifted by 16
        // Results: 000G000H000E000F000G000H000E000F
        exponent_shift = _mm256_permutevar8x32_epi32(exponent_max, exponent_shift_offset_1);
        // Take the max again
        // Results: 000G000H000G000H000G000H000G000H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // Shuffle such that first 16 buts are shifted by 8
        // Results: 000H000G000H000G000H000G000H000G
        exponent_shift = _mm256_permutevar8x32_epi32(exponent_max, exponent_shift_offset_2);
        // Take max one last time:
        // Results: 000H000H000H000H000H000H000H000H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // Broadcast the first 8-bits over all 256bits:
        // Results: HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH
        __m256i max_exponent_broadcast = _mm256_broadcastb_epi8(_mm256_castsi256_si128(exponent_max));

        /////////////////////////////////////////////////////////
        // Subtract each exponent from maximum exponent
        /////////////////////////////////////////////////////////

        // Subtract max exponent from all exponents and mask the lower 8 bits
        // HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH - 0005000600070008000A000B000C000D
        __m256i exponent_diff_0 = _mm256_sub_epi8(max_exponent_broadcast, fp32_in_shifted_exponent_0);
        exponent_diff_0 = _mm256_and_si256(exponent_diff_0, lower_8b_mask);
        // HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH - 0001000200030004000E000F000G000H
        __m256i exponent_diff_1 = _mm256_sub_epi8(max_exponent_broadcast, fp32_in_shifted_exponent_1);
        exponent_diff_1 = _mm256_and_si256(exponent_diff_1, lower_8b_mask);

        /////////////////////////////////////////////////////////
        // Extract mantissa bits
        /////////////////////////////////////////////////////////

        __m256i fp16_in_exp_man_mask = _mm256_andnot_si256(sign_bit_mask, fp32_in_0);
        fp32_in_0 = _mm256_and_si256(fp32_in_0, mantissa_mask);
        __m256i check_zero_0 = _mm256_cmpeq_epi32(fp16_in_exp_man_mask, zero_compare);
        // bit-wise AND check_zero with explicit_mantissa_one_mask. In this case, if num == 0 --> do not add the explicit one
        __m256i explicit_mantissa_one_mask_with_zero_check_0 = _mm256_andnot_si256(check_zero_0, explicit_mantissa_one_mask);
        // Always set bit[23] in each fp32[31:0] number such that when bits[23:16] are extracted for mantissa we will have {1, [22:16]}
        __m256i fp32_in_with_explicit_mantissa_one_0 = _mm256_or_si256(fp32_in_0, explicit_mantissa_one_mask_with_zero_check_0);

        fp16_in_exp_man_mask = _mm256_andnot_si256(sign_bit_mask, fp32_in_1);
        fp32_in_1 = _mm256_and_si256(fp32_in_1, mantissa_mask);
        __m256i check_zero_1 = _mm256_cmpeq_epi32(fp16_in_exp_man_mask, zero_compare);
        // bit-wise NOT of check_zero will be: (exp == zero)? 0: 0xFF
        // bit-wise AND check_zero with explicit_mantissa_one_mask. In this case, if exp == 0 --> do not add the explicit one
        __m256i explicit_mantissa_one_mask_with_zero_check_1 = _mm256_andnot_si256(check_zero_1, explicit_mantissa_one_mask);
        // Always set bit[23] in each fp32[31:0] number such that when bits[23:16] are extracted for mantissa we will have {1, [22:16]}
        __m256i fp32_in_with_explicit_mantissa_one_1 = _mm256_or_si256(fp32_in_1, explicit_mantissa_one_mask_with_zero_check_1);

        /////////////////////////////////////////////////////////
        // Shift each mantissa by the exponent diff
        /////////////////////////////////////////////////////////

        // Shift each extended 32b mantissa by the corresponding extended 32b mantissa diff
        __m256i mantissa_shifted_0 = _mm256_srlv_epi32(fp32_in_with_explicit_mantissa_one_0, exponent_diff_0);
        __m256i mantissa_shifted_1 = _mm256_srlv_epi32(fp32_in_with_explicit_mantissa_one_1, exponent_diff_1);

        /////////////////////////////////////////////////////////
        // Add sign bit to each mantissa
        /////////////////////////////////////////////////////////
        if(truncate_bfp_mantissa) {
            mantissa_shifted_0 = _mm256_srli_epi32(mantissa_shifted_0, 17);
            mantissa_shifted_1 = _mm256_srli_epi32(mantissa_shifted_1, 17);
        }
        else {
            mantissa_shifted_0 = _mm256_add_epi32(mantissa_shifted_0, _mm256_slli_epi32(_mm256_set1_epi32(1), 16));
            mantissa_shifted_0 = _mm256_srli_epi32(mantissa_shifted_0, 17);
            mantissa_shifted_0 = _mm256_min_epi32(mantissa_shifted_0, _mm256_set1_epi32(127));

            mantissa_shifted_1 = _mm256_add_epi32(mantissa_shifted_1, _mm256_slli_epi32(_mm256_set1_epi32(1), 16));
            mantissa_shifted_1 = _mm256_srli_epi32(mantissa_shifted_1, 17);
            mantissa_shifted_1 = _mm256_min_epi32(mantissa_shifted_1, _mm256_set1_epi32(127));
        }
        check_zero_0 = _mm256_cmpeq_epi32(mantissa_shifted_0, zero_compare);
        sign_shifted_0 = _mm256_andnot_si256(check_zero_0, sign_shifted_0);
        mantissa_shifted_0 = _mm256_or_si256(mantissa_shifted_0, sign_shifted_0);

        
        check_zero_1 = _mm256_cmpeq_epi32(mantissa_shifted_1, zero_compare);
        sign_shifted_1 = _mm256_andnot_si256(check_zero_1, sign_shifted_1);
        mantissa_shifted_1 = _mm256_or_si256(mantissa_shifted_1, sign_shifted_1);

        /////////////////////////////////////////////////////////
        // Pack mantissa bits for all values and store
        /////////////////////////////////////////////////////////

        // Take the 3rd hightst 8-bits of each 32bit values with fused one, into the lowest 32-bits of each 128-bit half
        __m256i post_shuffle_mantissa = _mm256_shuffle_epi8(mantissa_shifted_0, shuffle_mask_mantissa);
        __m128i post_shuffle_mantissa_high = _mm256_extracti128_si256(post_shuffle_mantissa, 1);
        // The 256bit result: 000000000000000000000000XYZWQRST
        __m256i packed_mantissa_0 = _mm256_unpacklo_epi32(post_shuffle_mantissa, _mm256_castsi128_si256(post_shuffle_mantissa_high));

        // Take the 3rd hightst 8-bits of each 32bit values with fused one, into the lowest 32-bits of each 128-bit half
        post_shuffle_mantissa = _mm256_shuffle_epi8(mantissa_shifted_1, shuffle_mask_mantissa);
        post_shuffle_mantissa_high = _mm256_extracti128_si256(post_shuffle_mantissa, 1);
        // The 256bit result: 000000000000000000000000XYZWQRST
        __m256i packed_mantissa_1 = _mm256_unpacklo_epi32(post_shuffle_mantissa, _mm256_castsi128_si256(post_shuffle_mantissa_high));

        __m256i packed_mantissa = _mm256_unpacklo_epi64(packed_mantissa_0, packed_mantissa_1);
        _mm_store_si128(vout, _mm256_castsi256_si128(packed_mantissa));
        (*(uint8_t**)out_exp)[j] = _mm256_extract_epi8(exponent_max, 0);
    
        out += 16;
        in += row_stride;
    }

    *(uint8_t**)out_exp += quad_height;
    return out;
}

template <>
std::uint8_t* tensor_to_tile<16, 16, Fp16b_Bfp8_all, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void** out_exp, bool conversion_to_a_format, int, uint32_t, uint32_t quad_height, bool truncate_bfp_mantissa)
{
    // AS: This converter can be used in 2 modes. If conversion_to_a_format = true, this function is a Fp16b -> Bfp8 converter. By default its an Fp16b -> Bfp8b converter.
    // This reduces code reuse, as both conversions are very similar.

    // FP16B -> BFP8B requires the following steps:
    // 1. Extract the exponent bits {bits[14:7]} of all values in a single row and find the maximum value among those
    // 2. Extract the mantissa bits and add an explicit 1 as MSB and shift by 1 again to reserve MSB of each mantissa for the sign bit: {0-1-bits[5:0]}
    //    If exponent == 0, we don't add the explicit one.
    // 3. Shift to right, each mantissa values by the diff of max-exponent and the corresponding exponent
    // 4. Extract the sign bits (MSB of each fp16b value) and append that to each corresponding shifted mantissa values

    // FP16B -> BFP8 requires the following steps:
    // 1. Extract the exponent bits {bits[14:7]} of all values in a single row 
    // 2. Rebias and saturate the exponents and mantissa values for each datum
    // 3. Find the maximum value among the rebiased exponents
    // 4. Extract the mantissa bits and add an explicit 1 as MSB and shift by 1 again to reserve MSB of each mantissa for the sign bit: {0-1-bits[5:0]}
    //    If exponent == 0, we don't add the explicit one.
    // 5. Shift to right, each mantissa values by the diff of max-exponent and the corresponding exponent
    // 6. Extract the sign bits (MSB of each fp16b value) and append that to each corresponding shifted mantissa values

    const __m256i exponent_shift_offset_0 = _mm256_setr_epi32(4, 5, 6, 7, 0, 1, 2, 3);
    const __m256i exponent_shift_offset_1 = _mm256_setr_epi32(2, 3, 0, 1, 6, 7, 4, 5);
    const __m256i exponent_shift_offset_2 = _mm256_setr_epi32(1, 0, 3, 2, 5, 4, 7, 6);
    const __m256i zero_compare            = _mm256_setr_epi32(0, 0, 0, 0, 0, 0, 0, 0);
    const __m256i exponent_shift_offset_3 = _mm256_setr_epi8(2, 0xFF, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    const __m256i explicit_mantissa_one_mask = _mm256_setr_epi16(0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080,
                                                    0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080);

    const __m256i sign_bit_mask =               _mm256_setr_epi16(0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
                                                    0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000);
    
    const __m256i lower_8b_mask =               _mm256_setr_epi16(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    const __m256i mantissa_mask =               _mm256_setr_epi16(0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
                                                    0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F);

    const __m256i mantissa_repack =             _mm256_setr_epi8(0, 4, 8, 12, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0, 4, 8, 12, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    
    const __m256i sign_pack =                   _mm256_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    1, 3, 5, 7, 9, 11, 13, 15,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;

    for (std::size_t j = 0; j < quad_height; j++) {
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        __m128i* vout = reinterpret_cast<__m128i*>(out);

        PREFETCH(in + prefetch_offset);
        __m256i fp16_in = simd_256_load(vin);
        /////////////////////////////////////////////////////////
        // Extract sign bits - First 8 float values or row
        /////////////////////////////////////////////////////////

        __m256i sign_bits = _mm256_and_si256(fp16_in, sign_bit_mask);
        // 8, 8-bit values each {sign-0000000}
        // Shift all 16bit values by 8, such that sign bit will be placed at bit[7]. We will do bitwise `or` on this and mantissa later
        // Pack each value as a single byte
        __m256i sign_packed = _mm256_shuffle_epi8(sign_bits, sign_pack);
        __m128i sign_packed_high = _mm256_extracti128_si256(sign_packed, 1);
        sign_packed = _mm256_unpacklo_epi64(sign_packed, _mm256_castsi128_si256(sign_packed_high));

        /////////////////////////////////////////////////////////
        // Extract Exponent bits - First 8 float values or row
        /////////////////////////////////////////////////////////

        __m256i fp16_in_shifted_exponent = _mm256_srli_epi16(fp16_in, 7);

        //////////////////////////////////////////////////////////////////////////////////
        // Rebiasing the exponent and saturating the exponent and mantissa (only for conversions from Fp16b -> Bfp8)
        //////////////////////////////////////////////////////////////////////////////////

        // rebias the exp and saturate the exp + mantissa (need this for Bfp8_a formats)
        if(conversion_to_a_format){
            //fp16_in_shifted_exponent = ;
            fp16_in_shifted_exponent = _mm256_sub_epi16(_mm256_and_si256(fp16_in_shifted_exponent, _mm256_set1_epi16(0xff)), _mm256_set1_epi16(112)); //rebias exp
            __m256i cmp_mask = _mm256_cmpgt_epi16(fp16_in_shifted_exponent, _mm256_set1_epi16(31));
            fp16_in_shifted_exponent = _mm256_blendv_epi8(fp16_in_shifted_exponent, _mm256_set1_epi16(31), cmp_mask); //if(exp > 31) exp = 31
            fp16_in = _mm256_blendv_epi8(fp16_in, _mm256_or_si256(fp16_in, _mm256_set1_epi16(0x7f)), cmp_mask); // if(exp > 31) man = 0x7f

            cmp_mask = _mm256_or_si256(_mm256_cmpgt_epi16(fp16_in_shifted_exponent, _mm256_setzero_si256()), _mm256_cmpeq_epi16(fp16_in_shifted_exponent, _mm256_setzero_si256()));
            fp16_in_shifted_exponent = _mm256_blendv_epi8(_mm256_setzero_si256(), fp16_in_shifted_exponent, cmp_mask); // if(exp < 0) exp = 0
            fp16_in = _mm256_blendv_epi8(_mm256_and_si256(fp16_in, _mm256_set1_epi16(0xFF80)), fp16_in, cmp_mask); // if(exp < 0) man = 0
        }
        /////////////////////////////////////////////////////////
        // Reduction tree for finding the maximum exponent
        /////////////////////////////////////////////////////////

        // A reduction tree for finding the max value between all the exponents

        // Swap the top and bottom 128bit halves
        // initial: 0A0B0C0D0E0F0G0H0102030405060708
        // Results: 01020304050607080A0B0C0D0E0F0G0H
        __m256i exponent_shift = _mm256_permutevar8x32_epi32(fp16_in_shifted_exponent, exponent_shift_offset_0);
        // Take the max again
        // Results: 0A0B0C0D0E0F0G0H0A0B0C0D0E0F0G0H
        __m256i exponent_max = _mm256_max_epu8(fp16_in_shifted_exponent, exponent_shift);
        // In each 128bit halves, swap top and bottom 64b
        // Results: 0E0F0G0H0A0B0C0D0E0F0G0H0A0B0C0D
        exponent_shift = _mm256_permutevar8x32_epi32(exponent_max, exponent_shift_offset_1);
        // Take the max again
        // Results: 0E0F0G0H0E0F0G0H0E0F0G0H0E0F0G0H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // In each 64bits, swap the top and bottom 32b
        // Results: 0G0H0E0F0G0H0E0F0G0H0E0F0G0H0E0F
        exponent_shift = _mm256_permutevar8x32_epi32(exponent_max, exponent_shift_offset_2);
        // Take max again:
        // Results: 0G0H0G0H0G0H0G0H0G0H0G0H0G0H0G0H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // swap the lower 8b of first and second lowest 16bits
        // Results: 00000000000000000000000000000H0G
        exponent_shift = _mm256_shuffle_epi8(exponent_max, exponent_shift_offset_3);
        // Take max again:
        // Results: 0G0H0G0H0G0H0G0H0G0H0G0H0G0H0H0H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // Broadcast the first 8-bits over all 256bits:
        // Results: HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH
        __m256i max_exponent_broadcast = _mm256_broadcastb_epi8(_mm256_castsi256_si128(exponent_max));

        /////////////////////////////////////////////////////////
        // Subtract each exponent from maximum exponent
        /////////////////////////////////////////////////////////

        // Subtract max exponent from all exponents and mask the lower 8 bits
        // HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH - 0A0B0C0D0E0F0G0H0102030405060708
        __m256i exponent_diff = _mm256_sub_epi8(max_exponent_broadcast, fp16_in_shifted_exponent);
        exponent_diff = _mm256_and_si256(exponent_diff, lower_8b_mask);

        /////////////////////////////////////////////////////////
        // Extract mantissa bits
        /////////////////////////////////////////////////////////

        __m256i fp16_in_exp_man_mask = _mm256_andnot_si256(sign_bit_mask, fp16_in);
        fp16_in = _mm256_and_si256(fp16_in, mantissa_mask);
        // Compare each input value (excluding sign) with zero, (num == zero)? 0xFFFF: 0
        __m256i check_zero = _mm256_cmpeq_epi16(fp16_in_exp_man_mask, zero_compare);
        // bit-wise NOT of check_zero will be: (num == zero)? 0: 0xFFFF
        // bit-wise AND check_zero with explicit_mantissa_one_mask. In this case, if exp == 0 --> do not add the explicit one
        __m256i explicit_mantissa_one_mask_with_zero_check = _mm256_andnot_si256(check_zero, explicit_mantissa_one_mask);
        // Always set bit[7] in each fp16[15:0] number such that when bits[6:0] are extracted for mantissa we will have {1, [6:0]}
        __m256i fp16_in_with_explicit_mantissa_one = _mm256_or_si256(fp16_in, explicit_mantissa_one_mask_with_zero_check);
        // Shift each mantissa bit by 1 to reserve the MSB of each lower 8bits for sign {0-1, [5:0]}
        fp16_in_with_explicit_mantissa_one = _mm256_srli_epi16(fp16_in_with_explicit_mantissa_one, (int) ((truncate_bfp_mantissa) * 1));

        /////////////////////////////////////////////////////////
        // Shift each mantissa by the exponent diff
        /////////////////////////////////////////////////////////

        __m128i mantissa_split_high = _mm256_extracti128_si256(fp16_in_with_explicit_mantissa_one, 1);
        __m256i mantissa_split_low_extend = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(fp16_in_with_explicit_mantissa_one));
        __m256i mantissa_split_high_extend = _mm256_cvtepu16_epi32(mantissa_split_high);

        __m128i exponent_split_high = _mm256_extracti128_si256(exponent_diff, 1);
        __m256i exponent_split_low_extend = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(exponent_diff));
        __m256i exponent_split_high_extend = _mm256_cvtepu16_epi32(exponent_split_high);

        // Shift each extended 32b mantissa by the corresponding extended 32b exponent diff
        __m256i mantissa_shifted_low = _mm256_srlv_epi32(mantissa_split_low_extend, exponent_split_low_extend);
        __m256i mantissa_shifted_high = _mm256_srlv_epi32(mantissa_split_high_extend, exponent_split_high_extend);
        if(!truncate_bfp_mantissa) {
            mantissa_shifted_low = _mm256_add_epi32(mantissa_shifted_low, _mm256_set1_epi32(1));
            mantissa_shifted_low = _mm256_srli_epi32(mantissa_shifted_low, 1);
            mantissa_shifted_low = _mm256_min_epi32(mantissa_shifted_low, _mm256_set1_epi32(127));

            mantissa_shifted_high = _mm256_add_epi32(mantissa_shifted_high, _mm256_set1_epi32(1));
            mantissa_shifted_high = _mm256_srli_epi32(mantissa_shifted_high, 1);
            mantissa_shifted_high = _mm256_min_epi32(mantissa_shifted_high, _mm256_set1_epi32(127));
        }

        __m256i mantissa_shifted_low_16b = _mm256_shuffle_epi8(mantissa_shifted_low, mantissa_repack);
        __m128i mantissa_shifted_low_16b_top = _mm256_extracti128_si256(mantissa_shifted_low_16b, 1);
        __m256i mantissa_shifted_low_16b_packed = _mm256_unpacklo_epi32(mantissa_shifted_low_16b, _mm256_castsi128_si256(mantissa_shifted_low_16b_top));
        
        __m256i mantissa_shifted_high_16b = _mm256_shuffle_epi8(mantissa_shifted_high, mantissa_repack);
        __m128i mantissa_shifted_high_16b_top = _mm256_extracti128_si256(mantissa_shifted_high_16b, 1);
        __m256i mantissa_shifted_high_16b_packed = _mm256_unpacklo_epi32(mantissa_shifted_high_16b, _mm256_castsi128_si256(mantissa_shifted_high_16b_top));

        __m256i mantissa_shifted = _mm256_unpacklo_epi64(mantissa_shifted_low_16b_packed, mantissa_shifted_high_16b_packed);

        /////////////////////////////////////////////////////////
        // Add sign bit to each mantissa
        /////////////////////////////////////////////////////////

        check_zero = _mm256_cmpeq_epi8(mantissa_shifted, zero_compare);
        sign_packed = _mm256_andnot_si256(check_zero, sign_packed);
        mantissa_shifted = _mm256_or_si256(mantissa_shifted, sign_packed);

        /////////////////////////////////////////////////////////
        // Pack mantissa bits for all values and store
        /////////////////////////////////////////////////////////
        _mm_store_si128(vout, _mm256_castsi256_si128(mantissa_shifted));
        (*(uint8_t**)out_exp)[j] = _mm256_extract_epi8(exponent_max, 0);
    
        out += 16;
        in += row_stride;
    }
    *(uint8_t**)out_exp += quad_height;

    return out;
}

template<>
std::uint8_t* tensor_to_tile<16, 16, Int8_Bfp8, false>(const std::uint8_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void**, bool, int, uint32_t, uint32_t, bool) {
     // Reverts 2's complement representation to get negative numbers in floating point mantissa form and tilizes data
    const __m256i sign_mask = _mm256_set1_epi8(0x80);
    const __m256i offset = _mm256_set1_epi8(0x01);
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;


    for (std::size_t j = 0; j < 8; j++) {
        PREFETCH(in + prefetch_offset);
        PREFETCH(in + prefetch_offset + row_stride);
        __m256i input_data = _mm256_insertf128_si256(_mm256_castsi128_si256(simd_128_load((__m128i*)(in))), simd_128_load((__m128i*)(in + row_stride)), 1);

        __m256i is_negative = _mm256_and_si256(input_data, sign_mask);
        __m256i negative_input = ~input_data;
        
        negative_input = _mm256_or_si256(_mm256_add_epi8(negative_input, offset), is_negative);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(out), _mm256_blendv_epi8(input_data, negative_input, is_negative));
        out += 32; 
        in += 2 * row_stride;
    }
    return out;
}

template <>
std::uint8_t* tensor_to_tile<16, 16, Fp16_Bfp8, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void** out_exp, bool, int, uint32_t, uint32_t quad_height, bool truncate_bfp_mantissa)
{
    // FP16 -> BFP8 requires the following step:
    // 1. Extract the exponent bits {bits[14:10]} of all values in a single row and find the maximum value among those
    // 2. Extract the mantissa bits and add an explicit 1 as MSB and shift by 1 again to reserve MSB of each mantissa for the sign bit: {0-1-bits[9:4]}
    //    If exponent == 0, we don't add the explicit one.
    // 3. Shift to right, each mantissa values by the diff of max-exponent and the corresponding exponent
    // 4. Extract the sign bits (MSB of each fp16b value) and append that to each corresponding shifted mantissa values

    const __m256i exponent_shift_offset_0 = _mm256_setr_epi32(4, 5, 6, 7, 0, 1, 2, 3);
    const __m256i exponent_shift_offset_1 = _mm256_setr_epi32(2, 3, 0, 1, 6, 7, 4, 5);
    const __m256i exponent_shift_offset_2 = _mm256_setr_epi32(1, 0, 3, 2, 5, 4, 7, 6);
    const __m256i zero_compare            = _mm256_setr_epi32(0, 0, 0, 0, 0, 0, 0, 0);
    const __m256i exponent_shift_offset_3 = _mm256_setr_epi8(2, 0xFF, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    
    const __m256i exponent_mask =               _mm256_setr_epi8(0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
                                                            0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
                                                            0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F,
                                                            0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F);
    const __m256i mantissa_mask =               _mm256_setr_epi16(0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF,
                                                            0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF, 0x3FF);

    uint16_t  explicit_mantissa_one_mask_datum = truncate_bfp_mantissa ? 0x0040 : 0x0080;
    const __m256i explicit_mantissa_one_mask = _mm256_setr_epi16(explicit_mantissa_one_mask_datum, explicit_mantissa_one_mask_datum, 
                                                                explicit_mantissa_one_mask_datum, explicit_mantissa_one_mask_datum, 
                                                                explicit_mantissa_one_mask_datum, explicit_mantissa_one_mask_datum,
                                                                explicit_mantissa_one_mask_datum, explicit_mantissa_one_mask_datum,
                                                                explicit_mantissa_one_mask_datum, explicit_mantissa_one_mask_datum, 
                                                                explicit_mantissa_one_mask_datum, explicit_mantissa_one_mask_datum, 
                                                                explicit_mantissa_one_mask_datum, explicit_mantissa_one_mask_datum, 
                                                                explicit_mantissa_one_mask_datum, explicit_mantissa_one_mask_datum);

    const __m256i sign_bit_mask =               _mm256_setr_epi16(0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
                                                    0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000);
    
    const __m256i lower_8b_mask =               _mm256_setr_epi16(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    const __m256i mantissa_repack =             _mm256_setr_epi8(0, 4, 8, 12, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0, 4, 8, 12, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    
    const __m256i sign_pack =                   _mm256_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    1, 3, 5, 7, 9, 11, 13, 15,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;

    for (std::size_t j = 0; j < quad_height; j++) {
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        __m128i* vout = reinterpret_cast<__m128i*>(out);

        PREFETCH(in + prefetch_offset);

        __m256i fp16_in = simd_256_load(vin);

        /////////////////////////////////////////////////////////
        // Extract sign bits - First 8 float values or row
        /////////////////////////////////////////////////////////

        __m256i sign_bits = _mm256_and_si256(fp16_in, sign_bit_mask);
        // 8, 8-bit values each {sign-0000000}
        // Shift all 16bit values by 8, such that sign bit will be placed at bit[7]. We will do bitwise `or` on this and mantissa later
        // Pack each value as a single byte
        __m256i sign_packed = _mm256_shuffle_epi8(sign_bits, sign_pack);
        __m128i sign_packed_high = _mm256_extracti128_si256(sign_packed, 1);
        sign_packed = _mm256_unpacklo_epi64(sign_packed, _mm256_castsi128_si256(sign_packed_high));

        /////////////////////////////////////////////////////////
        // Extract Exponent bits - First 8 float values or row
        /////////////////////////////////////////////////////////

        __m256i fp16_in_shifted_exponent = _mm256_srli_epi16(fp16_in, 10);
        fp16_in_shifted_exponent = _mm256_and_si256(fp16_in_shifted_exponent, exponent_mask);

        /////////////////////////////////////////////////////////
        // Reduction tree for finding the maximum exponent
        /////////////////////////////////////////////////////////

        // A reduction tree for finding the max value between all the exponents

        // Swap the top and bottom 128bit halves
        // initial: 0A0B0C0D0E0F0G0H0102030405060708
        // Results: 01020304050607080A0B0C0D0E0F0G0H
        __m256i exponent_shift = _mm256_permutevar8x32_epi32(fp16_in_shifted_exponent, exponent_shift_offset_0);
        // Take the max again
        // Results: 0A0B0C0D0E0F0G0H0A0B0C0D0E0F0G0H
        __m256i exponent_max = _mm256_max_epu8(fp16_in_shifted_exponent, exponent_shift);
        // In each 128bit halves, swap top and bottom 64b
        // Results: 0E0F0G0H0A0B0C0D0E0F0G0H0A0B0C0D
        exponent_shift = _mm256_permutevar8x32_epi32(exponent_max, exponent_shift_offset_1);
        // Take the max again
        // Results: 0E0F0G0H0E0F0G0H0E0F0G0H0E0F0G0H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // In each 64bits, swap the top and bottom 32b
        // Results: 0G0H0E0F0G0H0E0F0G0H0E0F0G0H0E0F
        exponent_shift = _mm256_permutevar8x32_epi32(exponent_max, exponent_shift_offset_2);
        // Take max again:
        // Results: 0G0H0G0H0G0H0G0H0G0H0G0H0G0H0G0H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // swap the lower 8b of first and second lowest 16bits
        // Results: 00000000000000000000000000000H0G
        exponent_shift = _mm256_shuffle_epi8(exponent_max, exponent_shift_offset_3);
        // Take max again:
        // Results: 0G0H0G0H0G0H0G0H0G0H0G0H0G0H0H0H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // Broadcast the first 8-bits over all 256bits:
        // Results: HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH
        __m256i max_exponent_broadcast = _mm256_broadcastb_epi8(_mm256_castsi256_si128(exponent_max));

        /////////////////////////////////////////////////////////
        // Subtract each exponent from maximum exponent
        /////////////////////////////////////////////////////////

        // Subtract max exponent from all exponents and mask the lower 8 bits
        // HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH - 0A0B0C0D0E0F0G0H0102030405060708
        __m256i exponent_diff = _mm256_sub_epi8(max_exponent_broadcast, fp16_in_shifted_exponent);
        exponent_diff = _mm256_and_si256(exponent_diff, lower_8b_mask);

        /////////////////////////////////////////////////////////
        // Extract mantissa bits
        /////////////////////////////////////////////////////////

        __m256i fp16_in_exp_man_mask = _mm256_andnot_si256(sign_bit_mask, fp16_in);
        fp16_in = _mm256_and_si256(fp16_in, mantissa_mask);
        fp16_in = _mm256_srli_epi16(fp16_in, truncate_bfp_mantissa ?  4 : 3);
        // Compare each input value (excluding sign) with zero, (num == zero)? 0xFFFF: 0
        __m256i check_zero = _mm256_cmpeq_epi16(fp16_in_exp_man_mask, zero_compare);
        // bit-wise NOT of check_zero will be: (exp == zero)? 0: 0xFF
        // bit-wise AND check_zero with explicit_mantissa_one_mask. In this case, if exp == 0 --> do not add the explicit one
        __m256i explicit_mantissa_one_mask_with_zero_check = _mm256_andnot_si256(check_zero, explicit_mantissa_one_mask);
        // Always set bit[7] in each fp16[15:0] number such that when bits[6:0] are extracted for mantissa we will have {1, [6:0]}
        __m256i fp16_in_with_explicit_mantissa_one = _mm256_or_si256(fp16_in, explicit_mantissa_one_mask_with_zero_check);
        // // Shift each mantissa bit by 1 to reserve the MSB of each lower 8bits for sign {0-1, [5:0]}
        // fp16_in_with_explicit_mantissa_one = _mm256_srli_epi16(fp16_in_with_explicit_mantissa_one, 1);

        /////////////////////////////////////////////////////////
        // Shift each mantissa by the exponent diff
        /////////////////////////////////////////////////////////

        __m128i mantissa_split_high = _mm256_extracti128_si256(fp16_in_with_explicit_mantissa_one, 1);
        __m256i mantissa_split_low_extend = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(fp16_in_with_explicit_mantissa_one));
        __m256i mantissa_split_high_extend = _mm256_cvtepu16_epi32(mantissa_split_high);

        __m128i exponent_split_high = _mm256_extracti128_si256(exponent_diff, 1);
        __m256i exponent_split_low_extend = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(exponent_diff));
        __m256i exponent_split_high_extend = _mm256_cvtepu16_epi32(exponent_split_high);

        // Shift each extended 32b mantissa by the corresponding extended 32b mantissa diff
        __m256i mantissa_shifted_low = _mm256_srlv_epi32(mantissa_split_low_extend, exponent_split_low_extend);
        __m256i mantissa_shifted_high = _mm256_srlv_epi32(mantissa_split_high_extend, exponent_split_high_extend);

        if(!truncate_bfp_mantissa) {
            mantissa_shifted_low = _mm256_add_epi32(mantissa_shifted_low, _mm256_set1_epi32(1));
            mantissa_shifted_low = _mm256_srli_epi32(mantissa_shifted_low, 1);
            mantissa_shifted_low = _mm256_min_epi32(mantissa_shifted_low, _mm256_set1_epi32(127));

            mantissa_shifted_high = _mm256_add_epi32(mantissa_shifted_high, _mm256_set1_epi32(1));
            mantissa_shifted_high = _mm256_srli_epi32(mantissa_shifted_high, 1);
            mantissa_shifted_high = _mm256_min_epi32(mantissa_shifted_high, _mm256_set1_epi32(127));
        }
        
        __m256i mantissa_shifted_low_16b = _mm256_shuffle_epi8(mantissa_shifted_low, mantissa_repack);
        __m128i mantissa_shifted_low_16b_top = _mm256_extracti128_si256(mantissa_shifted_low_16b, 1);
        __m256i mantissa_shifted_low_16b_packed = _mm256_unpacklo_epi32(mantissa_shifted_low_16b, _mm256_castsi128_si256(mantissa_shifted_low_16b_top));
        
        __m256i mantissa_shifted_high_16b = _mm256_shuffle_epi8(mantissa_shifted_high, mantissa_repack);
        __m128i mantissa_shifted_high_16b_top = _mm256_extracti128_si256(mantissa_shifted_high_16b, 1);
        __m256i mantissa_shifted_high_16b_packed = _mm256_unpacklo_epi32(mantissa_shifted_high_16b, _mm256_castsi128_si256(mantissa_shifted_high_16b_top));

        __m256i mantissa_shifted = _mm256_unpacklo_epi64(mantissa_shifted_low_16b_packed, mantissa_shifted_high_16b_packed);

        /////////////////////////////////////////////////////////
        // Add sign bit to each mantissa
        /////////////////////////////////////////////////////////

        check_zero = _mm256_cmpeq_epi8(mantissa_shifted, zero_compare);
        sign_packed = _mm256_andnot_si256(check_zero, sign_packed);
        mantissa_shifted = _mm256_or_si256(mantissa_shifted, sign_packed);

        /////////////////////////////////////////////////////////
        // Pack mantissa bits for all values and store
        /////////////////////////////////////////////////////////

        _mm_store_si128(vout, _mm256_castsi256_si128(mantissa_shifted));
        (*(uint8_t**)out_exp)[j] = _mm256_extract_epi8(exponent_max, 0);
    
        out += 16;
        in += row_stride;
    }
    *(uint8_t**)out_exp += quad_height;

    return out;
}

template<> 
std::uint8_t* tensor_to_tile<16, 16, Fp16_Bfp8b, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, void** out_exp, bool, int, uint32_t, uint32_t quad_height, bool truncate_bfp_mantissa)
{
    // Compose 2 conversions: Fp16 -> Fp16_b then Fp16_b -> Bfp8_b
    const __m256i exponent_shift_offset_0 = _mm256_setr_epi32(4, 5, 6, 7, 0, 1, 2, 3);
    const __m256i exponent_shift_offset_1 = _mm256_setr_epi32(2, 3, 0, 1, 6, 7, 4, 5);
    const __m256i exponent_shift_offset_2 = _mm256_setr_epi32(1, 0, 3, 2, 5, 4, 7, 6);
    const __m256i zero_compare            = _mm256_setr_epi32(0, 0, 0, 0, 0, 0, 0, 0);
    const __m256i exponent_shift_offset_3 = _mm256_setr_epi8(2, 0xFF, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);

    const __m256i explicit_mantissa_one_mask = _mm256_setr_epi16(0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080,
                                                    0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080, 0x0080);

    const __m256i sign_bit_mask =               _mm256_setr_epi16(0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000,
                                                    0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000, 0x8000);
    
    const __m256i lower_8b_mask =               _mm256_setr_epi16(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    const __m256i mantissa_mask =               _mm256_setr_epi16(0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
                                                    0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F);

    const __m256i mantissa_repack =             _mm256_setr_epi8(0, 4, 8, 12, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0, 4, 8, 12, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    
    const __m256i sign_pack =                   _mm256_setr_epi8(1, 3, 5, 7, 9, 11, 13, 15,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                    1, 3, 5, 7, 9, 11, 13, 15,
                                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    
    const auto row_stride = tensor_row_pitch / sizeof(*in);

    PREFETCH(in);
    PREFETCH(in + row_stride);
    PREFETCH(in + 2 * row_stride);
    PREFETCH(in + 3 * row_stride);
    PREFETCH(in + 4 * row_stride);
    PREFETCH(in + 5 * row_stride);
    const auto prefetch_offset = 6 * row_stride;

    for(std::size_t j = 0; j < quad_height; j++){
        const __m256i* vin = reinterpret_cast<const __m256i*>(in);
        __m128i* vout = reinterpret_cast<__m128i*>(out);

        PREFETCH(in + prefetch_offset);
        // First convert Fp16 -> Fp16b
        __m256i fp16_in = simd_256_load(vin);
        __m256i exp = _mm256_srl_epi16(_mm256_and_si256(fp16_in, _mm256_set1_epi16(0x7c00)), _mm_set_epi64x(0, 10));
        exp = _mm256_add_epi16(exp, _mm256_set1_epi16(112));
        exp = _mm256_blendv_epi8(exp, _mm256_set1_epi16(0), _mm256_cmpeq_epi16(exp, _mm256_set1_epi16(112)));
        exp = _mm256_sll_epi16(exp, _mm_set_epi64x(0, 7));
        __m256i man = _mm256_srl_epi16(_mm256_and_si256(fp16_in, _mm256_set1_epi16(0x3ff)), _mm_set_epi64x(0, 3));
        __m256i sign = _mm256_and_si256(fp16_in, _mm256_set1_epi16(0x8000));
        fp16_in = _mm256_or_si256(sign, _mm256_or_si256(exp, man)); //this now has data in fp16_b format

        // Now convert Fp16b -> Bfp8b

        /////////////////////////////////////////////////////////
        // Extract sign bits - First 8 float values or row
        /////////////////////////////////////////////////////////

        __m256i sign_bits = _mm256_and_si256(fp16_in, sign_bit_mask);
        // 8, 8-bit values each {sign-0000000}
        // Shift all 16bit values by 8, such that sign bit will be placed at bit[7]. We will do bitwise `or` on this and mantissa later
        // Pack each value as a single byte
        __m256i sign_packed = _mm256_shuffle_epi8(sign_bits, sign_pack);
        __m128i sign_packed_high = _mm256_extracti128_si256(sign_packed, 1);
        sign_packed = _mm256_unpacklo_epi64(sign_packed, _mm256_castsi128_si256(sign_packed_high));

        /////////////////////////////////////////////////////////
        // Extract Exponent bits - First 8 float values or row
        /////////////////////////////////////////////////////////

        __m256i fp16_in_shifted_exponent = _mm256_srli_epi16(fp16_in, 7);

        /////////////////////////////////////////////////////////
        // Reduction tree for finding the maximum exponent
        /////////////////////////////////////////////////////////

        // A reduction tree for finding the max value between all the exponents

        // Swap the top and bottom 128bit halves
        // initial: 0A0B0C0D0E0F0G0H0102030405060708
        // Results: 01020304050607080A0B0C0D0E0F0G0H
        __m256i exponent_shift = _mm256_permutevar8x32_epi32(fp16_in_shifted_exponent, exponent_shift_offset_0);
        // Take the max again
        // Results: 0A0B0C0D0E0F0G0H0A0B0C0D0E0F0G0H
        __m256i exponent_max = _mm256_max_epu8(fp16_in_shifted_exponent, exponent_shift);
        // In each 128bit halves, swap top and bottom 64b
        // Results: 0E0F0G0H0A0B0C0D0E0F0G0H0A0B0C0D
        exponent_shift = _mm256_permutevar8x32_epi32(exponent_max, exponent_shift_offset_1);
        // Take the max again
        // Results: 0E0F0G0H0E0F0G0H0E0F0G0H0E0F0G0H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // In each 64bits, swap the top and bottom 32b
        // Results: 0G0H0E0F0G0H0E0F0G0H0E0F0G0H0E0F
        exponent_shift = _mm256_permutevar8x32_epi32(exponent_max, exponent_shift_offset_2);
        // Take max again:
        // Results: 0G0H0G0H0G0H0G0H0G0H0G0H0G0H0G0H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // swap the lower 8b of first and second lowest 16bits
        // Results: 00000000000000000000000000000H0G
        exponent_shift = _mm256_shuffle_epi8(exponent_max, exponent_shift_offset_3);
        // Take max again:
        // Results: 0G0H0G0H0G0H0G0H0G0H0G0H0G0H0H0H
        exponent_max = _mm256_max_epu8(exponent_max, exponent_shift);
        // Broadcast the first 8-bits over all 256bits:
        // Results: HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH
        __m256i max_exponent_broadcast = _mm256_broadcastb_epi8(_mm256_castsi256_si128(exponent_max));

        /////////////////////////////////////////////////////////
        // Subtract each exponent from maximum exponent
        /////////////////////////////////////////////////////////

        // Subtract max exponent from all exponents and mask the lower 8 bits
        // HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH - 0A0B0C0D0E0F0G0H0102030405060708
        __m256i exponent_diff = _mm256_sub_epi8(max_exponent_broadcast, fp16_in_shifted_exponent);
        exponent_diff = _mm256_and_si256(exponent_diff, lower_8b_mask);

        /////////////////////////////////////////////////////////
        // Extract mantissa bits
        /////////////////////////////////////////////////////////

        __m256i fp16_in_exp_man_mask = _mm256_andnot_si256(sign_bit_mask, fp16_in);
        fp16_in = _mm256_and_si256(fp16_in, mantissa_mask);
        // Compare each input value (excluding sign) with zero, (num == zero)? 0xFFFF: 0
        __m256i check_zero = _mm256_cmpeq_epi16(fp16_in_exp_man_mask, zero_compare);
        // bit-wise NOT of check_zero will be: (num == zero)? 0: 0xFFFF
        // bit-wise AND check_zero with explicit_mantissa_one_mask. In this case, if exp == 0 --> do not add the explicit one
        __m256i explicit_mantissa_one_mask_with_zero_check = _mm256_andnot_si256(check_zero, explicit_mantissa_one_mask);
        // Always set bit[7] in each fp16[15:0] number such that when bits[6:0] are extracted for mantissa we will have {1, [6:0]}
        __m256i fp16_in_with_explicit_mantissa_one = _mm256_or_si256(fp16_in, explicit_mantissa_one_mask_with_zero_check);
        // Shift each mantissa bit by 1 to reserve the MSB of each lower 8bits for sign {0-1, [5:0]}
        fp16_in_with_explicit_mantissa_one = _mm256_srli_epi16(fp16_in_with_explicit_mantissa_one, (int) ((truncate_bfp_mantissa) * 1));

        /////////////////////////////////////////////////////////
        // Shift each mantissa by the exponent diff
        /////////////////////////////////////////////////////////

        __m128i mantissa_split_high = _mm256_extracti128_si256(fp16_in_with_explicit_mantissa_one, 1);
        __m256i mantissa_split_low_extend = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(fp16_in_with_explicit_mantissa_one));
        __m256i mantissa_split_high_extend = _mm256_cvtepu16_epi32(mantissa_split_high);

        __m128i exponent_split_high = _mm256_extracti128_si256(exponent_diff, 1);
        __m256i exponent_split_low_extend = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(exponent_diff));
        __m256i exponent_split_high_extend = _mm256_cvtepu16_epi32(exponent_split_high);

        // Shift each extended 32b mantissa by the corresponding extended 32b mantissa diff
        __m256i mantissa_shifted_low = _mm256_srlv_epi32(mantissa_split_low_extend, exponent_split_low_extend);
        __m256i mantissa_shifted_high = _mm256_srlv_epi32(mantissa_split_high_extend, exponent_split_high_extend);

        if(!truncate_bfp_mantissa) {
            mantissa_shifted_low = _mm256_add_epi32(mantissa_shifted_low, _mm256_set1_epi32(1));
            mantissa_shifted_low = _mm256_srli_epi32(mantissa_shifted_low, 1);
            mantissa_shifted_low = _mm256_min_epi32(mantissa_shifted_low, _mm256_set1_epi32(127));

            mantissa_shifted_high = _mm256_add_epi32(mantissa_shifted_high, _mm256_set1_epi32(1));
            mantissa_shifted_high = _mm256_srli_epi32(mantissa_shifted_high, 1);
            mantissa_shifted_high = _mm256_min_epi32(mantissa_shifted_high, _mm256_set1_epi32(127));
        }

        __m256i mantissa_shifted_low_16b = _mm256_shuffle_epi8(mantissa_shifted_low, mantissa_repack);
        __m128i mantissa_shifted_low_16b_top = _mm256_extracti128_si256(mantissa_shifted_low_16b, 1);
        __m256i mantissa_shifted_low_16b_packed = _mm256_unpacklo_epi32(mantissa_shifted_low_16b, _mm256_castsi128_si256(mantissa_shifted_low_16b_top));
        
        __m256i mantissa_shifted_high_16b = _mm256_shuffle_epi8(mantissa_shifted_high, mantissa_repack);
        __m128i mantissa_shifted_high_16b_top = _mm256_extracti128_si256(mantissa_shifted_high_16b, 1);
        __m256i mantissa_shifted_high_16b_packed = _mm256_unpacklo_epi32(mantissa_shifted_high_16b, _mm256_castsi128_si256(mantissa_shifted_high_16b_top));

        __m256i mantissa_shifted = _mm256_unpacklo_epi64(mantissa_shifted_low_16b_packed, mantissa_shifted_high_16b_packed);

        /////////////////////////////////////////////////////////
        // Add sign bit to each mantissa
        /////////////////////////////////////////////////////////

        check_zero = _mm256_cmpeq_epi8(mantissa_shifted, zero_compare);
        sign_packed = _mm256_andnot_si256(check_zero, sign_packed);
        mantissa_shifted = _mm256_or_si256(mantissa_shifted, sign_packed);

        /////////////////////////////////////////////////////////
        // Pack mantissa bits for all values and store
        /////////////////////////////////////////////////////////
        _mm_store_si128(vout, _mm256_castsi256_si128(mantissa_shifted));
        (*(uint8_t**)out_exp)[j] = _mm256_extract_epi8(exponent_max, 0);
    
        out += 16;
        in += row_stride;
    }
    *(uint8_t**)out_exp += quad_height;
    return out;
}
} // anonymous namespace

// Copy 4 16x16 tiles within a row-major tensor face to a linear output.
// A B
// C D
// in must point to the start of A, they will be coped to out in A, B, C, D order.
template <std::uint_fast32_t TileX, std::uint_fast32_t TileY, class Converter, bool DataShuffling>
void tensor_to_quad_tiles(const typename Converter::in_type * NO_PTR_ALIAS in,
                          typename Converter::out_type * NO_PTR_ALIAS out,
                          std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t datum_col_offset_for_quad, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa)
{

    constexpr std::uint_fast32_t tile_pitch = TileX * sizeof(typename Converter::in_type);
    auto inA = in;
    auto inB = inA + tile_pitch / sizeof(*inA);
    auto inC = inA + (TileY * tensor_row_pitch) / sizeof(*inA);
    auto inD = inA + (TileY * tensor_row_pitch + tile_pitch) / sizeof(*inA);
    
    auto outA = out;
    void *out_exponent = nullptr;
    std::uint8_t* host_exp_init_val = nullptr;
    constexpr bool has_shared_exponent = std::is_base_of_v<SharedExponentOutput, Converter>;
    std::uint32_t exp_section_size = 0;

    if constexpr (has_shared_exponent) {
        // For Bfp8A/B
        exp_section_size = tt::io::align_up(quad_height * num_faces_y * num_faces_x, 16);
        host_exp_init_val = exp_host;
        Converter::adjust_output_pointers(&outA, &out_exponent, exp_section_size);
    }

    auto outFinal = tensor_to_tile<TileX, TileY, Converter, DataShuffling>(inA, outA, tensor_row_pitch, (void**)(&exp_host), conversion_from_b_to_a_format, 0, 0, quad_height, truncate_bfp_mantissa);
    if(num_faces_x == 2) {
        outFinal = tensor_to_tile<TileX, TileY, Converter, DataShuffling>(inB, outFinal, tensor_row_pitch, (void**)(&exp_host), conversion_from_b_to_a_format, 0, 0, quad_height, truncate_bfp_mantissa);
    }

    if(num_faces_y == 2) {
        auto outD = tensor_to_tile<TileX, TileY, Converter, DataShuffling>(inC, outFinal, tensor_row_pitch, (void**)(&exp_host), conversion_from_b_to_a_format, 0, 0, quad_height, truncate_bfp_mantissa);
        if(num_faces_x == 2) {
            tensor_to_tile<TileX, TileY, Converter, DataShuffling>(inD, outD, tensor_row_pitch, (void**)(&exp_host), conversion_from_b_to_a_format, 0, 0, quad_height, truncate_bfp_mantissa);
        }
    }
    if constexpr (has_shared_exponent and !std::is_same<Converter, Int8_Bfp8>::value ) {
        std::memcpy(out_exponent, host_exp_init_val, exp_section_size);
    }
}

template <std::uint_fast32_t TileX, std::uint_fast32_t TileY, class Converter>
void tensor_to_megarow_quad(const typename Converter::in_type * NO_PTR_ALIAS in,
                            typename Converter::out_type * NO_PTR_ALIAS out,
                            std::uint_fast32_t tensor_row_pitch)
{
    tensor_to_tile<TileX, TileY, Converter>(in, out, tensor_row_pitch);
}

// The following specializations are provided.

template void tensor_to_quad_tiles<16, 16, Fp32_Fp32, false>(const float* NO_PTR_ALIAS in, float* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp32_Fp16b, false>(const float* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp32_Fp16, false>(const float* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp16b_Fp16, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp16b_Fp16b, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp32_Bfp8_all, false>(const float* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp16b_Bfp8_all, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp16b_Fp32, false>(const std::uint16_t* NO_PTR_ALIAS in, float* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp16_Bfp8, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp16_Bfp8b, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp16_Fp16b, false>(const std::uint16_t* NO_PTR_ALIAS in, std::uint16_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Fp16_Fp32, false>(const std::uint16_t* NO_PTR_ALIAS in, float* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Int8_Int8, false>(const int8_t* NO_PTR_ALIAS in, int8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);
template void tensor_to_quad_tiles<16, 16, Int8_Bfp8, false>(const uint8_t* NO_PTR_ALIAS in, uint8_t* NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch, bool conversion_from_b_to_a_format, std::vector<bool>& fill_data_for_faces, int num_rows_with_data, uint32_t global_face_offset, uint32_t quad_height, uint32_t num_faces_y, uint32_t num_faces_x, uint8_t* exp_host, bool truncate_bfp_mantissa);


/////////////////// The following are for pushing raw data (masks) to DRAM ///////////////////
template void tensor_to_megarow_quad<32, 32, RawUInt32_RawUInt32>(const std::uint32_t*  NO_PTR_ALIAS in, std::uint32_t*  NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch);
template void tensor_to_megarow_quad<32, 32, RawUInt16_RawUInt16>(const std::uint16_t*  NO_PTR_ALIAS in, std::uint16_t*  NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch);
template void tensor_to_megarow_quad<32, 32, RawUInt8_RawUInt8>(const std::uint8_t*  NO_PTR_ALIAS in, std::uint8_t*  NO_PTR_ALIAS out, std::uint_fast32_t tensor_row_pitch);
