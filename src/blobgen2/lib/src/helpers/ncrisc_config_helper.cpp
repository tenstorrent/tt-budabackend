// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "helpers/ncrisc_config_helper.h"

#include "helpers/noc_helper.h"
#include "helpers/phase_config_helper.h"

namespace blobgen2
{

// TODO: Figure out how to merge this and the next function with the same name.
unsigned int NcriscConfigHelper::get_dram_q_slot_size_tiles() const
{
    if (ncrisc.dram_buf_size_q_slots > 0)
    {
        return ncrisc.dram_buf_size_tiles / ncrisc.dram_buf_size_q_slots;
    }
    else
    {
        // TODO: figure out if there is an example when this happens.
        return 1;
    }
}

unsigned int NcriscConfigHelper::get_dram_q_slot_size_tiles(const PhaseConfigHelper& phase) const
{
    if (phase.get_moves_raw_data())
    {
        return phase.get_batch_dim_size();
    }
    else if (has_dram_scatter_offsets())
    {
        // TODO: Figure out if this is ever different from
        // ncrisc.dram_buf_size_tiles / ncrisc.dram_buf_size_q_slots
        return get_num_scatter_offsets(phase) * get_dram_q_slot_size_tiles_single_scatter(phase);
    }
    else
    {
        // TODO: figure out if there is an example when this happens.
        // This should not happen since dram buffer always has scatter offsets now.
        return get_dram_q_slot_size_tiles();
    }
}

unsigned int NcriscConfigHelper::get_epoch_q_slots_remaining(
    const PhaseConfigHelper& phase, const std::map<PhaseId, PhaseConfig*>& phase_configs) const
{
    if (!phase.get_moves_raw_data() && has_dram_scatter_offsets())
    {
        unsigned int epoch_num_msgs = PhaseConfigHelper::get_epoch_num_msgs(phase_configs);

        unsigned dram_q_slot_size_tiles =
            get_num_scatter_offsets(phase) * get_dram_q_slot_size_tiles_single_scatter(phase);
        log_assert(epoch_num_msgs % dram_q_slot_size_tiles == 0, "Error! Could not compute epoch_q_slots_remaining.");
        return epoch_num_msgs / dram_q_slot_size_tiles;
    }
    else
    {
        return get_epoch_q_slots_remaining_non_dram_scatter(phase, phase_configs);
    }
}

unsigned int NcriscConfigHelper::get_num_scatter_offsets(const PhaseConfigHelper& phase) const
{
    unsigned num_scatter_offsets = 0;
    if (phase.has_scatter_list_stream_id())
    {
        num_scatter_offsets = phase.get_scatter_list_indicies_per_input();
    }
    else
    {
        num_scatter_offsets = get_dram_scatter_offsets_full_size();
        if (phase.get_hw_tilize())
        {
            num_scatter_offsets *= phase.get_c_dim_size();
        }
    }
    return num_scatter_offsets;
}

unsigned int NcriscConfigHelper::get_dram_q_slot_size_bytes() const
{
    return get_dram_q_slot_size_tiles() * ncrisc.msg_size;
}

unsigned int NcriscConfigHelper::get_epoch_q_slots_remaining_non_dram_scatter(
    const PhaseConfigHelper& phase, const std::map<PhaseId, PhaseConfig*>& phase_configs) const
{
    return PhaseConfigHelper::get_epoch_num_msgs(phase_configs) /
           (get_dram_q_slot_size_tiles() / phase.get_total_strides());
}

unsigned int NcriscConfigHelper::get_dram_q_slot_size_tiles_single_scatter(const PhaseConfigHelper& phase) const
{
    return ncrisc.dram_scatter_chunk_size_tiles.value() *
           (phase.get_hw_tilize() ? 1 : phase.get_num_scatter_inner_loop());
}

unsigned int NcriscConfigHelper::get_scatter_chunk_size_bytes() const
{
    return get_dram_scatter_chunk_size_tiles() * ncrisc.msg_size;
}

unsigned int NcriscConfigHelper::get_stream_flags() const
{
    unsigned int result = 0;
    result |= get_dram_io() ? 0x2 : 0;
    result |= get_dram_input() ? 0x4 : 0;
    result |= get_dram_output() ? 0x8 : 0;
    result |= get_dram_streaming() ? 0x20 : 0;
    result |= get_dram_ram() ? 0x200 : 0;
    return result;
}

uint64_t NcriscConfigHelper::get_modified_dram_addr(
    const PhaseConfigHelper& phase, const NOCHelper& noc_helper, const uint64_t addr) const
{
    NOC_ROUTE noc_id = get_dram_output() ? phase.get_outgoing_data_noc() : phase.get_incoming_data_noc();
    return noc_helper.modify_dram_noc_addr(addr, noc_id);
}

uint64_t NcriscConfigHelper::get_dram_buf_noc_addr(const PhaseConfigHelper& phase, const NOCHelper& noc_helper) const
{
    return get_modified_dram_addr(phase, noc_helper, ncrisc.dram_buf_noc_addr);
}

std::vector<uint64_t> NcriscConfigHelper::get_dram_scatter_offsets(
    const PhaseConfigHelper& phase, const NOCHelper& noc_helper) const
{
    std::vector<uint64_t> dram_scatter_offsets;
    for (uint64_t offset : ncrisc.dram_scatter_offsets.value())
    {
        if (!phase.get_hw_tilize() && (offset & pipegen2::constants::dram_io_is_scatter_loop_flag) == 0)
        {
            dram_scatter_offsets.push_back(get_modified_dram_addr(phase, noc_helper, offset));
        }
        else
        {
            dram_scatter_offsets.push_back(offset);
        }

        if (phase.has_scatter_list_stream_id())
        {
            // Return only the first modified offset in this case
            break;
        }
    }
    return dram_scatter_offsets;
}

unsigned int NcriscConfigHelper::get_data_chunk_size_tiles() const
{
    if (ncrisc.dram_output.value_or(false))
    {
        return ncrisc.dram_buf_write_chunk_size_tiles.value();
    }
    else
    {
        return ncrisc.dram_buf_read_chunk_size_tiles.value();
    }
}

unsigned int NcriscConfigHelper::get_data_chunk_size_bytes(const PhaseConfigHelper& first_phase_helper) const
{
    if (first_phase_helper.get_moves_raw_data())
    {
        return ncrisc.dram_buf_write_row_chunk_size_bytes.value();
    }
    else
    {
        return get_data_chunk_size_tiles() * first_phase_helper.get_msg_size();
    }
}

unsigned int NcriscConfigHelper::get_dram_streaming_epoch_id(const PhaseId& first_phase_id) const
{
    return (ncrisc.dram_input.value_or(false) && ncrisc.dram_streaming.value_or(false))
               ? first_phase_id >> pipegen2::constants::epoch_id_phase_shift
               : 0;
}

uint64_t NcriscConfigHelper::get_dram_streaming_header_addr(
    const PhaseConfigHelper& first_phase_helper, const NOCHelper& noc_helper, uint32_t desired_header_addr) const
{
    uint64_t core_coords = (get_dram_buf_noc_addr(first_phase_helper, noc_helper) >> c_l1_size_log2) << c_l1_size_log2;

    return core_coords | desired_header_addr;
}

unsigned int NcriscConfigHelper::get_dram_num_msgs(const std::vector<NcriscConfig>& ncriscs)
{
    unsigned int num_msgs = 0;
    for (auto& ncrisc : ncriscs)
    {
        num_msgs += ncrisc.num_msgs;
    }
    return num_msgs;
}

bool NcriscConfigHelper::has_multi_readers(const std::vector<NcriscConfig>& ncriscs)
{
    for (auto& ncrisc : ncriscs)
    {
        if (ncrisc.total_readers.value_or(1) > 1)
        {
            return true;
        }
    }
    return false;
}

}  // namespace blobgen2