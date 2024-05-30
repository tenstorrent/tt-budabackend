// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "overlay_generation/phase_blob_filler_arch_specific.h"

#include "helpers/phase_config_helper.h"
#include "noc_overlay_parameters.h"
#include "overlay_blob/blob_section.h"

namespace blobgen2
{

uint32_t PhaseBlobFillerArchSpecific::set_auto_cfg(const uint32_t blob_dw, const uint32_t val)
{
    return modify_blob_dw(blob_dw, STREAM_MISC_CFG_REG_INDEX, ~(1 << PHASE_AUTO_CONFIG), val << PHASE_AUTO_CONFIG);
}

uint32_t PhaseBlobFillerArchSpecific::stream_gather(
    const size_t chip_id,
    const uint32_t arb_group_size,
    const bool src_in_order_fwd,
    const uint32_t local_stream_clear_num,
    const uint32_t msg_group_stream_clear_type)
{
    return (arb_group_size << MSG_ARB_GROUP_SIZE) | (src_in_order_fwd << MSG_SRC_IN_ORDER_FWD);
}

void PhaseBlobFillerArchSpecific::setup_misc_cfg_reg(
    BlobSection& phase_blob,
    const NOC_ROUTE incoming_data_noc,
    const NOC_ROUTE outgoing_data_noc,
    const NOC_ROUTE remote_src_update_noc,
    const bool local_sources_connected,
    const bool source_endpoint,
    const bool remote_source,
    const bool receiver_endpoint,
    const bool local_receiver,
    const bool remote_receiver,
    const bool phase_auto_config,
    const bool phase_auto_advance,
    const bool data_auto_send,
    const bool next_phase_src_change,
    const bool next_phase_dest_change,
    const bool data_buf_no_flow_ctrl,
    const bool dest_data_buf_no_flow_ctrl,
    const bool remote_src_is_mcast,
    const bool no_prev_phase_outgoing_data_flush,
    const unsigned int unicast_vc_reg,
    const unsigned int reg_update_vc_reg)
{
    // clang-format off
    phase_blob.append(
        configure_4_byte_register(
            STREAM_MISC_CFG_REG_INDEX,
                static_cast<int>(incoming_data_noc) << INCOMING_DATA_NOC |
                static_cast<int>(outgoing_data_noc) << OUTGOING_DATA_NOC |
                static_cast<int>(remote_src_update_noc) << REMOTE_SRC_UPDATE_NOC |
                local_sources_connected << LOCAL_SOURCES_CONNECTED |
                (source_endpoint ? 1 : 0) << SOURCE_ENDPOINT |
                (remote_source ? 1 : 0) << REMOTE_SOURCE |
                (receiver_endpoint ? 1 : 0) << RECEIVER_ENDPOINT |
                (local_receiver ? 1 : 0) << LOCAL_RECEIVER |
                (remote_receiver ? 1 : 0) << REMOTE_RECEIVER |
                (phase_auto_config ? 1 : 0) << PHASE_AUTO_CONFIG |
                (phase_auto_advance ? 1 : 0) << PHASE_AUTO_ADVANCE |
                (data_auto_send ? 1 : 0) << DATA_AUTO_SEND |
                (next_phase_src_change ? 1 : 0) << NEXT_PHASE_SRC_CHANGE |
                (next_phase_dest_change ? 1 : 0) << NEXT_PHASE_DEST_CHANGE |
                (data_buf_no_flow_ctrl ? 1 : 0) << DATA_BUF_NO_FLOW_CTRL |
                (dest_data_buf_no_flow_ctrl ? 1 : 0) << DEST_DATA_BUF_NO_FLOW_CTRL |
                (remote_src_is_mcast ? 1 : 0) << REMOTE_SRC_IS_MCAST |
                (no_prev_phase_outgoing_data_flush ? 1 : 0) << NO_PREV_PHASE_OUTGOING_DATA_FLUSH |
                unicast_vc_reg << UNICAST_VC_REG |
                reg_update_vc_reg << REG_UPDATE_VC_REG),
        "STREAM_MISC_CFG_REG_INDEX");
    // clang-format on
}

void PhaseBlobFillerArchSpecific::setup_stream_remote_dest_when_src_change(BlobSection& phase_blob)
{
    phase_blob.append(
        configure_4_byte_register(STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX, 1),
        "STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX");
}

void PhaseBlobFillerArchSpecific::setup_stream_wr_ptr_reg(BlobSection& phase_blob)
{
    phase_blob.append(configure_4_byte_register(STREAM_WR_PTR_REG_INDEX, 0), "STREAM_WR_PTR_REG_INDEX");
}

void PhaseBlobFillerArchSpecific::setup_stream_remote_dest_traffic(BlobSection& phase_blob, const PhaseConfig& phase)
{
    PhaseConfigHelper phase_helper(phase);

    phase_blob.append(
        configure_4_byte_register(STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX, phase_helper.get_group_priority()),
        "STREAM_REMOTE_DEST_TRAFFIC_PRIORITY_REG_INDEX");
}

void PhaseBlobFillerArchSpecific::setup_stream_gather_reg(
    BlobSection& phase_blob, const PhaseConfig& phase, const size_t current_chip, const int overlay_version)
{
    PhaseConfigHelper phase_helper(phase);
    if (overlay_version > 1)
    {
        phase_blob.append(
            configure_4_byte_register(
                STREAM_GATHER_REG_INDEX,
                stream_gather(
                    current_chip,
                    phase_helper.get_arb_group_size(),
                    phase_helper.get_src_in_order_fwd(),
                    phase_helper.get_local_stream_clear_num(),
                    phase_helper.get_msg_group_stream_clear_type())),
            "STREAM_GATHER_REG_INDEX");
    }
    phase_blob.append(
        configure_4_byte_register(
            STREAM_MSG_SRC_IN_ORDER_FWD_NUM_MSGS_REG_INDEX, phase_helper.get_src_in_order_fwd_num_msgs()),
        "STREAM_MSG_SRC_IN_ORDER_FWD_NUM_MSGS_REG_INDEX");

    phase_blob.append(
        configure_4_byte_register(
            STREAM_GATHER_CLEAR_REG_INDEX,
            stream_gather_clear(
                phase_helper.get_local_stream_clear_num(), phase_helper.get_msg_group_stream_clear_type())),
        "STREAM_GATHER_CLEAR_REG_INDEX");
}

void PhaseBlobFillerArchSpecific::setup_stream_local_dest(
    BlobSection& phase_blob, const PhaseConfig& phase, const int overlay_version)
{
    PhaseConfigHelper phase_helper(phase);
    if (overlay_version > 1)
    {
        tt_cxy_pair dest_core = phase_helper.get_dest_stream_nodes()[0]->get_physical_location();
        StreamId dest_stream_id = phase_helper.get_dest_stream_nodes()[0]->get_stream_id();
        phase_blob.append(
            configure_4_byte_register(
                STREAM_LOCAL_DEST_REG_INDEX,
                (phase_helper.get_local_stream_clear_num() << STREAM_LOCAL_DEST_MSG_CLEAR_NUM) |
                    (dest_stream_id << STREAM_LOCAL_DEST_STREAM_ID)),
            "STREAM_LOCAL_DEST_REG_INDEX");
    }
}
}  // namespace blobgen2