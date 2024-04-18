// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <limits>

#include "size_lib.hpp"
#include "tile.hpp"
#include "model/tt_rnd_util.hpp"
#include "tt_backend_api_types.hpp"

// Utils
using std::cout;
using std::dec;
using std::endl;
using std::fixed;
using std::hex;
using std::isnan;
using std::mt19937;
using std::normal_distribution;
using std::uniform_real_distribution;
using std::numeric_limits;
using std::runtime_error;
using std::setprecision;
using std::setw;
using std::signbit;
using std::stringstream;
using std::uniform_int_distribution;

// Functions
using std::function;
using std::minus;
using std::multiplies;
using std::plus;
using std::swap;

// Datastructures
using std::map;

const bool tt::tt_tile::skip_bfp8_check =
    std::getenv("TT_BACKEND_PERF_ANALYZER") ? atoi(std::getenv("TT_BACKEND_PERF_ANALYZER")) : false;
const bool tt::tt_tile::truncate_bfp_mantissa =
    std::getenv("TT_BACKEND_DISABLE_BFP_RTE") ? atoi(std::getenv("TT_BACKEND_DISABLE_BFP_RTE")) : false;
const bool tt::tt_tile::force_slow_untilize =
    std::getenv("TT_BACKEND_FORCE_SLOW_UNTILIZE") ? atoi(std::getenv("TT_BACKEND_FORCE_SLOW_UNTILIZE")) : false;

namespace tt {
    static TT_TILE_FORCE_INLINE uint8_t get_common_exp(const uint32_t* vec, bool is_exp_a) {
        uint32_t max = 0;

        for (int i = 0; i < 16; ++i) {
            // mask & shift out exp
            uint32_t exp = (vec[i] & 0x7f800000) >> 23;

            if (is_exp_a) {
                int32_t se = static_cast<int32_t>(exp);
                // need to rebias from 127 to 15
                se = se - 127 + 15;

                if (se > 31) {
                    se = 31;
                } else if (se < 0) {
                    se = 0;
                }

                exp = static_cast<uint32_t>(se);
            }

            if (exp > max) {
                max = exp;
            }
        }
        return max;
    }
    tt_tile::tt_tile() {
        set_zero();
    }

    tt_tile::tt_tile(const array<array<float, tt::constants::TILE_HEIGHT>, tt::constants::TILE_WIDTH> &data) :
        data_format(DataFormat::Float32) {
        uint32_t i, j;
        for (i = 0; i < tt::constants::TILE_HEIGHT; ++i) {
            for (j = 0; j < tt::constants::TILE_WIDTH; ++j) {
                t[i][j] = data[i][j];
            }
        }
        clear_packed_data();
    }

    tt_tile::tt_tile(DataFormat data_formati, bool init_to_zero) {
        data_format = data_formati;
        if (init_to_zero)
            set_zero();
        clear_packed_data();
    }

    // Constructor for tiles in read back buffers
    tt_tile::tt_tile(DataFormat data_formati, uint64_t addr, uint32_t chan, uint32_t chip_id, bool host_resident_in)
    {
        d_addr = addr;
        d_chan = chan;
        d_chip_id = chip_id;
        d_record_vld = true;
        host_resident = host_resident_in;

        data_format = data_formati;
        set_zero();
        clear_packed_data();
    }

    void tt_tile::set_zero() {
        std::memset(t, 0, sizeof(t));
    }

    // Converts float to hardware int8 format
    // 1 bit sign, 7 bit magnitude [-127, 127]
    static uint8_t float_to_int8(float in)
    {
        float tmp_t = std::clamp(std::round(in), -127.0f, 127.0f);
        return tmp_t < 0 ? 0x80 | static_cast<uint8_t>(-tmp_t) : static_cast<uint8_t>(tmp_t);
    }

    static uint8_t float_to_uint8(float in)
    {
        float tmp_t = std::clamp(std::round(in), 0.0f, 255.0f);
        return static_cast<uint8_t>(tmp_t);
    }

    static uint16_t float_to_uint16(float in)
    {
        float tmp_t = std::clamp(std::round(in), 0.0f, 65535.0f);
        return static_cast<uint16_t>(tmp_t);
    }

    float tt_tile::dword_to_float(uint32_t in) const
    {
        union
        {
            float output; // assumes sizeof(float) == sizeof(int)
            uint32_t input;
        } data;
        data.input = in;
        return(data.output);
    }

    double tt_tile::inf_to_num(double in) const
    {
        if (isinf(in)){
            if (signbit(in))
              return -std::numeric_limits<double>::max();
            else
              return std::numeric_limits<double>::max();
        } else {
            return in;
        }
    }

    uint32_t tt_tile::get_exp_dword(vector<uint8_t> &vec)
    {
        log_assert(vec.size() == 4, "Expected vec to be 4 bytes");

        uint32_t tmp = 0;
        for(int i=0;i<4;++i)
        {
            tmp = tmp | ((vec[i] & 0xff) << (i*8));
        }
        return(tmp);
    }

    template <DataFormat data_format>
    static constexpr int get_nums_in_dword()
    {
        if((data_format == DataFormat::Float32) || (data_format == DataFormat::Tf32) || (data_format == DataFormat::Int32) || (data_format == DataFormat::RawUInt32))
        {
            return 1;
        }
        else if((data_format == DataFormat::Float16) || (data_format == DataFormat::Float16_b) || (data_format == DataFormat::RawUInt16) || (data_format == DataFormat::UInt16))
        {
            return 2;
        }
        else if((data_format == DataFormat::Bfp8) || (data_format == DataFormat::Bfp8_b) || (data_format == DataFormat::Lf8) || (data_format == DataFormat::Fp8_e4m3) || (data_format == DataFormat::RawUInt8) || (data_format == DataFormat::Int8) || (data_format == DataFormat::UInt8))
        {
            return 4;
        }
        else if((data_format == DataFormat::Bfp4) || (data_format == DataFormat::Bfp4_b))
        {
            return 8;
        }
        else if((data_format == DataFormat::Bfp2) || (data_format == DataFormat::Bfp2_b))
        {
            return 16;
        }

        return(0xabadface);
    }

    bool tt_tile::is_shared_exp_format(DataFormat data_format)
    {
        return((data_format == DataFormat::Bfp8) || (data_format == DataFormat::Bfp8_b) || (data_format == DataFormat::Bfp4) || (data_format == DataFormat::Bfp4_b) || (data_format == DataFormat::Bfp2) || (data_format == DataFormat::Bfp2_b));
    }

    bool tt_tile::is_shared_exp_a_format()
    {
        return((data_format == DataFormat::Bfp8) || (data_format == DataFormat::Bfp4) || (data_format == DataFormat::Bfp2));
    }

    bool tt_tile::is_shared_exp_b_format()
    {
        return((data_format == DataFormat::Bfp8_b) || (data_format == DataFormat::Bfp4_b) || (data_format == DataFormat::Bfp2_b));
    }

    static TT_TILE_FORCE_INLINE uint32_t conv_u32_to_lf8(uint32_t in)
    {
        uint32_t m = in & 0x007fffff;
        uint32_t e = (in & 0x7F800000) >> 23;
        uint32_t s = (in & 0x80000000) >> 24; // move to bit 8
        // unbias and rebias exponent, shift down mantissa
        // with saturation to bottom/top of range
        int se = (int)e;
        se = se - 127;
        // HW specific lf8 has 5 bit exponent and 2-bit mant with no representation for inf
        // this can hold 0 -> 31, which represents -15 -> 16
        if(se <= (-15))
        {
            se = -15;
            m = 0;
        }
        else if(se > 16)
        {
            se = 16;
            m = 0x007fffff >> 21;
        }
        else
        {
            se = se;
            m = m >> 21;
        }
        // add bias for lf8 (5-bit exp, 2-bit mant)
        se += 15;
        log_assert(se >= 0, "expected positive exp");
        e = (uint32_t) se;
        e = e << 2;
        uint32_t out = s | e | m;
        return(out);
    }

    static TT_TILE_FORCE_INLINE uint32_t conv_u32_to_fp8_e4m3(uint32_t in)
    {
        uint32_t m = in & 0x007fffff;
        uint32_t e = (in & 0x7F800000) >> 23;
        uint32_t s = (in & 0x80000000) >> 24; // move to bit 8
        // unbias and rebias exponent, shift down mantissa
        // with saturation to bottom/top of range
        int se = (int)e;
        se = se - 127;
        // HW specific Fp8_e4m3 has 4 bit exponent and 3-bit mant with no representation for inf
        // this can hold 0 -> 15, which represents -7 -> 8
        if(se <= (-7))
        {
            se = -7;
            m = 0;
        }
        else if(se > 8)
        {
            se = 8;
            m = 0x007fffff >> 20;
        }
        else
        {
            se = se;
            m = m >> 20;
        }
        // add bias for Fp8_e4m3 (4-bit exp, 3-bit mant)
        se += 7;
        TT_ASSERT(se >= 0);
        e = (uint32_t) se;
        e = e << 3;
        uint32_t out = s | e | m;
        return(out);
    }

    uint32_t tt_tile::conv_u32_to_int8(uint32_t in) const
    {
        union {
            float f;
            uint32_t u;
        } u2f = {.u = in};
        float f = u2f.f;
        int32_t out = (int32_t)f & 0x000000ff;
        return((uint32_t)out);
    }

    uint16_t tt_tile::conv_u32_to_raw_u16(uint32_t in) const
    {
        return in & 0x0000ffff;
    }

