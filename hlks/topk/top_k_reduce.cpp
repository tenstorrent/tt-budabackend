// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#include "hlks/inc/hlk_api.h"

const int TOPK_MAX_NUM_INPUTS = 2;
struct hlk_args_t {
    std::int32_t block_tile_dim;
    std::int32_t block_cnt;
    std::int32_t batch_cnt;
    std::int32_t num_m_sub_blocks;
    std::int32_t num_n_sub_blocks;
    std::int32_t num_tiles_per_m_sub_block;
    std::int32_t num_tiles_per_n_sub_block;
    std::int32_t k;
    std::int32_t sort;
    std::int32_t N;
    std::int32_t M;
    std::int32_t logk;
    std::int32_t num_k_sequences;
    std::int32_t seqs_per_2tiles;
    std::int32_t tiles_per_seq;
    std::int32_t kernel_broadcast[TOPK_MAX_NUM_INPUTS];
};

constexpr int gl_dst_data_start_index = 0;
constexpr int gl_dst_indices_start_index = 2;

void hlk_setup_kernel(tt_core *core_ptr, const hlk_args_t *args) {
    hlk_hw_config_two_operands(core_ptr, HlkOperand::in0, HlkOperand::in1, false);
}

TT_HLK_ALWAYS_INLINE void copy_input_to_intermediate(
    tt_core *core_ptr, int macro_block_cnt, int micro_block_size, HlkOperand input, HlkOperand intermed, bool transpose=true) {    
    hlk_copy_tile_to_dst_init_short(core_ptr, input, transpose, transpose);
    hlk_reconfig_unpacker_df_srca(core_ptr, input);
    hlk_reconfig_packer_df(core_ptr, intermed);

    for (int block = 0; block < macro_block_cnt; ++block) {
        hlk_acquire_dst(core_ptr);
        hlk_wait_tiles(core_ptr, input, micro_block_size);
        for (int t = 0; t < micro_block_size; ++t) {
            hlk_copy_tile_to_dst(core_ptr, input, t, t, transpose);
        }
        hlk_pop_tiles(core_ptr, input, micro_block_size);

        hlk_wait_for_free_tiles(core_ptr, intermed, micro_block_size);

        for (int t = 0; t < micro_block_size; ++t) {
            hlk_pack_tile_to_stream(core_ptr, t, intermed);
        }

        hlk_push_tiles(core_ptr, intermed, micro_block_size);
        hlk_release_dst(core_ptr);
    }
}

TT_HLK_ALWAYS_INLINE void copy_from_intermediate(
    tt_core *core_ptr, int macro_block_cnt, int micro_block_size, int intermed_buf_size, HlkOperand intermed, HlkOperand output, bool transpose=true) {    
    hlk_copy_tile_to_dst_init_short(core_ptr, intermed, transpose, transpose);
    hlk_reconfig_unpacker_df_srca(core_ptr, intermed);
    hlk_reconfig_packer_df(core_ptr, output);

    hlk_wait_tiles(core_ptr, intermed, intermed_buf_size);
    for (int block = 0; block < macro_block_cnt; ++block) {
        hlk_acquire_dst(core_ptr);
        for (int t = 0; t < micro_block_size; ++t) {
            hlk_copy_tile_to_dst(core_ptr, intermed, t, t, transpose);
        }

        hlk_wait_for_free_tiles(core_ptr, output, micro_block_size);
        for (int t = 0; t < micro_block_size; ++t) {
            hlk_pack_tile_to_stream(core_ptr, t, output);
        }
        hlk_push_tiles(core_ptr, output, micro_block_size);
        hlk_release_dst(core_ptr);
    }
    hlk_pop_tiles(core_ptr, intermed, intermed_buf_size);
}

TT_HLK_ALWAYS_INLINE void copy_index_and_data_tiles_to_dest(
    tt_core *core_ptr, HlkOperand input0, HlkOperand input1, int tile_ind0, int tile_ind1, bool skip_second=false) {

    hlk_reconfig_unpacker_df_srca(core_ptr, input0);
    hlk_copy_tile_to_dst(core_ptr, input0, tile_ind0, gl_dst_data_start_index+0, false);
    if (!skip_second) {
        hlk_copy_tile_to_dst(core_ptr, input0, tile_ind1, gl_dst_data_start_index+1, false);
    }

    hlk_reconfig_unpacker_df_srca(core_ptr, input1);
    hlk_copy_tile_to_dst(core_ptr, input1, tile_ind0, gl_dst_indices_start_index+0, false);
    if (!skip_second) {
        hlk_copy_tile_to_dst(core_ptr, input1, tile_ind1, gl_dst_indices_start_index+1, false);
    }

}

