// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/tensor.hpp"

#ifndef __ARM_ARCH
// Include AVX library for x86.
#include <immintrin.h>
#define PREFETCH(addr) _mm_prefetch((addr), _MM_HINT_T0)
#endif 

#include <stdexcept>

// This file contains the implementation of the host unpacker and untilizer.
// Unpacker: packed data on device -> tt_tile (used in tt_tensor)
// Untilizer: tt_tensor -> flattened Pytorch Tensor of appropriate format

// We have x86 optimized implementations (using AVX2 under #ifndef __ARM_ARCH) and scalar implementations for ARM.

/////////////////////////////////////////////////////////
//             Unpacker Implementation                 //
/////////////////////////////////////////////////////////

#ifndef __ARM_ARCH
// AVX optimized unpack functions
void tt_tile::unpack_fp32_and_fill_tile(){
    // Strips tile headers and "converts" FP32 to FP32. Then fills tile with the data.
    int data_index;
    int tile_r;
    int tile_c;
    for(int tr=0;tr<2;++tr)
    {
        for(int tc=0;tc<2;++tc)
        {
            for(int i=0;i<16;++i)
            {
                tile_r = tr * 16 + i;
                for(int j=0;j<16; j += 8)
                {
                    data_index = tr*512 + tc*256 + i*16 + j;
                    tile_c = tc * 16 + j;
                    // Offset data index by 4 to strip tile header. Cast data to FP32 (ps) and store in tile.
                    _mm256_storeu_ps(t[tile_r] + tile_c, _mm256_castsi256_ps(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(packed_data.data() + 4 + data_index))));
                }
            }
        }
    }
}

void tt_tile::unpack_fp16_and_fill_tile(){
    // Strips tile headers and "converts" FP16 to FP32. Then fills tile with the data.
    int data_index;
    int tile_r;
    int tile_c;
    const __m128i upper_bit_mask = _mm_set1_epi32(0xffff0000);
    const __m128i lower_bit_mask = _mm_set1_epi32(0x0000ffff);
    const __m128i right_shift = _mm_set_epi64x(0, 16);
    const __m256i permute_order = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
    for(int tr=0;tr<2;++tr)
    {
        for(int tc=0;tc<2;++tc)
        {
            for(int i=0;i<16;++i)
            {
                tile_r = tr * 16 + i;
                for(int j=0;j<16; j += 8)
                {
                    // Data is stored as uint32_t. Each uint32_t contains 2 FP16 values. Therefore we divide the data_index by 2 (compared to the value in unpack_fp32_and_fill_tile).
                    data_index = tr*256 + tc*128 + i*8 + j/2;
                    tile_c = tc * 16 + j;
                    __m128i vec1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(packed_data.data() + 4 + data_index)); // Take 4 uint32_t values. These are 8 FP16 values.
                    __m128i vec2 = vec1;
                    vec1 = _mm_and_si128(vec1, lower_bit_mask); // Extract the bottom 16 bits of each uint32_t as an FP16
                    vec2 = _mm_srl_epi32(_mm_and_si128(vec2, upper_bit_mask), right_shift); // Extract the top 16 bits of each uint32_t as an FP16.
                    __m256i ordered_data = _mm256_permutevar8x32_epi32(_mm256_set_m128i(vec2, vec1), permute_order); // Shuffle the data such that the order is correct. (uint32_t): AB CD EF GH -> (unordered FP16) A C E G B D F H -> (ordered FP16) A B C D E F G H
                    __m256i sign = _mm256_srl_epi32(_mm256_and_si256(ordered_data, _mm256_set1_epi32(0x8000)), _mm_set_epi64x(0, 15)); // Extract sign and shift
                    __m256i man = _mm256_and_si256(ordered_data, _mm256_set1_epi32(0x03ff)); //Extract mantissa and mask
                    __m256i exp = _mm256_srl_epi32(_mm256_and_si256(ordered_data, _mm256_set1_epi32(0x7C00)), _mm_set_epi64x(0, 10)); // Extract exp, mask and shift
                    // Bias and threshold exp
                    exp = _mm256_add_epi32(exp, _mm256_set1_epi32(112)); 
                    exp = _mm256_blendv_epi8(exp, _mm256_set1_epi32(0), _mm256_cmpeq_epi32(exp, _mm256_set1_epi32(112)));
                    // Convert 8 FP16s to 8 FP32s and store in ordered_data register
                    sign = _mm256_sll_epi32(sign, _mm_set_epi64x(0, 31));
                    exp = _mm256_sll_epi32(exp, _mm_set_epi64x(0, 23));
                    man = _mm256_sll_epi32(man, _mm_set_epi64x(0, 13));

                    ordered_data = _mm256_or_si256(_mm256_or_si256(sign, exp), man);
                    // Populate tile with FP32 data
                    _mm256_storeu_ps(t[tile_r] + tile_c,  _mm256_castsi256_ps(ordered_data));
                }
            }
        }
    }
}

void tt_tile::unpack_fp16b_and_fill_tile(){
    // Strips tile headers and "converts" FP16b to FP32. Then fills tile with the data.
    int data_index;
    int tile_r;
    int tile_c;
    const __m128i upper_bit_mask = _mm_set1_epi32(0xffff0000);
    const __m128i lower_bit_mask = _mm_set1_epi32(0x0000ffff);
    const __m128i left_shift = _mm_set_epi64x(0, 16);
    const __m256i permute_order = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
    for(int tr=0;tr<2;++tr)
    {
        for(int tc=0;tc<2;++tc)
        {
            for(int i=0;i<16;++i)
            {
                tile_r = tr * 16 + i;
                for(int j=0;j<16; j += 8)
                {
                    data_index = tr*256 + tc*128 + i*8 + j/2; //Same reasoning as unpack_fp16_and_fill_tile
                    tile_c = tc * 16 + j;
                    // Same procedure for extracting and reordering FP16 data as unpack_fp16_and_fill_tile: (uint32_t): AB CD EF GH -> (unordered FP16) A C E G B D F H -> (ordered FP16) A B C D E F G H
                    __m128i vec1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(packed_data.data() + 4 + data_index));
                    __m128i vec2 = vec1;
                    vec1 = _mm_sll_epi32(_mm_and_si128(vec1, lower_bit_mask), left_shift);
                    vec2 = _mm_and_si128(vec2, upper_bit_mask);
                    _mm256_storeu_ps(t[tile_r] + tile_c,  _mm256_castsi256_ps(_mm256_permutevar8x32_epi32(_mm256_set_m128i(vec2, vec1), permute_order))); //Simply store shuffled data in the tile (with an implicit left bit shift in vec2)
                }
            }
        }
    }
}

