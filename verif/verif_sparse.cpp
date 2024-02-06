// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "verif_sparse.hpp"
#include "verif_rand_utils.hpp"
#include "utils/logger.hpp"
#include <array>
#include <random>
#include <boost/functional/hash.hpp>

using namespace std;
using namespace verif::stimulus::sparse;

// Goes through all ops in the netlist workload and finds a connection between stimulus config for a queue and an
// identity matmul op. Then it stores the op information in the stimulus config.
void verif::stimulus::sparse::get_op_info(VerifStimulusConfig *const config, const netlist_workload_data &workload)
{
    log_assert(
        config->type == StimulusType::Sparse || config->type == StimulusType::SparseEncoding ||
            config->type == StimulusType::SparseEncodingNonzeroCounts,
        "Unexpected stimulus type");

    for (const auto &graph_it : workload.graphs)
    {
        const std::map<string, tt_op_info> &op_map = graph_it.second.my_graph_info.op_map;
        bool found_op = false;
        for (const auto &op_it : op_map)
        {
            if (op_it.first == config->sparse_encoding_attrs.matmul_ident_name)
            {
                config->sparse_encoding_attrs.matmul_ident_info = op_it.second;
                found_op = true;
                break;
            }
        }
        if (found_op) break;
    }
}

int rand_int(int low = 0, int high = tt::constants::TILE_HEIGHT - 1)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> uid(low, high);
    return uid(gen);
}

double rand_double(double low = 0.0, double high = 1.0)
{
    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<double> urd(low, high);
    return urd(gen);
}

//----------------------------------------------------------------------------------------------------------------------

size_t verif::stimulus::sparse::sparse_tile::hash() const
{
    size_t hash_val = 0;
    for (int i = 0; i < SIZE; ++i)
    {
        for (int j = 0; j < SIZE; ++j)
        {
            boost::hash_combine(hash_val, data[i][j]);
        }
    }
    return hash_val;
}

template<>
struct std::hash<sparse_tile>
{
    size_t operator()(sparse_tile const& st) const noexcept
    {
        return st.hash();
    }
};

void verif::stimulus::sparse::sparse_tile::copy_data_from(const sparse_tile& other)
{
    for (int i = 0; i < SIZE; ++i)
    {
        for (int j = 0; j < SIZE; ++j)
        {
            data[i][j] = other.data[i][j];
        }
    }
}

// Generates sparse tile that has exactly one 1 per row.
void verif::stimulus::sparse::sparse_tile::sparsify()
{
    for (int i = 0; i < SIZE; ++i)
    {
        int pos_one_in_row = random::tt_rnd_int(0, tt::constants::TILE_HEIGHT - 1);
        data[i][pos_one_in_row] = 1;
    }
}

string verif::stimulus::sparse::sparse_tile::to_string() const
{
    string result = "";
    for (int i = 0; i < SIZE; ++i)
    {
        for (int j = 0; j < SIZE; ++j)
        {
            result += data[i][j] + '0';
        }
        result += '\n';
    }
    return result;
}

tt_tile verif::stimulus::sparse::sparse_tile::to_tt_tile(DataFormat df) const
{
    std::array<std::array<float, SIZE>, SIZE> data_f;
    for (int i = 0; i < SIZE; ++i)
    {
        for (int j = 0; j < SIZE; ++j)
        {
            data_f[i][j] = (float)data[i][j];
        }
    }
    tt_tile result(data_f);
    result.set_data_format(df);
    return result;
}

//----------------------------------------------------------------------------------------------------------------------

