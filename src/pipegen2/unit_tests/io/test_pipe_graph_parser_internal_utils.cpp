// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_pipe_graph_parser_internal_utils.h"

using namespace pipegen2;

void compare_pipe_attributes(const PGPipe& pipe1, const PGPipe& pipe2)
{
    EXPECT_EQ(pipe1.get_id(), pipe2.get_id());
    EXPECT_EQ(pipe1.get_pipe_periodic_repeat(), pipe2.get_pipe_periodic_repeat());
    EXPECT_EQ(pipe1.get_consumer_repeat(), pipe2.get_consumer_repeat());
    EXPECT_EQ(pipe1.get_incoming_noc_id(), pipe2.get_incoming_noc_id());
    EXPECT_EQ(pipe1.get_outgoing_noc_id(), pipe2.get_outgoing_noc_id());
    EXPECT_EQ(pipe1.get_incoming_noc_vc(), pipe2.get_incoming_noc_vc());
    EXPECT_EQ(pipe1.get_outgoing_noc_vc(), pipe2.get_outgoing_noc_vc());
    EXPECT_EQ(pipe1.get_mcast_core_logical_locations(), pipe2.get_mcast_core_logical_locations());
    EXPECT_EQ(pipe1.get_dram_pipe_total_readers(), pipe2.get_dram_pipe_total_readers());
    EXPECT_EQ(pipe1.get_dram_pipe_reader_index(), pipe2.get_dram_pipe_reader_index());
    EXPECT_EQ(pipe1.is_ethernet_pipe(), pipe2.is_ethernet_pipe());
    EXPECT_EQ(pipe1.is_packer_multicast_optimization_enabled(), pipe2.is_packer_multicast_optimization_enabled());
    EXPECT_EQ(pipe1.get_op_input_dram_io_buf_size_tiles(), pipe2.get_op_input_dram_io_buf_size_tiles());
    EXPECT_EQ(pipe1.get_input_buffers_ids(), pipe2.get_input_buffers_ids());
    EXPECT_EQ(pipe1.get_output_buffers_ids(), pipe2.get_output_buffers_ids());
    EXPECT_EQ(pipe1.get_output_padding_buffers_ids(), pipe2.get_output_padding_buffers_ids());
    EXPECT_EQ(pipe1.get_output_padding_buffers_ids(), pipe2.get_output_padding_buffers_ids());
    EXPECT_EQ(pipe1.get_ethernet_channel(), pipe2.get_ethernet_channel());
    EXPECT_EQ(pipe1.is_mmio_pipe(), pipe2.is_mmio_pipe());
    EXPECT_EQ(pipe1.is_mmio_pipe_downstream(), pipe2.is_mmio_pipe_downstream());
    EXPECT_EQ(pipe1.is_gather_optimization_disabled(), pipe2.is_gather_optimization_disabled());
}