void tt_tile::unpack_bfp8_and_fill_tile(){
    // Strips tile headers and "converts" BFP8 and BFP8_b to FP32. Then fills tile with the data.
    int data_index;
    int tile_r;
    int tile_c;
    const vector<uint32_t> mask_vec = {0xff, 0xff00, 0xff0000, 0xff000000};
    const vector<uint32_t> shift_vec = {0, 8, 16, 24};
    const __m128i mask = _mm_loadu_si128(reinterpret_cast<const __m128i*>(mask_vec.data()));
    const __m128i shift = _mm_loadu_si128(reinterpret_cast<const __m128i*>(shift_vec.data()));
    __m256i rebias_offset = _mm256_setzero_si256();
    if(is_shared_exp_a_format()) rebias_offset = _mm256_set1_epi32(-112); // This rebias offset must be added if we are working with BFP8 format.
    uint32_t exp_word, sub_word_index;

    for(int tr=0;tr<2;++tr)
    {
        for(int tc=0;tc<2;++tc)
        {
            for(int i=0;i<16;++i)
            {
                tile_r = tr * 16 + i;
                for(int j=0;j<16; j += 8)
                {
                    tile_c = tc * 16 + j;
                    data_index = tr*128 + tc*64 + i*4 + j/4; // Each uint32_t contains 4 BFP8 values. Divide data index by 4.
                    exp_word = packed_data.at(4 + (data_index >> 4)); // Extract the uint32_t value that stores the shared exponent for this set of data. Each 32 bit word is shared amongst 64 datums
                    
                    sub_word_index = (data_index >> 2) & 0x3; // Extract the byte in which the shared exponent is stored. Each byte is shared amongst 16 datums.
                    __m256i exp_vector = _mm256_set1_epi32(tt_tile::get_byte(exp_word, sub_word_index)); // Replicate exp scalar in a vector
                    // Take 2 uint32_t values. These are 8 BFP8 values
                    __m128i first = _mm_set1_epi32(packed_data.at(20 + data_index)); // Replicate first uint32_t 4 times (one for each BFP8 value)
                    __m128i second = _mm_set1_epi32(packed_data.at(20 + data_index + 1)); //  Replicate second uint32_t 4 times
                    first = _mm_srlv_epi32(_mm_and_si128(first, mask), shift); // Extract each BFP8 from the first uint32_t
                    second = _mm_srlv_epi32(_mm_and_si128(second, mask), shift); // Extract each BFP8 from the second uint32_t
                    __m256i combined = _mm256_set_m128i(second, first); // Concatenate 2 128 vectors to 1 256
                    // Extract sign and mantissa (expo extracted above)
                    __m256i sign = _mm256_srl_epi32(combined,  _mm_set_epi64x(0, 7));
                    __m256i man = _mm256_and_si256(combined, _mm256_set1_epi32(0x7f));
                    // Initialize shift amount per datum to 0. This is incremented below.
                    __m256i shift_cnt = _mm256_setzero_si256();
                    __m256i select_mask = _mm256_cmpeq_epi32(man, shift_cnt); // This mask is used to set mantissa values to 0, if they start at 0.
                    __m256i man_shifted = man; // Initialize updated mantissa
                    for(int shift_val = 0; shift_val < 7; shift_val++){
                        // Shift each mantissa and update the corresponding shift_cnt until the 6th bit of the 8 bit data is set.
                        __m256i shift_mask = _mm256_or_si256(_mm256_cmpgt_epi32(man_shifted, _mm256_set1_epi32(0x40)), _mm256_cmpeq_epi32(man_shifted, _mm256_set1_epi32(0x40))); // If the 6th bit is set, propagate the current mantissa value. Else take the left shifted value
                        man_shifted = _mm256_blendv_epi8(_mm256_sll_epi32(man_shifted, _mm_set_epi64x(0, 1)), man_shifted, shift_mask);
                        shift_cnt = _mm256_blendv_epi8(_mm256_set1_epi32(shift_val + 1), shift_cnt, shift_mask); 
                    }
                    man_shifted = _mm256_and_si256(_mm256_sll_epi32(man_shifted, _mm_set_epi64x(0, 1)), _mm256_set1_epi32(0x7f)); // One more shift to clear 6th bit
                    man = _mm256_blendv_epi8(man_shifted, man, select_mask); // Choose new mantissa or keep old mantissa based on 0 initial condition.
                    // Assert if the exponent and corresponding mantissa for a datum are non-zero and the subtraction bias (shift_cnt) for that data is greater than the exponent value
                    log_assert(skip_bfp8_check ||
                                (!(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpgt_epi32(exp_vector, _mm256_setzero_si256()))) & _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpgt_epi32(shift_cnt, exp_vector))) & !_mm256_movemask_ps(_mm256_castsi256_ps(select_mask)))),
                                "Device returned incorrect data for Bfp8 formats: The Shift Count for a non-zero exponent is greater than the exponent value.");
                    exp_vector = _mm256_blendv_epi8(_mm256_sub_epi32(exp_vector, _mm256_add_epi32(rebias_offset, shift_cnt)),  _mm256_setzero_si256(), select_mask); // Choose new (rebiased exponent) or keep previous exponent based on mantissa intiial condition
                    
                    
                    sign = _mm256_sll_epi32(sign, _mm_set_epi64x(0, 31)); // Shift sign
                    exp_vector = _mm256_sll_epi32(exp_vector, _mm_set_epi64x(0, 23)); // Shift exp
                    man = _mm256_sll_epi32(man, _mm_set_epi64x(0, 16)); // Shift mantissa
                    man = _mm256_or_si256(sign, _mm256_or_si256(exp_vector, man)); // Store final value in mantissa register and save in tile memory
                    _mm256_storeu_ps(t[tile_r] + tile_c,  _mm256_castsi256_ps(man));
                }
            
            }
        }
    }
}

void tt_tile::unpack_raw32_and_fill_tile(){
    // Strips tile headers and fills tile with RawUInt32 data (megarow). 
    int data_index;
    for(int row = 0; row < 32; row++){
        for(int col = 0; col < 32; col += 8){
            data_index = 32*row + col;
            _mm256_storeu_ps(t[row] + col, _mm256_castsi256_ps(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(packed_data.data() + 4 + data_index))));
        }
    }
}