    uint32_t tt_tile::conv_u16_to_u32(uint16_t in) const {
        // https://github.com/Maratyszcza/FP16/blob/master/include/fp16/fp16.h
        uint32_t w = (uint32_t) in << 16;

        uint32_t sign = w & UINT32_C(0x80000000);
        uint32_t nonsign = w & UINT32_C(0x7FFFFFFF);
        uint32_t renorm_shift = __builtin_clz(nonsign);

        renorm_shift = renorm_shift > 5 ? renorm_shift - 5 : 0;
        const int32_t inf_nan_mask = ((int32_t) (nonsign + 0x04000000) >> 8) & INT32_C(0x7F800000);
        const int32_t zero_mask = (int32_t) (nonsign - 1) >> 31;
        return sign | ((((nonsign << renorm_shift >> 3) + ((0x70 - renorm_shift) << 23)) | inf_nan_mask) & ~zero_mask);
    }

    uint32_t tt_tile::conv_u16b_to_u32(uint16_t in) const {
        // converting fp16 -> f32 in atype elided way (no floats
        //
        // f16_b has the same number of exponent bits as fp32, so we can just do a direct cast and pad zeroes on mantissa 
        uint32_t w = (uint32_t) in << 16;
        return w;
    }
    
    bool tt_tile::packed_data_present() const { return !packed_data.empty(); }



    template <DataFormat data_format>
    static constexpr TT_TILE_FORCE_INLINE bool is_shared_exp_format() {
        return((data_format == DataFormat::Bfp8) || (data_format == DataFormat::Bfp8_b) || (data_format == DataFormat::Bfp4) || (data_format == DataFormat::Bfp4_b) || (data_format == DataFormat::Bfp2) || (data_format == DataFormat::Bfp2_b));
    }

    template <DataFormat data_format, bool truncate_bfp_mantissa, bool is_exp_a>
    static TT_TILE_FORCE_INLINE uint16_t conv_u32_to_bfp(uint32_t exp, uint32_t in)
    {
        //check for both +/- 0.0
        constexpr uint32_t EXP_MANTISSA_BMSK = ((1U << 31) - 1);
        bool is_zero = ((in & EXP_MANTISSA_BMSK) == 0);

        if(is_zero){return 0;}

        uint32_t m = in & 0x007fffff;
        uint32_t e = (in & 0x7F800000) >> 23;
        uint32_t s = (in & 0x80000000) >> 31;

        if (is_exp_a) {
            int32_t se = static_cast<int32_t>(e);
            // rebias
            se = se - 127 + 15;
            // check for saturation
            if(se > 31) {se = 31; m = 0x007fffff;}
            else if(se < 0){se = 0; m = 0x0;}

            e = static_cast<uint32_t>(se);
        }

        int exp_diff = exp - e; // this will be positive or zero by design

        // float mantissa is 23 bits + hidden bit = 24 bits
        // add hidden 1
        m = (1 << 23) | m;

        // shift mantissa further down by exp diff
        // In bit-shift operation (A >> B), the result is undefined if B is greater than or equal to the number of bits in A
        while (exp_diff > 31) {
            m = m >> 31;
            exp_diff -= 31;
        }
        m = m >> exp_diff;

        if ((data_format == DataFormat::Bfp8) || (data_format == DataFormat::Bfp8_b)){
            // this needs to become 7 bits so shift 17 times
            if(truncate_bfp_mantissa) {
                // Truncation: Round down
                m = m >> 17;
            }
            else {
                // Round mantissa to nearest even
                m += 1 << 16;
                m = m >> 17;
                if(m > 127) m = 127;
            }
            // add sign bit only if result is not 0
            if(0 == m){s = 0;}
            m = (s << 7) | m;
        } else if ((data_format == DataFormat::Bfp4) || (data_format == DataFormat::Bfp4_b)){
            // this needs to become 3 bits so shift 21 times
            if(truncate_bfp_mantissa) {
                // Truncation: Round down
                m = m >> 21;
            }
            else {
                // Round mantissa to nearest even
                m += 1 << 20;
                m = m >> 21;
                if(m > 7) m = 7;
            }
            // add sign bit only if result is not 0
            if(0 == m){s = 0;}
            m = (s << 3) | m;
        } else if ((data_format == DataFormat::Bfp2) || (data_format == DataFormat::Bfp2_b)){
            // this needs to become 1 bit so shift 23 times
            if(truncate_bfp_mantissa) {
                // Truncation: Round down
                m = m >> 23;
            }
            else {
                // Round mantissa to nearest even
                m += 1 << 22;
                m = m >> 23;
                if(m > 1) m = 1;
            }
            // add sign bit only if result is not 0
            if(0 == m){s = 0;}
            m = (s << 1) | m;
        }

        return m;
    }

    template <DataFormat data_format, bool truncate_bfp_mantissa>
    static TT_TILE_FORCE_INLINE uint32_t convert_num_format(uint32_t num_index_in_tile, uint32_t num, const vector<uint32_t>& packed_data)
    {
        uint32_t res = 0xbadface;

        // Look up exponent, although it will only be used
        // for BFP formats
        uint32_t exp_index; // skip header, look up exponent
        uint32_t exp = 0;

        if(is_shared_exp_format<data_format>())
        {
            exp_index = 4 + (num_index_in_tile >> 6); // skip header, look up exponent
            uint32_t byte_index = (num_index_in_tile >> 4) & 0x3;
            exp = tt_tile::get_byte(packed_data.at(exp_index), byte_index);
        }
        switch(data_format)
        {
            case DataFormat::Float32  : res = num; break;
            case DataFormat::Int32    : res = num; break;
            case DataFormat::Tf32     : res = num & 0xFFFFE000; break; //10b mantissa
            case DataFormat::Float16  : res = tt_tile::conv_u32_to_u16(num); break;
            case DataFormat::Bfp8     : res = conv_u32_to_bfp<data_format, truncate_bfp_mantissa, true>(exp,num); break;
            case DataFormat::Bfp4     : res = conv_u32_to_bfp<data_format, truncate_bfp_mantissa, true>(exp,num); break;
            case DataFormat::Bfp2     : res = conv_u32_to_bfp<data_format, truncate_bfp_mantissa, true>(exp,num); break;
            case DataFormat::Float16_b: res = tt_tile::conv_u32_to_u16b(num); break;
            case DataFormat::Bfp8_b   : res = conv_u32_to_bfp<data_format, truncate_bfp_mantissa, false>(exp,num); break;
            case DataFormat::Bfp4_b   : res = conv_u32_to_bfp<data_format, truncate_bfp_mantissa, false>(exp,num); break;
            case DataFormat::Bfp2_b   : res = conv_u32_to_bfp<data_format, truncate_bfp_mantissa, false>(exp,num); break;
            case DataFormat::Lf8      : res = conv_u32_to_lf8(num); break;
            case DataFormat::Fp8_e4m3 : res = conv_u32_to_fp8_e4m3(num); break;
            case DataFormat::UInt16   : res = num & 0x0000FFFF; break;
            case DataFormat::Int8     : res = num & 0x000000FF; break;
            case DataFormat::UInt8    : res = num & 0x000000FF; break;
            case DataFormat::RawUInt8 : res = num & 0x000000FF; break;
            case DataFormat::RawUInt16: res = num & 0x0000FFFF; break;
            case DataFormat::RawUInt32: res = num; break;
            case DataFormat::Invalid  : res = 0xbadface ; log_fatal("Incorrect Data Format"); break;
        }
        return res;
    }

    template <DataFormat data_format, bool truncate_bfp_mantissa>
    static TT_TILE_FORCE_INLINE uint32_t create_num_dword(
        int num_index, const uint32_t* vec, const vector<uint32_t>& packed_data) {
        uint32_t tmp_o;

        if constexpr ((data_format == DataFormat::Float32) || (data_format == DataFormat::Int32) ||
            (data_format == DataFormat::RawUInt32)) {
            return vec[0];
        } else if constexpr(data_format == DataFormat::Tf32) {
            return vec[0] & 0xFFFFE000;
        } else {
            tmp_o = 0;
            constexpr uint32_t nums_in_dword = get_nums_in_dword<data_format>();
            constexpr uint32_t dword_in_32 = 32 / nums_in_dword;
            constexpr uint32_t mask = (1 << dword_in_32) - 1;

            for (int i = nums_in_dword - 1; i >= 0; --i)  // [0] in LSBs of dword
            {
                uint32_t conv_num = convert_num_format<data_format, truncate_bfp_mantissa>(num_index, vec[i], packed_data);
                tmp_o = tmp_o << dword_in_32;
                tmp_o = tmp_o | (conv_num & mask);
            }
        }
        return tmp_o;
    }