void verif::stimulus::sparse::fill_sparse_tensor(tt_tensor &tensor, const VerifStimulusConfig &config)
{
    const int num_sparse_tiles = config.sparse_encoding_attrs.matmul_ident_info.attributes.num_sparse_tiles;
    // for sparse tile generation, we need to use grid size before possible grid transpose;
    // grid_size: [5, 1], grid_transpose: true 
    // this setup means we will end up with cores with grid [1, 5], but the cores will
    // behave as if they are still in one column - whole input1 will be sent to all cores,
    // while input0 and input2 wll be generated separately for each core
    const int num_rows = config.sparse_encoding_attrs.matmul_ident_info.original_grid_size_y();
    vector<vector<sparse_tile>> tiles(num_rows);
    for (int r = 0; r < num_rows; ++r)
    {
        unordered_set<sparse_tile> unique_tiles;
        while (tiles[r].size() < num_sparse_tiles)
        {
            sparse_tile new_st = sparse_tile();
            new_st.sparsify();
            if (unique_tiles.find(new_st) == unique_tiles.end())
            {
                unique_tiles.insert(new_st);
                tiles[r].push_back(new_st);
            }
        }
    }

    log_debug(tt::LogVerif, "sparse tiles #={} tensor size {}", tiles.size(), tensor.metadata.shape.volume());
    int row_idx = 0;
    int tile_idx = 0;
    DataFormat sparse_data_format = config.sparse_encoding_attrs.matmul_ident_info.input_data_formats[0];

    tensor.reserve_tile_tensor();
    tensor.apply([&tiles, &row_idx, &tile_idx, &num_sparse_tiles, &sparse_data_format](tt_tile &tile) {
        tile = tiles[row_idx][tile_idx].to_tt_tile(sparse_data_format);
        ++tile_idx;
        if (tile_idx == num_sparse_tiles)
        {
            tile_idx = 0;
            ++row_idx;
        }
    });
}

//----------------------------------------------------------------------------------------------------------------------

void verif::stimulus::sparse::encoding_tile::push_byte(uint8_t num)
{
    log_assert(write_ptr < vals.size(), "wptr out of bounds");
    vals[write_ptr++] = num;
}

void verif::stimulus::sparse::encoding_tile::push_bytes(uint16_t num)
{
    uint32_t mask = 0xFF;
    const int bits_in_byte = 8;
    for (int i = 0; i < sizeof(num) / sizeof(uint8_t); ++i)
    {
        push_byte(((uint32_t)num & mask) >> (i * bits_in_byte));
        mask <<= bits_in_byte;
    }
}

void verif::stimulus::sparse::encoding_tile::push_bytes(uint32_t num)
{
    uint32_t mask = 0xFF;
    const int bits_in_byte = 8;
    for (int i = 0; i < sizeof(num) / sizeof(uint8_t); ++i)
    {
        push_byte(((uint32_t)num & mask) >> (i * bits_in_byte));
        mask <<= bits_in_byte;
    }
}

uint32_t verif::stimulus::sparse::encoding_tile::get_num(const int idx) const
{
    uint32_t result = 0;
    const int bits_in_byte = 8;
    const int bytes_in_num = vals.size() / 1024 /* nums in tile */ ;
    for (int i = 0; i < bytes_in_num; ++i)
    {
        result |= vals[idx * bytes_in_num + i] << (bits_in_byte * i);
    }
    return result;
}

string verif::stimulus::sparse::encoding_tile::get_string() const
{
    stringstream ss;
    for (int i = 0; i < write_ptr; ++i)
    {
        ss << vals[i] << " ";
    }
    return ss.str();
}

//----------------------------------------------------------------------------------------------------------------------

int verif::stimulus::sparse::sparse_encoder::strip::get_byte_usage() const
{
    int usage = 4 /* strip_idx */ + 2 /* num_ublocks */;

    // 2 bytes split between ublock index and number of tiles.
    usage += 2 * ublock_vec.size();

    for (const auto& ub : ublock_vec)
    {
        // For each tile in the ublock we need 2 bytes. Those are split between tile index within the ublock and
        // unique sparse tile index.
        usage += 2 * ub.tile_vec.size();
    }
    return usage;
}

int verif::stimulus::sparse::sparse_encoder::get_next_unique_tile_idx()
{
    cur_unique_tile_idx = (cur_unique_tile_idx + 1) % num_unique_tiles;
    return cur_unique_tile_idx;
}