void tt_tile::unpack_raw16_and_fill_tile(){
    // Strips tile headers and fills tile with RawUint16 data (megarow).
    int data_index;
    const __m128i upper_bit_mask = _mm_set1_epi32(0xffff0000);
    const __m128i lower_bit_mask = _mm_set1_epi32(0x0000ffff);
    const __m128i right_shift = _mm_set_epi64x(0, 16);
    const __m256i permute_order = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
    for(int row = 0; row < 32; row++){
        for(int col = 0; col < 32; col += 8){
            data_index = 16*row + col/2;
            __m128i vec1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(packed_data.data() + 4 + data_index));
            __m128i vec2 = vec1;
            vec1 = _mm_and_si128(vec1, lower_bit_mask);
            vec2 = _mm_srl_epi32(_mm_and_si128(vec2, upper_bit_mask), right_shift); // Apply right shift to vec2 data so that the 16 bits specifying the value are in the lower half
            _mm256_storeu_ps(t[row] + col,  _mm256_castsi256_ps(_mm256_permutevar8x32_epi32(_mm256_set_m128i(vec2, vec1), permute_order))); //Simply store shuffled data in the tile
        }
    }
}
#endif

void tt_tile::packed_data_to_tile() {
    #ifndef __ARM_ARCH
    // If using x86, vector based unpack functions can be used.
    vector<DataFormat> formats_supporting_fast_unpack = {DataFormat::Float32, DataFormat::Float16_b, DataFormat::Float16, DataFormat::Bfp8, DataFormat::Bfp8_b, DataFormat::RawUInt32, DataFormat::RawUInt16};
    if(tile_height == 32 and tile_width == 32 and std::find(formats_supporting_fast_unpack.begin(), formats_supporting_fast_unpack.end(), data_format) != formats_supporting_fast_unpack.end() and !force_slow_untilize){
        // TODO: Add fast unpack support for arbitrary tile sizes - AS
        log_trace(tt::LogIO, "Using Fast Unpack");
        int data_index;
        if(data_format == DataFormat::RawUInt32){
            unpack_raw32_and_fill_tile();
        }
        else if(data_format == DataFormat::RawUInt16){
            unpack_raw16_and_fill_tile();
        }
        else if(data_format == DataFormat::Float32){
            unpack_fp32_and_fill_tile();
        }
        else if(data_format == DataFormat::Float16_b){
            unpack_fp16b_and_fill_tile();
        }

        else if (data_format == DataFormat::Float16){
            unpack_fp16_and_fill_tile();
        }
        else{
            unpack_bfp8_and_fill_tile();
        }
    }
    
    else{
    #endif
        // On ARM, we only use scalar functions
        log_trace(tt::LogIO, "Using Slow Unpack");
        uint32_t num;
        float fnum;
        int data_index;
        int tile_r;
        int tile_c;

        bool is_megarow = data_format == DataFormat::RawUInt8 or
                        data_format == DataFormat::RawUInt16 or
                        data_format == DataFormat::RawUInt32;

        if (is_megarow)
        {
            for(int i=0;i<32;++i)
            {
                for(int j=0;j<32;++j)
                {
                    data_index = 32*i + j;
                    t[i][j] = dword_to_float(get_indexed_num(data_index));
                }
            }
        }

        else{
            uint32_t num_faces_x = ceil(float(tile_width) / 16);
            uint32_t num_faces_y = ceil(float(tile_height) / 16);
            uint32_t face_height = std::min(tile_height, static_cast<uint32_t>(16));
            uint32_t face_width = std::min(tile_width, static_cast<uint32_t>(16));
            for(int tr = 0; tr < num_faces_y; tr++) {
                for(int tc = 0; tc < num_faces_x; tc++) {
                    for(int i = 0; i < face_height; i++) {
                        for(int j = 0; j < face_width; j++) {
                            data_index = tr * (face_height * face_width * num_faces_x) + tc * face_height * face_width + i * face_width + j;
                            num = get_indexed_num(data_index);
                            fnum = dword_to_float(num);
                            tile_r = tr * 16 + i;
                            tile_c = tc * 16 + j;
                            
                            if((data_format == tt::DataFormat::Int8) || (data_format == tt::DataFormat::Int32) ||
                                (data_format == tt::DataFormat::UInt8) || (data_format == tt::DataFormat::UInt16)) {
                                uint32_t raw_data = get_indexed_num(data_index);
                                t[tile_r][tile_c] = (float)(*reinterpret_cast<int32_t*>(&raw_data));
                            }
                            else {
                                num = get_indexed_num(data_index);
                                fnum = dword_to_float(num);
                                t[tile_r][tile_c] = fnum;
                            }
                        }
                    }
                }
            }
        }
    #ifndef __ARM_ARCH
    }
    #endif
}

 
void tt_tile::verify_tile_header() {
    if ((packed_data[0] << 4) != size_bytes(true)) {
        throw std::runtime_error("Invalid tile header. Expected tile size: " + std::to_string(size_bytes(true)) + 
            " but got: " + std::to_string(packed_data[0]));
    }
}


/////////////////////////////////////////////////////////
//             Untilizer Implementation                //
/////////////////////////////////////////////////////////  

// ********* Common functions used across both host architectures. *********
namespace tt {
namespace untilize_utils {
    void convert_device_int8_representation_to_host(uint8_t* host_storage, uint32_t tensor_size) {
        for(uint32_t idx = 0; idx < tensor_size; idx++) {
            if(host_storage[idx] & 0x80) {
                host_storage[idx] = 0x80 | (~(host_storage[idx] & 0x7f) + 1);
            }
        }
    }

    void convert_device_int32_representation_to_host(uint32_t* host_storage, uint32_t tensor_size) {
        for(uint32_t idx = 0; idx < tensor_size; idx++) {
            if(host_storage[idx] & 0x80000000) {
                host_storage[idx] = 0x80000000 | (~(host_storage[idx] & 0x7fffffff) + 1);
            }
        }
    }
}
}

void copy_to_flat_vector(vector<float>& flattened_tensor_data_vec, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width){
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        flattened_tensor_data_vec[dest_row_index + i_tile_col] = *((float*)(&(tile.t[i_tile_row][i_tile_col])));
    }
}

void copy_byte_to_flat_vector_int8(vector<int8_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width) {
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = (int8_t)(std::min(std::max(tile.t[i_tile_row][i_tile_col], float(-127)), float(127))) & 0x000000ff;
    }
}

void copy_byte_to_flat_vector_uint8(vector<uint8_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width) {
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = (uint8_t)(std::min(std::max(tile.t[i_tile_row][i_tile_col], float(0)), float(255)));
    }
}

void copy_dword_to_flat_vector_int32(vector<int32_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width) {
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = (int32_t)(std::min(std::max(tile.t[i_tile_row][i_tile_col], float(std::numeric_limits<int32_t>::min())), float(std::numeric_limits<int32_t>::max())))  & 0xffffffff;
    }
}