    template <DataFormat data_format, bool truncate_bfp_mantissa>
    static void pack_datums(float t[][tt::constants::TILE_WIDTH], std::array<uint32_t, 16>& accum_vec, int& accum_vec_index, vector<uint32_t>& packed_data, int num_faces_y, int num_faces_x, int face_height, int face_width) {
        // Raw formats are treated as a megarow of raw data without faces
        constexpr bool is_megarow = data_format == DataFormat::RawUInt8 or
                          data_format == DataFormat::RawUInt16 or
                          data_format == DataFormat::RawUInt32;

        // Pack numbers
        constexpr uint32_t nums_in_dword = get_nums_in_dword<data_format>();

        int num_cntr = 0;
        uint32_t tmp;
        int top_saturate, bot_saturate;
        if (is_megarow) {
            for (int i = 0; i < 32; ++i) {
                for (int j = 0; j < 32; ++j) {
                    tmp = tt_tile::float_to_dword(t[i][j]);
                    accum_vec[accum_vec_index++] = tmp;
                    num_cntr++;
                    if (num_cntr % nums_in_dword == 0) {
                        tmp = create_num_dword<data_format, truncate_bfp_mantissa>(num_cntr - 1, accum_vec.begin(), packed_data);
                        packed_data.push_back(tmp);
                        accum_vec_index = 0;
                    }
                }
            }
        } else {
            for (int tr = 0; tr < num_faces_y; ++tr) {
                for (int tc = 0; tc < num_faces_x; ++tc) {
                    for (int i = 0; i < face_height; ++i) {
                        for (int j = 0; j < face_width; ++j) {
                            if (data_format == DataFormat::Int8) {
                                tmp = float_to_int8(t[tr*16 + i][tc*16+j]);
                            } else if (data_format == DataFormat::UInt8) {
                                tmp = float_to_uint8(t[tr*16 + i][tc*16+j]);
                            } else if (data_format == DataFormat::Int32) {
                                if ((t[tr*16 + i][tc*16 + j])<0) {
                                    float tmp_t = std::min(-t[tr*16 + i][tc*16 + j], float(std::numeric_limits<int32_t>::max())); // bottom saturate host side values, since sign | mag has smaller range than 2's complement
                                    tmp = 0x80000000 | static_cast<int>(std::round(tmp_t)); // int32 is represented as {sign_bit, 31-bit mag}
                                } else {
                                    tmp = t[tr * 16 + i][tc * 16 + j];
                                    tmp = static_cast<int>(std::round(tmp));
                                }
                            } else if (data_format == DataFormat::UInt16) {
                                tmp = float_to_uint16(t[tr*16 + i][tc*16+j]);
                            } else {
                                tmp = tt_tile::float_to_dword(t[tr*16 + i][tc*16 + j]);
                            }
                            accum_vec[accum_vec_index++] = tmp;
                            num_cntr++;
                            if (num_cntr % nums_in_dword == 0) {
                                tmp = create_num_dword<data_format, truncate_bfp_mantissa>(num_cntr - 1, accum_vec.begin(), packed_data);
                                packed_data.push_back(tmp);
                                accum_vec_index=0;
                            }
                        }
                    }
                }
            }
        }
    }

