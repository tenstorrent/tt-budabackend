// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/tensor.hpp"
#include "verif_stimulus_types.hpp"
#include "verif_utils.hpp"

#include <unordered_set>
#include <array>

namespace verif {
namespace stimulus {
namespace sparse {

class sparse_tile
{
public:
    sparse_tile()
    {
        for (int i = 0; i < SIZE; ++i)
        {
            data[i].fill(0);
        }
    }

    sparse_tile(const sparse_tile &other)
    {
        copy_data_from(other);
    }

    sparse_tile& operator=(sparse_tile other)
    {
        copy_data_from(other);
        return *this;
    }

    bool operator==(const sparse_tile &other) const
    {
        for (int i = 0; i < SIZE; ++i)
        {
            for (int j = 0; j < SIZE; ++j)
            {
                if (data[i][j] != other.data[i][j]) return false;
            }
        }
        return true;
    }

    std::string to_string() const;

    tt_tile to_tt_tile(DataFormat df) const;

    void sparsify();

    std::size_t hash() const;

private:
    static constexpr unsigned int SIZE = tt::constants::TILE_HEIGHT;
    std::array< std::array<int, SIZE>, SIZE> data;

    void copy_data_from(const sparse_tile& other);
};

//----------------------------------------------------------------------------------------------------------------------

void get_op_info(VerifStimulusConfig *config, const netlist_workload_data &workload);

void fill_sparse_tensor(tt_tensor &tensor, const VerifStimulusConfig &config);

//----------------------------------------------------------------------------------------------------------------------

class encoding_tile
{
public:
    typedef struct {
        uint32_t index:30;
        uint32_t last_strip_in_row:1;
        uint32_t last_strip_in_tile:1;
    } strip_encoding_t;

    typedef union {
        strip_encoding_t strip;
        uint32_t val;
    } strip_encoding_u;

    encoding_tile(int bytes_capacity = 4096) : write_ptr(0)
    {
        vals.resize(bytes_capacity, 0);
    }

    // Following functions write bytes to 'vals'.
    void push_byte(uint8_t num);
    void push_bytes(uint32_t num);
    void push_bytes(uint16_t num);

    // Returns 4-byte number starting at 'idx' in byte array 'vals'.
    uint32_t get_num(int idx) const;

    // Returns byte array string representation.
    string get_string() const;

private:
    // Vector of bytes that represent encoded information about sparse tensor.
    vector<uint8_t> vals;

    // Write index in the 'vals' vector.
    uint write_ptr;
};

//----------------------------------------------------------------------------------------------------------------------

class sparse_encoder
{
public:
    sparse_encoder(int num_unique_tiles, int num_index_tiles, int sparse_tile_ptr_bits, int sparse_ublock_idx_bits,
                   int output_t, int act_t, int grid_size_y, int m_k, int mb_m, int mb_n, int ub_r, int u_kt,
                   double zero_strip_freq, double zero_ublock_freq, double zero_tile_freq,
                   int nz_strips, int nz_ublocks, int nz_tiles,
                   int enc_tile_capacity = 4096) :
        num_unique_tiles(num_unique_tiles), num_index_tiles(num_index_tiles), cur_unique_tile_idx(0),
        sparse_tile_ptr_bits(sparse_tile_ptr_bits), sparse_ublock_idx_bits(sparse_ublock_idx_bits), output_t(output_t),
        act_t(act_t), grid_size_y(grid_size_y), m_k(m_k), mb_m(mb_m), mb_n(mb_n), ub_r(ub_r), u_kt(u_kt),
        zero_strip_freq(zero_strip_freq), zero_ublock_freq(zero_ublock_freq), zero_tile_freq(zero_tile_freq),
        nz_strips(nz_strips), nz_ublocks(nz_ublocks), nz_tiles(nz_tiles),
        encoding_tile_capacity(enc_tile_capacity) {        
        const int bits_in_2B = 16;
        int u_kt_bits = get_bit_width(u_kt);
        int ub_r_bits = get_bit_width(ub_r);
        encoding_tile_data_format = enc_tile_capacity == 1024 ?
                                    DataFormat::RawUInt8 :
                                    (enc_tile_capacity == 2048 ? DataFormat::RawUInt16 : DataFormat::RawUInt32);
        log_assert(mb_m < (1 << sparse_ublock_idx_bits), "Not enough bits to encode ublock indices");
        log_assert(ub_r * u_kt <= (1 << (bits_in_2B - sparse_ublock_idx_bits)),  "Not enough bits to encode nz tiles in ublock");
        log_assert(num_unique_tiles < (1 << sparse_tile_ptr_bits), "Not enough bits to encode sparse tile pointers");
        log_assert(ub_r_bits + u_kt_bits <= bits_in_2B - sparse_tile_ptr_bits, "Not enough bits to encode tile indices");
    }