void verif::stimulus::sparse::sparse_encoder::update_last_strip_bits()
{
    for (int row_idx = 0; row_idx < strip_vec.size(); ++row_idx)
    {
        int cur_tile_size = 0;
        for (int s_idx = 0; s_idx < strip_vec[row_idx].size(); ++s_idx)
        {
            strip &cur_strip = strip_vec[row_idx][s_idx];
            log_assert(cur_strip.get_byte_usage() <= encoding_tile_capacity,
                       "Byte usage of a single strip crosses encoding tile capacity - try increasing zero frequencies");
            cur_tile_size += cur_strip.get_byte_usage();
            if (s_idx == 0) continue;
            if (cur_tile_size + cur_strip.get_byte_usage() > encoding_tile_capacity)
            {
                strip &prev_strip = strip_vec[row_idx][s_idx - 1];
                prev_strip.last_strip_in_tile = true;
                cur_tile_size = cur_strip.get_byte_usage();
            }
        }
        strip_vec[row_idx].back().last_strip_in_tile = true;
        strip_vec[row_idx].back().last_strip_in_row = true;
    }
}

void verif::stimulus::sparse::sparse_encoder::generate_encoding_using_zero_frequencies()
{
    // this method supports the following cases:
    // 1. act_t == output_t - strip indices are obtained as t_idx * m_k + strip_idx - each t having 
    //  its own set of strip indices, since each t will have a corresponding t on input1
    // 2. act_t != output_t and act_t == 1 - all t on the input 0 will correspond to only one t
    //  on the input1 - strips in each t will be indexed from 0 to m_k - 1
    int encoded_strip_count = 0;
    int encoded_nz_strip_count = 0;
    int nz_ublock_count = 0;
    int nz_tile_count = 0;

    // we split strips into groups within m_k;
    // each t can have non-zero strips up to index = (t_idx + 1) * strip_group_width,
    // while inclusive lower bound is set by the last strip index in the previous t
    //
    // this behavior mimics real encodings on FE to some extent - encoded strip index 
    // always increases or stays the same, while with the upper bound, we try to mimic
    // the diagonal distribution of non-zero strips
    const int strip_group_width = (m_k + output_t - 1) / output_t;

    const int tile_idx_ptr_bits = 16 - sparse_tile_ptr_bits;
    int tile_col_ptr_bits = 0;
    do {
        tile_col_ptr_bits++;
    } while (((u_kt - 1) >> tile_col_ptr_bits) > 0);
    int tile_row_ptr_bits = tile_idx_ptr_bits - tile_col_ptr_bits;

    for (int row_idx = 0; row_idx < grid_size_y; ++row_idx)
    {
        strip_vec.emplace_back(vector<strip>());
        vector<strip> &cur_strip_row = strip_vec.back();

        // index of the last encoded strip in the previous t
        int last_strip_index = -1;

        for (int t_idx = 0; t_idx < output_t; ++t_idx)
        {   
            bool empty_t = true;

            for (int strip_idx = 0; strip_idx < m_k; ++strip_idx)
            {
                const bool is_zero_strip = random::tt_rnd_float(0, 1) <= zero_strip_freq ||
                    // strip index can only be greater or equal than the last encoded strip index
                    (strip_idx < last_strip_index) ||
                    (strip_idx >= (t_idx + 1) * strip_group_width);

                if (is_zero_strip)
                {
                    continue;
                }

                int strip_idx_to_encode = act_t == 1 ? strip_idx : m_k * t_idx + strip_idx;
                // last strip in row/tile fields are populated later on since at this moment they are unknown
                cur_strip_row.emplace_back(strip_idx_to_encode, /* last_strip_in_row */ false, /* last_strip_in_tile */ false);
                strip &cur_strip = cur_strip_row.back();

                encoded_strip_count++;
                encoded_nz_strip_count++;
                empty_t = false;

                const int nz_ub_idx = random::tt_rnd_int(0, mb_m - 1);
                for (int ublock_idx = 0; ublock_idx < mb_m; ++ublock_idx)
                {
                    const bool is_zero_ublock = random::tt_rnd_float(0, 1) <= zero_ublock_freq &&
                                                nz_ub_idx != ublock_idx;

                    if (is_zero_ublock)
                    {
                        continue;
                    }
                    
                    nz_ublock_count++;

                    cur_strip.ublock_vec.emplace_back(ublock_idx);
                    ublock &cur_ublock = cur_strip.ublock_vec.back();

                    const int nz_tile_r = random::tt_rnd_int(0, ub_r - 1);
                    const int nz_tile_c = random::tt_rnd_int(0, u_kt - 1);
                    for (int tile_row_idx = 0; tile_row_idx < ub_r; ++tile_row_idx)
                    {
                        log_assert(tile_row_idx < (1 << tile_row_ptr_bits),
                                   "Not enough bits to store tile_row_idx aka output ublock_rt in tile index encoding");
                        for (int tile_col_idx = 0; tile_col_idx < u_kt; ++tile_col_idx)
                        {
                            const bool is_zero_tile = random::tt_rnd_float(0, 1) <= zero_tile_freq &&
                                                      (tile_row_idx != nz_tile_r || tile_col_idx != nz_tile_c);
                            if (is_zero_tile)
                            {
                                continue;
                            }
                            
                            nz_tile_count++;

                            log_assert(tile_col_idx < (1 << tile_col_ptr_bits),
                                       "Not enough bits to store tile_cold_idx aka u_kt in tile index encoding");
                            cur_ublock.tile_vec.emplace_back(get_next_unique_tile_idx(),
                                                             (tile_row_idx << tile_col_ptr_bits) | tile_col_idx);
                        }
                    }
                }
            }

            // in case there were no non-zero strips in this t, we need to encode at least one zero strip,
            // so we encode the last one that was non-zero, or strip 0 if t_idx == 0
            if (empty_t)
            {
                if (t_idx > 0)
                {
                    cur_strip_row.emplace_back(cur_strip_row.back().idx, /* last_strip_in_row */ true, /* last_strip_in_tile */ false);
                } 
                else
                {
                    cur_strip_row.emplace_back(0, /* last_strip_in_row */ true, /* last_strip_in_tile */ false);
                }
                encoded_strip_count++;
            }

            // save last strip index in this t to use as a lower bound for the next t
            last_strip_index = act_t == 1 || t_idx == 0 ? cur_strip_row.back().idx : cur_strip_row.back().idx % (m_k * t_idx);

            cur_strip_row.back().last_strip_in_row = true;
        }
    }

    update_last_strip_bits();

    log_info(
        tt::LogVerif, 
        "Encoding info: number of encoded strips - {}, encoded non-zero strips - {}, non-zero ublocks - {}, non-zero "
        "tiles - {}.",
        encoded_strip_count,
        encoded_nz_strip_count,
        nz_ublock_count,
        nz_tile_count);
}