    template <bool truncate_bfp_mantissa>
    static void pack_datums(DataFormat data_format, float t[][tt::constants::TILE_WIDTH], std::array<uint32_t, 16>& accum_vec, int& accum_vec_index, vector<uint32_t>& packed_data, int num_faces_y, int num_faces_x, int face_height, int face_width) {
       switch(data_format) {
            case DataFormat::Float32  : pack_datums<DataFormat::Float32, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Int32    : pack_datums<DataFormat::Int32, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Tf32     : pack_datums<DataFormat::Tf32, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Float16  : pack_datums<DataFormat::Float16, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Bfp8     : pack_datums<DataFormat::Bfp8, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Bfp4     : pack_datums<DataFormat::Bfp4, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Bfp2     : pack_datums<DataFormat::Bfp2, truncate_bfp_mantissa>(t,  accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Float16_b: pack_datums<DataFormat::Float16_b, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Bfp8_b   : pack_datums<DataFormat::Bfp8_b, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Bfp4_b   : pack_datums<DataFormat::Bfp4_b, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Bfp2_b   : pack_datums<DataFormat::Bfp2_b, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Fp8_e4m3 :
            case DataFormat::Lf8      : pack_datums<DataFormat::Lf8, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::UInt16   : pack_datums<DataFormat::UInt16, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Int8     : pack_datums<DataFormat::Int8, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::UInt8    : pack_datums<DataFormat::UInt8, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::RawUInt8 : pack_datums<DataFormat::RawUInt8, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::RawUInt16: pack_datums<DataFormat::RawUInt16, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::RawUInt32: pack_datums<DataFormat::RawUInt32, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
            case DataFormat::Invalid  : pack_datums<DataFormat::Invalid, truncate_bfp_mantissa>(t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width); break;
        }
    }

    void tt_tile::pack_data(bool truncate_bfp_mantissa) {
        log_assert(packed_data.empty(), "expected packed data to be empty");

        const int packed_data_size = size_bytes() / 4;
        packed_data.reserve(packed_data_size);

        int num_cntr = 0;
        std::array<uint32_t, 16> accum_vec;
        int accum_vec_index = 0;

        // Pack header
        packed_data.push_back((uint32_t)(packed_data_size >> 2));
        for (int i = 0; i < 3; ++i) packed_data.push_back(0x0);
        uint32_t num_faces_x = ceil(float(tile_width) / 16);
        uint32_t num_faces_y = ceil(float(tile_height) / 16);
        uint32_t face_height = std::min(tile_height, static_cast<uint32_t>(16));
        uint32_t face_width = std::min(tile_width, static_cast<uint32_t>(16));
        bool partial_face = face_height<16; // Tiles 1,2,4,8x32

        // pack exponents
        if (is_shared_exp_format(data_format)) {
            std::array<int8_t, 4> exponents;
            int exponent_cnt = 0;
            int exponent_dword_cnt = 0;
            num_cntr = 0;
            const bool is_exp_a = is_shared_exp_a_format();
            uint32_t tmp;
            for (int tr = 0; tr < num_faces_y; ++tr) {
                for (int tc = 0; tc < num_faces_x; ++tc) {
                    for (int i = 0; i < face_height; ++i) {
                        for (int j = 0; j < face_width; ++j) {
                            tmp = 0;  // Keep this at 0 if the datum is outside fractional tile bounds
                            if (i < tile_height && j < tile_width) {
                                tmp = float_to_dword(t[tr * 16 + i][tc * 16 + j]);
                            }
                            accum_vec[accum_vec_index++] = tmp;
                            num_cntr++;
                            if (num_cntr % 16 == 0) {
                                exponents[exponent_cnt++] = get_common_exp(accum_vec.begin(), is_exp_a);
                                accum_vec_index = 0;
                            }
                            if (num_cntr % 64 == 0) {
                                uint32_t tmp = 0;
                                for (int i = 0; i < 4; ++i) {
                                    tmp = tmp | ((exponents[i] & 0xff) << (i * 8));
                                }
                                packed_data.push_back(tmp);
                                exponent_dword_cnt++;
                                exponent_cnt = 0;
                            }
                        }
                    }
                }
            }

            if (partial_face) {
                // Pad exponents to dword (handle 1x32 and 2x32 tiles where we have only 1 or 2 exponent bytes)
                if (exponent_cnt != 0) {  // Check if there are some exponents left
                    uint32_t tmp = 0;
                    for (int i = 0; i < exponent_cnt; ++i) {
                        tmp = tmp | ((exponents[i] & 0xff) << (i * 8));
                    }
                    // Push padded dword
                    packed_data.push_back(tmp);
                    exponent_dword_cnt++;
                    exponent_cnt = 0;
                }

                uint dwords_to_pad =
                    ((exponent_dword_cnt % 4) > 0) ? 4 - exponent_dword_cnt % 4 : 0;  // Pad to 16B word

                for (uint i = 0; i < dwords_to_pad; ++i) {
                    packed_data.push_back(0);
                    exponent_cnt += 4;
                    exponent_dword_cnt++;
                }
                log_assert(exponent_dword_cnt == 1 * num_faces_y * 4, "Unexpected number of exps");
            } else {
                log_assert(exponent_dword_cnt == num_faces_x * num_faces_y * 4, "Unexpected number of exps");
            }
        }

        if (tt_tile::truncate_bfp_mantissa || truncate_bfp_mantissa) {
            pack_datums<true>(data_format, t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width);
        } else {
            pack_datums<false>(data_format, t, accum_vec, accum_vec_index, packed_data, num_faces_y, num_faces_x, face_height, face_width);
        }

        // Pad to 32B boundary if needed
        if (!(partial_face && is_shared_exp_format(data_format))) {
            for (int i = 0; i < 4; ++i) packed_data.push_back(0x0);
        }

        // Check that we ended up on 32B granularity and correct size
        log_assert((packed_data.size() % 8) == 0, "Packed data is not 32B aligned"); 
        log_assert(packed_data.size() == (uint32_t) (size_bytes() / 4), "Incorrect size for packed data");
    }

    int tt_tile::size_bytes(bool include_header_padding) {
        return tt::size::get_tile_size_in_bytes(data_format, include_header_padding, tile_height, tile_width);
    }

    uint32_t tt_tile::get_quarter_byte(uint32_t word, uint32_t index)
    {
        log_assert(index < 16, "Index out of bounds");
        uint32_t mask = 0x3 << (2 * index);
        uint32_t masked = word & mask;
        masked = masked >> (2*index);
        return masked;
    }

    uint32_t tt_tile::get_half_byte(uint32_t word, uint32_t index)
    {
        log_assert(index < 8, "Index out of bounds");
        uint32_t mask = 0xf << (4 * index);
        uint32_t masked = word & mask;
        masked = masked >> (4*index);
        return masked;
    }

    

    uint32_t tt_tile::get_half(uint32_t word, uint32_t index)
    {
        log_assert(index < 2, "Index out of bounds");
        uint32_t mask = 0xffff << (16 * index);
        uint32_t masked = word & mask;
        masked = masked >> (16*index);
        return masked;
    }

    uint32_t tt_tile::get_indexed_num(uint32_t data_index)
    {
        uint32_t exp;
        uint32_t exp_word;
        uint32_t num_word;
        uint32_t sub_word_index;
        uint32_t sign;
        uint32_t num;
        uint32_t man;

        uint32_t out_num = 0;

        if(is_shared_exp_format(data_format))
        {
            uint32_t num_faces_x = ceil(float(tile_width) / 16);
            uint32_t num_faces_y = ceil(float(tile_height) / 16);
            bool partial_face = tile_height<16; // Tiles 1,2,4,8x32
            uint32_t exp_section_size = partial_face ? 1 * num_faces_y * 4 : num_faces_x * num_faces_y * 4;
            uint32_t tile_header_size = 4;
            exp_word = packed_data.at(4 + (data_index >> 6));
            sub_word_index = (data_index >> 4) & 0x3;
            exp = get_byte(exp_word, sub_word_index);

            if ((data_format == DataFormat::Bfp2_b) || (data_format == DataFormat::Bfp2)) {

                num_word = packed_data.at(tile_header_size + exp_section_size + (data_index >> 4));
                sub_word_index = data_index & 0xf;
                num = get_quarter_byte(num_word, sub_word_index);

                sign = num >> 1;
                man = num & 0x1;

                // Shift mantissa up until there is a 1 in bit 1
                int shift_cnt=0;
                if(man == 0)
                {
                    man = 0;
                    exp = 0;
                }
                else
                {
                    // shift again to put first non-hidden mantissa
                    // bit in bit 1
                    man = man << 1;
                    man = man & 0x1;

                    // adjust exponent
                    log_assert(exp >= (uint32_t) shift_cnt, "incorrect shift_cnt");
                    exp = exp - shift_cnt;

                    //if exp_a rebias exp to 127
                    if (is_shared_exp_a_format()) 
                    {
                        exp=exp-15+127;
                    }                 
                }

                // put s, e, m together
                out_num = (sign << 31) | (exp << 23) | (man << 22);
            } else if ((data_format == DataFormat::Bfp4_b) || (data_format == DataFormat::Bfp4)) {

                num_word = packed_data.at(tile_header_size + exp_section_size + (data_index >> 3));
                sub_word_index = data_index & 0x7;
                num = get_half_byte(num_word, sub_word_index);

                sign = num >> 3;
                man = num & 0x7;

                // Shift mantissa up until there is a 1 in bit 3
                int shift_cnt=0;
                if(man == 0)
                {
                    man = 0;
                    exp = 0;
                }
                else
                {
                    while((man & 0x04) == 0)
                    {
                        man = man << 1;
                        shift_cnt++;
                    }
                    // shift one more time and zero the
                    // hidden top mantissa bit
                    // shift again to put first non-hidden mantissa
                    // bit in bit 3
                    man = man << 1;
                    man = man & 0x7;

                    // adjust exponent
                    log_assert(exp >= (uint32_t) shift_cnt, "incorrect shift_cnt");
                    exp = exp - shift_cnt;

                    //if exp_a rebias exp to 127
                    if (is_shared_exp_a_format()) 
                    {
                    exp=exp-15+127;
                    }                 
                }

                // put s, e, m together
                out_num = (sign << 31) | (exp << 23) | (man << 20);
            } else if ((data_format == DataFormat::Bfp8_b) || (data_format == DataFormat::Bfp8)) {
                num_word = packed_data.at(tile_header_size + exp_section_size + (data_index >> 2));
                sub_word_index = data_index & 0x3;
                num = get_byte(num_word, sub_word_index);

                sign = num >> 7;
                man = num & 0x7f;

                // Shift mantissa up until there is a 1 in bit 6
                int shift_cnt=0;
                if(man == 0)
                {
                    man = 0;
                    exp = 0;
                }
                else
                {
                    //shift_cnt = 6 - (31 - __builtin_clz(man));
                    //man = (man << (shift_cnt + 1)) & 0x7f;
                    while((man & 0x40) == 0)
                    {
                        man = man << 1;
                        shift_cnt++;
                    }
                    // shift one more time and zero the
                    // hidden top mantissa bit
                    // shift again to put first non-hidden mantissa
                    // bit in bit 7
                    man = man << 1;
                    man = man & 0x7f;

                    // adjust exponent
                    log_assert(exp >= (uint32_t) shift_cnt, "incorrect shift_cnt");
                    exp = exp - shift_cnt;

                    //if exp_a rebias exp to 127
                    if (is_shared_exp_a_format()) 
                    {
                        exp=exp-15+127;
                    }                 
                }
                

                // put s, e, m together
                out_num = (sign << 31) | (exp << 23) | (man << 16);
            }
        }
        else if ((data_format == DataFormat::Float16_b) || (data_format == DataFormat::Float16))// 16a/b
        {
            num_word = packed_data.at(4 + (data_index >> 1));
            sub_word_index = data_index & 0x1;
            num = get_half(num_word,sub_word_index);
            if(data_format == DataFormat::Float16_b)
            {
                out_num = num << 16;
            }
            else
            {
                // split into sign, mantissa, exponent
                sign = (0x8000 & num) >> 15;
                man = num & 0x3ff;
                exp = (num & 0x7C00) >> 10;

                // unbias and rebias exponent
                // no need to check ranges and saturate as target
                // range is larger than source
                if (exp == 0) {
                    exp = 0;    
                } else {
                    exp = exp - 15 + 127;
                }


                // re-assemble number
                out_num = (sign << 31) | (exp << 23) | (man << 13);
            }
        }
        else if(data_format == DataFormat::RawUInt16){
            num_word = packed_data.at(4 + (data_index >> 1));
            sub_word_index = data_index & 0x1;
            out_num = get_half(num_word, sub_word_index);
        }
        else if(data_format == DataFormat::RawUInt8) {
            num_word = packed_data.at(4 + (data_index >> 2));
            sub_word_index = data_index & 0x3;
            out_num = get_byte(num_word, sub_word_index);
        } 
        else if(data_format == DataFormat::Int8){
            num_word = packed_data.at(4 + (data_index >> 2));
            sub_word_index = data_index & 0x3;

            uint32_t raw_byte = get_byte(num_word, sub_word_index);
            // Convert back from device int8 representation to 2's complement host representation
            uint32_t magnitude = raw_byte & 0x7f;
            if(raw_byte & 0x80) {
                out_num = (~magnitude) + 1;
            }
            else {
                out_num = magnitude;
            }
        }
        else if(data_format == DataFormat::UInt8){
            num_word = packed_data.at(4 + (data_index >> 2));
            sub_word_index = data_index & 0x3;

            out_num = get_byte(num_word, sub_word_index);
        }
        else if(data_format == DataFormat::Int32){
            num_word = packed_data.at(4 + data_index);

            // Convert back from device int32 representation to 2's complement host representation
            uint32_t magnitude = num_word & 0x7fffffff;
            if(num_word & 0x80000000) {
                out_num = (~magnitude) + 1;
            }
            else {
                out_num = magnitude;
            }
        }
        else if (data_format == DataFormat::UInt16) {
            num_word = packed_data.at(4 + (data_index >> 1));
            sub_word_index = data_index & 0x1;
            out_num = get_half(num_word, sub_word_index);
        }
        else if(data_format == DataFormat::Lf8){
            num_word = packed_data.at(4 + (data_index >> 2));
            sub_word_index = data_index & 0x3;
            num = get_byte(num_word, sub_word_index);

            // split into sign, mantissa, exponent
            sign = (0x80 & num) >> 7;
            man = num & 0x3;
            exp = (num & 0x7C) >> 2;

            // unbias and rebias exponent
            // no need to check ranges and saturate as target
            // range is larger than source
            if (exp == 0) {
                exp = 0;    
            } else {
                exp = exp - 15 + 127;
            }

            // re-assemble number
            out_num = (sign << 31) | (exp << 23) | (man << 21);
        }
        else if(data_format == DataFormat::Fp8_e4m3){
            num_word = packed_data.at(4 + (data_index >> 2));
            sub_word_index = data_index & 0x3;
            num = get_byte(num_word, sub_word_index);

            // split into sign, mantissa, exponent
            sign = (0x80 & num) >> 7;
            man = num & 0x7; //3-bit man
            exp = (num & 0x78) >> 3; //4 bit exp

            // unbias and rebias exponent
            // no need to check ranges and saturate as target
            // range is larger than source
            if (exp == 0) {
                exp = 0;    
            } else {
                exp = exp - 7 + 127;
            }

            // re-assemble number
            out_num = (sign << 31) | (exp << 23) | (man << 20);
        }    
        else
        { //32 bit data
            num_word = packed_data.at(4 + (data_index));
            out_num = num_word;
        }
        return out_num;
    }

    // Silicon might dump inf numbers with all exponent bits equal to 1 and nonzero mantissa.
    // This pass checks the exponent bits and if all are equal to 1, resets mantissa to zero.
    // Currently only implemented for Float16A/B and Float32/TF32.
    inline uint32_t adjust_for_nan_as_inf(uint32_t num, DataFormat data_format) {

        bool skip_adjust = (data_format != DataFormat::Float16) && 
                           (data_format != DataFormat::Float16_b) && 
                           (data_format != DataFormat::Float32) && 
                           (data_format != DataFormat::Tf32);
        if (skip_adjust) {
            return num;
        }
        uint32_t mantissa_all_ones = (1 << 23) - 1;

        uint32_t exponent = (num >> 23) & 0xff;
        uint32_t mask = ~mantissa_all_ones;
        if (exponent == 0xff) {
            uint32_t mantissa = num & 0x7fffff;
            if (mantissa != 0) {
                cout << "WARNING: The exponent bits from value returned from device is all 1s with nonzero mantissa." << std::endl;
                cout << "WARNING: Zeroing out the mantissa bits to represent the number correctly as inf." << std::endl;
                num &= mask;
            }
        }
        return num;
    }
    
    /* 
       Used to take into account precision loss due to data format
       e.g. if input is BFP8, then there is offline conversion

       This routine converts to target data format and back 
       e.g. fp32 (activations) -> bfp8 -> fp32
    */
    void tt_tile::adjust_tile_for_accuracy(bool truncate_bfp_mantissa)
    {
        if(DataFormat::Float32 != data_format){
            pack_data(truncate_bfp_mantissa);
            packed_data_to_tile();
            clear_packed_data();
        }
    }

    tt_tile tt_tile::isclose(tt_tile &rhs, float target)
    {
        tt_tile tmp(data_format);

        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                float diff;
                float perc;
                diff = fabs(t[i][j] - rhs.t[i][j]);
                perc = 100 * diff / (fabs(t[i][j]));
                if(perc > target) tmp.t[i][j] = 0.0;
                else tmp.t[i][j] = 1.0;
            }
        }
        return(tmp);
    }

    static bool inf_compare(float a, float b) {
        log_assert(isinf(a) || isinf(b), "Expected at least one tensor to be inf");
        log_assert(!isnan(a) && !isnan(b), "Expected both tensors to contain no NANs");

        if (a == b) {
            return true;
        }

        if (signbit(a) != signbit(b)) {
            return false;
        }

        // Store the potential non-inf value in b
        if (!isinf(a) && isinf(b)) {
            swap(a, b);
        }

        int exp;
        frexp(b, &exp);

        // It's not big enough to be inf
        if (exp <= 126) {
            return false;
        }

        return true;
    }

    bool tt_tile::allclose(const tt_tile &rhs, const double rtol, const double atol, double pct_matched, bool print_diffs) const
    {
        bool close = true;
        int total_mismatches = 0;
        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                // Implements the C++ equivalent of the numpy allclose function:
                // https://numpy.org/doc/stable/reference/generated/numpy.isclose.html
                if (isnan(t[i][j]) or isnan(rhs.t[i][j])) {
                    bool nan_same = isnan(t[i][j]) and isnan(rhs.t[i][j]);
                    close &= nan_same;
                    total_mismatches += nan_same ? 0 : 1;
                } else if (isinf(t[i][j]) or isinf(rhs.t[i][j])) {
                    bool inf_same = inf_compare(t[i][j], rhs.t[i][j]);
                    close &= inf_same;
                    total_mismatches += inf_same ? 0 : 1;
                } else {
                    bool nums_close = fabs(t[i][j] - rhs.t[i][j]) <= (atol + rtol * fabs(rhs.t[i][j]));
                    double rel_error;
                    if (rhs.t[i][j] == 0.0) {
                        rel_error = (fabs(t[i][j] - rhs.t[i][j])) / numeric_limits<float>::min();
                    } else {
                        rel_error = (fabs(t[i][j] - rhs.t[i][j])) / fabs(rhs.t[i][j]);
                    }
                    total_mismatches += (!nums_close) ? 1 : 0;
                    if (print_diffs) {
                        if (nums_close) {
                            cout << "(" << i << ", " << j <<  ") lhs = " << t[i][j] << ", rhs = " << rhs.t[i][j] << ", pct error =  " << rel_error << endl;
                        } else {
                            cout << "-- Not close --> (" << i << ", " << j <<  ") lhs = " << t[i][j] << ", rhs = " << rhs.t[i][j] << ", pct error =  " << rel_error << endl;
                        }
                    }
                    close &= nums_close;
                }
            }
        }
        double total_nums = (double)(tt::constants::TILE_HEIGHT*tt::constants::TILE_WIDTH);
        double total_macthed_pct = (total_nums - (double)total_mismatches) / total_nums;
        cout << "Allclose total matched percentage:  " << fixed << setprecision(5) << total_macthed_pct << endl;
        cout << "Allclose total number of mismatches: " << total_mismatches << endl;

        if (total_macthed_pct >= pct_matched) {
            close = true;
        } else {
            cout << fixed << setprecision(5);
            cout << "\033[1;31m<EXIT-SIG> Failed due to match % " << total_macthed_pct << " being lower than pass threshhold of " << pct_matched << "\033[0m\n" << endl;
        }

        return(close);
    }

    /*
     Compare two tiles using Pearson correlation coefficient (PCC). PCC it is a measure of the linear correlation between two variables X and Y.
     It has a value between +1 and −1:
        1 indicates a strong positive relationship.
        -1 indicates a strong negative relationship.
        0 indicates no relationship at all.
    */
    bool tt_tile::pcc_compare(const tt_tile &rhs, const double rtol, const double atol, const double pass_pcc) const
    {
        bool pass = false;
        bool debug = false;
        if (fabs(pass_pcc) > 1.0) {
            throw runtime_error("tt_tile::pcc_compare: pcc_compare must be -1 <= x <= 1");
        }

        if ((rtol < 0.0) || (atol < 0.0)) {
            throw runtime_error("tt_tile::pcc_compare: rtol and atol must be non-negative!");
        }

        double sum_t1 = 0;
        double sum_t2 = 0;
        double square_sum_t1 = 0;
        double square_sum_t2 = 0;
        double sum_t1_t2 = 0;
        double total_data_count = (double)(tt::constants::TILE_HEIGHT*tt::constants::TILE_WIDTH);

        bool is_same_t1 = true;
        bool is_same_t2 = true;
        const double first_val_t1 = inf_to_num(static_cast<double>(t[0][0]));
        const double first_val_t2 = inf_to_num(static_cast<double>(rhs.t[0][0]));

        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                double num1 = (double)t[i][j];
                double num2 = (double)rhs.t[i][j];

                if (isinf(num1)) num1 = inf_to_num(num1);
                if (isinf(num2)) num2 = inf_to_num(num2);

                is_same_t1 = is_same_t1 && (num1 == first_val_t1);
                is_same_t2 = is_same_t2 && (num2 == first_val_t2);

                sum_t1 += num1;                  // Sum of elements of tile1
                square_sum_t1 += (num1 * num1);  // Sum of square of tile1 elements.
                sum_t2 += num2;                  // Sum of elements of tile2
                square_sum_t2 += (num2 * num2);  // Sum of square of tile2 elements.
                sum_t1_t2 +=(num1 * num2);
            }
        }

        double mean_t1 = sum_t1 / total_data_count;
        double mean_t2 = sum_t2 / total_data_count;
        double var_t1 = (square_sum_t1/total_data_count) - (mean_t1 * mean_t1);
        double var_t2 = (square_sum_t2/total_data_count) - (mean_t2 * mean_t2);
        double mean_delta = fabs(mean_t1 - mean_t2);
        // use allclose condition to prevent false pass
        bool pass_threshold = (mean_delta <= (atol + rtol*mean_t2));
        bool tiles_identical = pass_threshold && is_same_t1 && is_same_t2;

        // Handle rounding error cases when near identical numbers are seen
        double ratio_t1 = (mean_t1 * mean_t1) / (square_sum_t1/total_data_count);
        double ratio_t2 = (mean_t2 * mean_t2) / (square_sum_t2/total_data_count);
        bool rnd_err_t1 = (var_t1 < 0 && ratio_t1 >= 1.00 && ratio_t1 <= 1.0000001);
        bool rnd_err_t2 = (var_t2 < 0 && ratio_t2 >= 1.00 && ratio_t2 <= 1.0000001);
        if (rnd_err_t1) var_t1 = 0.0f;
        if (rnd_err_t2) var_t2 = 0.0f;

        double stdev_t1 = sqrt(var_t1);  // Standard deviation for tile1
        double stdev_t2 = sqrt(var_t2);  // Standard deviation for tile2
        double cov_t1_t2 = (sum_t1_t2/total_data_count) - (mean_t1 * mean_t2);    // Covariance of tile1 and tile2

        double ratio_cov = (mean_t1 * mean_t2) / (sum_t1_t2/total_data_count);
        bool rnd_err_cov = (cov_t1_t2 < 0 && ratio_cov >= 1.00 && ratio_cov <= 1.0000001);
        if (rnd_err_cov) cov_t1_t2 = 0.0f;  // correct neg rounding error
        bool tiles_very_close = rnd_err_t1 && rnd_err_t2 && rnd_err_cov;

        if (stdev_t1 == 0.0) stdev_t1 = 0.00001;  // Adding a small offset if standard deviation is zero, to avoid div by 0 in PCC formula below
        if (stdev_t2 == 0.0) stdev_t2 = 0.00001;

        double pcc = (cov_t1_t2)/(stdev_t1 * stdev_t2);

        if (cov_t1_t2 == 0.0)
            cout << "tt_tile::pcc_compare: Covariance for two tiles is 0. Data in two tiles might be independent" << endl;

        cout << "Pearson's correlation coefficient:  " << fixed << setprecision(5) << pcc << endl;
        // cout << "Expected correlation coefficient:   " << fixed << setprecision(5) << pass_pcc << endl;

        bool sign_equal = signbit(pcc) == signbit(pass_pcc);
        if (fabs(pcc) >= fabs(pass_pcc) && sign_equal) {
            if (fabs(pcc) <= 1.0000001) {
                pass = true;
            } else {
                cout << "\033[1;31m<EXIT-SIG> PCC " << pcc << " greater than 1 is unexpected \033[0m\n" << endl;
                pass = false;
            }
        } else if (tiles_identical or tiles_very_close) {
            pass = true;
            if (debug) {
                cout << "Encountered a special condition for which we skip the Pearson correlation coefficient check:\n";
                cout << "   within each tile, all values in one tile are all the same except for a few very close datums \n";
                cout << "   resulting in a coefficient of effectively 0, thus Pearson's correlation is not defined.\n\n";
            }
        } else if (pass_pcc == 0.0) {
            pass = true;
            cout << "PCC check skipped..." << endl;
        } else {
            pass = false;
            cout << fixed << setprecision(5);
            cout << "\033[1;31m<EXIT-SIG> Failed due to PCC " << pcc << " being lower than pass threshhold of " << pass_pcc << "\033[0m\n" << endl;
        }
        if (debug or !pass) {
            cout << fixed << setw(10) << setprecision(3);
            cout << "tt_tile::pcc_compare: T1 -- sum: " << sum_t1 << ", mean: " << mean_t1
                 << " sq_sum:" << square_sum_t1 << ", var: " << var_t1 << ", stddev: " << stdev_t1 << endl;
            cout << "tt_tile::pcc_compare: T2 -- sum: " << sum_t2 << ", mean: " << mean_t2
                 << " sq_sum:" << square_sum_t2 << ", var: " << var_t2 << ", stddev: " << stdev_t2 << endl;
            cout << "tt_tile::pcc_compare: T1-T2 Covariance: " << cov_t1_t2 << endl << endl;
        }
        return pass;
    }

    void tt_tile::set_data_format(DataFormat data_formati)
    {
        data_format = data_formati;
    }

    void tt_tile::fill_from_row(const tt_tile &src, int row_index)
    {
        uint32_t i,j;
        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                t[i][j] = src.t[0][j];
            }
        }
    }

    bool tt_tile::operator==(const tt_tile &rhs) const {
        bool match = true;

        uint32_t i,j;
        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                if(t[i][j] == rhs.t[i][j]) {
                    continue;
                }

                if (isnan(t[i][j]) and isnan(rhs.t[i][j])) {
                    continue;
                }

                if (isnan(t[i][j])) {
                    match = false;
                    cout << "tile::operator==: value of input 0 is a NaN!" << endl;
                    goto done;
                } else if (t[i][j] == -numeric_limits<float>::infinity()) {
                    match = false;
                    cout << "tile::operator==: value of input 0 is a negative inf!" << endl;
                    goto done;
                }

                if (isnan(rhs.t[i][j])) {
                    match = false;
                    cout << "tile::operator==: value of input 1 is a NaN!" << endl;
                    goto done;
                } else if (rhs.t[i][j] == -numeric_limits<float>::infinity()) {
                    match = false;
                    cout << "tile::operator==: value of input 1 is a negative inf!" << endl;
                    goto done;
                }

                if(t[i][j] != rhs.t[i][j]) {
                    match = false;
                    goto done;
                }
            }
        }
        done:
        return(match);
    }

    tt_tile tt_tile::operator*(const tt_tile &rhs) const
    {
        tt_tile tmp;
        tmp.data_format = data_format;
        uint32_t i,j;
        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                float a = t[i][j];
                float b = rhs.t[i][j];
                if (a == 0.0 && isinf(b)) b = signbit(b) ? -numeric_limits<float>::max() : numeric_limits<float>::max();
                if (b == 0.0 && isinf(a)) a = signbit(a) ? -numeric_limits<float>::max() : numeric_limits<float>::max();
                tmp.t[i][j] = a * b;
            }
        }
        return(tmp);
    }


    tt_tile tt_tile::max(const tt_tile &rhs) const
    {
        tt_tile tmp;
        tmp.data_format = data_format;
        int i,j;
        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                tmp.t[i][j] = std::max(t[i][j], rhs.t[i][j]);
            }
        }
        return tmp;
    }

    tt_tile tt_tile::broadcast(Dim dim) const
    {
        tt_tile tmp;
        tmp.data_format = data_format;

        if (dim == Dim::R) {
            for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
            {
                for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
                {
                    tmp.t[i][j] = this->t[0][j];
                }
            }
        } else if (dim == Dim::C) {
            for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
            {
                for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
                {
                    tmp.t[i][j] = this->t[i][0];
                }
            }
        }  else if (dim == Dim::RC) {
            for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
            {
                for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
                {
                    tmp.t[i][j] =  this->t[0][0];
                }
            }
        } else {
            log_assert(false ,  "Bcast operation not implemented yet");
        }

        return(tmp);
    }

    tt_tile tt_tile::eltwise_binary_with_broadcast(const tt_tile &rhs, BinaryOp binary_op, Dim dim) const
    {
        tt_tile tmp;
        tmp.data_format = data_format;

        const map<BinaryOp, function<float(const float, const float)> > binary_op_to_function = {
                {BinaryOp::Add, plus<float>{}},
                {BinaryOp::Multiply, multiplies<float>{}},
                {BinaryOp::Subtract, minus<float>{}},
        };
        function<float(const float, const float)> function = binary_op_to_function.at(binary_op);

        if (dim == Dim::R) {

            for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
            {
                for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
                {
                    tmp.t[i][j] = function(t[i][j], rhs.t[0][j]); // dim col 0 to all other columns
                }
            }
        } else if (dim == Dim::C) {

            for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
            {
                for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
                {
                    tmp.t[i][j] = function(t[i][j], rhs.t[i][0]); // dim col 0 to all other columns
                }
            }
        }  else if (dim == Dim::RC) {

            for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
            {
                for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
                {
                    tmp.t[i][j] = function(t[i][j], rhs.t[0][0]); // dim col 0 to all other columns
                }
            }
        } else {
            log_assert(false ,  "Bcast operation not implemented yet");
        }

        return(tmp);
    }

    tt_tile tt_tile::matmul(const tt_tile &rhs) const
    {        
        tt_tile tmp(data_format, true);
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
                tmp.t[i][j] = acc;
            }
        }
        return(tmp);
    }

    tt_tile tt_tile::reduce(ReduceFunc reduce_func, Dim dim, float coefficient) const
    {
        float init_value = 0.0f;

        if (reduce_func == ReduceFunc::Avg or reduce_func == ReduceFunc::Sum)
        {
            init_value = 0.0f;
        }
        else if (reduce_func == ReduceFunc::Max)
        {
            init_value = -std::numeric_limits<float>::infinity();
        }
        else
        {
            log_fatal("Unsupported Reduce func");
        }

        const auto reduce_func_call = [reduce_func](float acc, float value)
        {
            if (reduce_func == ReduceFunc::Avg or reduce_func == ReduceFunc::Sum)
            {
                return acc + value;
            }
            else if (reduce_func == ReduceFunc::Max)
            {
                return std::max(acc, value);
            }
            else
            {
                log_fatal("Unsupported Reduce func");
            }
        };

        tt_tile tmp;
        tmp.data_format = data_format;

        if (dim == Dim::C) {
            for(int i = 0; i < tt::constants::TILE_HEIGHT; ++i)
            {
                float acc = init_value;
                for(int j = 0; j < tt::constants::TILE_WIDTH; ++j)
                {
                    acc = reduce_func_call(acc, t[i][j]);
                }
                tmp.t[i][0] = acc * coefficient;
            }
        } else if (dim == Dim::R) {
            for(int j = 0; j < tt::constants::TILE_WIDTH; ++j)
            {
                float acc = init_value;
                for(int i = 0; i < tt::constants::TILE_HEIGHT; ++i)
                {
                    acc = reduce_func_call(acc, t[i][j]);
                }
                tmp.t[0][j] = acc * coefficient;
            }
        } else if (dim == Dim::RC) {
            float acc = init_value;
            for(int i = 0; i < tt::constants::TILE_HEIGHT; ++i)
            {
                for(int j = 0; j < tt::constants::TILE_WIDTH; ++j)
                {
                    acc = reduce_func_call(acc, t[i][j]);
                }
            }
            tmp.t[0][0] = acc * coefficient;
        } else {
            throw runtime_error("Unsupported dim");
        }
        return(tmp);
    }

    static float calc_perc_err(float f1, float f2)
    {
        float diff = f1 - f2;
        float perc_err = 0.0;

        if ((f1 == 0.0)||((fabs(diff) / fabs(f1)) == 1.0))
            perc_err = fabs(diff);
        else
            perc_err = fabs(diff) / fabs(f1);

        return perc_err;
    }

    void tt_tile::print(std::ostream & os) const
    {
        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                os << fixed << setw(8) << setprecision(4) << this->t[i][j] << " ";
            }
            os << endl;
        }
    }

    void tt_tile::dump(bool raw) const {
        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                if (raw) {
                    cout << fixed << setw(2) << hex << "0x" << std::setfill('0');
                    cout << setw(8) << float_to_dword(this->t[i][j]) << " ";
                } else {
                    cout << fixed << setw(8) << setprecision(4) << this->t[i][j] << " ";
                }
            }
            cout << endl;
        }
    }

    std::string tt_tile::get_string() const {
        std::stringstream ss;
        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                ss << fixed << setw(8) << setprecision(4) << this->t[i][j] << " ";
            }
            ss << endl;
        }
        return ss.str();
    }

    uint32_t tt_tile::hash_one(std::uint32_t modulo_factor) const {
        // Simple hash function
        // Sums all values (reinterpreted as uint32_t) and returns the result (modulo modulo_factor)

        uint64_t hash_sum = 0;
        for (uint32_t i = 0; i < tt::constants::TILE_HEIGHT; i++)
        {
            for (uint32_t j = 0; j < tt::constants::TILE_WIDTH; j++)
            {
                hash_sum = (hash_sum + this->t_u32[i][j]) % modulo_factor;
            }
        }

        return hash_sum;
    }

    uint32_t tt_tile::hash_two(std::uint32_t modulo_factor) const {
        // Simple hash function
        // Multiples all values (reinterpreted as uint32_t), including their positions and returns the result (modulo
        // modulo_factor)

        uint64_t hash_prod = 1;
        for (uint32_t i = 0; i < tt::constants::TILE_HEIGHT; i++)
        {
            for (uint32_t j = 0; j < tt::constants::TILE_WIDTH; j++)
            {
                if (this->t_u32[i][j] != 0)
                {
                    hash_prod = (((hash_prod * this->t_u32[i][j]) % modulo_factor) * (i + 1) * (j + 1)) % modulo_factor;
                }
            }
        }

        return hash_prod;
    }

    void tt_tile::dump_packed()
    {
        if (packed_data_present()) {
            for (auto val : packed_data) {
                cout << hex << val << " ";
            }
            cout << dec << endl;
        } else {
            cout << "No Packed data present" << endl;
        }
    }


    string tt_tile::get_diff_string(const tt_tile& rhs, string lhs_header, string rhs_header) const{
        const char* c_normal  = "\x1b[0m";
        const char* c_red     = "\x1b[31m";
        const char* c_yellow  = "\x1b[33m";
        double thresh_red     = 0.30;
        double thresh_yellow  = 0.15;


        stringstream t1, t2;        
        t1 << "- " << lhs_header << ":"<< endl;
        t2 << "- " << rhs_header << ":" << endl;    

        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                float perc_err = calc_perc_err(this->t[i][j], rhs.t[i][j]);
                // cout << "Cacl perf: t1=" << this->t[i][j] << " t2=" << rhs.t[i][j] << " --> " << perc_err << endl;
                if (perc_err > thresh_red)
                {
                    t1 << c_red;
                    t2 << c_red;
                }
                else if (perc_err > thresh_yellow)
                {
                    t1 << c_yellow;
                    t2 << c_yellow;
                }
                t1 << fixed << setw(8) << setprecision(4) << this->t[i][j] << c_normal << " ";
                t2 << fixed << setw(8) << setprecision(4) << rhs.t[i][j] << c_normal << " ";
            }
            t1 << endl;
            t2 << endl;
        }
        return t1.str() + t2.str();
    }
    void tt_tile::dump_diff(const tt_tile& rhs) const
    {
        cout << get_diff_string(rhs);
    }

    void tt_tile::acc(int i, int j, float val)
    {
        log_assert((uint32_t) i<tt::constants::TILE_HEIGHT, "Out of bounds for tile height");
        log_assert((uint32_t) j<tt::constants::TILE_WIDTH, "Out of bounds for tile width");
        t[i][j] += val;
    }

    float tt_tile::get(int i, int j) const
    {
        log_assert(data_format != DataFormat::Invalid, "Invalid data format");
        log_assert((uint32_t) i<tt::constants::TILE_HEIGHT, "Out of bounds for tile height");
        log_assert((uint32_t) j<tt::constants::TILE_WIDTH, "Out of bounds for tile width");
        return(t[i][j]);
    }

    bool tt_tile::operator!=(const tt_tile &rhs) const {
        return(!operator==(rhs));
    }

    // Copy constructor  makes a deep copy of input tile
    tt_tile::tt_tile(const tt_tile &in_tile)
    {
        uint32_t i,j;
        data_format = in_tile.data_format;
        d_addr = in_tile.d_addr;
        d_chan = in_tile.d_chan;
        d_chip_id = in_tile.d_chip_id;
        host_resident = in_tile.host_resident;
        d_record_vld = in_tile.d_record_vld;
        log_assert(in_tile.data_format != DataFormat::Invalid, "Invalid data format");

        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                t[i][j] = in_tile.t[i][j];
            }
        }
        if (in_tile.packed_data_present()) {
            packed_data = in_tile.packed_data;
        }
    }

    tt_tile::tt_tile(const tt_tile &in_tile, DataFormat data_formati)
    {

        uint32_t i,j;
        data_format = data_formati;
        d_addr = in_tile.d_addr;
        d_chan = in_tile.d_chan;
        d_chip_id = in_tile.d_chip_id;
	    host_resident = in_tile.host_resident;
        d_record_vld = in_tile.d_record_vld;

        for(i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                t[i][j] = in_tile.t[i][j];
            }
        }

        if (in_tile.packed_data_present()) {
            if (data_format != in_tile.data_format) {
                pack_data();
            } else {
                packed_data = in_tile.packed_data;
            }
        }
    }

    void tt_tile::randomize(float mean, float stddev) {
        // Use the same generator from tt_rnd_util which should be seeded once at the start of tests
        mt19937 &generator = tt::test::rand_gen;

        normal_distribution<float> distribution(mean,stddev);
        // Initializing with (0, 2) results in very large values coming into exp (e.g., softmax in BERT) and we get inf's at the output of exp
        //normal_distribution<float> distribution(0.0,2);

        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                t[i][j] = distribution(generator);
            }
        }
    }

    void tt_tile::randomize_uniform(float lower_bound, float upper_bound) {
        mt19937 &generator = tt::test::rand_gen;

        uniform_real_distribution<float> distribution(lower_bound, upper_bound);
        // Initializing with (0, 2) results in very large values coming into exp (e.g., softmax in BERT) and we get inf's at the output of exp
        //normal_distribution<float> distribution(0.0,2);

        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                t[i][j] = distribution(generator);
            }
        }
    }

    // Debug values to track locations of tiles and figure out data mismatches
    void tt_tile::init_to_xyz_values(int z)
    {
        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                t[i][j] = i * tt::constants::TILE_WIDTH + j + z * 1000;
            }
        }
    }

    void tt_tile::randomize_manual_float(int spread, int man_variance_bits) {
        // Use the same generator from tt_rnd_util which should be seeded once at the start of tests
        mt19937 &generator = tt::test::rand_gen;

        int man_max = (1 << man_variance_bits) - 1;
        uint32_t man_shift = 23 - man_variance_bits;
        uniform_int_distribution<uint32_t> exp_distribution(127-spread,128+spread);
        // uniform_int_distribution<uint32_t> man_distribution(0,8388600);
        uniform_int_distribution<uint32_t> man_distribution(0,man_max);
        uniform_int_distribution<uint32_t> sign_distribution(0,1);

        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                uint32_t sign = sign_distribution(generator);
                uint32_t exp = exp_distribution(generator);
                uint32_t man = man_distribution(generator);
                log_assert((sign == 0) || (sign == 1), "expected binary value for sign");
                log_assert((exp >= (uint32_t) (127-spread)) && (exp <= (uint32_t) (128+spread)), "Incorrect value for exp");
                // log_assert((man < 8388607));
                log_assert((man < (uint32_t) (man_max+4)), "Incorrect value for mantissa");
                uint32_t num = (sign << 31) | (exp << 23) | (man << man_shift);
                float bla = dword_to_float(num);
                t[i][j] = bla;
            }
        }
    }

    void tt_tile::randomize_per_col_mask(int spread, int man_variance_bits, vector<bool> &col_mask) {
        // Entire column must have only 1 non-zero element
        // Allowed locations for a non-zero element are provided in the col_mask vector
        // Side-effect of the function is the update to the col_mask structure

        // Use the same generator from tt_rnd_util which should be seeded once at the start of tests
        mt19937 &generator = tt::test::rand_gen;

        uint32_t man_max = (1 << man_variance_bits) - 1;
        uint32_t man_shift = 23 - man_variance_bits;
        uniform_int_distribution<uint32_t> exp_distribution(127-spread,127+spread);
        // uniform_int_distribution<uint32_t> man_distribution(0,8388600);
        uniform_int_distribution<uint32_t> man_distribution(0,man_max);
        uniform_int_distribution<uint32_t> sign_distribution(0,1);
        uniform_int_distribution<uint32_t> index_distribution(0,tt::constants::TILE_HEIGHT-1);

        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                t[i][j] = 0.0;
            }
        }

        vector<bool> all_false(tt::constants::TILE_HEIGHT, false);
        log_assert(col_mask.size() == tt::constants::TILE_HEIGHT, "out of bounds for tile_height");
        for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
        {

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
            } while ((exp < (uint32_t) (127-spread)) || (exp > (uint32_t) (127+spread)));
            do {
                man = (man_max == 0) ? 0 : man_distribution(generator);
            } while (man > man_max);

            uint32_t num = (sign << 31) | (exp << 23) | (man << man_shift);
            float bla = dword_to_float(num);
        
            // log_assert(col_mask[j].size() == tt::constants::TILE_HEIGHT);
            // log_assert(col_mask != all_false);
            if (col_mask != all_false) {
                bool row_index_allowed = false;
                int row_index;
                while (row_index_allowed == false) {
                    row_index = index_distribution(generator);
                    row_index_allowed = col_mask[row_index] && ((uint32_t) row_index < tt::constants::TILE_HEIGHT) && (row_index >= 0);
                }
                t[row_index][j] = bla;
                col_mask[row_index] = false;
            }
        }

        // check if at most 1 number is non-zero for every column
        for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
        {
            bool found_non_zero = false;
            for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
            {
                if (t[i][j] != 0.0) {
                    log_assert(!found_non_zero, "Expected max of one zero element");
                    found_non_zero = true;
                }
            }
        }
    }

    void tt_tile::randomize_diagonal(float mean, float stddev) {
        // Use the same generator from tt_rnd_util which should be seeded once at the start of tests
        mt19937 &generator = tt::test::rand_gen;

        normal_distribution<float> distribution(mean, stddev);

        memset(t, 0, sizeof(t));
        for (uint32_t i = 0; i < tt::constants::TILE_HEIGHT; ++i) {
            t[i][i] = distribution(generator);
        }
    }

    void tt_tile::set_number(float num)
    {
        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                 t[i][j] = num;
            }
        }
    }

    tt_tile tt_tile::zero_tile(DataFormat dataformati)
    {
        tt_tile t(dataformati);
        t.set_number(0.0);
        return t;
    }


    void tt_tile::set_tile_rc(int r, int c, float val)
    {
        t[r][c] = val;
    }

    void tt_tile::adjust_man_precision(uint32_t new_man_prec) {
        log_assert(new_man_prec <= 23, "incorrect precision");
        uint32_t num_mask = ~((1 << (23-new_man_prec)) - 1);
        for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
        {
            for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
            {
                uint32_t val_u = float_to_dword(t[i][j]);
                t[i][j] = dword_to_float(val_u & num_mask);
            }
        }
    }

    void tt_tile::clear_packed_data() { packed_data.clear(); }

    void tt_tile::clear_values(int dim_r, int dim_c) {
        if (dim_r == tt::constants::TILE_HEIGHT && dim_c == tt::constants::TILE_WIDTH) {
            return;
        }

        log_assert(dim_r > 0 && dim_r <= tt::constants::TILE_HEIGHT, "Invalid r dim {}.", dim_r);
        log_assert(dim_c > 0 && dim_c <= tt::constants::TILE_WIDTH, "Invalid c dim {}.", dim_c);

        tt_tile mask;
        for (int i = 0; i < dim_r; i++) {
            for (int j = 0; j < dim_c; j++) {
                mask.t[i][j] = 1.0f;
            }
        }

        *this = *this * mask;
    }

