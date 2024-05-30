// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "epoch.h"
#include "model/stream_graph/phase_config.h"
#include "model/stream_graph/stream_node.h"
#include "model/typedefs.h"
#include "noc_parameters.h"
#include "pipegen2_constants.h"

constexpr static unsigned int c_epoch_id_phase_shift_firmware = 15;
constexpr static unsigned int c_epoch_max = 31;

constexpr static unsigned int c_noc_virtual_channel_default_multicast = 0;
constexpr static unsigned int c_noc_virtual_channel_multicast_mask = 0x1;
constexpr static unsigned int c_noc_virtual_channel_default_unicast = 2;
constexpr static unsigned int c_noc_virtual_channel_default_reg_update = 3;

// The register which holds phase increments when changing phases
// is 12 bits wide.
constexpr static unsigned int WRAPPED_PHASE_LIMIT = 4096;

namespace blobgen2
{

class NOCHelper;

using pipegen2::NOC_ROUTE;
using pipegen2::PhaseConfig;
using pipegen2::PhaseId;
using pipegen2::StreamId;
using pipegen2::StreamNode;

// This helper is used with phase configuration related calculations:
//  - It holds logic around default values for fields in PhaseConfig.
//  - Calculations made solely using fields from PhaseConfig, but are potentially used at more places in code and have
//  their own meaning.
//  - Calculations made using previous or next phase in addition to current phase.
//  - Values calculated based on all phase configs for a single stream.
// TODO: Most of the code here has a potential to move to pipegen2
// TODO: Make this class static, as it doesn't hold any state. Same for NcriscConfigHelper.
class PhaseConfigHelper
{
public:
    PhaseConfigHelper(const PhaseConfig& phase) : phase(phase) {};

    bool get_next_phase_src_change() const { return phase.config.get_next_phase_src_change().value_or(true); }

    bool get_next_phase_dest_change() const { return phase.config.get_next_phase_dest_change().value_or(true); }

    unsigned int get_num_mblock_buffering() const { return phase.config.get_num_mblock_buffering().value_or(1); }

    bool has_dummy_phase_sender() const { return phase.config.get_follow_by_sender_dummy_phase().value_or(false); }

    bool has_dummy_phase_receiver() const { return phase.config.get_follow_by_receiver_dummy_phase().value_or(false); }

    bool has_dummy_phase() const { return has_dummy_phase_sender() || has_dummy_phase_receiver(); }

    bool get_is_scatter_pack() const { return phase.config.get_is_scatter_pack().value_or(false); }

    bool is_pipe_scatter() const { return get_is_scatter_pack() && is_scatter_order_size_larger_than_one(); }

    bool is_scatter_order_size_larger_than_one() const { return phase.config.get_scatter_order_size().value_or(1) > 1; }

    bool get_remote_receiver() const { return phase.config.get_remote_receiver().value_or(false); }

    bool get_remote_source() const { return phase.config.get_remote_source().value_or(false); }

    bool get_eth_sender() const { return phase.config.get_eth_sender().value_or(false); }

    bool get_eth_receiver() const { return phase.config.get_eth_receiver().value_or(false); }

    bool get_has_packer_mcast_opt() const { return phase.config.get_has_packer_mcast_opt().value_or(false); }

    // This two flags in combination are defining that this is an ethernet sender core, but the sending protocol
    // is implemented using firmware and not streams.
    bool is_eth_firmware_sender() const { return get_eth_sender() && get_receiver_endpoint(); }

    // This two flags in combination are defining that this is an ethernet receiver core, but the receiving protocol
    // is implemented using firmware and not streams.
    bool is_eth_firmware_receiver() const { return get_eth_receiver() && get_source_endpoint(); }

    // Is there any kind of remote receiver, including the case where the sending ethernet protocol is implemented
    // in firmware.
    bool has_remote_receiver() const { return get_remote_receiver() || is_eth_firmware_sender(); }

    // Is there any kind of remote source, including the case where the receiving ethernet protocol is implemented
    // in firmware.
    bool has_remote_source() const { return get_remote_source() || is_eth_firmware_receiver(); }

    bool has_source_stream() const { return phase.config.get_source().has_value(); }

    bool get_source_endpoint() const { return phase.config.get_source_endpoint().value_or(false); }

    bool get_receiver_endpoint() const { return phase.config.get_receiver_endpoint().value_or(false); }

    StreamNode* get_src_stream_node() const { return phase.config.get_source().value_or(nullptr); }

    bool get_intermediate() const { return phase.config.get_buffer_intermediate().value_or(false); }