void verif::stimulus::sparse::sparse_encoder::generate_encoding_using_nonzero_counts() {
    log_assert(
        nz_strips != -1 && nz_ublocks != -1 && nz_tiles != -1,
        "Non-zero strips, ublocks and tiles all need to be set.");

    log_assert(
        nz_strips <= m_k + output_t - 1 && nz_strips >= output_t,
        "Invalid non-zero strip count {}. Each t must have a non-zero strip, and there can be maximum m_k + t - 1 "
        "non-zero strips.",
        nz_strips);

    log_assert(
        nz_ublocks <= nz_strips * mb_m && nz_ublocks >= nz_strips, "Invalid non-zero ublock count {}.", nz_ublocks);

    log_assert(
        nz_tiles <= nz_ublocks * ub_r * u_kt && nz_tiles >= nz_ublocks, "Invalid non-zero tile count {}.", nz_tiles);

    const int tile_idx_ptr_bits = 16 - sparse_tile_ptr_bits;
    int tile_col_ptr_bits = 0;
    do {
        tile_col_ptr_bits++;
    } while (((u_kt - 1) >> tile_col_ptr_bits) > 0);
    int tile_row_ptr_bits = tile_idx_ptr_bits - tile_col_ptr_bits;

    // we split strips into balanced groups within m_k;
    // each t can have non-zero strips in the following range:
    // t_idx * strip_group_width - 1 <= strip_idx < (t_idx + 1) * strip_group_width
    //
    // this behavior mimics real encodings on FE to some extent - we try to follow
    // the diagonal distribution of non-zero strips and the fact that strip indices
    // always increase or stay the same
    const int strip_group_width = nz_strips / output_t;

    // calculate how many times we need to repeat the last strip from the previous t to 
    // reach the required non-zero strip count;
    // we will repeat last strip from the previous t for last strips_to_repeat ts
    int strips_to_repeat = nz_strips % output_t;
    if (strip_group_width * output_t > m_k) {
        strips_to_repeat += strip_group_width * output_t - m_k;
    }

    // for ublocks and tiles we use the same approach: distribute
    // them as close to uniform as possible; the extra n ublocks/tiles left after
    // division with the number of strips/ublocks will be put in the first n strips/ublocks
    const int nz_ublocks_per_strip = nz_ublocks / nz_strips;
    int extra_ublocks = nz_ublocks % nz_strips;
    const int nz_tiles_per_ublock = nz_tiles / nz_ublocks;
    int extra_tiles = nz_tiles % nz_ublocks;

    int nz_strip_count = 0;
    int nz_ublock_count = 0;
    int nz_tile_count = 0;

    for (int row_idx = 0; row_idx < grid_size_y; ++row_idx) {
        vector<strip> &cur_strip_row = strip_vec.emplace_back(vector<strip>());

        int previous_strip_idx = -1;

        for (int t_idx = 0; t_idx < output_t; ++t_idx) {
            int max_strip_idx = previous_strip_idx + strip_group_width < m_k ? previous_strip_idx + strip_group_width : m_k - 1;
            int starting_strip_idx = previous_strip_idx != -1 ? previous_strip_idx : 0;
            
            for (int strip_idx = starting_strip_idx; strip_idx <= max_strip_idx; strip_idx++) {
                // we repeat last strip from the previous t only for last strips_to_repeat ts
                if (strip_idx == previous_strip_idx && t_idx < output_t - strips_to_repeat) {
                    continue;
                }

                int strip_idx_to_encode = act_t == 1 ? strip_idx : m_k * t_idx + strip_idx;
                // last strip in row/tile fields are populated later on since at this moment they are unknown
                cur_strip_row.emplace_back(
                    strip_idx_to_encode, /* last_strip_in_row */ false, /* last_strip_in_tile */ false);
                strip &cur_strip = cur_strip_row.back();

                const int nz_ublocks_in_strip = nz_ublocks_per_strip + (extra_ublocks > nz_strip_count ? 1 : 0);
                for (int ub_idx = 0; ub_idx < nz_ublocks_in_strip; ub_idx++) {

                    cur_strip.ublock_vec.emplace_back(ub_idx);
                    ublock &cur_ublock = cur_strip.ublock_vec.back();

                    const int nz_tiles_in_ublock = nz_tiles_per_ublock + (nz_ublock_count < extra_tiles ? 1 : 0);
                    for (int tile_r_idx = 0; tile_r_idx < ub_r; tile_r_idx++) {
                        log_assert(
                            tile_r_idx < (1 << tile_row_ptr_bits),
                            "Not enough bits to store tile_row_idx aka output ublock_rt in tile index encoding");
                        for (int tile_c_idx = 0; tile_c_idx < u_kt; tile_c_idx++) {
                            log_assert(
                                tile_c_idx < (1 << tile_col_ptr_bits),
                                "Not enough bits to store tile_cold_idx aka u_kt in tile index encoding");

                            cur_ublock.tile_vec.emplace_back(
                                get_next_unique_tile_idx(), (tile_r_idx << tile_col_ptr_bits) | tile_c_idx);

                            nz_tile_count++;
                            if (tile_r_idx * u_kt + tile_c_idx + 1 == nz_tiles_in_ublock) {
                                break;
                            }
                        }
                        if ((tile_r_idx + 1) * u_kt >= nz_tiles_in_ublock) {
                            break;
                        }
                    }
                    nz_ublock_count++;
                }
                nz_strip_count++;
            }
            previous_strip_idx = max_strip_idx;
            cur_strip_row.back().last_strip_in_row = true;
        }
    }

    log_info(
        tt::LogVerif, 
        "Encoding info: number of encoded strips - {}, encoded non-zero strips - {}, non-zero ublocks - {}, non-zero "
        "tiles - {}.",
        nz_strip_count,
        nz_strip_count,
        nz_ublock_count,
        nz_tile_count);

    update_last_strip_bits();
}

