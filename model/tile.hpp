// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <vector>

#include "common/base.hpp"
#include "forward_declarations.hpp"
#include "hlks/inc/hlk_api.h"

// Utils
using std::isinf;
using std::uint32_t;

// Functions
using std::function;

// Datastructures
using std::array;
using std::deque;
using std::vector;

#define TT_TILE_FORCE_INLINE __attribute__((always_inline)) inline

namespace tt
{
class tt_tile : public tt_dram_resident
{
    public:
    union alignas(32) {
       float t[tt::constants::TILE_HEIGHT][tt::constants::TILE_WIDTH];
       float t_vector[tt::constants::TILE_HEIGHT * tt::constants::TILE_WIDTH];
       uint32_t t_u32[tt::constants::TILE_HEIGHT][tt::constants::TILE_WIDTH];
    };
    vector<uint32_t> packed_data;
    std::uint32_t tile_height = tt::constants::TILE_HEIGHT;
    std::uint32_t tile_width = tt::constants::TILE_WIDTH;
    DataFormat data_format = DataFormat::Invalid;
    const static bool skip_bfp8_check;
    const static bool truncate_bfp_mantissa;
    const static bool force_slow_untilize;
    tt_tile();
    tt_tile(const std::array<std::array<float, tt::constants::TILE_HEIGHT>, tt::constants::TILE_WIDTH> &data);
    tt_tile(DataFormat data_formati, bool init_to_zero = true);
    // Constructor for tiles in read back buffers
    tt_tile(DataFormat data_formati, uint64_t addr, uint32_t chan, uint32_t chip_id, bool host_resident_in = false);

    //!< Sets tile contents to 0
    void set_zero();

    static TT_TILE_FORCE_INLINE uint32_t float_to_dword(float in) {
        union
        {
            float input; // assumes sizeof(float) == sizeof(int)
            int   output;
        } data;
        data.input = in;
        return(data.output);
    }

    float dword_to_float(uint32_t in) const;
    double inf_to_num(double in) const;

    uint32_t get_exp_dword(vector<uint8_t> &vec);

    bool is_shared_exp_format(DataFormat data_format);
    bool is_shared_exp_a_format();
    bool is_shared_exp_b_format();
    static TT_TILE_FORCE_INLINE uint16_t conv_u32_to_u16b(uint32_t in) {
        uint32_t tmp = (in & 0xffffffff) >> 16;
        return tmp;
    }

    static TT_TILE_FORCE_INLINE uint16_t conv_u32_to_u16(uint32_t in) {
        uint32_t m = in & 0x007fffff;
        uint32_t e = (in & 0x7F800000) >> 23;
        uint32_t s = (in & 0x80000000) >> 16; // move to bit 16
        // unbias and rebias exponent, shift down mantissa
        // with saturation to bottom/top of range
        int se = (int)e;
        se = se - 127;
        // HW specific fp16_a has 5 bit exponent with no representation for inf
        // this can hold 0 -> 31, which represents -15 -> 16
        if(se <= (-15))
        {
            se = -15;
            m = 0;
        }
        else if(se > 16)
        {
            se = 16;
            m = 0x007fffff >> 13;
        }
        else
        {
            se = se;
            m = m >> 13;
        }
        // add bias for fp16_a
        se += 15;
        log_assert(se >= 0, "Expected positive exp");
        e = (uint32_t) se;
        e = e << 10;
        uint32_t out = s | e | m;
        return(out);
    }
    uint32_t conv_u32_to_int8(uint32_t in) const;
    uint16_t conv_u32_to_raw_u16(uint32_t in) const;
    uint32_t conv_u16_to_u32(uint16_t in) const;
    uint32_t conv_u16b_to_u32(uint16_t in) const;

    void unpack_raw32_and_fill_tile();
    void unpack_raw16_and_fill_tile();
    void unpack_fp32_and_fill_tile();
    void unpack_fp16_and_fill_tile();
    void unpack_fp16b_and_fill_tile();
    void unpack_bfp8_and_fill_tile();

    bool packed_data_present() const;
    void pack_data(bool truncate_bfp_mantissa = false);
    int size_bytes(bool include_header_padding = true);
    uint32_t get_quarter_byte(uint32_t word, uint32_t index);
    uint32_t get_half_byte(uint32_t word, uint32_t index);
    static TT_TILE_FORCE_INLINE uint32_t get_byte(uint32_t word, uint32_t index) {
        uint32_t mask = 0xff << (8 * index);
        uint32_t masked = word & mask;
        masked = masked >> (8*index);
        return masked;
    }
    uint32_t get_half(uint32_t word, uint32_t index);
    uint32_t get_indexed_num(uint32_t data_index);
    void packed_data_to_tile();
    void adjust_tile_for_accuracy(bool truncate_bfp_mantissa = false);
    tt_tile isclose(tt_tile &rhs, float target);
    bool allclose(const tt_tile &rhs, const double rtol = tt::constants::DEFAULT_RTOL, const double atol = tt::constants::DEFAULT_ATOL, double pct_matched=tt::constants::DEFAULT_PCT_MATCHED, bool print_diffs=false) const;
    bool pcc_compare(const tt_tile &rhs, const double rtol, const double atol, const double pass_pcc) const;
    void set_data_format(DataFormat data_formati);
    void fill_from_row(const tt_tile &src, int row_index);
    bool operator==(const tt_tile &rhs) const;
    tt_tile operator+(const tt_tile &rhs) const;
    tt_tile operator*(const tt_tile &rhs) const;
    tt_tile operator-(const tt_tile &rhs) const;
    tt_tile max(const tt_tile &rhs) const;
    tt_tile broadcast(Dim dim) const;
    tt_tile eltwise_binary_with_broadcast(const tt_tile &rhs, BinaryOp binary_op, Dim dim) const;
    tt_tile transpose_xy() const;
    tt_tile matmul(const tt_tile &rhs) const;
    void matmul_with_partial(const tt_tile &rhs, tt_tile &result) const;
    tt_tile reduce(ReduceFunc reduce_func, Dim dim, float coefficient) const;
    tt_tile &operator+=(const tt_tile& rhs);
    void operator = (float num);
    void operator = (const tt_tile& rhs);
    std::string get_string() const;
    std::uint32_t hash_one(std::uint32_t modulo_factor = 997) const;
    std::uint32_t hash_two(std::uint32_t modulo_factor = 997) const;
    void dump(bool raw = false) const;
    void dump_packed();
    string get_diff_string(const tt_tile& rhs, std::string lhs_header = "Golden", std::string rhs_header = "Observed") const;
    void dump_diff(const tt_tile& rhs) const;
    TT_TILE_FORCE_INLINE void set(int i, int j, float val) {
        t[i][j] = val;
    }
    TT_TILE_FORCE_INLINE void set(int i, int j, double val) {
        t[i][j] = (float)val;  // possible loss in precision
    }
    TT_TILE_FORCE_INLINE void set(int i, int j, int val) {
        t[i][j] = (float)val;  // allows for lossless conversion back-and-forth
    }
    TT_TILE_FORCE_INLINE void set(int i, int j, uint32_t val) {
        t_u32[i][j] = val;  // raw uint
    }

