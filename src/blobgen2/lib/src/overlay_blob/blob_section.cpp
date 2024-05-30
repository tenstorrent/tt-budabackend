// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "overlay_blob/blob_section.h"

#include <filesystem>

#include "epoch.h"
#include "helpers/noc_helper.h"
#include "utils/logger.hpp"

namespace blobgen2
{
void BlobSection::append(uint8_t byte) { m_data.push_back(byte); }

void BlobSection::append(uint16_t word)
{
    log_assert(
        m_data.size() % 2 == 0,
        "Writing a uint16_t, but current BlobSection write index not aligned to 2 bytes: {}",
        m_data.size());
    append(static_cast<uint8_t>(word & 0xFF));
    append(static_cast<uint8_t>(word >> 8));
}

void BlobSection::append(uint32_t dword)
{
    log_assert(
        m_data.size() % 4 == 0,
        "Writing a uint32_t, but current BlobSection write index not aligned to 4 bytes {}",
        m_data.size());
    append(static_cast<uint16_t>(dword & 0xFFFF));
    append(static_cast<uint16_t>(dword >> 16));
}

void BlobSection::append(const tt_uint64_t& qword)
{
    append(qword.hi);
    append(qword.lo);
}

void BlobSection::append_debug(const std::string debug_str)
{
    int ind = m_data.size() / 4;
    if (m_debug_data.size() <= ind)
    {
        m_debug_data.resize(ind + 1);
    }
    m_debug_data[ind] = " " + debug_str + m_debug_data[ind];
}

void BlobSection::pad_to_noc_alignment()
{
    size_t padding = get_noc_alignment_padding_bytes(m_data.size()) - m_data.size();
    for (size_t i = 0; i < padding; i++)
    {
        append(static_cast<uint8_t>(0), "padding");
    }
}

void BlobSection::add_epoch_t(const epoch_t* epoch)
{
    size_t size_before = m_data.size();
    append_debug("[EPOCH_T]");
    append(epoch->num_inputs, "num_inputs");
    append(epoch->num_outputs, "num_outputs");
    append(epoch->num_active_streams, "num_active_streams");
    append(epoch->epoch_valid, "epoch_valid");
    append(epoch->all_streams_ready, "all_streams_ready");
    append(epoch->all_streams_and_kernels_done, "all_streams_and_kernels_done");
    append(epoch->end_program, "end_program");
    append(epoch->num_tile_sizes, "num_tile_sizes");
    append(epoch->tile_size_words, "tile_size_words");
    append(epoch->tile_size_header_buf_addr, "tile_size_header_buf_addr");
    append(epoch->inputs, "inputs");
    append(epoch->outputs, "outputs");
    append(epoch->active_streams, "active_streams");
    append(epoch->perf_dram_copy_req, "perf_dram_copy_req");
    append(epoch->perf_dram_copy_ack, "perf_dram_copy_ack");
    append(epoch->perf_dram_addr, "perf_dram_addr");
    append(epoch->perf_req_max, "perf_req_max");
    append(epoch->unused1, "unused1");
    append(epoch->extra_dram_q_state_addr, "extra_dram_q_state_addr");
    append(epoch->ublock_rt, "ublock_rt");
    append(epoch->ublock_ct, "ublock_ct");
    append(epoch->mblock_m, "mblock_m");
    append(epoch->mblock_n, "mblock_n");
    append(epoch->mblock_k, "mblock_k");
    append(epoch->has_eth_stream_trans, "has_eth_stream_trans");
    append(epoch->has_packer_mcast_opt, "has_packer_mcast_opt");
    append(epoch->overlay_valid, "overlay_valid");
    append(epoch->skip_kernels, "skip_kernels");
    append(epoch->epoch_id, "epoch_id");
    append(epoch->tile_clear_blob.blob_words, "tile_clear_blob.blob_words");
#if NOC_ADDRESS_ALIGNMENT > 32
    // Needs to be added here and not at the end, since next two attributes *must* remain at the end of epoch_t.
    append(epoch->padding, "padding");
#endif
    append(epoch->dummy_phase_tile_header_and_data, "dummy_phase_tile_header_and_data");
    append(epoch->overlay_blob_extra_size, "overlay_blob_extra_size");

    log_assert(
        m_data.size() - size_before == sizeof(epoch_t),
        "Size of written data {} does not match size of epoch_t {}",
        m_data.size() - size_before,
        sizeof(epoch_t));
}

void BlobSection::add_epoch_stream_info_t(const epoch_stream_info_t* epoch_stream_info)
{
    size_t size_before = m_data.size();
    append_debug("[EPOCH_STREAM_INFO_T]");
    append(epoch_stream_info->stream_id, "stream_id");
    append(epoch_stream_info->producer_epoch_id, "producer_epoch_id");
    append(epoch_stream_info->epoch_start_phase, "epoch_start_phase");
    append(static_cast<uint8_t>(epoch_stream_info->datacopy_stream_type), "datacopy_stream_type");
    append(epoch_stream_info->datacopy_stream_id, "datacopy_stream_id");
    append(epoch_stream_info->epoch_num_tiles, "epoch_num_tiles");
    append(epoch_stream_info->tile_size_words, "tile_size_words");
    append(epoch_stream_info->buf_size_tiles, "buf_size_tiles");
    append(epoch_stream_info->buf_full_size_bytes, "buf_full_size_bytes");
    append(epoch_stream_info->buf_base_addr, "buf_base_addr");
    append(epoch_stream_info->num_msgs_in_block, "num_msgs_in_block");
    append(epoch_stream_info->start_phase_num_cfg_regs, "start_phase_num_cfg_regs");
    append(epoch_stream_info->packer_operand, "packer_operand");
    append(epoch_stream_info->msg_info_buf_start, "msg_info_buf_start");
    append(epoch_stream_info->blob_start_addr, "blob_start_addr");
    append(epoch_stream_info->blob_size, "blob_size");
    append(epoch_stream_info->num_iters_in_epoch, "num_iters_in_epoch");
    append(epoch_stream_info->epoch_iters_remaining, "epoch_iters_remaining");
    append(epoch_stream_info->num_scatter_inner_loop, "num_scatter_inner_loop");
    append(epoch_stream_info->legacy_pack, "legacy_pack");
    append(epoch_stream_info->eth_remote_fw_stream_id, "eth_remote_fw_stream_id");
    append(epoch_stream_info->num_mblock_buffering, "num_mblock_buffering");
    append(epoch_stream_info->flags, "flags");
    append(epoch_stream_info->stride, "stride");
    append(epoch_stream_info->total_strides, "total_strides");
    append(epoch_stream_info->stride_offset_size_bytes, "stride_offset_size_bytes");
    append(epoch_stream_info->skip_col_bytes, "(skip_col_bytes || pipe_scatter_output_loop_size)");
    append(epoch_stream_info->skip_col_tile_row_bytes, "(skip_col_tile_row_bytes || scatter_pipe_state)");
    append(epoch_stream_info->skip_col_row_bytes, "(skip_col_row_bytes || scatter_idx)");
    append(epoch_stream_info->skip_zcol_bytes, "(skip_zcol_bytes || pipe_scatter_output_loop_count)");
    append(epoch_stream_info->skip_col_zrow_bytes, "(skip_col_zrow_bytes || num_iter_tiles)");
    append(epoch_stream_info->c_dim_size, "c_dim_size");
    append(epoch_stream_info->r_dim_size, "r_dim_size");
    append(epoch_stream_info->zc_dim_size, "zc_dim_size");
    append(epoch_stream_info->zr_dim_size, "zr_dim_size");
    append(epoch_stream_info->unused0_fw, "unused0_fw");
    append(epoch_stream_info->unused1_fw, "unused1_fw");
    append(epoch_stream_info->unused2_fw, "unused2_fw");
    append(epoch_stream_info->unused3_fw, "unused3_fw");
    append(epoch_stream_info->unused4_fw, "unused4_fw");
    append(epoch_stream_info->unused5_fw, "unused5_fw");
    append(epoch_stream_info->unused6_fw, "unused6_fw");
    append(epoch_stream_info->unused7_fw, "unused7_fw");
    append(epoch_stream_info->untilize_copy_iters, "untilize_copy_iters");
    append(epoch_stream_info->log_2x_untilize_copy_iters, "log_2x_untilize_copy_iters");
    append(epoch_stream_info->num_dram_io_bufs, "num_dram_io_bufs");
    append(epoch_stream_info->dram_io_info, "dram_io_info");
    append(epoch_stream_info->num_fork_streams, "num_fork_streams");
    append(epoch_stream_info->scatter_order_size, "scatter_order_size");
    append(epoch_stream_info->fork_idxs, "fork_idxs");
    pad_to_noc_alignment();

    log_assert(
        m_data.size() - size_before == sizeof(epoch_stream_info_t),
        "Size of written data {} does not match size of epoch_stream_info_t {}",
        m_data.size() - size_before,
        sizeof(epoch_stream_info_t));
}

void BlobSection::add_epoch_stream_dram_io_info_t(const epoch_stream_dram_io_info_t* epoch_stream_info)
{
    size_t size_before = m_data.size();
    append_debug("[EPOCH_STREAM_DRAM_IO_INFO_T]");
    append(epoch_stream_info->dram_q_slot_size_bytes, "dram_q_slot_size_bytes");
    append(epoch_stream_info->input_noc, "input_noc");
    append(epoch_stream_info->output_noc, "output_noc");
    append(epoch_stream_info->dram_output_no_push, "dram_output_no_push");
    append(epoch_stream_info->has_multi_readers, "has_multi_readers");
    append(epoch_stream_info->scatter_list_stream_id, "scatter_list_stream_id");
    append(
        epoch_stream_info->scatter_list_indicies_per_tile, "(scatter_list_indicies_per_tile || c_dim_loop_num_rows)");
    append(
        epoch_stream_info->scatter_list_indicies_per_input,
        "(scatter_list_indicies_per_input || tilize_row_col_offset)");
    append(epoch_stream_info->dram_embeddings_row_shift, "dram_embeddings_row_shift");
    append(epoch_stream_info->epoch_q_slots_remaining, "epoch_q_slots_remaining");
    append(epoch_stream_info->dram_embeddings_row_tiles, "dram_embeddings_row_tiles");
    append(epoch_stream_info->dram_writes_with_cmd_buf, "dram_writes_with_cmd_buf");
    append(epoch_stream_info->hw_tilize, "hw_tilize");
    append(epoch_stream_info->dram_io_state, "dram_io_state");
    pad_to_noc_alignment();

    log_assert(
        m_data.size() - size_before == sizeof(epoch_stream_dram_io_info_t),
        "Size of written data {} does not match size of epoch_stream_dram_io_info_t {}",
        m_data.size() - size_before,
        sizeof(epoch_stream_dram_io_info_t));
}

void BlobSection::add_dram_io_state_t(const dram_io_state_t* dram_io_state)
{
    size_t size_before = m_data.size();
    append_debug("[DRAM_IO_STATE_T]");

    append(dram_io_state->dram_to_l1.rd_dram_rdptr, "dram_to_l1.rd_dram_rdptr");
    append(dram_io_state->dram_to_l1.rd_grd_ptr_autoinc, "dram_to_l1.rd_grd_ptr_autoinc");
    append(dram_io_state->dram_to_l1.rd_dram_wrptr, "dram_to_l1.rd_dram_wrptr");
    append(dram_io_state->dram_to_l1.rd_gwr_ptr_autoinc, "dram_to_l1.rd_gwr_ptr_autoinc");
    append(dram_io_state->dram_to_l1.rd_dram_local_rdptr, "dram_to_l1.rd_dram_local_rdptr");
    append(dram_io_state->dram_to_l1.rd_epoch_id_tag, "dram_to_l1.rd_epoch_id_tag");
    append(dram_io_state->dram_to_l1.rd_stride, "dram_to_l1.rd_stride");
    append(dram_io_state->dram_to_l1.rd_flags, "dram_to_l1.rd_flags");
    append(dram_io_state->dram_to_l1.rd_lrd_ptr_autoinc, "dram_to_l1.rd_lrd_ptr_autoinc");
    append(dram_io_state->dram_to_l1.unused0, "dram_to_l1.unused0");
    append(dram_io_state->dram_to_l1.rd_queue_update_stride, "dram_to_l1.rd_queue_update_stride");
    append(dram_io_state->dram_to_l1.unused1, "dram_to_l1.unused1");
    append(dram_io_state->dram_to_l1.dram_streaming_epoch_id, "dram_to_l1.dram_streaming_epoch_id");
    append(dram_io_state->dram_to_l1.dram_streaming_tag, "dram_to_l1.dram_streaming_tag");
    pad_to_noc_alignment();

    append(dram_io_state->l1_to_dram.wr_dram_rdptr, "l1_to_dram.wr_dram_rdptr");
    append(dram_io_state->l1_to_dram.wr_grd_ptr_autoinc, "l1_to_dram.wr_grd_ptr_autoinc");
    append(dram_io_state->l1_to_dram.wr_dram_wrptr, "l1_to_dram.wr_dram_wrptr");
    append(dram_io_state->l1_to_dram.wr_gwr_ptr_autoinc, "l1_to_dram.wr_gwr_ptr_autoinc");
    append(dram_io_state->l1_to_dram.wr_dram_local_rdptr, "l1_to_dram.wr_dram_local_rdptr");
    append(dram_io_state->l1_to_dram.wr_epoch_id_tag, "l1_to_dram.wr_epoch_id_tag");
    append(dram_io_state->l1_to_dram.wr_stride, "l1_to_dram.wr_stride");
    append(dram_io_state->l1_to_dram.wr_flags, "l1_to_dram.wr_flags");
    append(
        dram_io_state->l1_to_dram.dram_streaming_header_addr,
        "l1_to_dram.(wr_lrd_ptr_autoinc | unused2 | wr_queue_update_stride | unused3 || dram_streaming_header_addr)");
    append(dram_io_state->l1_to_dram.data_chunk_size_bytes, "l1_to_dram.data_chunk_size_bytes");
    append(dram_io_state->l1_to_dram.data_chunk_size_tiles, "l1_to_dram.data_chunk_size_tiles");
    append(dram_io_state->l1_to_dram.dram_padding, "l1_to_dram.dram_padding");
    pad_to_noc_alignment();

    append(dram_io_state->local.dram_buf_size_bytes, "local.dram_buf_size_bytes");
    append(dram_io_state->local.dram_buf_size_q_slots, "local.dram_buf_size_q_slots");
    append(dram_io_state->local.dram_buf_addr, "local.dram_buf_addr");
    append(dram_io_state->local.dram_q_slot_size_tiles, "local.dram_q_slot_size_tiles");
    append(dram_io_state->local.reader_index, "local.reader_index");
    append(dram_io_state->local.total_readers, "local.total_readers");
    append(dram_io_state->local.dram_decoupled, "local.dram_decoupled");
    append(dram_io_state->local.stride_wrap, "local.stride_wrap");
    append(dram_io_state->local.dram_io_scatter_state, "local.dram_io_scatter_state");
    append(dram_io_state->local.next, "local.next");
    pad_to_noc_alignment();

    log_assert(
        m_data.size() - size_before == sizeof(dram_io_state_t),
        "Size of written data {} does not match size of dram_io_state_t {}",
        m_data.size() - size_before,
        sizeof(dram_io_state_t));
}

void BlobSection::add_dram_io_scatter_state_t(const dram_io_scatter_state_t* dram_io_scatter_state)
{
    size_t size_before = m_data.size();
    append_debug("[DRAM_IO_SCATTER_STATE_T]");
    append(dram_io_scatter_state->unused1, "unused1");
    append(dram_io_scatter_state->scatter_offsets_size, "scatter_offsets_size");
    append(dram_io_scatter_state->scatter_chunk_size_bytes, "scatter_chunk_size_bytes");
    append(dram_io_scatter_state->q_slot_size_bytes, "q_slot_size_bytes");
    append(dram_io_scatter_state->scatter_chunk_size_tiles, "scatter_chunk_size_tiles");
    append(dram_io_scatter_state->unused2, "unused2");
    append(dram_io_scatter_state->unused3, "unused3");
    append(dram_io_scatter_state->scatter_offsets, "scatter_offsets");
    pad_to_noc_alignment();

    log_assert(
        m_data.size() - size_before == sizeof(dram_io_scatter_state_t),
        "Size of written data {} does not match size of dram_io_scatter_state_t {}",
        m_data.size() - size_before,
        sizeof(dram_io_scatter_state_t));
}

void BlobSection::add_dram_io_scatter_offsets(
    const tt_uint64_t* scatter_offsets, const size_t size, const bool hw_tilize)
{
    size_t size_before = m_data.size();
    append_debug("[DRAM_IO_SCATTER_OFFSETS]");

    // Note that we always pad as if the scatter_offsets are 64-bit, even if they are 32-bit in
    // case of hw_tilize. This can be probably changed to be more efficient.
    if (hw_tilize)
    {
        for (size_t i = 0; i < size; i++)
        {
            append(scatter_offsets[i].hi, "scatter_offsets[" + std::to_string(i) + "].hi");
        }
        for (size_t i = 0; i < size; i++)
        {
            append(static_cast<uint32_t>(0), "non hw_tilize padding");
        }
    }
    else
    {
        for (size_t i = 0; i < size; i++)
        {
            append(scatter_offsets[i], "scatter_offsets[" + std::to_string(i) + "]");
        }
    }

    log_assert(
        m_data.size() - size_before == sizeof(tt_uint64_t) * size,
        "Size of written data {} does not match size of tt_uint64_t array {}",
        m_data.size() - size_before,
        sizeof(tt_uint64_t) * size);

    pad_to_noc_alignment();
}

void BlobSection::add_scatter_pipe_states(const scatter_pipe_state_t* scatter_pipe_state_arr, const size_t size)
{
    size_t size_before = m_data.size();
    append_debug("[SCATTER_PIPE_STATES]");

    for (size_t i = 0; i < size; i++)
    {
        const scatter_pipe_state_t& scatter_pipe_state = scatter_pipe_state_arr[i];
        append(scatter_pipe_state.curr_unroll, "scatter_pipe_state[" + std::to_string(i) + "].curr_unroll");
        append(scatter_pipe_state.max_unroll, "scatter_pipe_state[" + std::to_string(i) + "].max_unroll");
        append(scatter_pipe_state.unused0, "scatter_pipe_state[" + std::to_string(i) + "].unused0");
        append(scatter_pipe_state.unroll_blobs, "scatter_pipe_state[" + std::to_string(i) + "].unroll_blobs");
    }

    log_assert(
        m_data.size() - size_before == sizeof(scatter_pipe_state_t) * size,
        "Size of written data {} does not match size of scatter_pipe_state_t array {}",
        m_data.size() - size_before,
        sizeof(scatter_pipe_state_t) * size);

    // We don't pad here to NOC alignment. scatter pipe states and scatter pipe blobs are padded together.
}

void BlobSection::add_scatter_pipe_blobs(const scatter_pipe_blob_t* scatter_pipe_blob_arr, const size_t size)
{
    append_debug("[SCATTER_PIPE_BLOBS]");
    for (size_t i = 0; i < size; i++)
    {
        const scatter_pipe_blob_t& scatter_pipe_blob = scatter_pipe_blob_arr[i];
        append(
            scatter_pipe_blob.scatter_blob_base_addr,
            "scatter_pipe_blob[" + std::to_string(i) + "].scatter_blob_base_addr");
        append(
            scatter_pipe_blob.start_scatter_blob_num_cfg_regs,
            "scatter_pipe_blob[" + std::to_string(i) + "].start_scatter_blob_num_cfg_regs");
    }

    // We don't pad here to NOC alignment. Padding has to be done only after all scatter_pipe_blobs.
}

void BlobSection::print_out(std::ostream& os, const bool dump_debug_info) const
{
    os << "@";
    // std::endl flushes the output. Since we are writing a lot of short lines, flushing often is very slow, so use
    // '\n\ instead.
    os << std::setw(8) << std::setfill('0') << std::hex << (m_address >> 2) << '\n';

    log_assert(m_data.size() % 4 == 0, "Data that in a section is not 4 byte aligned {}", m_data.size());

    for (int row_ind = 0; row_ind < m_data.size() / 4; row_ind++)
    {
        for (int m_data_ind = 3; m_data_ind >= 0; m_data_ind--)
        {
            os << std::setw(2) << std::setfill('0') << std::hex << (unsigned)m_data[row_ind * 4 + m_data_ind];
        }
        if (dump_debug_info && row_ind < m_debug_data.size() && !m_debug_data[row_ind].empty())
        {
            os << m_debug_data[row_ind];
        }
        os << '\n';
    }
}

// Ind is the normal index of the first byte of wanted dword.
uint32_t BlobSection::get_dw_at(const int ind)
{
    log_assert(ind + 3 < m_data.size(), "Trying to read a dword from a BlobSection that doesn't have enough data");
    uint32_t result = 0;
    result |= m_data[ind];
    result |= m_data[ind + 1] << 8;
    result |= m_data[ind + 2] << 16;
    result |= m_data[ind + 3] << 24;
    return result;
}

void BlobSection::merge(BlobSection&& other, std::function<uint32_t(uint32_t)> map_func)
{
    // Resize m_debug_data so that it doesn't become mismatched.
    while (m_debug_data.size() < m_data.size() / 4)
    {
        m_debug_data.push_back("");
    }

    for (int dwind = 0; dwind < other.m_data.size(); dwind += 4)
    {
        uint32_t dword = other.get_dw_at(dwind);
        if (map_func)
        {
            dword = map_func(dword);
        }
        append(dword);
        if (dwind / 4 < other.m_debug_data.size())
        {
            m_debug_data.push_back(other.m_debug_data[dwind / 4]);
        }
    }
    other.m_data.clear();
    other.m_debug_data.clear();
}

void BlobData::print_out(
    const std::string output_dir, const tt_cxy_pair& core_location, int epoch_num, bool dump_debug_info) const
{
    std::string file_name = output_dir + "/pipegen_epoch" + std::to_string(epoch_num) + "_" +
                            std::to_string(core_location.chip) + "_" + std::to_string(core_location.y) + "_" +
                            std::to_string(core_location.x) + ".hex";

    std::ofstream file;

    std::filesystem::create_directories(std::filesystem::path(file_name).parent_path());

    file.open(file_name, std::ios::out | std::ios::binary);
    for (const auto& section : blob_sections)
    {
        section.print_out(file, dump_debug_info);
    }

    file.close();
}

}  // namespace blobgen2