void verif::stimulus::sparse::sparse_encoder::pad_encodings()
{
    for (int r = 0; r < encoding_tile_vec.size(); ++r)
    {
        const int tiles_to_add = num_index_tiles - encoding_tile_vec[r].size();
        log_assert(
            tiles_to_add >= 0,
            "Not enough index tiles allocated to store encodings num_index_tiles={} vs generated_encoding_tiles={}",
            num_index_tiles, encoding_tile_vec[r].size());
        for (int t = 0; t < tiles_to_add; ++t)
        {
            encoding_tile_vec[r].push_back(encoding_tile(encoding_tile_capacity));
        }
    }
}

string verif::stimulus::sparse::sparse_encoder::get_string() const
{
    stringstream ss;
    ss << "Num block rows: " << strip_vec.size() << endl;
    for (int row_idx = 0; row_idx < strip_vec.size(); ++row_idx)
    {
        ss << "row_" << row_idx << " num_strips=" << strip_vec[row_idx].size() << endl;
        for (int s_idx = 0; s_idx < strip_vec[row_idx].size(); ++s_idx)
        {
            ss << "\t" << "loop_idx_" << s_idx << " strip_idx=" << strip_vec[row_idx][s_idx].idx
               << " last_strip_in_row=" << strip_vec[row_idx][s_idx].last_strip_in_row
               << " last_strip_in_tile=" << strip_vec[row_idx][s_idx].last_strip_in_tile
               << endl;
            ss << "\t" << "loop_idx_" << s_idx << " num_ublocks="
               << strip_vec[row_idx][s_idx].ublock_vec.size() << endl;
            for (int ub_idx = 0; ub_idx < strip_vec[row_idx][s_idx].ublock_vec.size(); ++ub_idx)
            {
                const ublock &ub = strip_vec[row_idx][s_idx].ublock_vec[ub_idx];
                ss << "\t" << "\t" << "ub_idx_" << ub_idx << " idx="
                   << ub.idx << " num_tiles=" << ub.tile_vec.size() << endl;
                for (int t_idx = 0; t_idx < ub.tile_vec.size(); ++t_idx)
                {
                    const tile &t = ub.tile_vec[t_idx];
                    ss << "\t" << "\t" << "\t" << "t_idx_" << t_idx << " unique_tile_idx=" << t.unique_tile_idx
                       << " within_ublock_idx=" << t.within_ublock_idx << endl;
                }
            }
            ss << endl;
        }
    }
    return ss.str();
}