    std::vector<StreamNode*> get_dest_stream_nodes() const
    {
        return phase.config.get_dest().value_or(std::vector<StreamNode*>());
    }

    bool has_dest_mcast_streams() const { return get_dest_stream_nodes().size() > 1; }

    unsigned int get_producer_epoch_id() const { return phase.config.get_producer_epoch_id().value_or(0); }

    unsigned int get_msg_size() const { return phase.config.get_msg_size().value_or(1024); }

    unsigned int get_datacopy_stream_id() const
    {
        return phase.config.get_space_shared_with_stream().has_value()
                   ? phase.config.get_space_shared_with_stream().value()->get_stream_id()
                   : 0;
    }

    unsigned int get_buf_full_size_bytes() const
    {
        return phase.config.get_buf_full_size_bytes().has_value() ? phase.config.get_buf_full_size_bytes().value()
                                                                  : phase.config.get_buffer_size().value();
    }

    unsigned int get_buf_base_addr() const
    {
        return phase.config.get_buf_base_addr().has_value() ? phase.config.get_buf_base_addr().value()
                                                            : phase.config.get_buf_addr().value();
    }

    unsigned int get_num_msgs_in_block() const { return phase.config.get_num_msgs_in_block().value_or(0); }

    unsigned int get_space_shared_with_operand() const
    {
        return phase.config.get_space_shared_with_operand().value_or(0);
    }

    unsigned int get_num_iters_in_epoch_field() const { return phase.config.get_num_iters_in_epoch().value_or(1); }

    unsigned int get_num_scatter_inner_loop() const { return phase.config.get_num_scatter_inner_loop().value_or(1); }

    bool get_legacy_pack() const { return phase.config.get_legacy_pack().value_or(false); }

    unsigned int get_stride() const { return phase.config.get_stride().value_or(0); }

    unsigned int get_total_strides() const { return phase.config.get_total_strides().value_or(1); }

    unsigned int get_stride_offset_size_bytes() const
    {
        return phase.config.get_stride_offset_size_bytes().value_or(0);
    }

    unsigned int get_pipe_scatter_output_loop_count() const
    {
        return phase.config.get_pipe_scatter_output_loop_count().value_or(1);
    }

    unsigned int get_c_dim_size() const { return phase.config.get_c_dim_size().value_or(0); }

    unsigned int get_r_dim_size() const { return phase.config.get_r_dim_size().value_or(0); }

    unsigned int get_zc_dim_size() const { return phase.config.get_zc_dim_size().value_or(0); }

    unsigned int get_zr_dim_size() const { return phase.config.get_zr_dim_size().value_or(0); }

    bool get_moves_raw_data() const { return phase.config.get_moves_raw_data().value_or(false); }

    unsigned int get_tile_dim_r() const { return phase.config.get_tile_dim_r().value_or(32); }

    unsigned int get_untilize_copy_iters() const { return get_tile_dim_r() / 2; }

    unsigned int get_log_2x_untilize_copy_iters() const { return std::round(std::log2(get_tile_dim_r())); }

    unsigned int get_padding_scatter_order_size() const
    {
        return phase.config.get_padding_scatter_order_size().value_or(0);
    }

    std::vector<StreamNode*> get_fork_streams() const
    {
        return phase.config.get_fork_streams().value_or(std::vector<StreamNode*>());
    }

    unsigned int get_num_fork_streams() const { return phase.config.get_num_fork_streams().value_or(0); }

    NOC_ROUTE get_incoming_data_noc() const { return phase.config.get_incoming_data_noc().value_or(NOC_ROUTE::NOC0); }

    NOC_ROUTE get_outgoing_data_noc() const { return phase.config.get_outgoing_data_noc().value_or(NOC_ROUTE::NOC0); }

    NOC_ROUTE get_remote_src_update_noc() const { return phase.config.get_remote_src_update_noc().value(); }

    NOC_ROUTE get_incoming_data_noc_for_stream_reg() const
    {
        return get_eth_receiver() ? NOC_ROUTE::NOC0 : get_incoming_data_noc();
    }

    NOC_ROUTE get_outgoing_data_noc_for_stream_reg() const
    {
        return get_eth_sender() ? NOC_ROUTE::NOC0 : get_outgoing_data_noc();
    }

    NOC_ROUTE get_remote_src_update_noc_for_stream_reg() const
    {
        return get_eth_receiver() ? NOC_ROUTE::NOC0 : get_remote_src_update_noc();
    }

    bool get_dram_output_no_push() const { return phase.config.get_dram_output_no_push().value_or(false); }