namespace math {

tt_tile unary(function<float(float)> func, const tt_tile& input_tile) {
    tt_tile output_tile;
    output_tile.data_format = input_tile.data_format;

    for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i) {
        for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j) {
            output_tile.t[i][j] = func(input_tile.t[i][j]);
        }
    }
    return(output_tile);
}

tt_tile binary(function<float(float,float)> func, const tt_tile& input_tile0, const tt_tile& input_tile1) {
    tt_tile output_tile;
    output_tile.data_format = input_tile0.data_format;

    for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i) {
        for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j) {
            output_tile.t[i][j] = func(input_tile0.t[i][j], input_tile1.t[i][j]);
        }
    }
    return(output_tile);
}

namespace utils {
    void dump_bits(float x) {
        union {
            float input; // assumes sizeof(float) == sizeof(int)
            int   output;
        } data;
        data.input = x;
        uint32_t m = data.output & 0x007fffff;
        uint32_t e = (data.output & 0x7F800000) >> 23;
        uint32_t s = (data.output & 0x80000000) >> 31;
        cout << "float = " << x << hex << ", s:" << s << " e:" << e << " m:" << m << endl;
    }
}

namespace utils {
float exp(float x) {
    return std::min(std::numeric_limits<float>::max(), std::exp(x));
};
} // namespace utils