void verif::stimulus::sparse::sparse_encoder::compress_encoding()
{
    const int tile_idx_ptr_bits = 16 - sparse_tile_ptr_bits;
    for (int row_idx = 0; row_idx < strip_vec.size(); ++row_idx)
    {
        encoding_tile_vec.emplace_back(vector<encoding_tile>());
        vector<encoding_tile> &cur_row_encoding = encoding_tile_vec.back();
        encoding_tile cur_enc_tile(encoding_tile_capacity);
        for (int s_idx = 0; s_idx < strip_vec[row_idx].size(); ++s_idx)
        {
            const strip &cur_strip = strip_vec[row_idx][s_idx];
            log_debug(tt::LogVerif, "Strip row={} idx={} - strip_idx={} last_strip_in_tile={} byte_usage={}",
                      row_idx, s_idx, cur_strip.idx, cur_strip.last_strip_in_tile, cur_strip.get_byte_usage());
            encoding_tile::strip_encoding_u strip_enc;
            strip_enc.val = 0;
            strip_enc.strip.index = cur_strip.idx;
            strip_enc.strip.last_strip_in_row = cur_strip.last_strip_in_row;
            strip_enc.strip.last_strip_in_tile = cur_strip.last_strip_in_tile;
            cur_enc_tile.push_bytes(strip_enc.val);
            cur_enc_tile.push_bytes((uint16_t)cur_strip.ublock_vec.size());
            for (int ub_idx = 0; ub_idx < cur_strip.ublock_vec.size(); ++ub_idx)
            {
                const ublock &cur_ublock = cur_strip.ublock_vec[ub_idx];
                // For each ublock we store 2 values within 16 bits:
                // - ublock_idx ~ width sparse_ublock_idx_bits
                // - num_nz_tiles  ~ width 16-sparse_ublock_idx_bits
                const int max_num_tiles = 1 << (16 - sparse_ublock_idx_bits);
                // maximum number of tiles in ublock is the first number that cannot be represented with num_nz_tiles bits 
                // (16 - sparse_ublock_idx_bits), so we represent it with 0
                const int num_tiles = cur_ublock.tile_vec.size() == max_num_tiles ? 0 : cur_ublock.tile_vec.size();

                uint16_t val_to_store = (num_tiles << sparse_ublock_idx_bits) |
                                        (cur_ublock.idx & ((1 << sparse_ublock_idx_bits) - 1));
                cur_enc_tile.push_bytes(val_to_store);
                for (const auto &tile : cur_ublock.tile_vec)
                {
                    uint16_t val_to_store = (tile.unique_tile_idx << tile_idx_ptr_bits) |
                                            (tile.within_ublock_idx & ((1 << tile_idx_ptr_bits) - 1));
                    cur_enc_tile.push_bytes(val_to_store);
                }
            }
            if (cur_strip.last_strip_in_tile)
            {
                cur_row_encoding.push_back(cur_enc_tile);
                cur_enc_tile = encoding_tile(encoding_tile_capacity);
            }
        }
    }
}

