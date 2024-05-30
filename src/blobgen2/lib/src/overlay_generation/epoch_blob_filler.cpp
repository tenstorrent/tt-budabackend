// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "overlay_generation/epoch_blob_filler.h"

#include "helpers/ncrisc_config_helper.h"
#include "helpers/noc_helper.h"
#include "helpers/phase_config_helper.h"
#include "model/stream_graph/stream_graph_collection.h"

namespace blobgen2
{

void EpochBlobFiller::fill_epoch_t(
    epoch_t* epoch,
    const int epoch_num,
    const CoreBlobData& core_blob_data,
    const std::map<uint32_t, uint32_t>& tile_size_and_address,
    const std::optional<dram_perf_info_t> perf_info,
    const std::optional<L1BufferAllocationInfo>& ncrisc_fallback_buffer,
    const bool is_eth_core)
{
    // Basic configuration.
    epoch->epoch_valid = 1;
    epoch->overlay_valid = 1;
    epoch->epoch_id = epoch_num;
    epoch->dummy_phase_tile_header_and_data[0] = 1;

    std::map<uint32_t, uint32_t> tile_size_and_address_eth;
    // Fill stuff from phase configs.
    for (const auto& [stream_id, stream_blob_data] : core_blob_data.streams)
    {
        for (const auto& [phase_id, phase_config] : stream_blob_data.phases)
        {
            PhaseConfigHelper phase_helper(*phase_config);

            assign_or_check_same(epoch->ublock_rt, phase_config->config.get_ublock_rt());
            assign_or_check_same(epoch->ublock_ct, phase_config->config.get_ublock_ct());
            assign_or_check_same(epoch->mblock_m, phase_config->config.get_mblock_m());
            assign_or_check_same(epoch->mblock_n, phase_config->config.get_mblock_n());
            assign_or_check_same(epoch->mblock_k, phase_config->config.get_mblock_k());

            if (phase_helper.get_has_packer_mcast_opt())
            {
                epoch->has_packer_mcast_opt = 1;
            }

            if (phase_helper.get_eth_sender())
            {
                epoch->has_eth_stream_trans = 1;
            }

            if (phase_helper.get_eth_receiver())
            {
                epoch->has_eth_stream_trans = 1;
            }

            if (phase_helper.should_skip_kernels())
            {
                epoch->skip_kernels = 1;
            }

            assign_or_check_same(
                epoch->overlay_blob_extra_size, is_eth_core ? 0 : phase_config->config.get_overlay_blob_extra_size());

            tile_size_and_address_eth[phase_helper.get_msg_size()] =
                phase_config->config.get_msg_info_buf_addr().value();
        }
    }

    // Fill tile sizes.
    for (const auto [tile_size, tile_address] : (is_eth_core ? tile_size_and_address_eth : tile_size_and_address))
    {
        epoch->tile_size_words[epoch->num_tile_sizes] = tile_size / MEM_WORD_BYTES;
        epoch->tile_size_header_buf_addr[epoch->num_tile_sizes] = tile_address;
        epoch->num_tile_sizes++;
    }

    // Fill perf info.
    if (perf_info.has_value())
    {
        const dram_perf_buf_noc_addr_t& dram_perf_buf_noc_addr = perf_info.value().first;
        const dram_perf_buf_max_req_t& dram_perf_buf_max_req = perf_info.value().second;
        for (int perf_i = 0; perf_i < dram_perf_buf_noc_addr.size(); perf_i++)
        {
            epoch->perf_dram_addr[perf_i] = {dram_perf_buf_noc_addr[perf_i]};
            epoch->perf_req_max[perf_i] = dram_perf_buf_max_req[perf_i];
        }
    }

    // Fill ncrisc fallback buffer allocation info.
    if (ncrisc_fallback_buffer.has_value())
    {
        epoch->extra_dram_q_state_addr = ncrisc_fallback_buffer.value().address;
    }
}

void EpochBlobFiller::fill_epoch_t_empty(epoch_t* epoch)
{
    epoch->epoch_valid = 1;
    epoch->overlay_valid = 0;
    epoch->skip_kernels = 1;
    epoch->dummy_phase_tile_header_and_data[0] = 1;
}

void EpochBlobFiller::fill_epoch_stream_info_t(epoch_stream_info_t* stream_info, const StreamBlobData& stream_blob_data)
{
    auto& [first_phase_id, first_phase] = *stream_blob_data.phases.begin();
    PhaseConfigHelper first_phase_helper = PhaseConfigHelper(*first_phase);

    stream_info->stream_id = stream_blob_data.stream_id;
    stream_info->producer_epoch_id = first_phase_helper.get_producer_epoch_id();
    stream_info->epoch_start_phase = first_phase_id;
    stream_info->datacopy_stream_type = first_phase_helper.get_datacopy_stream_type();
    stream_info->datacopy_stream_id = first_phase_helper.get_datacopy_stream_id();
    stream_info->epoch_num_tiles = PhaseConfigHelper::get_epoch_num_msgs(stream_blob_data.phases);
    stream_info->tile_size_words = first_phase_helper.get_msg_size() / MEM_WORD_BYTES;
    stream_info->buf_size_tiles = first_phase_helper.get_buf_full_size_bytes() / first_phase_helper.get_msg_size();
    stream_info->buf_full_size_bytes = first_phase_helper.get_buf_full_size_bytes();
    stream_info->buf_base_addr = first_phase_helper.get_buf_base_addr();
    stream_info->num_msgs_in_block = first_phase_helper.get_num_msgs_in_block();
    stream_info->start_phase_num_cfg_regs = stream_blob_data.info.start_phase_num_cfg_regs;
    stream_info->packer_operand = first_phase_helper.get_space_shared_with_operand();
    stream_info->msg_info_buf_start = first_phase->config.get_msg_info_buf_addr().value() / MEM_WORD_BYTES;
    // stream_info->blob_start_addr = Filled later.
    stream_info->blob_size = stream_blob_data.info.blob_size;
    stream_info->num_iters_in_epoch = first_phase_helper.get_num_iters_in_epoch();
    stream_info->epoch_iters_remaining = stream_info->num_iters_in_epoch;
    stream_info->num_scatter_inner_loop = first_phase_helper.get_num_scatter_inner_loop();
    stream_info->legacy_pack = first_phase_helper.get_legacy_pack() ? 1 : 0;
    stream_info->eth_remote_fw_stream_id = first_phase_helper.get_eth_remote_fw_stream_id();
    stream_info->num_mblock_buffering = first_phase_helper.get_num_mblock_buffering();
    stream_info->flags =
        first_phase_helper.get_stream_flags(stream_blob_data.stream_id) |
        (stream_blob_data.ncriscs.size() > 0 ? NcriscConfigHelper(stream_blob_data.ncriscs[0]).get_stream_flags() : 0);
    stream_info->stride = first_phase_helper.get_stride();
    stream_info->total_strides = first_phase_helper.get_total_strides();
    stream_info->stride_offset_size_bytes = first_phase_helper.get_stride_offset_size_bytes();
    // These two are unionized in the struct
    if (first_phase->config.get_skip_col_bytes().has_value())
    {
        stream_info->skip_col_bytes = first_phase->config.get_skip_col_bytes().value();
    }
    else
    {
        stream_info->pipe_scatter_output_loop_size = first_phase_helper.get_pipe_scatter_output_loop_count();
    }
    // These two values are unionized in the struct
    if (first_phase->config.get_skip_col_tile_row_bytes().has_value())
    {
        stream_info->skip_col_tile_row_bytes = first_phase->config.get_skip_col_tile_row_bytes().value();
    }
    else
    {
        // stream_info->scatter_pipe_state = Filled later
    }
    // These two values are unionized in the struct
    if (first_phase->config.get_skip_col_row_bytes().has_value())
    {
        stream_info->skip_col_row_bytes = first_phase->config.get_skip_col_row_bytes().value();
    }
    else
    {
        stream_info->scatter_idx = 0;
    }
    // These two values are unionized in the struct
    if (first_phase->config.get_skip_zcol_bytes().has_value())
    {
        stream_info->skip_zcol_bytes = first_phase->config.get_skip_zcol_bytes().value();
    }
    else
    {
        stream_info->pipe_scatter_output_loop_count = 0;
    }
    // These two are unionized in the struct
    if (first_phase->config.get_skip_col_zrow_bytes().has_value())
    {
        stream_info->skip_col_zrow_bytes = first_phase->config.get_skip_col_zrow_bytes().value();
    }
    else
    {
        stream_info->num_iter_tiles = PhaseConfigHelper::get_num_iter_tiles(stream_blob_data.phases);
    }
    stream_info->c_dim_size = first_phase_helper.get_c_dim_size();
    stream_info->r_dim_size = first_phase_helper.get_r_dim_size();
    stream_info->zc_dim_size = first_phase_helper.get_zc_dim_size();
    stream_info->zr_dim_size = first_phase_helper.get_zr_dim_size();
    if (first_phase_helper.get_moves_raw_data() && first_phase_helper.get_tile_dim_r() > 0)
    {
        stream_info->untilize_copy_iters = first_phase_helper.get_untilize_copy_iters();
        stream_info->log_2x_untilize_copy_iters = first_phase_helper.get_log_2x_untilize_copy_iters();
    }

    stream_info->num_dram_io_bufs = stream_blob_data.ncriscs.size();
    // stream_info->dram_io_info = Filled later
    // stream_info->num_fork_streams = Filled later
    stream_info->scatter_order_size = first_phase_helper.get_padding_scatter_order_size();
    // stream_info->fork_idxs  = Filled later
}

void EpochBlobFiller::fill_epoch_stream_dram_io_info_t(
    epoch_stream_dram_io_info_t* stream_dram_io_info, const StreamBlobData& stream_blob_data)
{
    const NcriscConfig& first_ncrisc = stream_blob_data.ncriscs[0];
    NcriscConfigHelper first_ncrisc_helper(first_ncrisc);
    const PhaseConfig& first_phase = *stream_blob_data.phases.begin()->second;
    PhaseConfigHelper first_phase_helper(first_phase);

    stream_dram_io_info->dram_q_slot_size_bytes = first_ncrisc_helper.get_dram_q_slot_size_bytes();
    stream_dram_io_info->input_noc = (first_phase_helper.get_incoming_data_noc() == NOC_ROUTE::NOC1 ? 1 : 0);
    stream_dram_io_info->output_noc = (first_phase_helper.get_outgoing_data_noc() == NOC_ROUTE::NOC1 ? 1 : 0);
    stream_dram_io_info->dram_output_no_push = first_phase_helper.get_dram_output_no_push();
    stream_dram_io_info->has_multi_readers = NcriscConfigHelper::has_multi_readers(stream_blob_data.ncriscs);
    if (first_phase.config.get_scatter_list_stream_id().has_value())
    {
        stream_dram_io_info->scatter_list_stream_id =
            first_phase.config.get_scatter_list_stream_id().value()->get_stream_id();
        stream_dram_io_info->scatter_list_indicies_per_tile =
            first_phase.config.get_scatter_list_indicies_per_tile().value();
        stream_dram_io_info->scatter_list_indicies_per_input =
            first_phase.config.get_scatter_list_indicies_per_input().value();
    }
    else
    {
        stream_dram_io_info->c_dim_loop_num_rows = first_phase_helper.get_c_dim_loop_num_rows();
        stream_dram_io_info->tilize_row_col_offset = first_phase_helper.get_tilize_row_col_offset();
    }
    stream_dram_io_info->dram_embeddings_row_shift = first_phase_helper.get_dram_embeddings_row_shift();
    stream_dram_io_info->epoch_q_slots_remaining =
        first_ncrisc_helper.get_epoch_q_slots_remaining(first_phase_helper, stream_blob_data.phases);
    stream_dram_io_info->dram_embeddings_row_tiles = first_phase_helper.get_dram_embeddings_row_tiles();
    stream_dram_io_info->dram_writes_with_cmd_buf = first_phase_helper.get_dram_writes_with_cmd_buf();
    stream_dram_io_info->hw_tilize = first_phase_helper.get_hw_tilize() ? 1 : 0;
}

void EpochBlobFiller::fill_dram_io_state_t(
    dram_io_state_t* dram_io_state,
    const NcriscConfig& ncrisc_config,
    const PhaseConfig& first_phase,
    const NOCHelper& noc_helper)
{
    NcriscConfigHelper ncrisc_config_helper(ncrisc_config);
    PhaseConfigHelper first_phase_helper(first_phase);

    dram_io_state->dram_to_l1.dram_streaming_epoch_id =
        ncrisc_config_helper.get_dram_streaming_epoch_id(first_phase.phase_id);
    // dram_io_state->l1_to_dram.dram_streaming_header_addr = Filled later

    dram_io_state->l1_to_dram.data_chunk_size_tiles = ncrisc_config_helper.get_data_chunk_size_tiles();
    dram_io_state->l1_to_dram.data_chunk_size_bytes =
        ncrisc_config_helper.get_data_chunk_size_bytes(first_phase_helper);
    dram_io_state->l1_to_dram.dram_padding = ncrisc_config.dram_padding.value_or(false) ? 1 : 0;

    dram_io_state->local.dram_buf_size_bytes = ncrisc_config.dram_buf_size_bytes;
    dram_io_state->local.dram_buf_size_q_slots = ncrisc_config.dram_buf_size_q_slots;
    dram_io_state->local.dram_buf_addr = {ncrisc_config_helper.get_dram_buf_noc_addr(first_phase_helper, noc_helper)};
    dram_io_state->local.dram_q_slot_size_tiles = ncrisc_config_helper.get_dram_q_slot_size_tiles(first_phase_helper);
    dram_io_state->local.reader_index = ncrisc_config_helper.reader_index();
    dram_io_state->local.total_readers = ncrisc_config_helper.total_readers();
}

void EpochBlobFiller::fill_dram_io_scatter_state_t(
    dram_io_scatter_state_t* dram_io_scatter_state,
    const NcriscConfig& ncrisc,
    const PhaseConfig& first_phase,
    const NOCHelper& noc_helper)
{
    NcriscConfigHelper ncrisc_config_helper(ncrisc);
    PhaseConfigHelper first_phase_helper(first_phase);

    dram_io_scatter_state->scatter_offsets_size =
        ncrisc_config_helper.get_dram_scatter_offsets(first_phase_helper, noc_helper).size();
    dram_io_scatter_state->scatter_chunk_size_bytes = ncrisc_config_helper.get_scatter_chunk_size_bytes();
    dram_io_scatter_state->q_slot_size_bytes = ncrisc_config_helper.get_dram_q_slot_size_bytes();
    dram_io_scatter_state->scatter_chunk_size_tiles = ncrisc_config_helper.get_dram_scatter_chunk_size_tiles();
}

}  // namespace blobgen2