tt_tile exp(const tt_tile& input_tile) {
    return unary(utils::exp, input_tile);
}

namespace utils {
float log(float x) {
    if (x == 0.0f) {
        x += std::numeric_limits<float>::epsilon();
    }
    return std::log(x);
};
} // namespace utils

tt_tile log(const tt_tile& input_tile) {
    return unary(utils::log, input_tile);
}

namespace utils {
float sqrt(float x) {
    if (isnan(x)) {
        dump_bits(x);
        throw runtime_error("sqrt: input x is a NaN!");
    }
    return std::sqrt(x);
};
} // namespace utils
tt_tile sqrt(const tt_tile& input_tile) {
    return unary(utils::sqrt, input_tile);
}

namespace utils {
float abs(float x) {
    return std::fabs(x);
};
} // namespace utils
tt_tile abs(const tt_tile& input_tile) {
    return unary(utils::abs, input_tile);
}

namespace utils {
float gelu(float x) {
    return 0.5 * x * (1 + std::tanh((M_2_SQRTPI / M_SQRT2) * (x + 0.044715 * std::pow(x, 3))));
};
} // namespace utils
tt_tile gelu(const tt_tile& input_tile) {
    return unary(utils::gelu, input_tile);
}

namespace utils {
float relu(float x) {
    return (x >= 0) ? x : 0;
};
} // namespace utils
tt_tile relu(const tt_tile& input_tile) {
    return unary(utils::relu, input_tile);
    
}