    // Generates encoding without compression using frequencies for zero strips/ublocks/tiles as parameters.
    // After calling this function 'strip_vec' will be filled with all the needed info.
    void generate_encoding_using_zero_frequencies();

    // Generates encoding without compression using exact counts of non-zero strips/ublocks/tiles as parameters.
    // After calling this function 'strip_vec' will be filled with all the needed info.
    void generate_encoding_using_nonzero_counts();

    // Takes generated encoding in 'strip_vec' and compresses it and puts that data into 'encoding_tile_vec'.
    void compress_encoding();

    // Adds as many as needed zero tiles to encoding vectors in each row.
    void pad_encodings();

    // Returns number of bytes used by 'encoding_tile_vec'.
    int get_compressed_size() const;

    // Returns string representation of 'strip_vec'.
    string get_string() const;

    // Returns tt_tile with RawUInt32 data format created out of encoding tile at position [row_idx][tile_idx]
    // in 'encoding_tile_vec'.
    tt_tile to_tt_tile(int row_idx, int tile_idx) const;

    // Returns number of index (encoding) tiles.
    int get_num_index_tiles() const;

    // Returns debug string with number of encoding tiles in each row in grid.
    string get_num_encoding_tiles_needed_per_row() const;

private:
    // When encoding is generated, this function is called to update bits 'last_strip_in_tile' and 'last_strip_in_row'.
    void update_last_strip_bits();

    // Returns index of the next unique tile to be referenced in encoding.
    int get_next_unique_tile_idx();

    inline int get_bit_width(uint32_t val) {
        int bit_width = 0;
        do {
            bit_width++;
        } while (((val - 1) >> bit_width) > 0);

        return bit_width;
    }

private:
    const int num_index_tiles;
    const int sparse_tile_ptr_bits;
    const int sparse_ublock_idx_bits;
    const int encoding_tile_capacity;
    DataFormat encoding_tile_data_format;
    int cur_unique_tile_idx;
    const int num_unique_tiles;
    const int output_t, act_t, grid_size_y, m_k, mb_m, mb_n, ub_r, u_kt; // matmul op params.
    // in case frequencies are used for encoding generation
    const double zero_strip_freq, zero_ublock_freq, zero_tile_freq;
    // in case exact non-zero coutns are used for encoding generation
    const int nz_strips;
    const int nz_ublocks;
    const int nz_tiles;

    // 2D vector of compressed encoding tiles. First dim is row (in grid) number.
    vector<vector<encoding_tile>> encoding_tile_vec;

    struct tile
    {
        int unique_tile_idx;
        int within_ublock_idx;
        tile(int uti, int wui) : unique_tile_idx(uti), within_ublock_idx(wui) {}
    };

    struct ublock
    {
        int idx;
        vector<tile> tile_vec;
        ublock(int idx) : idx(idx) {}
    };

    struct strip
    {
        int idx;
        bool last_strip_in_row;
        bool last_strip_in_tile;
        vector<ublock> ublock_vec;
        strip(int idx, bool lbir = false, bool lbit = false) :
            idx(idx), last_strip_in_row(lbir), last_strip_in_tile(lbit) {
            ublock_vec.clear();
        }

        int get_byte_usage() const;
    };

    // Non-compressed generated encoding information.
    vector<vector<strip>> strip_vec;
};

//----------------------------------------------------------------------------------------------------------------------

void fill_sparse_encoding_tensor(tt_tensor &tensor, const VerifStimulusConfig &config);

//----------------------------------------------------------------------------------------------------------------------

} } }
