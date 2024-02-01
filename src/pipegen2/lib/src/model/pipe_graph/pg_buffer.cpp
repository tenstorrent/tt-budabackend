// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/pipe_graph/pg_buffer.h"

#include "device/operand_stream_map.h"
#include "pipegen2_constants.h"

#include "pipegen2_utils.h"

namespace pipegen2
{
    PGBuffer::PGBuffer() :
        m_id(-1),
        m_type(BufferType::kUnknown),
        m_num_epoch_tiles(0),
        m_size_tiles(0),
        m_tile_size(0),
        m_num_tiles_per_input(0),
        m_scatter_gather_num_tiles(0),
        m_operand_id(-1),
        m_num_queue_slots(0),
        m_dram_io_flag(false),
        m_dram_io_flag_is_remote(false),
        m_dram_buf_streaming(false),
        m_dram_buf_flag(false),
        m_write_dram_buf_flag(false),
        m_dram_ram_flag(false),
        m_moves_raw_data(false),
        m_untilized_output_full_r_dim(0),
        m_untilized_output_full_c_dim(0),
        m_untilized_output_r_dim(0),
        m_untilized_output_c_dim(0),
        m_untilized_output_z_dim(0),
        m_untilized_output_type_0_zdim(0),
        m_untilized_output_type_1_zdim(0),
        m_untilized_output_tile_dim_r(constants::default_tile_size),
        m_untilized_output_tile_dim_c(constants::default_tile_size),
        m_ublock_rt(0),
        m_ublock_ct(0),
        m_mblock_m(0),
        m_mblock_n(0),
        m_mblock_k(0),
        m_tile_clear_granularity(0),
        m_shared_space_buffer_id(-1),
        m_producer_epoch_id(-1),
        m_logical_location(-1, -1, -1),
        m_is_scatter(false),
        m_replicate_count(0),
        m_ethernet_channel(-1),
        m_dram_channel(0),
        m_dram_sub_channel(0),
        m_dram_address(0),
        m_dram_prefetch_incoming_noc_id(0),
        m_prefetch_type(PrefetchType::PRE_TM),
        m_embedding_table(false),
        m_embedding_table_core_c_div(0),
        m_embedding_table_row_size_per_core(0),
        m_embedding_index(false),
        m_embedding_indices_per_tile(0),
        m_embedding_indices_per_input(0),
        m_hw_tilize(false),
        m_tilize_mblock_n_loop_num_rows(constants::default_tile_size),
        m_tilize_row_col_offset(0),
        m_input_pipe(nullptr),
        m_is_padding(false),
        m_use_ethernet_fw_stream(false),
        m_overlay_blob_size(0)
    {
    }

    PGBuffer::PGBuffer(const PGBuffer& other) :
        m_id(-1),
        m_type(other.m_type),
        m_num_epoch_tiles(other.m_num_epoch_tiles),
        m_size_tiles(other.m_size_tiles),
        m_tile_size(other.m_tile_size),
        m_num_tiles_per_input(other.m_num_tiles_per_input),
        m_scatter_gather_num_tiles(other.m_scatter_gather_num_tiles),
        m_operand_id(other.m_operand_id),
        m_num_queue_slots(other.m_num_queue_slots),
        m_dram_io_flag(other.m_dram_io_flag),
        m_dram_io_flag_is_remote(other.m_dram_io_flag_is_remote),
        m_dram_buf_streaming(other.m_dram_buf_streaming),
        m_dram_buf_flag(other.m_dram_buf_flag),
        m_write_dram_buf_flag(other.m_write_dram_buf_flag),
        m_dram_ram_flag(other.m_dram_ram_flag),
        m_moves_raw_data(other.m_moves_raw_data),
        m_untilized_output_full_r_dim(other.m_untilized_output_full_r_dim),
        m_untilized_output_full_c_dim(other.m_untilized_output_full_c_dim),
        m_untilized_output_r_dim(other.m_untilized_output_r_dim),
        m_untilized_output_c_dim(other.m_untilized_output_c_dim),
        m_untilized_output_z_dim(other.m_untilized_output_z_dim),
        m_untilized_output_type_0_zdim(other.m_untilized_output_type_0_zdim),
        m_untilized_output_type_1_zdim(other.m_untilized_output_type_1_zdim),
        m_untilized_output_tile_dim_r(other.m_untilized_output_tile_dim_r),
        m_untilized_output_tile_dim_c(other.m_untilized_output_tile_dim_c),
        m_ublock_rt(other.m_ublock_rt),
        m_ublock_ct(other.m_ublock_ct),
        m_mblock_m(other.m_mblock_m),
        m_mblock_n(other.m_mblock_n),
        m_mblock_k(other.m_mblock_k),
        m_tile_clear_granularity(other.m_tile_clear_granularity),
        m_shared_space_buffer_id(other.m_shared_space_buffer_id),
        m_producer_epoch_id(other.m_producer_epoch_id),
        m_logical_location(other.m_logical_location),
        m_is_scatter(other.m_is_scatter),
        m_replicate_count(other.m_replicate_count),
        m_ethernet_channel(other.m_ethernet_channel),
        m_dram_channel(other.m_dram_channel),
        m_dram_sub_channel(other.m_dram_sub_channel),
        m_dram_address(other.m_dram_address),
        m_dram_prefetch_incoming_noc_id(other.m_dram_prefetch_incoming_noc_id),
        m_prefetch_type(other.m_prefetch_type),
        m_embedding_table(other.m_embedding_table),
        m_embedding_table_core_c_div(other.m_embedding_table_core_c_div),
        m_embedding_table_row_size_per_core(other.m_embedding_table_row_size_per_core),
        m_embedding_index(other.m_embedding_index),
        m_embedding_indices_per_tile(other.m_embedding_indices_per_tile),
        m_embedding_indices_per_input(other.m_embedding_indices_per_input),
        m_hw_tilize(other.m_hw_tilize),
        m_tilize_mblock_n_loop_num_rows(other.m_tilize_mblock_n_loop_num_rows),
        m_tilize_row_col_offset(other.m_tilize_row_col_offset),
        m_input_pipe(nullptr),
        m_is_padding(other.m_is_padding),
        m_use_ethernet_fw_stream(other.m_use_ethernet_fw_stream),
        m_overlay_blob_size(other.m_overlay_blob_size)
    {
    }