int verif::stimulus::sparse::sparse_encoder::get_compressed_size() const
{
    int total_size = 0;
    for (int r = 0; r < encoding_tile_vec.size(); ++r)
    {
        total_size += num_index_tiles * encoding_tile_capacity;
    }
    return total_size;
}

string verif::stimulus::sparse::sparse_encoder::get_num_encoding_tiles_needed_per_row() const
{
    stringstream ss;
    for (int r = 0; r < encoding_tile_vec.size(); ++r)
    {
        ss << "row=" << r << " : " << encoding_tile_vec[r].size() << endl;
    }
    return ss.str();
}

tt_tile verif::stimulus::sparse::sparse_encoder::to_tt_tile(const int row_idx, const int tile_idx) const
{
    const encoding_tile &enc_tile = encoding_tile_vec[row_idx][tile_idx];
    tt_tile result_tile(encoding_tile_data_format);

    for (int r = 0; r < tt::constants::TILE_HEIGHT; ++r)
    {
        for (int c = 0; c < tt::constants::TILE_WIDTH; ++c)
        {
            result_tile.t_u32[r][c] = enc_tile.get_num(r * tt::constants::TILE_WIDTH + c);
        }
    }
    return result_tile;
}

int verif::stimulus::sparse::sparse_encoder::get_num_index_tiles() const
{
    return num_index_tiles;
}