    unsigned int get_c_dim_loop_num_rows() const { return phase.config.get_c_dim_loop_num_rows().value_or(32); }

    unsigned int get_tilize_row_col_offset() const { return phase.config.get_tilize_row_col_offset().value_or(0); }

    unsigned int get_dram_embeddings_row_shift() const
    {
        return phase.config.get_dram_embeddings_row_shift().value_or(0);
    }

    bool get_hw_tilize() const { return phase.config.get_hw_tilize().value_or(false); }

    unsigned int get_num_msgs() const { return phase.config.get_num_msgs().value(); }

    bool has_scatter_list_stream_id() const
    {
        return phase.config.get_scatter_list_stream_id().value_or(nullptr) != nullptr;
    }

    unsigned int get_scatter_list_indicies_per_input() const
    {
        return phase.config.get_scatter_list_indicies_per_input().value_or(0);
    }

    unsigned int get_dram_embeddings_row_tiles() const
    {
        return phase.config.get_dram_embeddings_row_tiles().value_or(0);
    }

    bool get_dram_writes_with_cmd_buf() const { return phase.config.get_dram_writes_with_cmd_buf().value_or(false); }

    bool get_dram_output() const { return phase.config.get_dram_output().value_or(false); }

    bool get_dram_input() const { return phase.config.get_dram_input().value_or(false); }

    bool get_scatter_to_dram() const { return get_dram_output() && !get_legacy_pack(); }

    bool get_dram_input_no_push() const { return phase.config.get_dram_input_no_push().value_or(false); }

    unsigned int get_src_dest_index() const { return phase.config.get_src_dest_index().value_or(0); }

    unsigned int get_arb_group_size() const { return phase.config.get_arb_group_size().value_or(1); }

    unsigned int get_local_stream_clear_num() const { return phase.config.get_local_stream_clear_num().value_or(1); }

    unsigned int get_msg_group_stream_clear_type() const { return 0; }

    unsigned int get_src_in_order_fwd_num_msgs() const { return 0; }

    unsigned int get_src_in_order_fwd() const { return 0; }

    bool get_no_dest_handshake() const { return false; }

    unsigned int get_group_priority() const { return phase.config.get_group_priority().value_or(0); }

    bool get_linked() const { return phase.config.get_linked().value_or(false); }

    unsigned int get_vc_for_multicast() const
    {
        return phase.config.get_vc().value_or(c_noc_virtual_channel_default_multicast) &
               c_noc_virtual_channel_multicast_mask;
    }

    unsigned int get_vc_for_unicast() const
    {
        return phase.config.get_vc().value_or(c_noc_virtual_channel_default_unicast);
    }

    unsigned int get_vc_for_reg_update() const
    {
        return phase.config.get_reg_update_vc().value_or(c_noc_virtual_channel_default_reg_update);
    }

    bool get_no_path_res() const { return false; }

    unsigned int get_mcast_xy() const { return 0; }

    bool get_local_sources_connected() const { return phase.config.get_local_sources_connected().value_or(false); }

    bool get_local_receiver() const { return phase.config.get_local_receiver().value_or(false); }

    // Whether there is io transfer with dram.
    bool get_dram_io() const { return phase.config.get_dram_io().value_or(false); }

    // This designates that the stream is communicating with dram streams used for communication over PCIe with
    // another card.
    bool get_mmio_io() const { return phase.config.get_dram_streaming().value_or(false); }

    // Has receiver stream which is local, and it is a true local receiver, not just an optimization for tile
    // clearing.
    bool get_regular_local_receiver() const
    {
        return get_local_receiver() && !phase.config.get_local_receiver_tile_clearing().value_or(false);
    }

    bool get_dram_or_mmio_io() const { return get_dram_io() || get_mmio_io(); }

    // If is reading from dram, but not prolog.
    bool is_regular_dram_input() const { return get_dram_input() && get_source_endpoint(); }

    // If is reading from dram, but not epilogue.
    bool is_regular_dram_output() const { return get_dram_output() && get_receiver_endpoint(); }

    bool is_dram_input_prolog() const { return get_dram_input() && !get_dram_or_mmio_io(); }

    bool is_dram_or_mmio_output() const { return get_dram_output() && get_dram_or_mmio_io(); }

    // get_receiver_endpoint is set for unpacker on wormhole
    // get_local_receiver_tile_clearing is set for unpacker on grayskull
    bool is_receiver_unpacker() const
    {
        return (get_receiver_endpoint() || phase.config.get_local_receiver_tile_clearing().value_or(false)) &&
               !is_dram_or_mmio_output();
    }