#ifndef __ARM_ARCH
// ********* x86 specific functions. *********
namespace tt {
namespace untilize_utils {
    void convert_device_int8_representation_to_host_vectorized(uint8_t* host_storage, uint32_t tensor_size) {
        // Vectorized converter to go from sign|magnitude int8 representation to 2's complement.
        const __m256i sign_mask = _mm256_set1_epi8(0x80);
        const __m256i offset = _mm256_set1_epi8(0x01);
        const __m256i mantissa_mask = _mm256_set1_epi8(0x7f);
        for(uint32_t idx = 0; idx < tensor_size; idx += 32) {
            __m256i input_data = _mm256_loadu_si256(reinterpret_cast<__m256i*>(host_storage + idx));
            __m256i is_negative = _mm256_and_si256(input_data, sign_mask);
            __m256i negative_input = _mm256_add_epi8(~_mm256_and_si256(input_data, mantissa_mask), offset);
            negative_input = _mm256_or_si256(negative_input, is_negative);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(host_storage + idx), _mm256_blendv_epi8(input_data, negative_input, is_negative));
        } 
    }
    void convert_device_int32_representation_to_host_vectorized(uint32_t* host_storage, uint32_t tensor_size) {
        // Vectorized converter to go from sign|magnitude int32 representation to 2's complement.
        const __m256i sign_mask = _mm256_set1_epi32(0x80000000);
        const __m256i offset = _mm256_set1_epi32(0x00000001);
        const __m256i mantissa_mask = _mm256_set1_epi32(0x7fffffff);
        for(uint32_t idx = 0; idx < tensor_size; idx += 8) {
            __m256i input_data = _mm256_loadu_si256(reinterpret_cast<__m256i*>(host_storage + idx));
            __m256i is_negative = _mm256_and_si256(input_data, sign_mask);
            __m256i negative_input = _mm256_add_epi32(~_mm256_and_si256(input_data, mantissa_mask), offset);
            negative_input = _mm256_or_si256(negative_input, is_negative);
            // Blend at 32 bit granularity...no AVX function for directly blending int32 use blend for packed singles (float) instead
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(host_storage + idx), _mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(input_data), _mm256_castsi256_ps(negative_input), _mm256_castsi256_ps(is_negative))));
        } 
    }
}
}

void copy_half_to_flat_vector_uint16(vector<uint16_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, __m256i& mask, __m128i& shift, __m256i& placeholder1, __m256i& placeholder2, int tile_width) {
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = (uint16_t)(std::min(std::max(tile.t[i_tile_row][i_tile_col], float(0)), float(65535)));
    }
}

void copy_half_to_flat_vector_raw_u16(vector<uint16_t>& to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, __m256i& mask, __m128i& shift, __m256i& placeholder1, __m256i& placeholder2, int tile_width){
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = tile.conv_u32_to_raw_u16(tt_tile::float_to_dword(tile.t[i_tile_row][i_tile_col]));
    }
}

void copy_half_to_flat_vector_u16b(vector<uint16_t>& to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, __m256i& mask, __m128i& shift, __m256i& placeholder1, __m256i& placeholder2, int tile_width){
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = tt_tile::conv_u32_to_u16b(tt_tile::float_to_dword(tile.t[i_tile_row][i_tile_col]));
    }
} 

void copy_half_to_flat_vector_u16(vector<uint16_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, __m256i& mantissa_mask, __m128i& placeholder, __m256i& exp_mask, __m256i& sign_mask, int tile_width){
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = tt_tile::conv_u32_to_u16(tt_tile::float_to_dword(tile.t[i_tile_row][i_tile_col]));
    }

}

void vectorized_copy_byte_to_flat_vector_int8(vector<int8_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width) {
    for(size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col += 8) {
        __m256 in = _mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col);
        __m256i int32_data = _mm256_cvtps_epi32(_mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col));;
        __m256i int16_data = _mm256_packs_epi32(int32_data, int32_data);
        __m256i int8_data = _mm256_packs_epi16(int16_data, int16_data);

        *(reinterpret_cast<uint32_t*>(to.data() + dest_row_index + i_tile_col)) = _mm256_extract_epi32(int8_data, 0);
        *(reinterpret_cast<uint32_t*>(to.data() + dest_row_index + i_tile_col + 4)) = _mm256_extract_epi32(int8_data, 4);
    }
}

void vectorized_copy_byte_to_flat_vector_uint8(vector<uint8_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width) {
    for(size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col += 8) {
        __m256 in = _mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col);
        __m256i int32_data = _mm256_cvtps_epi32(_mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col));;
        __m256i int16_data = _mm256_packus_epi32(int32_data, int32_data);
        __m256i int8_data = _mm256_packus_epi16(int16_data, int16_data);

        *(reinterpret_cast<uint32_t*>(to.data() + dest_row_index + i_tile_col)) = _mm256_extract_epi32(int8_data, 0);
        *(reinterpret_cast<uint32_t*>(to.data() + dest_row_index + i_tile_col + 4)) = _mm256_extract_epi32(int8_data, 4);
    }
}

void vectorized_copy_dword_to_flat_vector_int32(vector<int32_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width) {
    // Simple datacopy
    for(size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col += 8) {
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(to.data() + dest_row_index + i_tile_col), _mm256_cvtps_epi32(_mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col)));
    }
}

void vectorized_copy_word_to_flat_vector_uint16(vector<uint16_t>& to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, __m256i& mask, __m128i& shift, __m256i& placeholder1, __m256i& placeholder2, int tile_width) {
    for(size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col += 16) {
        // Load 256 bits (8 elements of float) from tile into a __m256 register
        __m256 first_half = _mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col);
        __m256 second_half = _mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col + 8);

        // Convert the 8 float values to 32-bit integers
        __m256i data_int32_first = _mm256_cvtps_epi32(first_half);
        __m256i data_int32_second = _mm256_cvtps_epi32(second_half);

        // Pack 32-bit integers into 16-bit integers
        __m128i pack_low = _mm_packus_epi32(_mm256_extractf128_si256(data_int32_first, 0), _mm256_extractf128_si256(data_int32_first, 1));
        __m128i pack_high = _mm_packus_epi32(_mm256_extractf128_si256(data_int32_second, 0), _mm256_extractf128_si256(data_int32_second, 1));

        // Store the results in the destination vector
        _mm_storeu_si128(reinterpret_cast<__m128i*>(to.data() + dest_row_index + i_tile_col), pack_low);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(to.data() + dest_row_index + i_tile_col + 8), pack_high);
    }
}

