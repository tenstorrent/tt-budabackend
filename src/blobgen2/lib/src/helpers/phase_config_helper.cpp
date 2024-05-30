// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "helpers/phase_config_helper.h"

#include "helpers/noc_helper.h"

namespace blobgen2
{

bool PhaseConfigHelper::get_src_change(const PhaseConfig* prev_phase) const
{
    return prev_phase == nullptr || PhaseConfigHelper(*prev_phase).get_next_phase_src_change();
}

bool PhaseConfigHelper::get_dest_change(const PhaseConfig* prev_phase) const
{
    return prev_phase == nullptr || PhaseConfigHelper(*prev_phase).get_next_phase_dest_change() ||
           get_no_dest_handshake();
}

bool PhaseConfigHelper::should_skip_kernels() const
{
    return phase.config.get_space_shared_with_stream().has_value() &&
           (get_source_endpoint() || get_receiver_endpoint());
}

StreamNode* PhaseConfigHelper::get_dest_mcast_first_stream() const
{
    log_assert(
        has_dest_mcast_streams(),
        "Expected to contain multicast destinations when calling get_dest_mcast_first_stream.");
    return *get_dest_stream_nodes().begin();
}

// When we read stream graph collection from blob.yaml, it will only include the first and last dest nodes.
// When we call blobgen directly with streamgraphcollection from pipegen, it will include all dest nodes.
// In both cases, tha last dest stream node should be the same one.
StreamNode* PhaseConfigHelper::get_dest_mcast_last_stream() const
{
    log_assert(
        has_dest_mcast_streams(),
        "Expected to contain multicast destinations when calling get_dest_mcast_last_stream.");
    return *get_dest_stream_nodes().rbegin();
}

StreamNode* PhaseConfigHelper::get_dest_single_stream() const
{
    log_assert(
        get_dest_stream_nodes().size() == 1,
        "Expected to have a single destination when calling get_dest_single_stream.");
    return *get_dest_stream_nodes().begin();
}

// Throws in case destination doesn't exist.
StreamNode* PhaseConfigHelper::get_dest_any_stream() const
{
    return has_dest_mcast_streams() ? get_dest_mcast_first_stream() : get_dest_single_stream();
}

std::optional<StreamNode*> PhaseConfigHelper::get_dest_any_stream_optional() const
{
    std::vector<StreamNode*> dest_stream_nodes = get_dest_stream_nodes();
    return dest_stream_nodes.size() > 0 ? std::optional(*dest_stream_nodes.begin()) : std::nullopt;
}

StreamId PhaseConfigHelper::get_dest_stream_id() const
{
    if (has_dest_mcast_streams())
    {
        StreamId stream1 = get_dest_mcast_first_stream()->get_stream_id();
        StreamId stream2 = get_dest_mcast_last_stream()->get_stream_id();
        log_assert(stream1 == stream2, "Expected all destination stream ids to be equal for multicast destination.");
        return stream1;
    }
    else
    {
        return get_dest_single_stream()->get_stream_id();
    }
}

datacopy_stream_type_t PhaseConfigHelper::get_datacopy_stream_type() const
{
    datacopy_stream_type_t datacopy_stream_type =
        phase.config.get_space_shared_with_stream().has_value()
            ? (get_remote_receiver() ? datacopy_stream_type_t::UNPACKER : datacopy_stream_type_t::PACKER)
            : datacopy_stream_type_t::NONE;
    if (get_use_ethernet_fw_stream() &&
        (is_eth_firmware_sender() || (has_remote_receiver() && is_eth_firmware_receiver())))
    {
        datacopy_stream_type |= datacopy_stream_type_t::ETH_REMOTE_FW;
    }
    return datacopy_stream_type;
}

StreamId PhaseConfigHelper::get_eth_remote_fw_stream_id() const
{
    if (is_eth_firmware_sender())
    {
        return get_dest_any_stream()->get_stream_id();
    }
    if (has_remote_receiver() && is_eth_firmware_receiver())
    {
        return get_src_stream_node()->get_stream_id();
    }
    return 0;
}

unsigned int PhaseConfigHelper::get_num_iters_in_epoch() const
{
    return get_num_iters_in_epoch_field() *
           (get_is_scatter_pack()
                ? phase.config.get_num_unroll_iter().value() * phase.config.get_padding_scatter_order_size().value()
                : 1);
}

// Epoch and phase id are combined in an uint64_t phase_id, where higher 32 bits are epoch and lower are
// phase_id. Now pack it into a smaller register, to be consumed by firmware.
unsigned int PhaseConfigHelper::get_wrap_phase_id() const
{
    unsigned int epoch_num = phase.phase_id >> pipegen2::constants::epoch_id_phase_shift;
    unsigned int phase_mask = (1 << c_epoch_id_phase_shift_firmware) - 1;
    unsigned int wrapped_phase_id = phase.phase_id & phase_mask;
    return (((epoch_num % c_epoch_max) << c_epoch_id_phase_shift_firmware) | wrapped_phase_id);
}

bool PhaseConfigHelper::get_phase_auto_config(const PhaseConfig* next_phase) const
{
    if (get_intermediate())
    {
        return true;
    }
    if (next_phase == nullptr)
    {
        return false;
    }
    return phase.config.get_phase_auto_config().value_or(false);
}

bool PhaseConfigHelper::scatter_idx_change(const PhaseConfig& other_phase) const
{
    return other_phase.config.get_scatter_idx().value() != phase.config.get_scatter_idx().value();
}

unsigned int PhaseConfigHelper::extract_epoch(PhaseId phase_id) const
{
    return phase_id >> c_epoch_id_phase_shift_firmware << c_epoch_id_phase_shift_firmware;
}

bool PhaseConfigHelper::is_epoch_changed(const PhaseId& prev_phase_id) const
{
    return extract_epoch(phase.phase_id) != extract_epoch(prev_phase_id);
}

bool PhaseConfigHelper::is_phase_wrapped(const PhaseId& prev_phase_id) const
{
    unsigned int phase_num_inc = phase.phase_id - prev_phase_id;
    return phase_num_inc >= WRAPPED_PHASE_LIMIT || is_epoch_changed(prev_phase_id);
}

bool PhaseConfigHelper::get_is_fork_stream_id(StreamId stream_id) const
{
    bool is_fork_stream_id = false;
    for (auto& fork_stream : get_fork_streams())
    {
        is_fork_stream_id |= fork_stream->get_stream_id() == stream_id;
    }

    return is_fork_stream_id;
}

unsigned int PhaseConfigHelper::get_stream_flags(StreamId stream_id) const
{
    unsigned int result = 0;
    result |= get_park_input() ? 0x1 : 0;
    result |= get_park_output() ? 0x80 : 0;
    result |= get_intermediate() ? 0x10 : 0;
    result |= get_moves_raw_data() ? 0x40 : 0;
    result |= get_is_fork_stream_id(stream_id) ? 0x100 : 0;
    result |= get_is_brisc_pack() ? 0x400 : 0;
    result |= get_dram_output_no_push() || get_dram_input_no_push() ? 0x800 : 0;
    result |= get_ncrisc_clear() ? 0x1000 : 0;
    return result;
}

tt_xy_pair PhaseConfigHelper::get_stream_source_coords(
    const tt_cxy_pair& core_location, const NOCHelper& noc_helper) const
{
    tt_cxy_pair src_core = get_src_stream_node()->get_physical_location();
    log_assert(get_eth_receiver() || src_core.chip == core_location.chip, "Source core is not on the same chip.");

    if (get_eth_receiver())
    {
        return noc_helper.get_ethernet_noc_coords();
    }
    if (get_remote_src_update_noc() == NOC_ROUTE::NOC1)
    {
        return noc_helper.get_noc1_coords(src_core);
    }
    return src_core;
}

tt_xy_pair PhaseConfigHelper::get_remote_dest_coords(
    const tt_cxy_pair& core_location, const NOCHelper& noc_helper) const
{
    tt_cxy_pair dest_core;
    StreamId dest_stream_id;

    tt_cxy_pair dest_core2;

    if (has_dest_mcast_streams())
    {
        dest_core = get_dest_mcast_first_stream()->get_physical_location();
        dest_stream_id = get_dest_mcast_first_stream()->get_stream_id();
        dest_core2 = get_dest_mcast_last_stream()->get_physical_location();
        log_assert(get_eth_sender() || dest_core2.chip == core_location.chip, "Dest core is not on the same chip.");
    }
    else
    {
        dest_core = get_dest_single_stream()->get_physical_location();
        dest_stream_id = get_dest_single_stream()->get_stream_id();
    }
    log_assert(get_eth_sender() || dest_core.chip == core_location.chip, "Dest core is not on the same chip.");

    if (get_eth_sender())
    {
        return noc_helper.get_ethernet_noc_coords();
    }
    if (get_outgoing_data_noc() == NOC_ROUTE::NOC1)
    {
        if (has_dest_mcast_streams())
        {
            return noc_helper.get_noc1_coords(dest_core2);
        }
        return noc_helper.get_noc1_coords(dest_core);
    }
    return dest_core;
}

tt_xy_pair PhaseConfigHelper::get_dest_noc_coords(const NOCHelper& noc_helper) const
{
    if (get_eth_sender())
    {
        return noc_helper.get_ethernet_noc_coords();
    }
    else if (get_outgoing_data_noc() == NOC_ROUTE::NOC1)
    {
        return noc_helper.get_noc1_coords(get_dest_mcast_first_stream()->get_physical_location());
    }
    else
    {
        return get_dest_mcast_last_stream()->get_physical_location();
    }
}

unsigned int PhaseConfigHelper::get_epoch_num_msgs(const std::map<PhaseId, PhaseConfig*>& phase_configs)
{
    unsigned int num_msgs = 0;
    for (auto& [phase_id, phase] : phase_configs)
    {
        num_msgs += PhaseConfigHelper(*phase).get_num_msgs() * PhaseConfigHelper(*phase).get_num_iters_in_epoch_field();
    }
    return num_msgs;
}

unsigned int PhaseConfigHelper::get_num_iter_tiles(const std::map<PhaseId, PhaseConfig*>& phase_configs)
{
    PhaseConfigHelper last_phase_helper(*phase_configs.rbegin()->second);
    return get_epoch_num_msgs(phase_configs) / last_phase_helper.get_num_iters_in_epoch_field();
}

}  // namespace blobgen2