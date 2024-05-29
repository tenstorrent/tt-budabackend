// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// The implementation of read_tile given here is taken from
// budabackend/model/tile.cpp

#include "dbdserver/read_tile.hpp"

namespace tt::dbd::tile {

constexpr uint32_t TILE_HEIGHT = 32;
constexpr uint32_t TILE_WIDTH = 32;

TileDataFormat to_data_format(uint8_t i) {
    constexpr std::array<TileDataFormat, 12> supported_formats = {
        TileDataFormat::Float32, TileDataFormat::Float16,   TileDataFormat::Bfp8,   TileDataFormat::Bfp4,
        TileDataFormat::Tf32,    TileDataFormat::Float16_b, TileDataFormat::Bfp8_b, TileDataFormat::Bfp4_b,
        TileDataFormat::Lf8,     TileDataFormat::Bfp2,      TileDataFormat::UInt16, TileDataFormat::Int8,
    };

    for (TileDataFormat format : supported_formats)
        if (format == (TileDataFormat)i) return format;
    return TileDataFormat::Invalid;
}

bool is_shared_exp_format(TileDataFormat data_format) {
    return ((data_format == TileDataFormat::Bfp8) || (data_format == TileDataFormat::Bfp8_b) ||
            (data_format == TileDataFormat::Bfp4) || (data_format == TileDataFormat::Bfp4_b) ||
            (data_format == TileDataFormat::Bfp2) || (data_format == TileDataFormat::Bfp2_b));
}

bool is_shared_exp_a_format(TileDataFormat data_format) {
    return ((data_format == TileDataFormat::Bfp8) || (data_format == TileDataFormat::Bfp4) ||
            (data_format == TileDataFormat::Bfp2));
}

static __attribute__((always_inline)) inline uint32_t get_byte(uint32_t word, uint32_t index) {
    uint32_t mask = 0xff << (8 * index);
    uint32_t masked = word & mask;
    masked = masked >> (8 * index);
    return masked;
}

static __attribute__((always_inline)) inline uint32_t get_half_byte(uint32_t word, uint32_t index) {
    // log_assert(index < 8, "Index out of bounds");
    uint32_t mask = 0xf << (4 * index);
    uint32_t masked = word & mask;
    masked = masked >> (4 * index);
    return masked;
}

static __attribute__((always_inline)) inline uint32_t get_quarter_byte(uint32_t word, uint32_t index) {
    // log_assert(index < 16, "Index out of bounds");
    uint32_t mask = 0x3 << (2 * index);
    uint32_t masked = word & mask;
    masked = masked >> (2 * index);
    return masked;
}

static __attribute__((always_inline)) inline uint32_t get_half(uint32_t word, uint32_t index) {
    // log_assert(index < 2, "Index out of bounds");
    uint32_t mask = 0xffff << (16 * index);
    uint32_t masked = word & mask;
    masked = masked >> (16 * index);
    return masked;
}

uint32_t get_indexed_num(uint32_t data_index, const std::vector<uint32_t>& packed_data, TileDataFormat data_format) {
    uint32_t exp;
    uint32_t exp_word;
    uint32_t num_word;
    uint32_t sub_word_index;
    uint32_t sign;
    uint32_t num;
    uint32_t man;

    uint32_t out_num = 0;

    if (is_shared_exp_format(data_format)) {
        // We alway use 32x32 tiles, with 2x2 faces
        uint32_t num_faces_x = 2;
        uint32_t num_faces_y = 2;
        // bool partial_face = tile_height<16; // Tiles 1,2,4,8x32
        uint32_t exp_section_size =
            num_faces_x * num_faces_y * 4;  // partial_face ? 1 * num_faces_y * 4 : num_faces_x * num_faces_y * 4;
        uint32_t tile_header_size = 4;
        exp_word = packed_data.at(4 + (data_index >> 6));
        sub_word_index = (data_index >> 4) & 0x3;
        exp = get_byte(exp_word, sub_word_index);

        if ((data_format == TileDataFormat::Bfp2_b) || (data_format == TileDataFormat::Bfp2)) {
            num_word = packed_data.at(tile_header_size + exp_section_size + (data_index >> 4));
            sub_word_index = data_index & 0xf;
            num = get_quarter_byte(num_word, sub_word_index);

            sign = num >> 1;
            man = num & 0x1;

            // Shift mantissa up until there is a 1 in bit 1
            int shift_cnt = 0;
            if (man == 0) {
                man = 0;
                exp = 0;
            } else {
                // shift again to put first non-hidden mantissa
                // bit in bit 1
                man = man << 1;
                man = man & 0x1;

                // adjust exponent
                // log_assert(exp >= (uint32_t) shift_cnt, "incorrect shift_cnt");
                exp = exp - shift_cnt;

                // if exp_a rebias exp to 127
                if (is_shared_exp_a_format(data_format)) {
                    exp = exp - 15 + 127;
                }
            }

            // put s, e, m together
            out_num = (sign << 31) | (exp << 23) | (man << 22);
        } else if ((data_format == TileDataFormat::Bfp4_b) || (data_format == TileDataFormat::Bfp4)) {
            num_word = packed_data.at(tile_header_size + exp_section_size + (data_index >> 3));
            sub_word_index = data_index & 0x7;
            num = get_half_byte(num_word, sub_word_index);

            sign = num >> 3;
            man = num & 0x7;

            // Shift mantissa up until there is a 1 in bit 3
            int shift_cnt = 0;
            if (man == 0) {
                man = 0;
                exp = 0;
            } else {
                while ((man & 0x04) == 0) {
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
                // log_assert(exp >= (uint32_t) shift_cnt, "incorrect shift_cnt");
                exp = exp - shift_cnt;

                // if exp_a rebias exp to 127
                if (is_shared_exp_a_format(data_format)) {
                    exp = exp - 15 + 127;
                }
            }

            // put s, e, m together
            out_num = (sign << 31) | (exp << 23) | (man << 20);
        } else if ((data_format == TileDataFormat::Bfp8_b) || (data_format == TileDataFormat::Bfp8)) {
            num_word = packed_data.at(tile_header_size + exp_section_size + (data_index >> 2));
            sub_word_index = data_index & 0x3;
            num = get_byte(num_word, sub_word_index);

            sign = num >> 7;
            man = num & 0x7f;

            // Shift mantissa up until there is a 1 in bit 6
            int shift_cnt = 0;
            if (man == 0) {
                man = 0;
                exp = 0;
            } else {
                // shift_cnt = 6 - (31 - __builtin_clz(man));
                // man = (man << (shift_cnt + 1)) & 0x7f;
                while ((man & 0x40) == 0) {
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
                // log_assert(exp >= (uint32_t) shift_cnt, "incorrect shift_cnt");
                exp = exp - shift_cnt;

                // if exp_a rebias exp to 127
                if (is_shared_exp_a_format(data_format)) {
                    exp = exp - 15 + 127;
                }
            }

            // put s, e, m together
            out_num = (sign << 31) | (exp << 23) | (man << 16);
        }
    } else if ((data_format == TileDataFormat::Float16_b) || (data_format == TileDataFormat::Float16))  // 16a/b
    {
        num_word = packed_data.at(4 + (data_index >> 1));
        sub_word_index = data_index & 0x1;
        num = get_half(num_word, sub_word_index);
        if (data_format == TileDataFormat::Float16_b) {
            out_num = num << 16;
        } else {
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
    } else if (data_format == TileDataFormat::RawUInt16) {
        num_word = packed_data.at(4 + (data_index >> 1));
        sub_word_index = data_index & 0x1;
        out_num = get_half(num_word, sub_word_index);
    } else if (data_format == TileDataFormat::RawUInt8) {
        num_word = packed_data.at(4 + (data_index >> 2));
        sub_word_index = data_index & 0x3;
        out_num = get_byte(num_word, sub_word_index);
    } else if (data_format == TileDataFormat::Int8) {
        num_word = packed_data.at(4 + (data_index >> 2));
        sub_word_index = data_index & 0x3;

        uint32_t raw_byte = get_byte(num_word, sub_word_index);
        // Convert back from device int8 representation to 2's complement host representation
        uint32_t magnitude = raw_byte & 0x7f;
        if (raw_byte & 0x80) {
            out_num = (~magnitude) + 1;
        } else {
            out_num = magnitude;
        }
    } else if (data_format == TileDataFormat::UInt8) {
        num_word = packed_data.at(4 + (data_index >> 2));
        sub_word_index = data_index & 0x3;

        out_num = get_byte(num_word, sub_word_index);
    } else if (data_format == TileDataFormat::Int32) {
        num_word = packed_data.at(4 + data_index);

        // Convert back from device int32 representation to 2's complement host representation
        uint32_t magnitude = num_word & 0x7fffffff;
        if (num_word & 0x80000000) {
            out_num = (~magnitude) + 1;
        } else {
            out_num = magnitude;
        }
    } else if (data_format == TileDataFormat::UInt16) {
        num_word = packed_data.at(4 + (data_index >> 1));
        sub_word_index = data_index & 0x1;
        out_num = get_half(num_word, sub_word_index);
    } else if (data_format == TileDataFormat::Lf8) {
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
    } else if (data_format == TileDataFormat::Fp8_e4m3) {
        num_word = packed_data.at(4 + (data_index >> 2));
        sub_word_index = data_index & 0x3;
        num = get_byte(num_word, sub_word_index);

        // split into sign, mantissa, exponent
        sign = (0x80 & num) >> 7;
        man = num & 0x7;          // 3-bit man
        exp = (num & 0x78) >> 3;  // 4 bit exp

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
    } else {  // 32 bit data
        num_word = packed_data.at(4 + (data_index));
        out_num = num_word;
    }
    return out_num;
}

float dword_to_float(uint32_t in) {
    union {
        float output;  // assumes sizeof(float) == sizeof(int)
        uint32_t input;
    } data;
    data.input = in;
    return (data.output);
}

std::optional<std::string> dump_tile(const std::vector<uint32_t>& mem_vector, TileDataFormat df) {
    // We don't support following data types:
    // RawUInt8, RawUInt16, RawUInt32
    // So we don't need to handle megarows here. For more details, see packed_data_to_tile() in
    // budabackend/model/tilize_untilize_api/host_untilize.cpp.
    // Furthermore, we don't implement any speedup tricks.

    uint32_t num;
    float fnum;
    int data_index;
    int tile_r;
    int tile_c;

    // We only support 32x32 tiles, with 2x2 faces
    uint32_t num_faces_x = 2;
    uint32_t num_faces_y = 2;
    uint32_t face_height = 16;
    uint32_t face_width = 16;

    float t[TILE_HEIGHT][TILE_WIDTH];

    for (int tr = 0; tr < num_faces_y; tr++)
        for (int tc = 0; tc < num_faces_x; tc++)
            for (int i = 0; i < face_height; i++)
                for (int j = 0; j < face_width; j++) {
                    data_index = tr * (face_height * face_width * num_faces_x) + tc * (face_height * face_width) +
                                 i * face_width + j;
                    num = get_indexed_num(data_index, mem_vector, df);
                    fnum = dword_to_float(num);

                    tile_r = tr * 16 + i;
                    tile_c = tc * 16 + j;

                    // Changed to account only for supported data formats
                    // if((data_format == tt::DataFormat::Int8) || (data_format == tt::DataFormat::Int32) ||
                    //    (data_format == tt::DataFormat::UInt8) || (data_format == tt::DataFormat::UInt16))
                    if ((df == TileDataFormat::Int8) || (df == TileDataFormat::UInt16)) {
                        // uint32_t raw_data = get_indexed_num(data_index);
                        t[tile_r][tile_c] = (float)(*reinterpret_cast<int32_t*>(&num));
                    } else {
                        // TODO: Check if neccessary?
                        // num = get_indexed_num(data_index);
                        // fnum = dword_to_float(num);
                        t[tile_r][tile_c] = fnum;
                    }
                }

    std::stringstream ss;
    for (uint32_t i = 0; i < TILE_HEIGHT; ++i) {
        for (uint32_t j = 0; j < TILE_WIDTH; ++j) {
            ss << std::fixed << std::setw(8) << std::setprecision(4) << t[i][j] << " ";
        }
        ss << std::endl;
    }
    return ss.str();
}

std::optional<std::string> read_tile_implementation(uint8_t chip_id, uint8_t noc_x, uint8_t noc_y, uint64_t address,
                                                    uint32_t size, uint8_t data_format, tt_device* device) {
    TileDataFormat df = to_data_format(data_format);
    std::vector<std::uint32_t> mem_vector;

    if (df != TileDataFormat::Invalid) {
        tt_cxy_pair target(chip_id, noc_x, noc_y);
        bool small_access = false;
        bool register_txn = true;  // FIX: This should not be register access, actually

        std::string tlb_to_use =
            "REG_TLB";  // register_txn ? "REG_TLB" : (small_access ? "SMALL_READ_WRITE_TLB" : "LARGE_READ_TLB");
        device->read_from_device(mem_vector, target, address, size, tlb_to_use);

        return dump_tile(mem_vector, df);
    } else {
        return {};
    }
}

}  // namespace tt::dbd::tile