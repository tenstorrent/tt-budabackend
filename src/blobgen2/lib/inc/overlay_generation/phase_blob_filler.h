// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "device/tt_arch_types.h"
#include "device/tt_xy_pair.h"
#include "model/typedefs.h"

namespace pipegen2
{
class NcriscConfig;
class PhaseConfig;
class StreamNode;
}  // namespace pipegen2

namespace blobgen2
{

class BlobSection;
class NOCHelper;
class PhaseConfigHelper;

using pipegen2::NcriscConfig;
using pipegen2::NOC_ROUTE;
using pipegen2::PhaseConfig;
using pipegen2::PhaseId;
using pipegen2::StreamId;
using pipegen2::StreamNode;

// Helper functions used for generating phase blob sections which hold configuration for stream registers needed per
// each phase.
// There is a specific overridden class for each architecture. This is due to different stream registers
// present on each archs, and different ways of setting them up. This base class holds common code.
class PhaseBlobFiller
{
public:
    static BlobSection get_phase_blob(
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
        const NOCHelper& noc_helper);

    static BlobSection get_dummy_phase_blob(
        const tt_cxy_pair& core_location,
        const StreamId stream_id,
        const PhaseConfig& phase,
        const uint32_t dummy_phase_addr,
        const uint32_t dest_dummy_phase_addr,
        const bool is_sender,
        const bool is_receiver,
        const unsigned int overlay_version,
        const NOCHelper& noc_helper);

    static uint32_t blob_header_dw(
        const uint32_t next_phase_num_cfg_reg_writes,
        const uint32_t curr_phase_num_msgs,
        const uint32_t phase_num_incr);
    static uint32_t intermediates_blob_loop_dw(const uint32_t phase_start_addr);

private:
    static uint32_t configure_4_byte_register(const uint8_t reg_index, const uint32_t reg_val);
    static uint32_t stream_gather_clear(
        const uint32_t local_stream_clear_num, const uint32_t msg_group_stream_clear_type);
    static uint32_t modify_blob_dw(
        const uint32_t blob_dw, const uint32_t reg_index, const uint32_t mask, const uint32_t val);

    static void generate_blob_for_stream_with_local_source(
        BlobSection& phase_blob, const StreamNode* src_stream_node, const size_t current_chip, const bool first_phase);

    static void setup_phase_blob_for_src_change(
        BlobSection& phase_blob,
        const NOCHelper& noc_helper,
        const PhaseConfig& phase,
        const bool first_phase,
        const tt_cxy_pair& core_location,
        const unsigned int src_old_base,
        const unsigned int src_new_base,
        const unsigned int overlay_version);
    static void setup_phase_blob_for_dest_change(
        BlobSection& phase_blob,
        const NOCHelper& noc_helper,
        const PhaseConfig& phase,
        const std::optional<PhaseConfig>& dest_phase,
        const bool first_phase,
        const tt_cxy_pair& core_location,
        const int overlay_version,
        const bool phase_resets_pointers,
        const int dram_num_msgs);

    // This piece of code handles streams that have data in L1 but have to process it in multiple phases. Examples are
    // scatter packer, prefetch post-tm.
    static void setup_phase_blob_for_resend(BlobSection& phase_blob, const PhaseConfig& phase);

    static void setup_misc_cfg_reg_regular_phase(
        BlobSection& phase_blob,
        const PhaseConfig& phase,
        const PhaseConfig* next_phase,
        const bool next_phase_is_dummy_phase);

    static void setup_misc_cfg_reg_dummy_phase(
        BlobSection& phase_blob, const PhaseConfig& phase, const bool is_sender, const bool is_receiver);

    static int get_dram_num_msgs(
        const PhaseConfig& phase,
        const std::vector<NcriscConfig>& dram_blobs,
        const bool dram_read_phase_resets_pointers);

    static uint32_t stream_remote_src(
        const size_t x, const size_t y, const StreamId stream_id, const unsigned int dest_index);
    static uint32_t stream_remote_dest(const uint32_t dest_x, const uint32_t dest_y, const StreamId dest_stream_id);
    static uint32_t stream_mcast_dest_v1(
        const uint8_t mcast_en, const tt_xy_pair& dest_end, const PhaseConfigHelper& phase_helper);
    static uint32_t stream_mcast_dest_v2(
        const uint8_t mcast_en, const tt_xy_pair& dest_end, const PhaseConfigHelper& phase_helper);
    static uint32_t dummy_phase(const PhaseId phase_num);

    friend class PhaseBlobFillerArchSpecific;
};

}  // namespace blobgen2
