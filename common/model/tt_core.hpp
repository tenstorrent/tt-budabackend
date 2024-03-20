// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <functional>
#include <vector>

#include "hlks/inc/hlk_api.h"   
#include "common/model/base_types.hpp"
#include "common/model/hlk_desc.hpp"

#include "model/tile.hpp"

#include <boost/fiber/all.hpp>

// IO
using std::ifstream;
using std::ofstream;
using std::stringstream;
using std::cout;

// Containers
using std::deque;
using std::string;
using std::vector;
using std::pair;

// Forward declaration
namespace tt
{
    class tt_buffer;
    class tt_tile;
    class tt_dram_io;
    class tt_op;
    class tt_dram;
}

////////////////////////
// Cores contain all the stuff needed to model tensix functionality
// 1. Input buffers - which the core owns, but which do note own their tiles
//    their tiles are instead pointing to tiles in output buffers that sourced them
// 2. Output buffers - which the core owns and de-allocates; probably most memory
//    heavy parts of the core model
// 3. Parameter buffers which the core does not own, DRAM owns them and deallocates
//    them. The core does pretend they are local and owned, and uses them in tabulating
//    memory footprint
// 4. Destination register set, which is implemented as a 2d vector of tiles
class tt_core
{
    static constexpr int MAX_BUFFER_COUNT = 64; // up to 1 per stream
    public:

    // Whether to initialize simulation memory in constructor (not needed usually)
    const static bool init_tt_core;

    tt::tt_logical_physical_core_coords core_coords;
    tt::tt_hlk_desc local_desc;
    tt::tt_buffer *b_arr[64] = {NULL};

    // for simplicity using array of 64, but "num_free_tiles" / "num_packed_tile" is relevant only for output streams
    int b_arr_num_free_tiles[64];
    int b_arr_num_packed_tiles[64];

    vector <tt::tt_buffer *> owned_buf_vec;
    tt::tt_op *my_op_ptr = nullptr;

    unsigned int next_bufid;
    unsigned int next_param_bufid;
    unsigned int next_intermediate_bufid;
    unsigned int next_in_bufid;

    std::vector<tt::tt_tile> dst;

    bool dst_valid; // FIXME: do we need this?

    int dst_size_full = tt::constants::MAX_DST_SIZE;
    bool dst_acquired;
    bool pack_executed;
    int dst_offset;
    DstMode dst_mode = DstMode::NUM_DST_MODES;
    static_assert(3 == NUM_DST_MODES);
    int dst_mode_size[NUM_DST_MODES];

    // Shifted packer packs a partial tile to L1... model needs this partial tile
    // recorded so we can assemble a full one later. Real hw just writes this to L1.
    std::vector<tt::tt_tile> partial_packed_tile;

    vector<int> get_allocated_buffer_ids_for_range(int start_id, int end_id);
    vector<int> get_allocated_input_buffer_ids();
    vector<int> get_allocated_output_buffer_ids();
    vector<int> get_allocated_param_buffer_ids();
    vector<int> get_allocated_intermediate_buffer_ids();

    // This is state used by coremodel to track wait_tiles and wait_for_tiles for each stream
    std::unordered_map<int, int> stream_wait_tiles;
    std::unordered_map<int, int> stream_wait_for_tiles;
    // This is state to track how many tiles were popped by hlk_pop_tiles for each stream
    std::unordered_map<int, int> stream_pop_tiles;
    bool scout_mode;    //!< Set to true when we want to compute wait tiles and wait_for_tiles
    // This is to save the previous acquire dest state if coremodel runs out of tiles on hlk_wait call
    bool dst_acquired_prev;
    int dst_offset_prev;
    void rollback_acquire_dst();
    bool wrote_intermediate_buffer;
    // For synchronizing between Boost fiber HLK thread and main coremodel thread
    boost::fibers::condition_variable stream_cond;
    boost::fibers::mutex              stream_mutex;
    // the synchronization variables to be locked
    int wait_stream_id = -1;
    int wait_stream_tiles = 0;
    int wait_for_free_tiles_stream_id = -1;
    int wait_for_free_tiles_stream_tiles = 0;
    void clear_wait_state();