void vectorized_copy_half_to_flat_vector_raw_u16(vector<uint16_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, __m256i& mask, __m128i& shift, __m256i& placeholder1, __m256i& placeholder2, int tile_width){
    const __m256i shuffle_order = _mm256_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    const __m256i permute_order = _mm256_setr_epi32(0, 1, 4, 5, 2, 33, 6, 7);
    for(size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col += 8){
        __m256i in = _mm256_castps_si256(_mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col));

        in = _mm256_shuffle_epi8(in, shuffle_order);
        in = _mm256_permutevar8x32_epi32(in, permute_order); // Pack all the data together in the top 128 bits of the register. The lower 128 bits are junk data.
        __m128i truncated_data = _mm256_extractf128_si256(in, 0); // Copy the top 128 bits. This is a uint16_t representation of the data.
        _mm_storeu_si128(reinterpret_cast<__m128i*>(to.data() + dest_row_index + i_tile_col), truncated_data);
    }
}

void vectorized_copy_half_to_flat_vector_u16b(vector<uint16_t>& to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, __m256i& mask, __m128i& shift, __m256i& placeholder1, __m256i& placeholder2, int tile_width){
    // Fast AVX implementation of copy_half_to_flat_vector_u16b. Placeholder vars in the input args ensure that vectorized_copy_half_to_flat_vector_u16b and vectorized_copy_half_to_flat_vector_u16 can be assigned to the same function ptr.
    PREFETCH(tile.t[i_tile_row]);
    PREFETCH(tile.t[i_tile_row] + 8);
    PREFETCH(tile.t[i_tile_row] + 16);
    PREFETCH(tile.t[i_tile_row] + 24);
    const __m256i shuffle_order = _mm256_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    const __m256i permute_order = _mm256_setr_epi32(0, 1, 4, 5, 2, 33, 6, 7);
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col += 8) {
        // For each row load 8 datums at once. mask = 0xffffffff and shift = 16. _mm256_and_si256 and _mm256_srl_epi32 (right shift) correspond to the steps from conv_u32_to_u16b. Bit level ops performed after converting the float to an int (castps)
        __m256i in = _mm256_srl_epi32(_mm256_and_si256(_mm256_castps_si256(_mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col)), mask), shift);
        // Convert epi32 to epi16 and store the 8 epi16 datums in truncated_data: 
        in = _mm256_shuffle_epi8(in, shuffle_order); //Do this independently for the 2 128 bit lanes: Place the lower 16 bits from each of the 4 epi32 values in the top half (64 bit) of the 128 bit lane.
        in = _mm256_permutevar8x32_epi32(in, permute_order); // Pack all the data together in the top 128 bits of the register. The lower 128 bits are junk data.
        __m128i truncated_data = _mm256_extractf128_si256(in, 0); // Copy the top 128 bits. This is a uint16_t representation of the data.
        _mm_storeu_si128(reinterpret_cast<__m128i*>(to.data() + dest_row_index + i_tile_col), truncated_data);
    }
}

void vectorized_copy_half_to_flat_vector_u16(vector<uint16_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, __m256i& mantissa_mask, __m128i& placeholder, __m256i& exp_mask, __m256i& sign_mask, int tile_width){
    // Fast AVX implementation of copy_half_to_flat_vector_u16. Placeholder vars in the input args ensure that vectorized_copy_half_to_flat_vector_u16b and vectorized_copy_half_to_flat_vector_u16 can be assigned to the same function ptr.
    PREFETCH(tile.t[i_tile_row]);
    PREFETCH(tile.t[i_tile_row] + 8);
    PREFETCH(tile.t[i_tile_row] + 16);
    PREFETCH(tile.t[i_tile_row] + 24);
    const __m256i shuffle_order = _mm256_setr_epi8(0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                0, 1, 4, 5, 8, 9, 12, 13,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF);
    const __m256i permute_order = _mm256_setr_epi32(0, 1, 4, 5, 2, 33, 6, 7);
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col += 8) {
        __m256i data_int = _mm256_castps_si256(_mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col)); // cast float to epi32
        __m256i m = _mm256_and_si256(data_int, mantissa_mask); // extract mantissa
        __m256i e = _mm256_sub_epi32(_mm256_srl_epi32(_mm256_and_si256(data_int, exp_mask), _mm_set_epi64x(0, 23)), _mm256_set1_epi32(127)); // extract exponent, shift and bias
        __m256i s = _mm256_srl_epi32(_mm256_and_si256(data_int, sign_mask), _mm_set_epi64x(0, 16)); // extract sign and shift (uint16_t) format
        //////// This corresponds to the if and else block in conv_u32_to_u16 ////////
        __m256i cmp_mask = _mm256_cmpgt_epi32(e, _mm256_set1_epi32(-15));
        e =  _mm256_blendv_epi8(_mm256_set1_epi32(-15), e, cmp_mask);
        m = _mm256_blendv_epi8(_mm256_set1_epi32(0), m, cmp_mask);
        cmp_mask = _mm256_cmpgt_epi32(e, _mm256_set1_epi32(16));
        e = _mm256_blendv_epi8(e, _mm256_set1_epi32(16), cmp_mask);
        m = _mm256_srl_epi32(_mm256_blendv_epi8(m, mantissa_mask, cmp_mask), _mm_set_epi64x(0, 13));
        /////////////////////////////////////////////////////////////////////////////
        e = _mm256_sll_epi32(_mm256_add_epi32(e, _mm256_set1_epi32(15)), _mm_set_epi64x(0, 10)); // Bias exponent again and shift
        s = _mm256_or_si256(_mm256_or_si256(s, e), m); // Store the final value in the sign register (saves a register)
        // Same procedure as vectorized_copy_half_to_flat_vector_u16b: convert epi32 to epi16 and store the 8 epi16 datums in truncated_data
        s = _mm256_shuffle_epi8(s, shuffle_order);
        s = _mm256_permutevar8x32_epi32(s, permute_order);
        __m128i truncated_data = _mm256_extractf128_si256(s, 0);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(to.data() + dest_row_index + i_tile_col), truncated_data);
    }
}


void vectorized_copy_to_flat_vector(vector<float>& flattened_tensor_data_vec, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width){
    // Fast AVX implementation of copy_half_to_flat_vector. Copies 8 float values (ps) to flattened_tensor_data_vec at once.
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col += 8) {
        _mm256_storeu_ps(flattened_tensor_data_vec.data() + dest_row_index + i_tile_col, _mm256_loadu_ps(tile.t[i_tile_row] + i_tile_col));
    }
}


