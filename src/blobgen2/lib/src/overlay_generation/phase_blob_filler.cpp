// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "overlay_generation/phase_blob_filler.h"

#include "helpers/ncrisc_config_helper.h"
#include "helpers/noc_helper.h"
#include "helpers/phase_config_helper.h"
#include "overlay_blob/blob_section.h"
#include "overlay_generation/phase_blob_filler_arch_specific.h"

namespace blobgen2
{

// TODO: The code in old blobgen ruby highly suggests that overlay_version > 1 <=> arch > grayskull.
// Confirm and refactor the code accordingly.
BlobSection PhaseBlobFiller::get_phase_blob(
    const tt_cxy_pair& core_location,
    const StreamId stream_id,
    const PhaseConfig& phase,
    const PhaseConfig* prev_phase,
    const PhaseConfig* next_phase,
    const std::optional<PhaseConfig> dest_phase,
    const std::vector<NcriscConfig>& dram_blobs,
    const unsigned int src_old_base,
    const unsigned int src_new_base,
    const bool next_phase_is_dummy_phase,
    const unsigned int overlay_version,
    const NOCHelper& noc_helper)
{
    PhaseConfigHelper phase_helper(phase);

    bool first_phase = prev_phase == nullptr;
    bool src_change = phase_helper.get_src_change(prev_phase);
    bool dest_change = phase_helper.get_dest_change(prev_phase);

    bool dram_read_phase_resets_pointers =
        phase_helper.get_dram_input() && src_change && phase_helper.get_ptrs_not_zero();
    bool phase_resets_pointers = src_change;

    BlobSection phase_blob(0);
    phase_blob.append_debug("[PHASE_BLOB]");

    if (first_phase || phase_helper.is_scatter_order_size_larger_than_one() ||
        phase_helper.is_phase_wrapped(prev_phase->phase_id))
    {
        phase_blob.append(
            configure_4_byte_register(STREAM_CURR_PHASE_REG_INDEX, phase_helper.get_wrap_phase_id()),
            "STREAM_CURR_PHASE_REG_INDEX");
    }

    int dram_num_msgs = get_dram_num_msgs(phase, dram_blobs, dram_read_phase_resets_pointers);

    if (src_change)
    {
        setup_phase_blob_for_src_change(
            phase_blob, noc_helper, phase, first_phase, core_location, src_old_base, src_new_base, overlay_version);
    }

    if ((src_change || phase_helper.get_dram_input_no_push()) && phase_helper.get_dram_or_mmio_io() &&
        phase_helper.is_regular_dram_input())
    {
        // Used as scratch register to track tiles pushed into the stream on the dram read side.
        phase_blob.append(
            configure_4_byte_register(
                STREAM_REMOTE_SRC_PHASE_REG_INDEX,
                ((dram_read_phase_resets_pointers ? 1 << 16 : 0) | (phase_helper.get_resend() ? 0 : dram_num_msgs))),
            "STREAM_REMOTE_SRC_PHASE_REG_INDEX");
    }

    if (dest_change)
    {
        setup_phase_blob_for_dest_change(
            phase_blob,
            noc_helper,
            phase,
            dest_phase,
            first_phase,
            core_location,
            overlay_version,
            phase_resets_pointers,
            dram_num_msgs);
    }
    else
    {
        if (phase_helper.get_remote_receiver())
        {
            phase_blob.append(
                configure_4_byte_register(
                    STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX,
                    dest_phase.value().config.get_msg_info_buf_addr().value() / MEM_WORD_BYTES),
                "STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX");
        }
    }

    setup_misc_cfg_reg_regular_phase(phase_blob, phase, next_phase, next_phase_is_dummy_phase);

    if (phase.config.get_buf_space_available_ack_thr().has_value())
    {
        phase_blob.append(
            configure_4_byte_register(
                STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX,
                phase.config.get_buf_space_available_ack_thr().value()),
            "STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX");
    }
    else if (first_phase)
    {
        phase_blob.append(
            configure_4_byte_register(STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX, 0),
            "STREAM_MEM_BUF_SPACE_AVAILABLE_ACK_THRESHOLD_REG_INDEX");
    }

    if (phase_helper.get_source_endpoint() && (phase_helper.is_dram_input_prolog() || phase_helper.get_legacy_pack()))
    {
        // Used as scratch register to track tiles pushed into the stream on the packer side.
        phase_blob.append(
            configure_4_byte_register(
                STREAM_REMOTE_SRC_PHASE_REG_INDEX, phase_helper.get_resend() ? 0 : phase_helper.get_num_msgs()),
            "STREAM_REMOTE_SRC_PHASE_REG_INDEX");
        if (phase_resets_pointers)
        {
            phase_blob.append(configure_4_byte_register(STREAM_REMOTE_SRC_REG_INDEX, 1), "STREAM_REMOTE_SRC_REG_INDEX");
        }
    }

    if (phase_helper.is_receiver_unpacker())
    {
        // Used as scratch register to track tiles popped from the stream on the unpacker side.
        phase_blob.append(
            configure_4_byte_register(STREAM_REMOTE_DEST_REG_INDEX, phase_helper.get_num_msgs()),
            "STREAM_REMOTE_DEST_REG_INDEX");
        if (phase_resets_pointers)
        {
            PhaseBlobFillerArchSpecific::setup_stream_remote_dest_when_src_change(phase_blob);
        }
    }

    if (phase_helper.get_resend())
    {
        setup_phase_blob_for_resend(phase_blob, phase);
    }

    return phase_blob;
}

BlobSection PhaseBlobFiller::get_dummy_phase_blob(
    const tt_cxy_pair& core_location,
    const StreamId stream_id,
    const PhaseConfig& phase,
    const uint32_t dummy_phase_addr,
    const uint32_t dest_dummy_phase_addr,
    const bool is_sender,
    const bool is_receiver,
    const unsigned int overlay_version,
    const NOCHelper& noc_helper)
{
    PhaseConfigHelper phase_helper(phase);
    BlobSection phase_blob(0);
    phase_blob.append_debug("[DUMMY_BLOB]");

    phase_blob.append(
        configure_4_byte_register(STREAM_CURR_PHASE_REG_INDEX, dummy_phase(phase.phase_id)),
        "STREAM_CURR_PHASE_REG_INDEX");

    uint32_t dummy_phase_word_address = dummy_phase_addr / MEM_WORD_BYTES;
    uint32_t dest_dummy_phase_word_address = dest_dummy_phase_addr / MEM_WORD_BYTES;

    phase_blob.append(
        configure_4_byte_register(STREAM_BUF_START_REG_INDEX, dummy_phase_word_address), "STREAM_BUF_START_REG_INDEX");
    phase_blob.append(configure_4_byte_register(STREAM_BUF_SIZE_REG_INDEX, 1), "STREAM_BUF_SIZE_REG_INDEX");
    phase_blob.append(
        configure_4_byte_register(STREAM_MSG_INFO_PTR_REG_INDEX, dummy_phase_word_address),
        "STREAM_MSG_INFO_PTR_REG_INDEX");
    phase_blob.append(
        configure_4_byte_register(STREAM_MSG_INFO_WR_PTR_REG_INDEX, dummy_phase_word_address),
        "STREAM_MSG_INFO_WR_PTR_REG_INDEX");

    if (is_receiver)
    {
        tt_xy_pair stream_src = phase_helper.get_stream_source_coords(core_location, noc_helper);
        StreamId src_stream_id = phase_helper.get_src_stream_node()->get_stream_id();

        phase_blob.append(
            configure_4_byte_register(
                STREAM_REMOTE_SRC_REG_INDEX,
                stream_remote_src(stream_src.x, stream_src.y, src_stream_id, phase_helper.get_src_dest_index())),
            "STREAM_REMOTE_SRC_REG_INDEX");
        phase_blob.append(
            configure_4_byte_register(STREAM_REMOTE_SRC_PHASE_REG_INDEX, dummy_phase(phase.phase_id)),
            "STREAM_REMOTE_SRC_PHASE_REG_INDEX");
    }

    if (is_sender)
    {
        StreamId dest_stream_id = phase_helper.get_dest_stream_id();
        tt_xy_pair remote_dest_coords = phase_helper.get_remote_dest_coords(core_location, noc_helper);

        phase_blob.append(
            configure_4_byte_register(
                STREAM_REMOTE_DEST_REG_INDEX,
                stream_remote_dest(remote_dest_coords.x, remote_dest_coords.y, dest_stream_id)),
            "STREAM_REMOTE_DEST_REG_INDEX");
        phase_blob.append(
            configure_4_byte_register(STREAM_REMOTE_DEST_BUF_START_REG_INDEX, dest_dummy_phase_word_address),
            "STREAM_REMOTE_DEST_BUF_START_REG_INDEX");
        phase_blob.append(
            configure_4_byte_register(STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX, 1),
            "STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX");
        phase_blob.append(
            configure_4_byte_register(STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX, dest_dummy_phase_word_address),
            "STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX");

        if (phase_helper.has_dest_mcast_streams())
        {
            tt_xy_pair dest_noc = phase_helper.get_dest_noc_coords(noc_helper);

            if (overlay_version > 1)
            {
                phase_blob.append(
                    configure_4_byte_register(
                        STREAM_MCAST_DEST_REG_INDEX, stream_mcast_dest_v2(1, dest_noc, phase_helper)),
                    "STREAM_MCAST_DEST_REG_INDEX");
            }
            else
            {
                phase_blob.append(
                    configure_4_byte_register(
                        STREAM_MCAST_DEST_REG_INDEX, stream_mcast_dest_v1(1, dest_noc, phase_helper)),
                    "STREAM_MCAST_DEST_REG_INDEX");
            }
            phase_blob.append(
                configure_4_byte_register(STREAM_MCAST_DEST_NUM_REG_INDEX, phase.config.get_num_mcast_dests().value()),
                "STREAM_MCAST_DEST_NUM_REG_INDEX");
        }
        else
        {
            if (overlay_version > 1)
            {
                phase_blob.append(
                    configure_4_byte_register(
                        STREAM_MCAST_DEST_REG_INDEX, stream_mcast_dest_v2(0, {0, 0}, phase_helper)),
                    "STREAM_MCAST_DEST_REG_INDEX");
            }
            else
            {
                phase_blob.append(
                    configure_4_byte_register(
                        STREAM_MCAST_DEST_REG_INDEX, stream_mcast_dest_v1(0, {0, 0}, phase_helper)),
                    "STREAM_MCAST_DEST_REG_INDEX");
            }
            phase_blob.append(
                configure_4_byte_register(STREAM_MCAST_DEST_NUM_REG_INDEX, 1), "STREAM_MCAST_DEST_NUM_REG_INDEX");
        }
    }

    setup_misc_cfg_reg_dummy_phase(phase_blob, phase, is_sender, is_receiver);

    return phase_blob;
}

uint32_t PhaseBlobFiller::blob_header_dw(
    const uint32_t next_phase_num_cfg_reg_writes, const uint32_t curr_phase_num_msgs, const uint32_t phase_num_incr)
{
    uint32_t header = 0;
    header |= next_phase_num_cfg_reg_writes << 24;
    header |= curr_phase_num_msgs << 12;
    header |= phase_num_incr;
    return header;
}

uint32_t PhaseBlobFiller::intermediates_blob_loop_dw(const uint32_t phase_start_addr)
{
    return configure_4_byte_register(STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX, phase_start_addr);
}

uint32_t PhaseBlobFiller::configure_4_byte_register(const uint8_t reg_index, const uint32_t reg_val)
{
    return (reg_val << 8) | reg_index;
}

uint32_t PhaseBlobFiller::stream_gather_clear(
    const uint32_t local_stream_clear_num, const uint32_t msg_group_stream_clear_type)
{
    return (local_stream_clear_num << MSG_LOCAL_STREAM_CLEAR_NUM) |
           (msg_group_stream_clear_type << MSG_GROUP_STREAM_CLEAR_TYPE);
}

uint32_t PhaseBlobFiller::modify_blob_dw(
    const uint32_t blob_dw, const uint32_t reg_index, const uint32_t mask, const uint32_t val)
{
    uint32_t new_blob_dw = blob_dw;
    if ((blob_dw & 0xFF) == reg_index)
    {
        new_blob_dw = blob_dw >> 8;
        new_blob_dw = (new_blob_dw & mask) | val;
        new_blob_dw = configure_4_byte_register(reg_index, new_blob_dw);
    }
    return new_blob_dw;
}

// TODO: This function can be greatly simplified.
// There are three registers (see noc_overlay_parameters.h) starting at STREAM_LOCAL_SRC_MASK_REG_INDEX.
// Their combined 24*3=72 bits are used to indicate which local streams are connected to the current stream.
// This will always be a single stream, so this code can be simplified to just set the appropriate bit.
// The hypothesis is also that no_sender_below_stream_40 doesn't do anything.
void PhaseBlobFiller::generate_blob_for_stream_with_local_source(
    BlobSection& phase_blob, const StreamNode* src_stream_node, const size_t current_chip, const bool first_phase)
{
    bool no_sender_below_stream_40 = true;
    std::vector<bool> local_src_mask(NOC_NUM_STREAMS, false);

    if (src_stream_node != nullptr)
    {
        tt_cxy_pair src_core = src_stream_node->get_physical_location();
        StreamId src_stream_id = src_stream_node->get_stream_id();
        log_assert(
            src_core.chip == current_chip,
            "Expected source core {} to be on the same chip {}",
            src_core.chip,
            current_chip);
        local_src_mask[src_stream_id] = true;
        if (src_stream_id < 40)
        {
            no_sender_below_stream_40 = false;
        }
    }

    uint32_t num_local_src_mask_regs = NOC_NUM_STREAMS / STREAM_REG_CFG_DATA_WIDTH;
    if (NOC_NUM_STREAMS % STREAM_REG_CFG_DATA_WIDTH != 0)
    {
        num_local_src_mask_regs += 1;
    }
    for (int reg_ind = 0; reg_ind < num_local_src_mask_regs; reg_ind++)
    {
        uint32_t val = 0;
        for (int stream_id = reg_ind * STREAM_REG_CFG_DATA_WIDTH; stream_id < (reg_ind + 1) * STREAM_REG_CFG_DATA_WIDTH;
             stream_id++)
        {
            if (stream_id < NOC_NUM_STREAMS)
            {
                if (local_src_mask[stream_id])
                {
                    val = val | (1 << (stream_id - (reg_ind * STREAM_REG_CFG_DATA_WIDTH)));
                }
            }
        }
        // Configure register which tells gather stream which local stream is the source in the current phase. This is
        // done by setting appropriate bit in a register.
        if (first_phase || !no_sender_below_stream_40 || reg_ind > 0)
        {
            phase_blob.append(
                configure_4_byte_register(STREAM_LOCAL_SRC_MASK_REG_INDEX + reg_ind, val),
                "STREAM_LOCAL_SRC_MASK_REG_INDEX");
        }
    }
}

void PhaseBlobFiller::setup_phase_blob_for_src_change(
    BlobSection& phase_blob,
    const NOCHelper& noc_helper,
    const PhaseConfig& phase,
    const bool first_phase,
    const tt_cxy_pair& core_location,
    const unsigned int src_old_base,
    const unsigned int src_new_base,
    const unsigned int overlay_version)
{
    PhaseConfigHelper phase_helper(phase);

    phase_blob.append(
        configure_4_byte_register(
            STREAM_BUF_START_REG_INDEX,
            (phase.config.get_buf_addr().value() - src_old_base + src_new_base) / MEM_WORD_BYTES),
        "STREAM_BUF_START_REG_INDEX");
    phase_blob.append(
        configure_4_byte_register(STREAM_BUF_SIZE_REG_INDEX, phase.config.get_buffer_size().value() / MEM_WORD_BYTES),
        "STREAM_BUF_SIZE_REG_INDEX");
    phase_blob.append(
        configure_4_byte_register(
            STREAM_MSG_INFO_PTR_REG_INDEX, phase.config.get_msg_info_buf_addr().value() / MEM_WORD_BYTES),
        "STREAM_MSG_INFO_PTR_REG_INDEX");

    if (!phase_helper.get_resend())
    {
        phase_blob.append(
            configure_4_byte_register(
                STREAM_MSG_INFO_WR_PTR_REG_INDEX, phase.config.get_msg_info_buf_addr().value() / MEM_WORD_BYTES),
            "STREAM_MSG_INFO_WR_PTR_REG_INDEX");
    }

    if (phase_helper.has_remote_source())
    {
        StreamId src_stream_id = phase_helper.get_src_stream_node()->get_stream_id();

        tt_xy_pair stream_src = phase_helper.get_stream_source_coords(core_location, noc_helper);

        phase_blob.append(
            configure_4_byte_register(
                STREAM_REMOTE_SRC_REG_INDEX,
                stream_remote_src(stream_src.x, stream_src.y, src_stream_id, phase_helper.get_src_dest_index())),
            "STREAM_REMOTE_SRC_REG_INDEX");
        phase_blob.append(
            configure_4_byte_register(STREAM_REMOTE_SRC_PHASE_REG_INDEX, phase_helper.get_wrap_phase_id()),
            "STREAM_REMOTE_SRC_PHASE_REG_INDEX");
    }

    if (phase.config.get_local_sources_connected().value_or(false))
    {
        if (first_phase)
        {
            PhaseBlobFillerArchSpecific::setup_stream_gather_reg(
                phase_blob, phase, core_location.chip, overlay_version);
        }

        generate_blob_for_stream_with_local_source(
            phase_blob, phase_helper.get_src_stream_node(), core_location.chip, first_phase);
    }
}

void PhaseBlobFiller::setup_phase_blob_for_dest_change(
    BlobSection& phase_blob,
    const NOCHelper& noc_helper,
    const PhaseConfig& phase,
    const std::optional<PhaseConfig>& dest_phase,
    const bool first_phase,
    const tt_cxy_pair& core_location,
    const int overlay_version,
    const bool phase_resets_pointers,
    const int dram_num_msgs)
{
    PhaseConfigHelper phase_helper(phase);
    if (phase_helper.has_remote_receiver())
    {
        StreamId dest_stream_id = phase_helper.get_dest_stream_id();

        tt_xy_pair remote_dest_coords = phase_helper.get_remote_dest_coords(core_location, noc_helper);

        phase_blob.append(
            configure_4_byte_register(
                STREAM_REMOTE_DEST_BUF_START_REG_INDEX,
                dest_phase.value().config.get_buf_addr().value() / MEM_WORD_BYTES),
            "STREAM_REMOTE_DEST_BUF_START_REG_INDEX");
        phase_blob.append(
            configure_4_byte_register(
                STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX,
                dest_phase.value().config.get_buffer_size().value() / MEM_WORD_BYTES),
            "STREAM_REMOTE_DEST_BUF_SIZE_REG_INDEX");

        if (phase_helper.get_remote_receiver())
        {
            phase_blob.append(
                configure_4_byte_register(
                    STREAM_REMOTE_DEST_REG_INDEX,
                    stream_remote_dest(remote_dest_coords.x, remote_dest_coords.y, dest_stream_id)),
                "STREAM_REMOTE_DEST_REG_INDEX");

            phase_blob.append(
                configure_4_byte_register(
                    STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX,
                    dest_phase.value().config.get_msg_info_buf_addr().value() / MEM_WORD_BYTES),
                "STREAM_REMOTE_DEST_MSG_INFO_WR_PTR_REG_INDEX");
        }

        PhaseBlobFillerArchSpecific::setup_stream_remote_dest_traffic(phase_blob, phase);

        if (first_phase)
        {
            if (phase_helper.has_dest_mcast_streams())
            {
                tt_xy_pair dest_noc = phase_helper.get_dest_noc_coords(noc_helper);

                if (overlay_version > 1)
                {
                    phase_blob.append(
                        configure_4_byte_register(
                            STREAM_MCAST_DEST_REG_INDEX, stream_mcast_dest_v2(1, dest_noc, phase_helper)),
                        "STREAM_MCAST_DEST_REG_INDEX");
                }
                else
                {
                    phase_blob.append(
                        configure_4_byte_register(
                            STREAM_MCAST_DEST_REG_INDEX, stream_mcast_dest_v1(1, dest_noc, phase_helper)),
                        "STREAM_MCAST_DEST_REG_INDEX");
                }
                phase_blob.append(
                    configure_4_byte_register(
                        STREAM_MCAST_DEST_NUM_REG_INDEX, phase.config.get_num_mcast_dests().value()),
                    "STREAM_MCAST_DEST_NUM_REG_INDEX");
            }
            else
            {
                if (overlay_version > 1)
                {
                    phase_blob.append(
                        configure_4_byte_register(
                            STREAM_MCAST_DEST_REG_INDEX, stream_mcast_dest_v2(0, {0, 0}, phase_helper)),
                        "STREAM_MCAST_DEST_REG_INDEX");
                }
                else
                {
                    phase_blob.append(
                        configure_4_byte_register(
                            STREAM_MCAST_DEST_REG_INDEX, stream_mcast_dest_v1(0, {0, 0}, phase_helper)),
                        "STREAM_MCAST_DEST_REG_INDEX");
                }
                phase_blob.append(
                    configure_4_byte_register(STREAM_MCAST_DEST_NUM_REG_INDEX, 1), "STREAM_MCAST_DEST_NUM_REG_INDEX");
            }
        }
    }
    else if (phase_helper.get_regular_local_receiver())
    {
        PhaseBlobFillerArchSpecific::setup_stream_local_dest(phase_blob, phase, overlay_version);
    }
    else
    {
        if (first_phase && overlay_version == 1)
        {
            phase_blob.append(
                configure_4_byte_register(STREAM_MCAST_DEST_REG_INDEX, stream_mcast_dest_v1(0, {0, 0}, phase_helper)),
                "STREAM_MCAST_DEST_REG_INDEX");
        }
    }

    if (phase_helper.get_dram_or_mmio_io() && phase_helper.is_regular_dram_output())
    {
        // Used as scratch register to track tiles popped from the stream on the dram write side.
        phase_blob.append(
            configure_4_byte_register(
                STREAM_REMOTE_DEST_REG_INDEX, ((phase_resets_pointers ? 1 << 16 : 0) | dram_num_msgs)),
            "STREAM_REMOTE_DEST_REG_INDEX");
    }
}

void PhaseBlobFiller::setup_phase_blob_for_resend(BlobSection& phase_blob, const PhaseConfig& phase)
{
    PhaseConfigHelper phase_helper(phase);
    if (phase_helper.get_all_msgs_not_fit_into_buffer())
    {
        phase_blob.append(
            configure_4_byte_register(STREAM_WR_PTR_REG_INDEX, phase_helper.get_all_msgs_size() / MEM_WORD_BYTES),
            "STREAM_WR_PTR_REG_INDEX");
    }
    else
    {
        PhaseBlobFillerArchSpecific::setup_stream_wr_ptr_reg(phase_blob);

        phase_blob.append(
            configure_4_byte_register(STREAM_MSG_INFO_WR_PTR_REG_INDEX, phase_helper.get_msg_info_buf_addr_end()),
            "STREAM_MSG_INFO_WR_PTR_REG_INDEX");
    }
}

void PhaseBlobFiller::setup_misc_cfg_reg_regular_phase(
    BlobSection& phase_blob,
    const PhaseConfig& phase,
    const PhaseConfig* next_phase,
    const bool next_phase_is_dummy_phase)
{
    PhaseConfigHelper phase_helper(phase);

    PhaseBlobFillerArchSpecific::setup_misc_cfg_reg(
        phase_blob,
        phase_helper.get_incoming_data_noc_for_stream_reg(),
        phase_helper.get_outgoing_data_noc_for_stream_reg(),
        phase_helper.get_remote_src_update_noc_for_stream_reg(),
        phase_helper.get_local_sources_connected(),
        phase_helper.get_source_endpoint(),
        phase_helper.get_remote_source(),
        phase_helper.get_receiver_endpoint(),
        phase_helper.get_local_receiver(),
        phase_helper.get_remote_receiver(),
        phase_helper.get_phase_auto_config(next_phase) || next_phase_is_dummy_phase,
        phase_helper.get_phase_auto_advance(),
        phase_helper.get_data_auto_send(),
        phase_helper.get_next_phase_src_change() || next_phase_is_dummy_phase,
        phase_helper.get_next_phase_dest_change() || next_phase_is_dummy_phase,
        phase_helper.get_data_buf_no_flow_ctrl(),
        phase_helper.get_dest_data_buf_no_flow_ctrl(),
        phase_helper.get_remote_src_is_mcast(),
        phase_helper.get_no_prev_phase_outgoing_data_flush(),
        phase_helper.get_vc_for_unicast(),
        phase_helper.get_vc_for_reg_update());
}

void PhaseBlobFiller::setup_misc_cfg_reg_dummy_phase(
    BlobSection& phase_blob, const PhaseConfig& phase, const bool is_sender, const bool is_receiver)
{
    PhaseConfigHelper phase_helper(phase);

    PhaseBlobFillerArchSpecific::setup_misc_cfg_reg(
        phase_blob,
        phase_helper.get_incoming_data_noc_for_stream_reg(),
        phase_helper.get_outgoing_data_noc_for_stream_reg(),
        phase_helper.get_remote_src_update_noc_for_stream_reg(),
        false,
        is_sender,
        is_receiver,
        false,
        false,
        is_sender,
        true,
        phase_helper.get_phase_auto_advance(),
        phase_helper.get_data_auto_send(),
        true,
        true,
        phase_helper.get_data_buf_no_flow_ctrl(),
        phase_helper.get_dest_data_buf_no_flow_ctrl(),
        phase_helper.get_remote_src_is_mcast(),
        phase_helper.get_no_prev_phase_outgoing_data_flush(),
        phase_helper.get_vc_for_unicast(),
        phase_helper.get_vc_for_reg_update());
}

int PhaseBlobFiller::get_dram_num_msgs(
    const PhaseConfig& phase, const std::vector<NcriscConfig>& dram_blobs, const bool dram_read_phase_resets_pointers)
{
    PhaseConfigHelper phase_helper(phase);
    bool scatter_to_dram = phase_helper.get_scatter_to_dram();

    int dram_num_msgs = 0;
    if (phase_helper.get_dram_or_mmio_io())
    {
        dram_num_msgs = NcriscConfigHelper::get_dram_num_msgs(dram_blobs) * phase.config.get_num_unroll_iter().value();
        if (dram_num_msgs > pipegen2::constants::general_max_num_tiles_per_phase ||
            phase_helper.get_dram_input_no_push() || scatter_to_dram || dram_read_phase_resets_pointers ||
            phase_helper.get_next_phase_src_change())
        {
            dram_num_msgs = phase_helper.get_num_msgs();
        }
    }

    return dram_num_msgs;
}

uint32_t PhaseBlobFiller::stream_remote_src(
    const size_t x, const size_t y, const StreamId stream_id, const unsigned int dest_index)
{
    return (x << STREAM_REMOTE_SRC_X) | (y << STREAM_REMOTE_SRC_Y) | (stream_id << REMOTE_SRC_STREAM_ID) |
           (dest_index << STREAM_REMOTE_SRC_DEST_INDEX);
}

uint32_t PhaseBlobFiller::stream_remote_dest(
    const uint32_t dest_x, const uint32_t dest_y, const StreamId dest_stream_id)
{
    return (dest_x << STREAM_REMOTE_DEST_X) | (dest_y << STREAM_REMOTE_DEST_Y) |
           (dest_stream_id << STREAM_REMOTE_DEST_STREAM_ID);
}

uint32_t PhaseBlobFiller::stream_mcast_dest_v1(
    const uint8_t mcast_en, const tt_xy_pair& dest_end, const PhaseConfigHelper& phase_helper)
{
    log_assert(mcast_en == 0 || phase_helper.get_vc_for_multicast() != 1, "VC for multicast must be different than 1");
    return (mcast_en << STREAM_MCAST_EN) | (dest_end.x << STREAM_MCAST_END_X) | (dest_end.y << STREAM_MCAST_END_Y) |
           (phase_helper.get_arb_group_size() << MSG_ARB_GROUP_SIZE) |
           (phase_helper.get_src_in_order_fwd() << MSG_SRC_IN_ORDER_FWD) |
           (phase_helper.get_linked() << STREAM_MCAST_LINKED) |
           (phase_helper.get_no_path_res() << STREAM_MCAST_NO_PATH_RES) |
           (phase_helper.get_mcast_xy() << STREAM_MCAST_XY) | (phase_helper.get_vc_for_multicast() << STREAM_MCAST_VC);
}

uint32_t PhaseBlobFiller::stream_mcast_dest_v2(
    const uint8_t mcast_en, const tt_xy_pair& dest_end, const PhaseConfigHelper& phase_helper)
{
    log_assert(mcast_en == 0 || phase_helper.get_vc_for_multicast() != 1, "VC for multicast must be different than 1");
    return (mcast_en << STREAM_MCAST_EN) | (dest_end.x << STREAM_MCAST_END_X) | (dest_end.y << STREAM_MCAST_END_Y) |
           (phase_helper.get_linked() << STREAM_MCAST_LINKED) |
           (phase_helper.get_no_path_res() << STREAM_MCAST_NO_PATH_RES) |
           (phase_helper.get_mcast_xy() << STREAM_MCAST_XY) | (phase_helper.get_vc_for_multicast() << STREAM_MCAST_VC);
}

uint32_t PhaseBlobFiller::dummy_phase(const PhaseId phase_num)
{
    int epoch_num = phase_num >> pipegen2::constants::epoch_id_phase_shift;
    unsigned int phase_mask = (1 << c_epoch_id_phase_shift_firmware) - 1;
    return (0x1F << c_epoch_id_phase_shift_firmware) | (epoch_num & phase_mask);
}

}  // namespace blobgen2