    int get_buffer_id_list_L1_usage_bytes(const vector<int>& buf_id_list);
    int get_allocated_input_buffers_L1_usage_bytes();
    int get_allocated_output_buffers_L1_usage_bytes();
    int get_allocated_param_buffers_L1_usage_bytes();
    int get_allocated_intermediate_buffers_L1_usage_bytes();
    int get_allocated_all_buffers_L1_usage_bytes();
    int get_non_buffer_reserved_L1_usage_bytes();
    int get_total_allocated_L1_usage_bytes();

    void init_b_arr_num_free_and_packed_tiles();
    tt_core(tt::tt_hlk_desc desc, tt::tt_op *op_ptr, int max_num_dst_tiles = 16);
    ~tt_core();
    unsigned int get_logical_absolute_row_id() const;
    unsigned int get_logical_absolute_col_id() const;
    unsigned int get_logical_relative_row_id() const;
    unsigned int get_logical_relative_col_id() const;
    unsigned int get_physical_row_id() const;
    unsigned int get_physical_col_id() const;
    void epoch_reset();
    unsigned int get_next_param_bufid();
    unsigned int get_next_intermediate_bufid();
    unsigned int get_num_param_bufs();
    unsigned int get_next_in_bufid();
    tt::tt_buffer *get_buf_ptr(unsigned int b);
    tt::tt_buffer *get_in_buf_ptr(unsigned int b);
    tt::tt_buffer *get_param_buf_ptr(unsigned int b);
    tt::tt_buffer *get_out_buf_ptr(unsigned int b);
    tt::tt_buffer *get_int_buf_ptr(unsigned int b);
    bool input_valid(int index);
    bool buffer_ready_to_write (int index);
    void clear_buffer(int index);
    bool any_input_ready();
    void flip_wr_phase(int index);
    // To be used only for intermediate buffers!!!!
    // others read phase is flipped by pipe.tx()
    void flip_rd_phase(int index);
    void hlk_prolog();
    void hlk_epilog(vector<tt::tt_dram_io*> dram_io_list, int epoch);
    void copy_dram_buf(int id, tt::tt_buffer *buf);
    void copy_param_buf(int id, tt::tt_buffer *buf);
    void set_buffer_for_op_epilog(int id, tt::tt_dram* dram, int epoch);
    void create_core_buffers(bool owned, bool dbl, bool books_only);
    void write_tile_to_dst(int dstindex, const tt::tt_tile& tile);
    tt::tt_tile& read_tile_from_dst(int dstindex);

    void hlk_clear_dst();
    void hlk_clear_dst(int offset, int size);
    void hlk_acquire_dst();
    void hlk_release_dst();
    void hlk_reconfig_packer_df(int old_stream, int new_stream);
    void hlk_reconfig_unpacker_df(int old_lstream, int new_lstream, int old_rstream, int new_rstream);
    void hlk_relu_config(int config);
    void hlk_add_tile(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose);
    void hlk_subtract_tile(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose);
    void hlk_multiply_tile(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose);
    void hlk_add_tile_bcast(Dim dim, int lstream, int rstream, int lindex, int rindex, int dstindex);
    void hlk_multiply_tile_bcast(Dim dim, int lstream, int rstream, int lindex, int rindex, int dstindex);
    void hlk_subtract_tile_bcast(Dim dim, int lstream, int rstream, int lindex, int rindex, int dstindex);
    void hlk_add_tile_from_dst(int rstream, int rindex, int dstindex);
    void hlk_subtract_tile_from_dst(int rstream, int rindex, int dstindex);
    void hlk_multiply_tile_from_dst(int rstream, int rindex, int dstindex);
    void hlk_copy_tile_to_dst(int stream, int index, int dstindex, int transpose);
    void hlk_load_tile_to_dst(int stream, int index, int dstindex, int transpose);
    void hlk_transpose_yz_tile(int stream, int index, int dstindex, int dim, int phase);
    void hlk_mm_tile(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim);
    void hlk_mm_block(int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim);
    void hlk_reduce_tile(ReduceFunc reduce_func, Dim dim, int lstream, int lindex, int dstindex, float coefficient);
    void hlk_tilize(int rstream, int rindex, int dstindex);
    void hlk_sfpu_op_requant_int32(int dstindex_a, int dstindex_b);
    void hlk_sfpu_op_dequant_int32(int dstindex_a, int dstindex_b);

