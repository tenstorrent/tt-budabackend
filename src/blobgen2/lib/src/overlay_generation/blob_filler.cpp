// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "overlay_generation/blob_filler.h"

#include "helpers/soc_helper.h"
#include "overlay_generation/epoch_allocator.h"
#include "overlay_generation/epoch_blob_filler.h"
#include "overlay_generation/phase_blob_filler.h"
#include "overlay_generation/phase_blob_filler_arch_specific.h"

namespace blobgen2
{

std::map<tt_cxy_pair, BlobData> BlobFiller::fill_blobs(
    const std::map<tt_cxy_pair, EpochAllocator>& epoch_allocators,
    EpochBlobData& epoch_blob_data,
    const SoCHelper& soc_helper)
{
    std::map<tt_cxy_pair, BlobData> blobs;

    for (auto& [core_id, core_blob_data] : epoch_blob_data.cores)
    {
        blobs.emplace(core_id, BlobData{});
        fill_valid_blob(core_id, blobs.at(core_id), soc_helper, epoch_allocators.at(core_id), core_blob_data);
    }

    std::set<tt_cxy_pair> valid_cores;
    std::transform(
        epoch_blob_data.cores.begin(),
        epoch_blob_data.cores.end(),
        std::inserter(valid_cores, valid_cores.end()),
        [](auto& pair) { return pair.first; });

    fill_empty_blobs(blobs, valid_cores, soc_helper);

    return blobs;
}

void BlobFiller::fill_valid_blob(
    const tt_cxy_pair& core_id,
    BlobData& blob,
    const SoCHelper& soc_helper,
    const EpochAllocator& epoch_allocator,
    CoreBlobData& core_blob_data)
{
    blob.blob_sections.emplace_back(soc_helper.get_overlay_start_address(core_id));
    BlobSection& epoch_section = blob.blob_sections.back();
    epoch_section.append_debug(soc_helper.is_ethernet_core(core_id) ? "[ETH CORE]" : "[WORKER CORE]");

    epoch_t* epoch = epoch_allocator.get_epoch_t(soc_helper.get_overlay_start_address(core_id));
    BlobPtr blob_start_addr = epoch_allocator.get_current_address();

    fill_epoch_section(epoch_allocator, epoch_section, core_id, core_blob_data, soc_helper);

    blob.blob_sections.emplace_back(blob_start_addr);
    BlobSection& blob_space_section = blob.blob_sections.back();

    fill_phase_blob_section(blob_space_section, core_blob_data);

    int curr_size = 0;
    for (auto& blob_section : blob.blob_sections)
    {
        curr_size += blob_section.size();
    }
    size_t overlay_blob_size = soc_helper.is_ethernet_core(core_id)
                                   ? eth_l1_mem::address_map::OVERLAY_BLOB_SIZE
                                   : (l1_mem::address_map::OVERLAY_BLOB_SIZE + epoch->overlay_blob_extra_size);
    log_assert(
        curr_size <= overlay_blob_size,
        "Blob size {} exceeds predefined maximum overlay blob size for chip {}",
        curr_size,
        overlay_blob_size);
}

void BlobFiller::fill_epoch_section(
    const EpochAllocator& epoch_allocator,
    BlobSection& epoch_section,
    const tt_cxy_pair& core_id,
    const CoreBlobData& core_blob_data,
    const SoCHelper& soc_helper)
{
    epoch_t* epoch = epoch_allocator.get_epoch_t(soc_helper.get_overlay_start_address(core_id));
    epoch_section.add_epoch_t(epoch);

    // Writing stream info ts.
    for (auto& stream_blob_ptr : epoch->active_streams)
    {
        if (stream_blob_ptr == NullBlobPtr)
        {
            break;
        }
        epoch_stream_info_t* stream = epoch_allocator.get_epoch_stream_info_t(stream_blob_ptr);
        epoch_section.add_epoch_stream_info_t(stream);

        const StreamBlobData& stream_blob_data = core_blob_data.streams.at(stream->stream_id);

        // Note that bellow check doesn't work since this field is in union with another one.
        // We have to check phase_offset object instead.
        // But it should work according to how firmware uses it?
        // if (stream->scatter_pipe_state != NullBlobPtr) {
        const int scatter_states_num = stream_blob_data.info.phase_offsets.size();
        if (scatter_states_num > 0)
        {
            scatter_pipe_state_t* scatter_pipe_states_arr =
                epoch_allocator.get_scatter_pipe_state_t(stream->scatter_pipe_state);
            epoch_section.add_scatter_pipe_states(scatter_pipe_states_arr, scatter_states_num);

            for (size_t i = 0; i < scatter_states_num; i++)
            {
                epoch_section.add_scatter_pipe_blobs(
                    epoch_allocator.get_scatter_pipe_blob_t(scatter_pipe_states_arr[i].unroll_blobs),
                    scatter_pipe_states_arr[i].max_unroll);
            }
            // This is the only time this is explicitly needed, since previous structures are arrays with objects
            // not individually aligned. But the following space prior to the next object needs to be padded, so
            // that the following objects are aligned.
            epoch_section.pad_to_noc_alignment();
        }

        if (stream->dram_io_info == NullBlobPtr)
        {
            continue;
        }

        epoch_stream_dram_io_info_t* epoch_stream_dram_io_info =
            epoch_allocator.get_epoch_stream_dram_io_info_t(stream->dram_io_info);
        epoch_section.add_epoch_stream_dram_io_info_t(epoch_stream_dram_io_info);

        BlobPtr next_dram_io_state_blob_ptr = epoch_stream_dram_io_info->dram_io_state;
        while (next_dram_io_state_blob_ptr != NullBlobPtr)
        {
            dram_io_state_t* next_dram_io_state = epoch_allocator.get_dram_io_state_t(next_dram_io_state_blob_ptr);
            epoch_section.add_dram_io_state_t(next_dram_io_state);

            if (next_dram_io_state->local.dram_io_scatter_state != NullBlobPtr)
            {
                dram_io_scatter_state_t* dram_io_scatter_state =
                    epoch_allocator.get_dram_io_scatter_state_t(next_dram_io_state->local.dram_io_scatter_state);
                epoch_section.add_dram_io_scatter_state_t(dram_io_scatter_state);

                epoch_section.add_dram_io_scatter_offsets(
                    epoch_allocator.get_tt_uint64_t(dram_io_scatter_state->scatter_offsets),
                    dram_io_scatter_state->scatter_offsets_size,
                    epoch_stream_dram_io_info->hw_tilize != NullBlobPtr);
            }

            next_dram_io_state_blob_ptr = next_dram_io_state->local.next;
        }
    }
}

void BlobFiller::fill_phase_blob_section(BlobSection& blob_space_section, CoreBlobData& core_blob_data)
{
    for (auto& [stream_id, stream_blob_data] : core_blob_data.streams)
    {
        int num_mblock_buffering = stream_blob_data.info.stream_binary_instruction_list.begin()->second.size();

        for (int mblock_buffering_idx = 0; mblock_buffering_idx < num_mblock_buffering; mblock_buffering_idx++)
        {
            for (auto& [phase_id, stream_binary_instruction_list] :
                 stream_blob_data.info.stream_binary_instruction_list)
            {
                blob_space_section.append(stream_blob_data.info.cfg_header_dw.at(phase_id), "cfg_header_dw");
                blob_space_section.merge(std::move(stream_binary_instruction_list[mblock_buffering_idx]));
            }

            auto& dummy_phase_stream_binary_instruction_list =
                stream_blob_data.info.dummy_phase_stream_binary_instruction_list[mblock_buffering_idx];

            for (int dummy_idx = 0; dummy_idx < dummy_phase_stream_binary_instruction_list.size(); dummy_idx++)
            {
                bool is_last_phase = dummy_idx == dummy_phase_stream_binary_instruction_list.size() - 1;
                size_t auto_cfg_size =
                    is_last_phase ? 0 : dummy_phase_stream_binary_instruction_list[dummy_idx + 1].dw_size();

                uint32_t header_dw = PhaseBlobFiller::blob_header_dw(auto_cfg_size, 1, 0);

                blob_space_section.append(header_dw, "header_dw");

                blob_space_section.merge(
                    std::move(dummy_phase_stream_binary_instruction_list[dummy_idx]),
                    [&is_last_phase](uint32_t item)
                    {
                        if (is_last_phase)
                        {
                            // If this is last phase, set_auto_cfg will be called on each dword, but will be no-op for
                            // most dwords in this blob section.
                            return PhaseBlobFillerArchSpecific::set_auto_cfg(item, 0);
                        }
                        return item;
                    });
            }
        }
    }
}

void BlobFiller::fill_empty_blobs(
    std::map<tt_cxy_pair, BlobData>& blobs, const std::set<tt_cxy_pair>& valid_cores, const SoCHelper& soc_helper)
{
    for (const auto& chip_id : soc_helper.get_chip_ids())
    {
        for (const tt_cxy_pair& core_id : soc_helper.get_worker_cores(chip_id))
        {
            if (valid_cores.find(core_id) != valid_cores.end())
            {
                continue;
            }
            fill_empty_blob(blobs, core_id, soc_helper);
        }
        for (const tt_cxy_pair& core_id : soc_helper.get_ethernet_cores(chip_id))
        {
            if (valid_cores.find(core_id) != valid_cores.end())
            {
                continue;
            }
            fill_empty_blob(blobs, core_id, soc_helper);
        }
    }
}

void BlobFiller::fill_empty_blob(
    std::map<tt_cxy_pair, BlobData>& blobs, const tt_cxy_pair& core_id, const SoCHelper& soc_helper)
{
    EpochAllocator epoch_allocator(soc_helper.get_overlay_start_address(core_id));
    auto [blob_epoch_ptr, epoch] = epoch_allocator.new_epoch_t();

    blobs.emplace(core_id, BlobData{});
    blobs.at(core_id).blob_sections.emplace_back(blob_epoch_ptr);
    BlobSection& epoch_section = blobs.at(core_id).blob_sections.back();
    epoch_section.append_debug(soc_helper.is_ethernet_core(core_id) ? "[ETH CORE]" : "[WORKER CORE]");

    EpochBlobFiller::fill_epoch_t_empty(epoch);

    epoch_section.add_epoch_t(epoch);
}
}  // namespace blobgen2