void tt_tensor::untilize_to_flat_tensor_data(bool is_c_contiguous, bool shape_only, bool force_megarow, vector<float>& flattened_tensor_data_vec) {
    vector<DataFormat> formats_supported = {DataFormat::RawUInt32, DataFormat::Float32, DataFormat::Tf32,  DataFormat::Float16_b};
    // No-op if tensor already tilized
    if (!this->is_tilized())
    {
        log_assert(this->flat_tensor_data.size() > 0 ,  "Expected tensor to be un-tilized, but flat_tensor_data is empty");
        flattened_tensor_data_vec = this->flat_tensor_data;
        return;
    } else {
        this->flat_tensor_data.clear();
    }

    // ADD back API function
    
    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    void(*copy_func_ptr)(vector<float>&, const tt::tt_tile&, int, int, int);
    
    if(std::getenv("TT_BACKEND_FORCE_SLOW_UNTILIZE")){
        copy_func_ptr = &copy_to_flat_vector;
    }
    else{
        copy_func_ptr = &vectorized_copy_to_flat_vector;
    }

    if(std::find(formats_supported.begin(), formats_supported.end(), get_data_format()) == formats_supported.end()){
        throw std::runtime_error("Unsupported data type requested for conversion from tt_tensor!");
    }
    size_t numel = get_shape().volume_full();
    flattened_tensor_data_vec.resize(numel);
    bool is_megarow = this->get_data_format() == DataFormat::RawUInt8 or
                        this->get_data_format() == DataFormat::RawUInt16 or
                        this->get_data_format() == DataFormat::RawUInt32 or force_megarow;
    size_t dest_tile_offset = 0;
    for (int iw = 0; iw < getw(); iw++) {
        for (int iz = 0; iz < getz(); iz++) {
            for (int irt = 0; irt < getrt(); irt++) {
                for (int ict = 0; ict < getct(); ict++) {
                    const tt_tile &tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    for (int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
                        size_t i_dest_z = iz * getrfull() * getcfull();
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t i_dest_c = (ict * get_shape().tile_width);

                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        if (is_megarow) {
                            dest_row_index = dest_tile_offset*TILE_HEIGHT*TILE_WIDTH + i_tile_row * TILE_WIDTH;
                        }
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, get_shape().tile_width);
                    }
                    dest_tile_offset++;
                }
            }
        }
    }
}
    
void tt_tensor::untilize_to_flat_tensor_data_byte(bool is_c_contiguous, bool shape_only, vector<int8_t>& flattened_tensor_data_vec) {
    log_assert(get_data_format() == tt::DataFormat::Int8, "Unsupported data type requested for conversion from tt_tensor!");
    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    void(*copy_func_ptr)(vector<int8_t>&, const tt::tt_tile&, int, int, int);
    
    if(std::getenv("TT_BACKEND_FORCE_SLOW_UNTILIZE")){
        copy_func_ptr = &copy_byte_to_flat_vector_int8;
    }
    else{
        copy_func_ptr = &vectorized_copy_byte_to_flat_vector_int8;
    }

    size_t numel = get_shape().volume_full();
    flattened_tensor_data_vec.resize(numel);

    for(int iw = 0; iw < getw(); iw++) {
        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
        for(int iz = 0; iz < getz(); iz++) {
            size_t i_dest_z = iz * getrfull() * getcfull();
            for(int irt = 0; irt < getrt(); irt++) {
                for(int ict = 0; ict < getct(); ict++) {
                    const tt_tile & tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    size_t i_dest_c = (ict * get_shape().tile_width);
                    for(int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, get_shape().tile_width);
                    }
                }
            }
        }
    }
}

void tt_tensor::untilize_to_flat_tensor_data_unsigned_byte(bool is_c_contiguous, bool shape_only, vector<uint8_t>& flattened_tensor_data_vec) {
    log_assert(get_data_format() == tt::DataFormat::UInt8, "Unsupported data type requested for conversion from tt_tensor!");
    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    void(*copy_func_ptr)(vector<uint8_t>&, const tt::tt_tile&, int, int, int);
    
    if(std::getenv("TT_BACKEND_FORCE_SLOW_UNTILIZE")){
        copy_func_ptr = &copy_byte_to_flat_vector_uint8;
    }
    else{
        copy_func_ptr = &vectorized_copy_byte_to_flat_vector_uint8;
    }

    size_t numel = get_shape().volume_full();
    flattened_tensor_data_vec.resize(numel);

    for(int iw = 0; iw < getw(); iw++) {
        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
        for(int iz = 0; iz < getz(); iz++) {
            size_t i_dest_z = iz * getrfull() * getcfull();
            for(int irt = 0; irt < getrt(); irt++) {
                for(int ict = 0; ict < getct(); ict++) {
                    const tt_tile & tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    size_t i_dest_c = (ict * get_shape().tile_width);
                    for(int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, get_shape().tile_width);
                    }
                }
            }
        }
    }
}