    void hlk_pop_tiles(int stream, int num_tiles);
    void hlk_wait_tiles(int stream, int num_tiles);
    void hlk_wait_for_free_tiles(int stream, int num_tiles);
    void hlk_push_tiles(int stream, int num_tiles);
    void hlk_pack_tile_to_stream(int dst_index, int stream, int tile_index, bool relu = false);
    void hlk_pack_tile_to_stream(int dst_index, int stream);
    void hlk_pack_tile_to_lbuf(int dst_index, int lbuf, int buf_tile_index);

    std::function<tt::tt_tile(const tt::tt_tile &)> get_sfpu_func_one_arg(SfpuOp sfpu_op) {
        std::function<tt::tt_tile(const tt::tt_tile &)> sfpu_function;

        switch (sfpu_op) {
            case SfpuOp::Exp: sfpu_function = tt::math::exp; break;
            case SfpuOp::Log: sfpu_function = tt::math::log; break;
            case SfpuOp::Sqrt: sfpu_function = tt::math::sqrt; break;
            case SfpuOp::Gelu: sfpu_function = tt::math::gelu; break;
            case SfpuOp::GeluDerivative: sfpu_function = tt::math::gelu_derivative; break;
            case SfpuOp::Reciprocal: sfpu_function = tt::math::reciprocal; break;
            case SfpuOp::Tanh: sfpu_function = tt::math::tanh; break;
            case SfpuOp::Sigmoid: sfpu_function = tt::math::sigmoid; break;
            case SfpuOp::Square: sfpu_function = tt::math::square; break;
            case SfpuOp::Sine: sfpu_function = tt::math::sine; break;
            case SfpuOp::Cosine: sfpu_function = tt::math::cosine; break;
            case SfpuOp::Abs: sfpu_function = tt::math::abs; break;
            case SfpuOp::ReluMin: sfpu_function = tt::math::relu; break;
            case SfpuOp::ReluMax: sfpu_function = tt::math::relu; break;
            default: log_fatal("Invalid sfpu_op: {}", (int)sfpu_op); break;
        }

        return sfpu_function;
    }

    std::function<tt::tt_tile(const tt::tt_tile &, float)> get_sfpu_func_two_args(SfpuOp sfpu_op) {
        std::function<tt::tt_tile(const tt::tt_tile &, float)> sfpu_function;

        switch (sfpu_op) {
            case SfpuOp::LRelu: sfpu_function = tt::math::lrelu; break;
            case SfpuOp::Power: sfpu_function = tt::math::power; break;
            case SfpuOp::Dropout: sfpu_function = tt::math::dropout; break;
            default: log_fatal("Invalid sfpu_op: {}", (int)sfpu_op);
        }

        return sfpu_function;
    }

