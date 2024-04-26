// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "utils/logger.hpp"

#include <type_traits>
#include <cstdint>
#include <cstring>

// Available in C++20 <bit>.
template <class DestT, class SrcT, typename = std::enable_if_t<sizeof(SrcT) == sizeof(DestT)>>
inline DestT bit_cast(SrcT x) { DestT d; std::memcpy(&d, &x, sizeof(x)); return d; }

template <class T>
struct IdentityConverter
{
    typedef T in_type;
    typedef T out_type;

    static out_type convert(in_type x) { return x; }
};

typedef IdentityConverter<float> Fp32_Fp32;
typedef IdentityConverter<std::uint16_t> Fp16b_Fp16b;

struct RawUInt32_RawUInt32{
    typedef uint32_t in_type;
    typedef std::uint32_t out_type;

    static out_type convert(in_type x){ return x; }
};

struct RawUInt16_RawUInt16{
    typedef uint16_t in_type;
    typedef std::uint16_t out_type;

    static out_type convert(in_type x){ return x; }
};

struct RawUInt8_RawUInt8{
    typedef uint8_t in_type;
    typedef std::uint8_t out_type;

    static out_type convert(in_type x){ return x; }
};

struct Int8_Int8{
    typedef int8_t in_type;
    typedef std::int8_t out_type;

    static out_type convert(in_type x){ return x; }
};
struct Fp32_Fp16b
{
    typedef float in_type;
    typedef std::uint16_t out_type;

    static out_type convert(in_type x) { return bit_cast<std::uint32_t>(x) >> 16; }
};

struct Fp32_Fp16
{
    typedef float in_type;
    typedef std::uint16_t out_type;

    static out_type convert(in_type x) {
        log_assert(false, "This function should not get executed for fp32 -> fp16. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return uint16_t(0); // To suppress compilation warning
    }

};

struct Fp16b_Fp16
{
    typedef std::uint16_t in_type;
    typedef std::uint16_t out_type;

    static out_type convert(in_type x) {
        log_assert(false, "This function should not get executed for fp16b -> fp16. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return uint16_t(0); // To suppress compilation warning
    }
};

struct Fp16b_Fp32
{
    typedef std::uint16_t in_type;
    typedef float out_type;

    static out_type convert(in_type x) {
        log_assert(false, "This function should not get executed for fp16b -> fp32. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return float(0); // To suppress compilation warning
    }
};

struct Fp16_Fp32
{
    typedef std::uint16_t in_type;
    typedef float out_type;

    static out_type convert(in_type x) {
        log_assert(false, "This function should not get executed for fp16b -> fp32. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return float(0); // To suppress compilation warning
    }
};

struct Fp16_Fp16b
{
    typedef std::uint16_t in_type;
    typedef std::uint16_t out_type;

    static out_type convert(in_type x) {
        log_assert(false, "This function should not get executed for fp16 -> fp16b. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return uint16_t(0); // To suppress compilation warning
    }
};

struct SharedExponentOutput { };

struct Fp32_Bfp8_all : SharedExponentOutput
{
    // Converter for Fp32 to Bfp8_b and Bfp8 formats
    // Corresponds to a single templated function in tilecopy.cpp that can run in 2 different conversion modes depending on the argument conversion_to_a_format
    typedef float in_type;
    typedef std::uint8_t out_type;

    static out_type convert(in_type x) {
        log_assert(false, "This function should not get executed for fp32 -> bfp8 or fp32 -> bfp8b. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return uint8_t(0); // To suppress compilation warning
    }
    static void adjust_output_pointers(unsigned char** out_mantissa, void** out_exponent, uint32_t exp_section_size) {
        *out_exponent = *out_mantissa;
        *(out_type**)out_mantissa += exp_section_size; // A single-byte shared exponent exist for each row of each face in the tile
    }
};
struct Int8_Bfp8 : SharedExponentOutput
{
    typedef uint8_t in_type;
    typedef std::uint8_t out_type;
    static out_type convert(in_type x){
        log_assert(false, "This function should not get executed for int8 -> bfp8. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return uint8_t(0);
    }
    static void adjust_output_pointers(unsigned char** out_mantissa, void** out_exponent, uint32_t exp_section_size) {
        *out_exponent = *out_mantissa;
        *(out_type**)out_mantissa += exp_section_size; // A single-byte shared exponent exist for each row of each face in the tile
    }
};
struct Fp16b_Bfp8_all : SharedExponentOutput
{
    // Converter for Fp16b to Bfp8_b and Bfp8 formats
    // Corresponds to a single templated function in tilecopy.cpp that can run in 2 different conversion modes depending on the argument conversion_to_a_format
    typedef std::uint16_t in_type;
    typedef std::uint8_t out_type;

    static out_type convert(in_type x) {
        log_assert(false, "This function should not get executed for fp16 -> bfp8 or fp16b -> bfp8b. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return uint8_t(0); // To suppress compilation warning
    }
    static void adjust_output_pointers(unsigned char** out_mantissa, void** out_exponent, uint32_t exp_section_size) {
        *out_exponent = *out_mantissa;
        *(out_type**)out_mantissa += exp_section_size; // A single-byte shared exponent exist for each row of each face in the tile
    }

};

struct Fp16_Bfp8 : SharedExponentOutput
{
    typedef std::uint16_t in_type;
    typedef std::uint8_t out_type;

    static out_type convert(in_type x) {
        log_assert(false, "This function should not get executed for fp16b -> bfp8b. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return uint8_t(0); // To suppress compilation warning
    }
    static void adjust_output_pointers(unsigned char** out_mantissa, void** out_exponent, uint32_t exp_section_size) {
        *out_exponent = *out_mantissa;
        *(out_type**)out_mantissa += exp_section_size; // A single-byte shared exponent exist for each row of each face in the tile
    }

};

struct Fp16_Bfp8b : SharedExponentOutput
{
    typedef std::uint16_t in_type;
    typedef std::uint8_t out_type;

    static out_type convert(in_type x) {
        log_assert(false, "This function should not get executed for fp16b -> bfp8. An explicit templated tensor_to_tile function is implemented in tilecopy.cpp which must be used in hw-tilize.");
        return uint8_t(0); // To suppress compilation warning
    }
    static void adjust_output_pointers(unsigned char** out_mantissa, void** out_exponent, uint32_t exp_section_size) {
        *out_exponent = *out_mantissa;
        *(out_type**)out_mantissa += exp_section_size; // A single-byte shared exponent exist for each row of each face in the tile
    }

};