void tt_tensor::untilize_to_flat_tensor_data_dword(bool is_c_contiguous, bool shape_only, vector<int32_t>& flattened_tensor_data_vec)
{
    log_assert(get_data_format() == tt::DataFormat::Int32, "Unsupported data type requested for conversion from tt_tensor!");
    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    void(*copy_func_ptr)(vector<int32_t>&, const tt::tt_tile&, int, int, int);
    
    if(std::getenv("TT_BACKEND_FORCE_SLOW_UNTILIZE")){
        copy_func_ptr = &copy_dword_to_flat_vector_int32;
    }
    else{
        copy_func_ptr = &vectorized_copy_dword_to_flat_vector_int32;
    }

    size_t numel = get_shape().volume_full();
    flattened_tensor_data_vec.resize(numel);

    for(int iw = 0; iw < getw(); iw++) {
        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
        for(int iz = 0; iz < getz(); iz++) {
            size_t i_dest_z = iz * getrfull() * getcfull();
            for(int irt = 0; irt < getrt(); irt++) {
                for(int ict = 0; ict < getct(); ict++) {
                    const tt_tile & tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    size_t i_dest_c = (ict * get_shape().tile_width);
                    for(int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, get_shape().tile_width);
                    }
                }
            }
        }
    }
}
void tt_tensor::untilize_to_flat_tensor_data_half(bool is_c_contiguous, bool shape_only, vector<uint16_t>& flattened_tensor_data_vec) {
    vector<DataFormat> formats_supported = {DataFormat::Float16_b, DataFormat::Bfp8_b, DataFormat::Bfp4_b, DataFormat::Bfp2_b, DataFormat::Float16, DataFormat::Bfp8, DataFormat::Bfp4, DataFormat::Bfp2, DataFormat::RawUInt16, DataFormat::RawUInt8, DataFormat::UInt16};
    vector<DataFormat> _b_formats = {DataFormat::Float16_b, DataFormat::Bfp8_b, DataFormat::Bfp4_b, DataFormat::Bfp2_b};

    // No-op if tensor already tilized
    if (!this->is_tilized())
    {
        log_assert(this->flat_tensor_data.size() > 0 ,  "Expected tensor to be un-tilized, but flat_tensor_data is empty");
        flattened_tensor_data_vec = this->flat_tensor_data_half;
        return;
    } else {
        // Clear any untilized data
        this->flat_tensor_data_half.clear();
    }

    // ADD back API function
    // float* flattened_tensor_data = tt::utils::tt_tensor_to_flat_rowmajor_tensor(*this);

    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    if(std::find(formats_supported.begin(), formats_supported.end(), get_data_format()) == formats_supported.end()){
        throw std::runtime_error("Unsupported data type requested for conversion from tt_tensor!");
    }
    size_t numel = get_shape().volume_full();

    flattened_tensor_data_vec.resize(numel);
    __m128i shift = _mm_set_epi64x(0, 16);
    __m256i mantissa_mask;
    if(std::find(_b_formats.begin(), _b_formats.end(), get_data_format()) != _b_formats.end()) mantissa_mask = _mm256_set1_epi32(0xffffffff); //*16b formats
    
    else mantissa_mask = _mm256_set1_epi32(0x007fffff); // *16 formats
    // Used for *16 formats
    __m256i exp_mask = _mm256_set1_epi32(0x7F800000);
    __m256i sign_mask = _mm256_set1_epi32(0x80000000);

    void(*copy_func_ptr)(vector<uint16_t>&, const tt::tt_tile&, int, int, __m256i&, __m128i&, __m256i&, __m256i&, int);
    if(std::getenv("TT_BACKEND_FORCE_SLOW_UNTILIZE")){
        if(std::find(_b_formats.begin(), _b_formats.end(), get_data_format()) != _b_formats.end()){
            copy_func_ptr =  &copy_half_to_flat_vector_u16b;
        }
        else if(get_data_format() == DataFormat::RawUInt16){
            copy_func_ptr = &copy_half_to_flat_vector_raw_u16;
        }
        else if(get_data_format() == DataFormat::UInt16){
            copy_func_ptr = &copy_half_to_flat_vector_uint16;
        }
        else{
            copy_func_ptr = &copy_half_to_flat_vector_u16;
        }
    }
    else{
        if(std::find(_b_formats.begin(), _b_formats.end(), get_data_format()) != _b_formats.end()){
            copy_func_ptr = &vectorized_copy_half_to_flat_vector_u16b;
        }
        else if(get_data_format() == DataFormat::RawUInt16){
            copy_func_ptr = &vectorized_copy_half_to_flat_vector_raw_u16;
        }
        else if (get_data_format() == DataFormat::UInt16) {
            copy_func_ptr = &vectorized_copy_word_to_flat_vector_uint16;
        }
        else{
            copy_func_ptr = &vectorized_copy_half_to_flat_vector_u16;
        }
    }
    for (int iw = 0; iw < getw(); iw++) {
        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
        for (int iz = 0; iz < getz(); iz++) {
            size_t i_dest_z = iz * getrfull() * getcfull();
            for (int irt = 0; irt < getrt(); irt++) {
                for (int ict = 0; ict < getct(); ict++) {
                    const tt_tile &tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    size_t i_dest_c = (ict * get_shape().tile_width);
                    for (int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, mantissa_mask, shift, exp_mask, sign_mask, get_shape().tile_width);
                    }
                }
            }
        }
    }
}

#else
// ********* ARM specific functions. *********
namespace tt {
namespace untilize_utils {
    void convert_device_int8_representation_to_host_vectorized(uint8_t* host_storage, uint32_t tensor_size) {
        // Vectorized converter to go from sign|magnitude int8 representation to 2's complement.
        convert_device_int8_representation_to_host(host_storage, tensor_size);
    }
    void convert_device_int32_representation_to_host_vectorized(uint32_t* host_storage, uint32_t tensor_size) {
        // Vectorized converter to go from sign|magnitude int8 representation to 2's complement.
        convert_device_int32_representation_to_host(host_storage, tensor_size);
    }
}
}

void copy_half_to_flat_vector_uint16(vector<uint16_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width) {
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = (uint16_t)(std::min(std::max(tile.t[i_tile_row][i_tile_col], float(0)), float(65535)));
    }
}

void copy_half_to_flat_vector_raw_u16(vector<uint16_t>& to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width){
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = tile.conv_u32_to_raw_u16(tile.float_to_dword(tile.t[i_tile_row][i_tile_col]));
    }
}

void copy_half_to_flat_vector_u16b(vector<uint16_t>& to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width){
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = tile.conv_u32_to_u16b(tile.float_to_dword(tile.t[i_tile_row][i_tile_col]));
    }
} 

void copy_half_to_flat_vector_u16(vector<uint16_t>&to, const tt::tt_tile& tile, int i_tile_row, int dest_row_index, int tile_width){
    for (size_t i_tile_col = 0; i_tile_col < tile_width; i_tile_col++) {
        to[dest_row_index + i_tile_col] = tile.conv_u32_to_u16(tile.float_to_dword(tile.t[i_tile_row][i_tile_col]));
    }

}

void tt_tensor::untilize_to_flat_tensor_data(bool is_c_contiguous, bool shape_only, bool force_megarow, vector<float>& flattened_tensor_data_vec) {
    vector<DataFormat> formats_supported = {DataFormat::RawUInt32, DataFormat::Float32, DataFormat::Tf32,  DataFormat::Float16_b};
    // No-op if tensor already tilized
    if (!this->is_tilized())
    {
        log_assert(this->flat_tensor_data.size() > 0 ,  "Expected tensor to be un-tilized, but flat_tensor_data is empty");
        flattened_tensor_data_vec = this->flat_tensor_data;
        return;
    } else {
        this->flat_tensor_data.clear();
    }

    // ADD back API function
    
    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    void(*copy_func_ptr)(vector<float>&, const tt::tt_tile&, int, int, int);
    copy_func_ptr = &copy_to_flat_vector;
    
    if(std::find(formats_supported.begin(), formats_supported.end(), get_data_format()) == formats_supported.end()){
        throw std::runtime_error("Unsupported data type requested for conversion from tt_tensor!");
    }
    size_t numel = get_shape().volume_full();
    flattened_tensor_data_vec.resize(numel);
    bool is_megarow = this->get_data_format() == DataFormat::RawUInt8 or
                        this->get_data_format() == DataFormat::RawUInt16 or
                        this->get_data_format() == DataFormat::RawUInt32 or force_megarow;
    size_t dest_tile_offset = 0;
    for (int iw = 0; iw < getw(); iw++) {
        for (int iz = 0; iz < getz(); iz++) {
            for (int irt = 0; irt < getrt(); irt++) {
                for (int ict = 0; ict < getct(); ict++) {
                    const tt_tile &tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    for (int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
                        size_t i_dest_z = iz * getrfull() * getcfull();
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t i_dest_c = (ict * get_shape().tile_width);

                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        if (is_megarow) {
                            dest_row_index = dest_tile_offset*TILE_HEIGHT*TILE_WIDTH + i_tile_row * TILE_WIDTH;
                        }
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, get_shape().tile_width);
                    }
                    dest_tile_offset++;
                }
            }
        }
    }
}