    void PGBuffer::replace_output_pipe(PGPipe* old_pipe, PGPipe* new_pipe)
    {
        m_output_pipes.erase(old_pipe);
        m_output_pipes.insert(new_pipe);
    }

    const PGPipe* PGBuffer::get_single_output_pipe() const
    {
        ASSERT(has_single_output(), "Buffer does not have only one output pipe!");
        return *m_output_pipes.begin();
    }

    PGPipe* PGBuffer::get_single_output_pipe()
    {
        return const_cast<PGPipe*>(const_cast<const PGBuffer*>(this)->get_single_output_pipe());
    }

    void PGBuffer::set_chip_id(ChipId chip_id)
    {
        m_logical_location.chip = chip_id;
    }

    bool PGBuffer::is_dram() const
    {
        return m_dram_io_flag && m_num_queue_slots > 0 &&
               !(m_dram_buf_flag || m_dram_buf_streaming || m_write_dram_buf_flag);
    }

    bool PGBuffer::is_dram_prefetch() const
    {
        // If it is DRAM buffer, but not PCIe streaming buffer nor intermediate,
        // then it can only be prefetch.
        // TODO: This should be set from net2pipe, not inferred like this.
        return m_dram_buf_flag && !m_dram_buf_streaming && !is_intermediate_operand();
    }

    bool PGBuffer::is_dram_input() const
    {
        bool is_dram_buf = is_dram() || is_dram_prefetch();
        return is_dram_buf && !m_moves_raw_data;
    }

    bool PGBuffer::is_non_prefetch_pre_tm_dram_input() const
    {
        return is_dram_input() && !is_dram_prefetch_pre_tm();
    }

    bool PGBuffer::is_dram_prefetch_post_tm() const
    {
        return is_dram_prefetch() && m_prefetch_type == PrefetchType::POST_TM;
    }

    bool PGBuffer::is_dram_prefetch_pre_tm() const
    {
        return is_dram_prefetch() && m_prefetch_type == PrefetchType::PRE_TM;
    }

    bool PGBuffer::is_scatter_prefetch_post_tm() const
    {
        return m_is_scatter && is_dram_prefetch_post_tm();
    }

    bool PGBuffer::is_dram_output() const
    {
        return is_dram();
    }

    bool PGBuffer::is_input_operand() const
    {
        return OperandStreamMap::is_input_operand(m_operand_id);
    }

    bool PGBuffer::is_output_operand() const
    {
        return OperandStreamMap::is_output_operand(m_operand_id);
    }

    bool PGBuffer::is_intermediate_operand() const
    {
        return OperandStreamMap::is_intermediate_operand(m_operand_id);
    }

    bool PGBuffer::is_relay() const
    {
        return OperandStreamMap::is_relay_operand(m_operand_id);
    }

    bool PGBuffer::is_packer() const
    {
        return m_type == BufferType::kPacker;
    }

    bool PGBuffer::is_unpacker() const
    {
        return m_type == BufferType::kUnpacker;
    }

    bool PGBuffer::is_located_in_l1() const
    {
        const bool is_dram_io_input = is_dram_input() && !is_dram_prefetch_pre_tm();
        const bool is_dram_io_output = is_dram_output();

        return !is_dram_io_input && !is_dram_io_output;
    }

    bool PGBuffer::is_end_to_end_queue() const
    {
        return is_dram() && !has_no_input() && !has_no_outputs();
    }
}