TT_HLK_ALWAYS_INLINE void pack_index_and_data_tiles_from_dest(
    tt_core *core_ptr, HlkOperand output0, HlkOperand output1, int tile_ind0, int tile_ind1, bool skip_second=false) {

    hlk_reconfig_packer_df(core_ptr, output0);
    hlk_pack_tile_to_stream(core_ptr, gl_dst_data_start_index+0, output0, tile_ind0);
    if (!skip_second) {
        hlk_pack_tile_to_stream(core_ptr, gl_dst_data_start_index+1, output0, tile_ind1);
    }

    hlk_reconfig_packer_df(core_ptr, output1);
    hlk_pack_tile_to_stream(core_ptr, gl_dst_indices_start_index+0, output1, tile_ind0);
    if (!skip_second) {
        hlk_pack_tile_to_stream(core_ptr, gl_dst_indices_start_index+1, output1, tile_ind1);
    }

}

SortDir hlk_switch_sort_direction(tt_core *core_ptr, const SortDir idir) {
    return (idir == SortDir::ArgMax) ? SortDir::ArgMin : SortDir::ArgMax;
 }

void hlk_process_single_input(tt_core *core_ptr, const hlk_args_t *args) {

    for (int batch = 0; batch < args->batch_cnt; ++batch) {
        copy_input_to_intermediate(core_ptr, args->block_cnt, args->block_tile_dim, HlkOperand::in0, HlkOperand::intermed0, true);
        copy_input_to_intermediate(core_ptr, args->block_cnt, args->block_tile_dim, HlkOperand::in1, HlkOperand::intermed1, true);

        hlk_copy_tile_to_dst_init_short(core_ptr, HlkOperand::in0, false, false);                                       // Enough to do init for in0 only
        hlk_topk_init(core_ptr, HlkOperand::in0);   // Enough to do init for in0 only
        
        int total_block_tiles = args->block_cnt * args->block_tile_dim;
        int num_k_sequences = args->num_k_sequences;
        int seqs_per_2tiles = args->seqs_per_2tiles;
        int tiles_per_seq = args->tiles_per_seq;
        SortDir dir = SortDir::ArgMax;
        bool switch_dir = args->k == 64;
        int hlk_left_ind = 0;
        int target_tiles = ((num_k_sequences == 1) && (tiles_per_seq == 1)) ? 1 : 2;

        if (args->k > 16) {// Initial Rebuild
            hlk_wait_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_wait_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);
            int i = 0;
            int sel_tile_id[2];
            int sel_tile_id_ptr = 0;
            dir = SortDir::ArgMax;
            while (i < num_k_sequences) {
                for (int t=0; t<tiles_per_seq; t++) {
                    hlk_left_ind = i*args->k + t*32;
                    int hlk_ltile_id = hlk_left_ind >> 5;       // left_ind / 32
                    sel_tile_id[sel_tile_id_ptr] = hlk_ltile_id;
                    sel_tile_id_ptr++;
                    if (sel_tile_id_ptr == target_tiles) {
                        hlk_acquire_dst(core_ptr);
                        copy_index_and_data_tiles_to_dest(core_ptr, HlkOperand::intermed0, HlkOperand::intermed1, sel_tile_id[0], sel_tile_id[1], target_tiles==1);
                        hlk_sfpu_topk_rebuild(core_ptr, HlkOperand::intermed0, 0, (std::uint32_t)dir, 0, args->k, args->logk, target_tiles==1);
                        pack_index_and_data_tiles_from_dest(core_ptr, HlkOperand::intermed0, HlkOperand::intermed1, sel_tile_id[0], sel_tile_id[1], target_tiles==1);
                        hlk_release_dst(core_ptr);
                        sel_tile_id_ptr = 0;
                        dir = switch_dir ? hlk_switch_sort_direction(core_ptr, dir) : dir;
                    }
                }
                i += (seqs_per_2tiles >> 1);
            }

            hlk_pop_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_pop_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);

            hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);

            hlk_push_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_push_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);
        }

        // Merge & Rebuild
        int num_m_loops = args->M;
        for (int m=0; m<num_m_loops; m++) {

            // Merge
            hlk_wait_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_wait_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);
            int dist = (1<<m)*args->k;
            int hlk_left_ind = 0;
            int i=0;
            while (i < num_k_sequences) {
                for (int t=0; t<tiles_per_seq; t++) {
                    hlk_left_ind = i*(1<<m)*args->k + (t*32);
                    int hlk_right_ind = hlk_left_ind + dist;
                    int hlk_ltile_id = hlk_left_ind >> 5;       // left_ind / 32
                    int hlk_rtile_id = hlk_right_ind >> 5;       // right_ind / 32
                    if (hlk_ltile_id == hlk_rtile_id) {
                        hlk_rtile_id = hlk_ltile_id + 1;
                    }
                    hlk_acquire_dst(core_ptr);
                    copy_index_and_data_tiles_to_dest(core_ptr, HlkOperand::intermed0, HlkOperand::intermed1, hlk_ltile_id, hlk_rtile_id);
                    hlk_sfpu_topk_merge(core_ptr, HlkOperand::intermed0, 0, m, args->k);
                    pack_index_and_data_tiles_from_dest(core_ptr, HlkOperand::intermed0, HlkOperand::intermed1, hlk_ltile_id, hlk_rtile_id);
                    hlk_release_dst(core_ptr);
                }
                i += seqs_per_2tiles;
            }

            hlk_pop_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_pop_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);

            hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);

            hlk_push_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_push_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);

            seqs_per_2tiles = (seqs_per_2tiles == 2) ? 2 : seqs_per_2tiles >> 1;
            num_k_sequences = num_k_sequences >> 1;
            int target_tiles = (total_block_tiles == 1 || ((num_k_sequences == 1) && (tiles_per_seq == 1))) ? 1 : 2;

            // Rebuild
            hlk_wait_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_wait_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);
            i=0;
            int sel_tile_id[2];
            int sel_tile_id_ptr = 0;
            dir = SortDir::ArgMax;
            while (i < num_k_sequences) {
                for (int t=0; t<tiles_per_seq; t++) {
                    hlk_left_ind = i*(1<<(m+1))*args->k + t*32;
                    int hlk_ltile_id = hlk_left_ind >> 5;       // left_ind / 32
                    sel_tile_id[sel_tile_id_ptr] = hlk_ltile_id;
                    sel_tile_id_ptr++;
                    if (sel_tile_id_ptr == target_tiles) {
                        hlk_acquire_dst(core_ptr);
                        copy_index_and_data_tiles_to_dest(core_ptr, HlkOperand::intermed0, HlkOperand::intermed1, sel_tile_id[0], sel_tile_id[1], target_tiles==1);
                        hlk_sfpu_topk_rebuild(core_ptr, HlkOperand::intermed0, 0, (std::uint32_t)dir, m, args->k, args->logk, target_tiles==1);
                        pack_index_and_data_tiles_from_dest(core_ptr, HlkOperand::intermed0, HlkOperand::intermed1, sel_tile_id[0], sel_tile_id[1], target_tiles==1);
                        hlk_release_dst(core_ptr);
                        sel_tile_id_ptr = 0;
                        dir = switch_dir ? hlk_switch_sort_direction(core_ptr, dir) : dir;
                    }
                }
                i += (seqs_per_2tiles >> 1);
            }

            hlk_pop_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_pop_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);

            hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_wait_for_free_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);

            hlk_push_tiles(core_ptr, HlkOperand::intermed0, total_block_tiles);
            hlk_push_tiles(core_ptr, HlkOperand::intermed1, total_block_tiles);

        }
        
        if (args->sort == 1) {      // MT FIXME: Can TopKSort be exposed to hlk_api.h as well??
            // Pack values
            copy_from_intermediate(core_ptr, 1, args->tiles_per_seq, total_block_tiles, HlkOperand::intermed0, HlkOperand::out0, true);
        } else {
            // Pack indices
            copy_from_intermediate(core_ptr, 1, args->tiles_per_seq, total_block_tiles, HlkOperand::intermed1, HlkOperand::out0, true);
        }
    }
    TT_LOG("Done!");
}