tt_tile relu_with_threshold(const tt_tile& input_tile, float threshold, ReluMode mode) {
    if (mode == ReluMode::Max) {
       return unary([threshold](float x) { return (x > threshold) ? threshold : ((x<0) ? 0 : x); }, input_tile);
    } else {
       return unary([threshold](float x) { return (x > threshold) ? ((x<0) ? 0 : x) : 0; }, input_tile);
    }
}

namespace utils {
float gelu_derivative(float x) {
    float intermediate_0 = 0.5 * (1 + std::tanh((M_2_SQRTPI / M_SQRT2) * (x + 0.044715 * std::pow(x, 3))));
    float intermediate_1 = x * std::exp(-0.5 * x * x) * (0.5 * M_2_SQRTPI / M_SQRT2);
    return intermediate_0 + intermediate_1;
}
} // namespace utils
tt_tile gelu_derivative(const tt_tile& input_tile) {
    return unary(utils::gelu_derivative, input_tile);
}

namespace utils {
float reciprocal(float x) {
    if (x == 0.0f) {
        x += std::numeric_limits<float>::epsilon();
    }
    float value = 1.0 / x;
    if (isnan(value)) {
        dump_bits(value);
        throw runtime_error("reciprocal: value is a NaN!");
    } else if (value == -numeric_limits<float>::infinity()) {
        throw runtime_error("reciprocal: value is a negative inf!");
    }
    return value;
};
} // namespace utils
tt_tile reciprocal(const tt_tile& input_tile) {
    return unary(utils::reciprocal, input_tile);
}
namespace utils {
float tanh(float x) {
    return std::tanh(x);
};
} // namespace utils