    unsigned int get_msg_info_buf_addr_end() const
    {
        return phase.config.get_msg_info_buf_addr().value() / MEM_WORD_BYTES + get_num_msgs();
    }

    bool get_data_buf_no_flow_ctrl() const { return phase.config.get_data_buf_no_flow_ctrl().value_or(false); }

    bool get_dest_data_buf_no_flow_ctrl() const
    {
        return phase.config.get_dest_data_buf_no_flow_ctrl().value_or(false);
    }

    bool get_remote_src_is_mcast() const { return phase.config.get_remote_src_is_mcast().value_or(false); }

    bool get_no_prev_phase_outgoing_data_flush() const { return false; }

    bool get_phase_auto_advance() const { return phase.config.get_phase_auto_advance().value_or(false); }

    bool get_resend() const { return phase.config.get_resend().value_or(false); }

    unsigned int get_all_msgs_size() const { return get_num_msgs() * get_msg_size(); }

    bool get_all_msgs_not_fit_into_buffer() const
    {
        return phase.config.get_buffer_size().value() > get_all_msgs_size();
    }

    bool get_data_auto_send() const { return phase.config.get_data_auto_send().value_or(false); }

    bool get_is_brisc_pack() const
    {
        return get_source_endpoint() && !get_legacy_pack() && !get_dram_input() && !get_mmio_io() &&
               !get_intermediate() && !get_park_input();
    }

    bool get_park_input() const { return false; }

    bool get_park_output() const { return false; }

    bool get_ncrisc_clear() const { return phase.config.get_ncrisc_clear().value_or(false); }

    unsigned int get_batch_dim_size() const { return phase.config.get_batch_dim_size().value_or(0); }

    bool get_ptrs_not_zero() const { return phase.config.get_ptrs_not_zero().value_or(false); }

    unsigned int get_mblock_size() const { return get_buf_full_size_bytes() / get_num_mblock_buffering(); }

    unsigned int get_use_ethernet_fw_stream() const
    {
        return phase.config.get_use_ethernet_fw_stream().value_or(false);
    }

    bool get_src_change(const PhaseConfig* prev_phase) const;

    bool get_dest_change(const PhaseConfig* prev_phase) const;

    bool should_skip_kernels() const;

    StreamNode* get_dest_mcast_first_stream() const;

    // When we read stream graph collection from blob.yaml, it will only include the first and last dest nodes.
    // When we call blobgen directly with streamgraphcollection from pipegen, it will include all dest nodes.
    // In both cases, tha last dest stream node should be the same one.
    StreamNode* get_dest_mcast_last_stream() const;

    StreamNode* get_dest_single_stream() const;

    // Throws in case destination doesn't exist.
    StreamNode* get_dest_any_stream() const;

    std::optional<StreamNode*> get_dest_any_stream_optional() const;

    StreamId get_dest_stream_id() const;

    datacopy_stream_type_t get_datacopy_stream_type() const;

    StreamId get_eth_remote_fw_stream_id() const;

    unsigned int get_num_iters_in_epoch() const;

    // Epoch and phase id are combined in an uint64_t phase_id, where higher 32 bits are epoch and lower are
    // phase_id. Now pack it into a smaller register, to be consumed by firmware.
    unsigned int get_wrap_phase_id() const;

    bool get_phase_auto_config(const PhaseConfig* next_phase) const;

    bool scatter_idx_change(const PhaseConfig& other_phase) const;

    unsigned int extract_epoch(PhaseId phase_id) const;

    bool is_epoch_changed(const PhaseId& prev_phase_id) const;

    bool is_phase_wrapped(const PhaseId& prev_phase_id) const;

    bool get_is_fork_stream_id(const StreamId stream_id) const;

    unsigned int get_stream_flags(const StreamId stream_id) const;

    tt_xy_pair get_stream_source_coords(const tt_cxy_pair& core_location, const NOCHelper& noc_helper) const;

    tt_xy_pair get_remote_dest_coords(const tt_cxy_pair& core_location, const NOCHelper& noc_helper) const;

    tt_xy_pair get_dest_noc_coords(const NOCHelper& noc_helper) const;

    static unsigned int get_epoch_num_msgs(const std::map<PhaseId, PhaseConfig*>& phase_configs);

    static unsigned int get_num_iter_tiles(const std::map<PhaseId, PhaseConfig*>& phase_configs);

private:
    const PhaseConfig& phase;
};

}  // namespace blobgen2