void compare_buffer_attributes(const PGBuffer& buffer1, const PGBuffer& buffer2)
{
    EXPECT_EQ(buffer1.get_type(), buffer2.get_type());
    EXPECT_EQ(buffer1.get_id(), buffer2.get_id());
    EXPECT_EQ(buffer1.get_operand_id(), buffer2.get_operand_id());
    EXPECT_EQ(buffer1.get_num_epoch_tiles(), buffer2.get_num_epoch_tiles());
    EXPECT_EQ(buffer1.get_size_tiles(), buffer2.get_size_tiles());
    EXPECT_EQ(buffer1.get_tile_size(), buffer2.get_tile_size());
    EXPECT_EQ(buffer1.get_num_tiles_per_input(), buffer2.get_num_tiles_per_input());
    EXPECT_EQ(buffer1.get_scatter_gather_num_tiles(), buffer2.get_scatter_gather_num_tiles());
    EXPECT_EQ(buffer1.get_num_queue_slots(), buffer2.get_num_queue_slots());
    EXPECT_EQ(buffer1.get_dram_io_flag(), buffer2.get_dram_io_flag());
    EXPECT_EQ(buffer1.get_dram_io_flag_is_remote(), buffer2.get_dram_io_flag_is_remote());
    EXPECT_EQ(buffer1.get_dram_buf_streaming(), buffer2.get_dram_buf_streaming());
    EXPECT_EQ(buffer1.get_dram_buf_flag(), buffer2.get_dram_buf_flag());
    EXPECT_EQ(buffer1.get_write_dram_buf_flag(), buffer2.get_write_dram_buf_flag());
    EXPECT_EQ(buffer1.get_dram_ram_flag(), buffer2.get_dram_ram_flag());
    EXPECT_EQ(buffer1.get_moves_raw_data(), buffer2.get_moves_raw_data());
    EXPECT_EQ(buffer1.get_untilized_output_full_r_dim(), buffer2.get_untilized_output_full_r_dim());
    EXPECT_EQ(buffer1.get_untilized_output_full_c_dim(), buffer2.get_untilized_output_full_c_dim());
    EXPECT_EQ(buffer1.get_untilized_output_r_dim(), buffer2.get_untilized_output_r_dim());
    EXPECT_EQ(buffer1.get_untilized_output_c_dim(), buffer2.get_untilized_output_c_dim());
    EXPECT_EQ(buffer1.get_untilized_output_z_dim(), buffer2.get_untilized_output_z_dim());
    EXPECT_EQ(buffer1.get_untilized_output_type_0_zdim(), buffer2.get_untilized_output_type_0_zdim());
    EXPECT_EQ(buffer1.get_untilized_output_type_1_zdim(), buffer2.get_untilized_output_type_1_zdim());
    EXPECT_EQ(buffer1.get_ublock_rt(), buffer2.get_ublock_rt());
    EXPECT_EQ(buffer1.get_ublock_ct(), buffer2.get_ublock_ct());
    EXPECT_EQ(buffer1.get_mblock_m(), buffer2.get_mblock_m());
    EXPECT_EQ(buffer1.get_mblock_n(), buffer2.get_mblock_n());
    EXPECT_EQ(buffer1.get_mblock_k(), buffer2.get_mblock_k());
    EXPECT_EQ(buffer1.get_tile_clear_granularity(), buffer2.get_tile_clear_granularity());
    EXPECT_EQ(buffer1.get_shared_space_buffer_id(), buffer2.get_shared_space_buffer_id());
    EXPECT_EQ(buffer1.get_producer_epoch_id(), buffer2.get_producer_epoch_id());
    EXPECT_EQ(buffer1.is_scatter(), buffer2.is_scatter());
    EXPECT_EQ(buffer1.get_replicate_count(), buffer2.get_replicate_count());
    EXPECT_EQ(buffer1.get_ethernet_channel(), buffer2.get_ethernet_channel());
    EXPECT_EQ(buffer1.get_dram_channel(), buffer2.get_dram_channel());
    EXPECT_EQ(buffer1.get_dram_sub_channel(), buffer2.get_dram_sub_channel());
    EXPECT_EQ(buffer1.get_dram_address(), buffer2.get_dram_address());
    EXPECT_EQ(buffer1.get_logical_location().x, buffer2.get_logical_location().x);
    EXPECT_EQ(buffer1.get_logical_location().y, buffer2.get_logical_location().y);
    EXPECT_EQ(buffer1.get_dram_prefetch_incoming_noc_id(), buffer2.get_dram_prefetch_incoming_noc_id());
    EXPECT_EQ(buffer1.get_prefetch_type(), buffer2.get_prefetch_type());
    EXPECT_EQ(buffer1.is_embedding_table(), buffer2.is_embedding_table());
    EXPECT_EQ(buffer1.get_embedding_table_core_c_div(), buffer2.get_embedding_table_core_c_div());
    EXPECT_EQ(buffer1.get_embedding_table_row_size_per_core(), buffer2.get_embedding_table_row_size_per_core());
    EXPECT_EQ(buffer1.is_embedding_index(), buffer2.is_embedding_index());
    EXPECT_EQ(buffer1.get_embedding_indices_per_tile(), buffer2.get_embedding_indices_per_tile());
    EXPECT_EQ(buffer1.get_embedding_indices_per_input(), buffer2.get_embedding_indices_per_input());
    EXPECT_EQ(buffer1.is_hw_tilize(), buffer2.is_hw_tilize());
    EXPECT_EQ(buffer1.get_tilize_mblock_n_loop_num_rows(), buffer2.get_tilize_mblock_n_loop_num_rows());
    EXPECT_EQ(buffer1.get_tilize_row_col_offset(), buffer2.get_tilize_row_col_offset());
    EXPECT_EQ(buffer1.is_padding(), buffer2.is_padding());
    EXPECT_EQ(buffer1.get_use_ethernet_fw_stream(), buffer2.get_use_ethernet_fw_stream());
    EXPECT_EQ(buffer1.get_overlay_blob_size(), buffer2.get_overlay_blob_size());
    EXPECT_EQ(buffer1.is_post_tm_relay_buf(), buffer2.is_post_tm_relay_buf());
}