//----------------------------------------------------------------------------------------------------------------------

void verif::stimulus::sparse::fill_sparse_encoding_tensor(tt_tensor &tensor, const VerifStimulusConfig &config)
{
    int act_t = config.sparse_encoding_attrs.matmul_ident_info.attributes.act_t;
    const int output_t = config.sparse_encoding_attrs.matmul_ident_info.output_dim.t;

    if (act_t == 0) {
        act_t = config.sparse_encoding_attrs.matmul_ident_info.input_dims[1].t;
    }

    log_assert(
        output_t == act_t || act_t == 1,
        "input1_t = {} and output_t = {} are not compatible. Encoding generation for sparse matmul supports "
        "mismatch between input1_t and output_t only if input1_t == 1.",
        act_t,
        output_t);

    const int num_sparse_tiles = config.sparse_encoding_attrs.matmul_ident_info.attributes.num_sparse_tiles;
    int sparse_ublock_idx_bits = config.sparse_encoding_attrs.matmul_ident_info.attributes.sparse_ublock_idx_bits;

    if (sparse_ublock_idx_bits <= 0) {
       sparse_ublock_idx_bits = config.sparse_encoding_attrs.matmul_ident_info.attributes.sparse_tile_ptr_bits;
    }

    // TODO read data format from tensor and derive size of encoded tile from it.
    sparse_encoder sp_enc(num_sparse_tiles,
                          config.sparse_encoding_attrs.matmul_ident_info.attributes.num_index_tiles,
                          config.sparse_encoding_attrs.matmul_ident_info.attributes.sparse_tile_ptr_bits,
                          sparse_ublock_idx_bits,
                          output_t,
                          act_t,
                          // for sparse encoding generation, we need to use grid size before possible grid transpose;
                          // grid_size: [5, 1], grid_transpose: true 
                          // this setup means we will end up with cores with grid [1, 5], but the cores will
                          // behave as if they are still in one column - whole input1 will be sent to all cores,
                          // while input0 and input2 wll be generated separately for each core
                          config.sparse_encoding_attrs.matmul_ident_info.original_grid_size_y(),
                          config.sparse_encoding_attrs.matmul_ident_info.attributes.m_k,
                          config.sparse_encoding_attrs.matmul_ident_info.output_dim.mblock_m,
                          config.sparse_encoding_attrs.matmul_ident_info.output_dim.mblock_n,
                          config.sparse_encoding_attrs.matmul_ident_info.output_dim.ublock_rt,
                          config.sparse_encoding_attrs.matmul_ident_info.attributes.u_kt,
                          config.sparse_encoding_attrs.zero_strip_freq,
                          config.sparse_encoding_attrs.zero_ublock_freq,
                          config.sparse_encoding_attrs.zero_tile_freq,
                          config.sparse_encoding_attrs.nz_strips,
                          config.sparse_encoding_attrs.nz_ublocks,
                          config.sparse_encoding_attrs.nz_tiles,
                          config.sparse_encoding_attrs.enc_tile_byte_size);

    if (config.type == StimulusType::SparseEncoding) {
        sp_enc.generate_encoding_using_zero_frequencies();
    } else {
        sp_enc.generate_encoding_using_nonzero_counts();
    }
    
    log_debug(tt::LogVerif, "Generated encoding \n{}", sp_enc.get_string());

    sp_enc.compress_encoding();
    sp_enc.pad_encodings();

    int row_idx = 0;
    int tile_idx = 0;
    tensor.reserve_tile_tensor();
    tensor.apply([&sp_enc, &row_idx, &tile_idx](tt_tile &tile) {
        tile = sp_enc.to_tt_tile(row_idx, tile_idx);
        ++tile_idx;
        if (tile_idx == sp_enc.get_num_index_tiles())
        {
            tile_idx = 0;
            ++row_idx;
        }
    });
}
