// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "overlay_generation/phase_blob_filler.h"

namespace blobgen2
{

// Arch specific functions for filling phase blob.
// Each of these should be defined for each arch in corresponding cpp file
// in src/overlay_blob/<arch>/phase_blob_filler_arch_specific.cpp
class PhaseBlobFillerArchSpecific : public PhaseBlobFiller
{
public:
    static uint32_t set_auto_cfg(const uint32_t blob_dw, const uint32_t val);

    static uint32_t stream_gather(
        const size_t chip_id,
        const uint32_t arb_group_size,
        const bool src_in_order_fwd,
        const uint32_t local_stream_clear_num,
        const uint32_t msg_group_stream_clear_type);

    static void setup_misc_cfg_reg(
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
        const unsigned int reg_update_vc_reg);

    static void setup_stream_remote_dest_when_src_change(BlobSection& phase_blob);

    static void setup_stream_wr_ptr_reg(BlobSection& phase_blob);

    static void setup_stream_remote_dest_traffic(BlobSection& phase_blob, const PhaseConfig& phase);

    static void setup_stream_gather_reg(
        BlobSection& phase_blob, const PhaseConfig& phase, const size_t current_chip, const int overlay_version);

    static void setup_stream_local_dest(BlobSection& phase_blob, const PhaseConfig& phase, const int overlay_version);

    friend class PhaseBlobFiller;
};

}  // namespace blobgen2