    void acc(int i, int j, float val);
    float get(int i, int j);
    bool operator!=(const tt_tile &rhs) const;
    // Copy constructor  makes a deep copy of input tile
    tt_tile(const tt_tile &in_tile);
    tt_tile(const tt_tile &in_tile, DataFormat data_formati);
    static deque<tt_tile*> deepcopy_tile_queue_on_heap(const deque<tt_tile*>& tile_queue);
    void randomize(float min=0.0, float max=0.25);
    void randomize_uniform(float lower_bound=0.0, float upper_bound=1.0);
    void randomize_manual_float(int spread, int man_variance_bits);
    void randomize_per_col_mask(int spread, int man_variance_bits, vector <bool> &col_mask);
    void randomize_diagonal(float mean = 0.0, float stddev = 0.25);
    void init_to_xyz_values(int z = 0);
    void set_number(float num);
    void set_tile_rc(int r, int c, float val);
    void adjust_man_precision(uint32_t new_man_prec);
    void clear_packed_data();

    // Tile is stored as a 32x32 float vector
    // If we want to support dimensions like 32x16, 16x32 etc,
    // we need to make parts of tile which don't fit in a certain dimension zero.
    // E.g. (if we want to make it 32x16, we set all values which have greater dimensions than that to zero)
    void clear_values(int dim_r, int dim_c);

    void print(std::ostream & os) const;

    static tt_tile zero_tile(DataFormat dataformati);
};

inline std::ostream & operator<<(std::ostream & os, const tt_tile & tile)
{
    tile.print(os);
    return os;
}

inline std::ostream & operator<<(std::ostream & os, tt_tile * tile)
{
    tile->print(os);
    return os;
}

inline std::ostream & operator<<(std::ostream & os, std::shared_ptr<tt_tile> tile)
{
    tile->print(os);
    return os;
}

namespace math
{
namespace utils
{
void dump_bits(float x);
float exp(float x);
float log(float x);
float sqrt(float x);
float abs(float x);
float gelu(float x);
float relu(float x);
float gelu_derivative(float x);
float reciprocal(float x);
float sigmoid(float x);
float tanh(float x);
bool tile_is_zeroed_out(float prob);
float zero(float x);
float identity(float x);
float square(float x);
float sine(float x);
float cosine(float x);
float lrelu(float x, float slope);
}
tt_tile unary(function<float(float)> func, const tt_tile& input_tile);
tt_tile binary(function<float(float,float)> func, const tt_tile& input_tile0, const tt_tile& input_tile1);
tt_tile exp(const tt_tile& input_tile);
tt_tile log(const tt_tile& input_tile);
tt_tile sqrt(const tt_tile& input_tile);
tt_tile abs(const tt_tile& input_tile);
tt_tile gelu(const tt_tile& input_tile);
tt_tile relu(const tt_tile& input_tile);
tt_tile relu_with_threshold(const tt_tile& input_tile, float threshold, ReluMode mode);
tt_tile gelu_derivative(const tt_tile& input_tile);
tt_tile reciprocal(const tt_tile& input_tile);
tt_tile tanh(const tt_tile& input_tile);
tt_tile sigmoid(const tt_tile& input_tile);
tt_tile max(const tt_tile& input_tile0, const tt_tile& input_tile1);
tt_tile dropout(const tt_tile& input_tile, float prob);
tt_tile square(const tt_tile& input_tile);
tt_tile sine(const tt_tile& input_tile);
tt_tile lrelu(const tt_tile& input_tile, const float& slope);
tt_tile cosine(const tt_tile& input_tile);
tt_tile power(const tt_tile& input_tile, int exponent);
} // end namespace math

namespace untilize_utils {
    void convert_device_int8_representation_to_host(uint8_t* host_storage, uint32_t tensor_size);
    void convert_device_int8_representation_to_host_vectorized(uint8_t* host_storage, uint32_t tensor_size);
    void convert_device_int32_representation_to_host(uint32_t* host_storage, uint32_t tensor_size);
    void convert_device_int32_representation_to_host_vectorized(uint32_t* host_storage, uint32_t tensor_size);
}
} // end namespace tt