tt_tile tanh(const tt_tile& input_tile) {
    return unary(utils::tanh, input_tile);
}

namespace utils {
float sigmoid(float x) {
    return 1.0 / (1.0 + exp(-x));
};
} // namespace utils
tt_tile sigmoid(const tt_tile& input_tile) {
    return unary(utils::sigmoid, input_tile);
}

namespace utils {
float max(float x, float y) {
    return ((x>y) ? x : y);
};
} // namespace utils
tt_tile max(const tt_tile& input_tile0, const tt_tile& input_tile1) {
    return binary(utils::max, input_tile0, input_tile1);
    //tt_tile tmp;
    //for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
    //{
    //    for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
    //    {
    //        tmp[i][j] = utils::max(input_tile0[i][j], max_tile1[i][j]);
    //    }
    //}
}

namespace utils {
float square(float x) {
    return (x*x);
};
} // namespace utils
tt_tile square(const tt_tile& input_tile) {
    return unary(utils::square, input_tile);
}

namespace utils {
float sine(float x) {
    return std::sin(x);
};
} // namespace utils
tt_tile sine(const tt_tile& input_tile) {
    return unary(utils::sine, input_tile);
}


namespace utils {
float cosine(float x) {
    return std::cos(x);
};
} // namespace utils
tt_tile cosine(const tt_tile& input_tile) {
    return unary(utils::cosine, input_tile);
}



namespace utils {
float lrelu(float x, float slope) {
    return (x > 0.0f) ? x : x * slope;  // [x*slope, x];
};
} // namespace utils
tt_tile lrelu(const tt_tile& input_tile, const float& slope) {
    tt_tile tmp(input_tile.data_format);
    for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
    {
        for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
        {
            tmp.t[i][j] = utils::lrelu(input_tile.t[i][j], slope);
        }
    }
    return tmp;
}
namespace utils {
bool tile_is_zeroed_out(float prob) {
    return ((float) rand() / RAND_MAX) < prob;
};
float zero(float x) {
    return 0.0;
};
float identity(float x) {
    return x;
}
} // namespace utils
tt_tile dropout(const tt_tile& input_tile, float prob) {
    if (utils::tile_is_zeroed_out(prob)) {
        return unary(utils::zero, input_tile);
    }
    return unary(utils::identity, input_tile);
}

tt_tile power(const tt_tile& input_tile, int exponent) {
    tt_tile tmp(input_tile.data_format);
    for(uint32_t i=0;i<tt::constants::TILE_HEIGHT;++i)
    {
        for(uint32_t j=0;j<tt::constants::TILE_WIDTH;++j)
        {
            tmp.t[i][j] = 1;
            for (int e=0; e<exponent; e++) {
               tmp.t[i][j]*=input_tile.t[i][j];
            }
        }
    }
    return tmp;
}
}  // namespace math
} // end namespace tt