    template <SfpuOp sfpu_op>
    void hlk_unary_sfpu_op(int dst_index, unsigned int sfpu_op_param_0, unsigned int sfpu_op_param_1) {
        tt::tt_tile input_tile = read_tile_from_dst(dst_index);
        tt::tt_tile output_tile;

        switch (sfpu_op) {
            case SfpuOp::Exp:
            case SfpuOp::Log:
            case SfpuOp::Sqrt:
            case SfpuOp::Gelu:
            case SfpuOp::GeluDerivative:
            case SfpuOp::Reciprocal:
            case SfpuOp::Tanh:
            case SfpuOp::Sigmoid:
            case SfpuOp::Square:
            case SfpuOp::Sine:
            case SfpuOp::Cosine:
            case SfpuOp::Abs:
            case SfpuOp::ReluMin:
            case SfpuOp::ReluMax: {
                std::function<tt::tt_tile(const tt::tt_tile &)> sfpu_func_one_arg = get_sfpu_func_one_arg(sfpu_op);
                output_tile = sfpu_func_one_arg(input_tile);
                break;
            }
            case SfpuOp::LRelu:
            case SfpuOp::Power:
            case SfpuOp::Dropout: {
                std::function<tt::tt_tile(const tt::tt_tile &, float)> sfpu_function_two_args =
                    get_sfpu_func_two_args(sfpu_op);
                if (sfpu_op != SfpuOp::Dropout) {
                    output_tile = sfpu_function_two_args(input_tile, sfpu_op_param_0);
                } else {
                    // Dropout
                    union {
                        float f;
                        int i32;
                    } tmp = {.i32 = (int)sfpu_op_param_0};
                    output_tile = sfpu_function_two_args(input_tile, tmp.f);
                }
                break;
            }
            case SfpuOp::Max: {
                tt::tt_tile input_tile1 = read_tile_from_dst(dst_index + 1);
                output_tile = tt::math::max(input_tile, input_tile1);
                break;
            }
            default: log_fatal("Invalid sfpu_op: {}", (int)sfpu_op); break;
        }

        write_tile_to_dst(dst_index, output_tile);
    }

    void print(std::ostream & os) const;

   private:
    // Common code for all hlk_pack_*tile* functions.
    void _pack_tile_setup(int stream);
};  // tt_core

inline std::ostream & operator<<(std::ostream & os, const tt_core & core)
{
    core.print(os);
    return os;
}

// hlk api impl currently here, possibly move into a seperate file
void hlk_wait_tiles(tt_core* core_ptr, int stream, int num_tiles);
void hlk_pack_tile_to_stream(tt_core* core_ptr, int dst_index, int stream, int pos, bool pack_l1_acc);
void hlk_copy_tile_to_dst(tt_core* core_ptr, int stream, int index, int dstindex, int transpose);
void hlk_load_tile_to_dst(tt_core* core_ptr, int stream, int index, int dstindex, int transpose);
void hlk_transpose_xy_tile(tt_core* core_ptr, int stream, int index, int dstindex, int transpose);
void hlk_transpose_yz_tile(tt_core* core_ptr, int stream, int index, int dstindex, int dim, int phase);
void hlk_mm_tile(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim);
void hlk_mm_block(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose, int inner_c_dim, int inner_r_dim, int inner_d_dim);
void hlk_wait_for_free_tiles(tt_core* core_ptr, int stream, int num_tiles);
void hlk_push_tiles(tt_core* core_ptr, int stream, int num_tiles);

void hlk_add_tile(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose);
void hlk_tilize(tt_core* core_ptr, int lstream, int lindex, int dstindex, int ct_dim);
void hlk_multiply_tile(tt_core* core_ptr, int lstream, int rstream, int lindex, int rindex, int dstindex, int transpose);
void hlk_acquire_dst(tt_core* core_ptr);
void hlk_release_dst(tt_core *core_ptr);
void hlk_reconfig_packer_df(tt_core *core_ptr, int old_stream, int new_stream);
void hlk_reconfig_unpacker_df(tt_core *core_ptr, int old_lstream, int new_lstream, int old_rstream, int new_rstream);
void hlk_reconfig_unpacker_df_srca(tt_core *core_ptr, int old_lstream, int new_lstream);
void hlk_reconfig_unpacker_df_srcb(tt_core *core_ptr, int old_rstream, int new_rstream);
void hlk_reconfig_unpacker_df(tt_core *core_ptr, int new_lstream, int new_rstream);
void hlk_reconfig_unpacker_df_srca(tt_core *core_ptr, int new_lstream);
void hlk_reconfig_unpacker_df_srcb(tt_core *core_ptr, int new_rstream);
void hlk_relu_config(tt_core *core_ptr, int config);