void tt_tensor::untilize_to_flat_tensor_data_byte(bool is_c_contiguous, bool shape_only, vector<int8_t>& flattened_tensor_data_vec) {
    log_assert(get_data_format() == tt::DataFormat::Int8, "Unsupported data type requested for conversion from tt_tensor!");
    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    void(*copy_func_ptr)(vector<int8_t>&, const tt::tt_tile&, int, int, int);
    copy_func_ptr = &copy_byte_to_flat_vector_int8;
    
    size_t numel = get_shape().volume_full();
    flattened_tensor_data_vec.resize(numel);

    for(int iw = 0; iw < getw(); iw++) {
        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
        for(int iz = 0; iz < getz(); iz++) {
            size_t i_dest_z = iz * getrfull() * getcfull();
            for(int irt = 0; irt < getrt(); irt++) {
                for(int ict = 0; ict < getct(); ict++) {
                    const tt_tile & tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    size_t i_dest_c = (ict * get_shape().tile_width);
                    for(int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, get_shape().tile_width);
                    }
                }
            }
        }
    }
}

void tt_tensor::untilize_to_flat_tensor_data_unsigned_byte(bool is_c_contiguous, bool shape_only, vector<uint8_t>& flattened_tensor_data_vec) {
    log_assert(get_data_format() == tt::DataFormat::UInt8, "Unsupported data type requested for conversion from tt_tensor!");
    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    void(*copy_func_ptr)(vector<uint8_t>&, const tt::tt_tile&, int, int, int);
    copy_func_ptr = &copy_byte_to_flat_vector_uint8;
    
    size_t numel = get_shape().volume_full();
    flattened_tensor_data_vec.resize(numel);

    for(int iw = 0; iw < getw(); iw++) {
        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
        for(int iz = 0; iz < getz(); iz++) {
            size_t i_dest_z = iz * getrfull() * getcfull();
            for(int irt = 0; irt < getrt(); irt++) {
                for(int ict = 0; ict < getct(); ict++) {
                    const tt_tile & tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    size_t i_dest_c = (ict * get_shape().tile_width);
                    for(int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, get_shape().tile_width);
                    }
                }
            }
        }
    }
}

void tt_tensor::untilize_to_flat_tensor_data_dword(bool is_c_contiguous, bool shape_only, vector<int32_t>& flattened_tensor_data_vec)
{
    log_assert(get_data_format() == tt::DataFormat::Int32, "Unsupported data type requested for conversion from tt_tensor!");
    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    void(*copy_func_ptr)(vector<int32_t>&, const tt::tt_tile&, int, int, int);
    copy_func_ptr = &copy_dword_to_flat_vector_int32;
    
    size_t numel = get_shape().volume_full();
    flattened_tensor_data_vec.resize(numel);

    for(int iw = 0; iw < getw(); iw++) {
        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
        for(int iz = 0; iz < getz(); iz++) {
            size_t i_dest_z = iz * getrfull() * getcfull();
            for(int irt = 0; irt < getrt(); irt++) {
                for(int ict = 0; ict < getct(); ict++) {
                    const tt_tile & tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    size_t i_dest_c = (ict * get_shape().tile_width);
                    for(int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, get_shape().tile_width);
                    }
                }
            }
        }
    }
}

void tt_tensor::untilize_to_flat_tensor_data_half(bool is_c_contiguous, bool shape_only, vector<uint16_t>& flattened_tensor_data_vec) {
    vector<DataFormat> formats_supported = {DataFormat::Float16_b, DataFormat::Bfp8_b, DataFormat::Bfp4_b, DataFormat::Bfp2_b, DataFormat::Float16, DataFormat::Bfp8, DataFormat::Bfp4, DataFormat::Bfp2, DataFormat::RawUInt16, DataFormat::RawUInt8};
    vector<DataFormat> _b_formats = {DataFormat::Float16_b, DataFormat::Bfp8_b, DataFormat::Bfp4_b, DataFormat::Bfp2_b};

    // No-op if tensor already tilized
    if (!this->is_tilized())
    {
        log_assert(this->flat_tensor_data.size() > 0 ,  "Expected tensor to be un-tilized, but flat_tensor_data is empty");
        flattened_tensor_data_vec = this->flat_tensor_data_half;
        return;
    } else {
        // Clear any untilized data
        this->flat_tensor_data_half.clear();
    }

    // ADD back API function
    // float* flattened_tensor_data = tt::utils::tt_tensor_to_flat_rowmajor_tensor(*this);

    using tt::constants::TILE_HEIGHT;
    using tt::constants::TILE_WIDTH;

    if(std::find(formats_supported.begin(), formats_supported.end(), get_data_format()) == formats_supported.end()){
        throw std::runtime_error("Unsupported data type requested for conversion from tt_tensor!");
    }
    size_t numel = get_shape().volume_full();

    flattened_tensor_data_vec.resize(numel);
    

    void(*copy_func_ptr)(vector<uint16_t>&, const tt::tt_tile&, int, int,  int);
    if(std::find(_b_formats.begin(), _b_formats.end(), get_data_format()) != _b_formats.end()){
        copy_func_ptr =  &copy_half_to_flat_vector_u16b;
    }
    else if(get_data_format() == DataFormat::RawUInt16){
        copy_func_ptr = &copy_half_to_flat_vector_raw_u16;
    }
    else if (get_data_format() == DataFormat::UInt16) {
        copy_func_ptr = &copy_half_to_flat_vector_uint16;
    }
    else{
        copy_func_ptr = &copy_half_to_flat_vector_u16;
    }

    for (int iw = 0; iw < getw(); iw++) {
        size_t i_dest_w = iw * getz() * getrfull() * getcfull();
        for (int iz = 0; iz < getz(); iz++) {
            size_t i_dest_z = iz * getrfull() * getcfull();
            for (int irt = 0; irt < getrt(); irt++) {
                for (int ict = 0; ict < getct(); ict++) {
                    const tt_tile &tile = tile_tensor.at(iw).at(iz).at(irt).at(ict);
                    size_t i_dest_c = (ict * get_shape().tile_width);
                    for (int i_tile_row = 0; i_tile_row < get_shape().tile_height; i_tile_row++) {
                        size_t ir = irt * get_shape().tile_height + i_tile_row;
                        size_t i_dest_r = ir * getcfull();
                        size_t dest_row_index = i_dest_w + i_dest_z + i_dest_r + i_dest_c;
                        copy_func_ptr(flattened_tensor_data_vec, tile, i_tile_row, dest_row_index, get_shape().tile_width);
                    }
                }
            }
        }
    }
}
#endif
