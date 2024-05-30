// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "overlay_generation/epoch_t_builder.h"

#include "helpers/ncrisc_config_helper.h"
#include "helpers/noc_helper.h"
#include "helpers/phase_config_helper.h"
#include "helpers/soc_helper.h"
#include "model/stream_graph/ncrisc_config.h"
#include "model/stream_graph/phase_config.h"
#include "model/stream_graph/stream_graph_collection.h"
#include "overlay_blob/stream_info.h"
#include "overlay_generation/epoch_allocator.h"
#include "overlay_generation/epoch_blob_filler.h"
#include "overlay_generation/phase_blob_filler.h"

namespace blobgen2
{

std::map<tt_cxy_pair, EpochAllocator> EpochTBuilder::fill_epoch_structs(
    EpochBlobData& epoch_blob_data,
    const std::map<tt_cxy_pair, dram_perf_info_t>& dram_perf_info_map,
    const std::map<tt_cxy_pair, L1BufferAllocationInfo>& ncrisc_fallback_buffers,
    const std::map<size_t, std::map<uint32_t, uint32_t>>& tile_size_and_address_per_chip,
    const int epoch_num,
    const SoCHelper& soc_helper)
{
    auto epoch_allocators = fill_epoch_t(
        epoch_blob_data,
        dram_perf_info_map,
        ncrisc_fallback_buffers,
        tile_size_and_address_per_chip,
        epoch_num,
        soc_helper);

    // This has to be done once we fill up all streams in an epoch, since this field of one stream can reference
    // other streams on the same core.
    fill_epoch_stream_info_t_fork_idxs_all_cores(epoch_allocators, epoch_blob_data, soc_helper);

    // This has to be done once we fill up the epoch structures, since only then we know the
    // absolute address of blobs.
    update_blob_base_addrs(epoch_allocators, epoch_blob_data, soc_helper);

    // This has to be done once we fill up the epoch structures on all cores, since it is searching
    // for absolute address of an epoch_stream_info_t struct on some other destination core.
    fill_dram_streaming_header_addr(epoch_allocators, epoch_blob_data, soc_helper);

    fill_intermediates_stream_binary_instruction_list(epoch_allocators, epoch_blob_data);

    return epoch_allocators;
}

std::map<tt_cxy_pair, EpochAllocator> EpochTBuilder::fill_epoch_t(
    EpochBlobData& epoch_blob_data,
    const std::map<tt_cxy_pair, dram_perf_info_t>& dram_perf_info_map,
    const std::map<tt_cxy_pair, L1BufferAllocationInfo>& ncrisc_fallback_buffers,
    const std::map<size_t, std::map<uint32_t, uint32_t>>& tile_size_and_address_per_chip,
    const int epoch_num,
    const SoCHelper& soc_helper)
{
    std::map<tt_cxy_pair, EpochAllocator> epoch_allocators;

    for (const auto& [core_id, core_blob_data] : epoch_blob_data.cores)
    {
        NOCHelper noc_helper = soc_helper.get_noc_helper(core_id.chip);

        epoch_allocators.emplace(core_id, EpochAllocator(soc_helper.get_overlay_start_address(core_id)));
        EpochAllocator& epoch_allocator = epoch_allocators.at(core_id);

        std::optional<dram_perf_info_t> perf_info = dram_perf_info_map.find(core_id) != dram_perf_info_map.end()
                                                        ? std::optional(dram_perf_info_map.at(core_id))
                                                        : std::nullopt;

        std::optional<L1BufferAllocationInfo> ncrisc_fallback_buffer =
            ncrisc_fallback_buffers.find(core_id) != ncrisc_fallback_buffers.end()
                ? std::optional(ncrisc_fallback_buffers.at(core_id))
                : std::nullopt;

        // Fills epoch_t struct and all other containing structures.
        fill_epoch_t_single_core(
            epoch_allocator,
            epoch_num,
            core_blob_data,
            tile_size_and_address_per_chip.at(core_id.chip),
            perf_info,
            ncrisc_fallback_buffer,
            soc_helper.is_ethernet_core(core_id),
            noc_helper);
    }

    return epoch_allocators;
}

BlobPtr EpochTBuilder::fill_epoch_t_single_core(
    EpochAllocator& epoch_allocator,
    const int epoch_num,
    const CoreBlobData& core_blob_data,
    const std::map<uint32_t, uint32_t>& tile_size_and_address,
    const std::optional<dram_perf_info_t> perf_info,
    const std::optional<L1BufferAllocationInfo>& ncrisc_fallback_buffer,
    const bool is_eth_core,
    const NOCHelper& noc_helper)
{
    auto [epoch_blob_ptr, epoch] = epoch_allocator.new_epoch_t();

    EpochBlobFiller::fill_epoch_t(
        epoch, epoch_num, core_blob_data, tile_size_and_address, perf_info, ncrisc_fallback_buffer, is_eth_core);

    for (const auto& [stream_id, stream_blob_data] : core_blob_data.streams)
    {
        BlobPtr epoch_stream_info_blob_ptr = fill_epoch_stream_info_t(epoch_allocator, stream_blob_data, noc_helper);

        PhaseConfig& first_phase = *stream_blob_data.phases.begin()->second;
        epoch->active_streams[epoch->num_active_streams++] = epoch_stream_info_blob_ptr;
        if (first_phase.config.get_input_index().has_value())
        {
            unsigned int index = first_phase.config.get_input_index().value();
            epoch->num_inputs = std::max(epoch->num_inputs, index + 1);
            epoch->inputs[index] = epoch_stream_info_blob_ptr;
        }
        if (first_phase.config.get_output_index().has_value())
        {
            unsigned int index = first_phase.config.get_output_index().value();
            epoch->num_outputs = std::max(epoch->num_outputs, index + 1);
            epoch->outputs[index] = epoch_stream_info_blob_ptr;
        }
    }

    if (epoch->num_inputs == 0 && epoch->num_outputs == 0)
    {
        // This field is calculated based on the number of input and output streams. Streams have to be filled first,
        // hence the reason this is not in fill_epoch_t function called above.
        epoch->skip_kernels = 1;
    }

    return epoch_blob_ptr;
}

BlobPtr EpochTBuilder::fill_epoch_stream_info_t(
    EpochAllocator& epoch_allocator, const StreamBlobData& stream_blob_data, const NOCHelper& noc_helper)
{
    auto [epoch_stream_info_blob_ptr, stream_info] = epoch_allocator.new_epoch_stream_info_t();

    EpochBlobFiller::fill_epoch_stream_info_t(stream_info, stream_blob_data);

    if (stream_blob_data.info.phase_offsets.size() > 0)
    {
        stream_info->scatter_pipe_state = fill_scatter_pipe_structs(epoch_allocator, stream_blob_data.info);
    }

    if (stream_blob_data.ncriscs.size() != NullBlobPtr)
    {
        stream_info->dram_io_info = fill_epoch_stream_dram_io_info_t(epoch_allocator, stream_blob_data, noc_helper);
    }

    return epoch_stream_info_blob_ptr;
}

BlobPtr EpochTBuilder::fill_scatter_pipe_structs(EpochAllocator& epoch_allocator, const StreamInfo& stream_info)
{
    size_t scatter_pipe_state_num = stream_info.phase_offsets.size();

    auto [scatter_pipe_state_blob_ptr, scatter_pipe_states_ptr] =
        epoch_allocator.new_scatter_pipe_state_t(scatter_pipe_state_num);

    size_t scatter_pipe_states_idx = 0;
    for (const auto& [scatter_idx, phase_offset] : stream_info.phase_offsets)
    {
        scatter_pipe_state_t& scatter_pipe_state = scatter_pipe_states_ptr[scatter_pipe_states_idx++];
        scatter_pipe_state.max_unroll = phase_offset.num_unroll_iter;
        log_assert(
            phase_offset.offsets.size() == phase_offset.num_unroll_iter,
            "Blob scatter offsets added mismatched with num_unroll_iter.");

        auto [unroll_blobs_blob_ptr, scatter_pipe_blobs_ptr] =
            epoch_allocator.new_scatter_pipe_blob_t(scatter_pipe_state.max_unroll);
        scatter_pipe_state.unroll_blobs = unroll_blobs_blob_ptr;

        for (int offset_idx = 0; offset_idx < phase_offset.offsets.size(); offset_idx++)
        {
            scatter_pipe_blob_t& scatter_pipe_blob = scatter_pipe_blobs_ptr[offset_idx];
            scatter_pipe_blob.scatter_blob_base_addr = phase_offset.offsets[offset_idx];
            scatter_pipe_blob.start_scatter_blob_num_cfg_regs = phase_offset.phase_num_cfg_regs[offset_idx];
        }
    }

    // Advance the blob section pointer by the pad amount needed for scatter_pipes.
    // For most other structures this is not needed, since the structures' sizes are themselves already padded.
    epoch_allocator.pad_to_noc_alignment();

    return scatter_pipe_state_blob_ptr;
}

BlobPtr EpochTBuilder::fill_epoch_stream_dram_io_info_t(
    EpochAllocator& epoch_allocator, const StreamBlobData& stream_blob_data, const NOCHelper& noc_helper)
{
    PhaseConfig& first_phase = *stream_blob_data.phases.begin()->second;
    PhaseConfigHelper first_phase_helper = PhaseConfigHelper(first_phase);

    auto [epoch_stream_dram_io_info_blob_ptr, stream_dram_io_info] = epoch_allocator.new_epoch_stream_dram_io_info_t();

    EpochBlobFiller::fill_epoch_stream_dram_io_info_t(stream_dram_io_info, stream_blob_data);

    stream_dram_io_info->dram_io_state = fill_dram_io_state_t(epoch_allocator, stream_blob_data, noc_helper);

    return epoch_stream_dram_io_info_blob_ptr;
}

BlobPtr EpochTBuilder::fill_dram_io_state_t(
    EpochAllocator& epoch_allocator, const StreamBlobData& stream_blob_data, const NOCHelper& noc_helper)
{
    BlobPtr first_dram_io_state = 0;
    dram_io_state_t* prev_dram_io_state = nullptr;
    PhaseConfig& first_phase = *stream_blob_data.phases.begin()->second;

    for (const NcriscConfig& ncrisc_config : stream_blob_data.ncriscs)
    {
        NcriscConfigHelper ncrisc_config_helper(ncrisc_config);

        auto [dram_io_state_blob_ptr, dram_io_state] = epoch_allocator.new_dram_io_state_t();

        if (prev_dram_io_state == nullptr)
        {
            first_dram_io_state = dram_io_state_blob_ptr;
        }
        else
        {
            prev_dram_io_state->local.next = dram_io_state_blob_ptr;
        }
        prev_dram_io_state = dram_io_state;

        EpochBlobFiller::fill_dram_io_state_t(dram_io_state, ncrisc_config, first_phase, noc_helper);

        if (ncrisc_config_helper.has_dram_scatter_offsets())
        {
            dram_io_state->local.dram_io_scatter_state =
                fill_dram_io_scatter_state_t(epoch_allocator, first_phase, ncrisc_config, noc_helper);
        }
    }

    return first_dram_io_state;
}

BlobPtr EpochTBuilder::fill_dram_io_scatter_state_t(
    EpochAllocator& epoch_allocator,
    const PhaseConfig& first_phase,
    const NcriscConfig ncrisc_config,
    const NOCHelper& noc_helper)
{
    auto [dram_io_scatter_state_blob_ptr, dram_io_scatter_state] = epoch_allocator.new_dram_io_scatter_state_t();

    EpochBlobFiller::fill_dram_io_scatter_state_t(dram_io_scatter_state, ncrisc_config, first_phase, noc_helper);

    dram_io_scatter_state->scatter_offsets = fill_dram_scatter_offsets(
        epoch_allocator, first_phase, ncrisc_config, noc_helper, dram_io_scatter_state->scatter_offsets_size);

    return dram_io_scatter_state_blob_ptr;
}

BlobPtr EpochTBuilder::fill_dram_scatter_offsets(
    EpochAllocator& epoch_allocator,
    const PhaseConfig& first_phase,
    const NcriscConfig ncrisc_config,
    const NOCHelper& noc_helper,
    const int scatter_offsets_size)
{
    PhaseConfigHelper first_phase_helper(first_phase);
    NcriscConfigHelper ncrisc_config_helper(ncrisc_config);

    auto [scatter_offsets_blob_ptr, scatter_offsets] = epoch_allocator.new_tt_uint64_t(scatter_offsets_size);

    // Note that we always pad as if the scatter_offsets are 64-bit, even if they are 32-bit in
    // case of hw_tilize. This can be probably changed to be more efficient.
    // Advance the blob section pointer by the pad amount needed for scatter_offsets.
    epoch_allocator.pad_to_noc_alignment();

    std::vector<uint64_t> dram_scatter_offsets =
        ncrisc_config_helper.get_dram_scatter_offsets(first_phase_helper, noc_helper);
    for (int i = 0; i < scatter_offsets_size; i++)
    {
        scatter_offsets[i] = {dram_scatter_offsets[i]};
    }

    return scatter_offsets_blob_ptr;
}

epoch_stream_info_t* EpochTBuilder::find_epoch_stream_info_t(
    EpochAllocator& epoch_allocator, const tt_cxy_pair core_id, const StreamId stream_id, const SoCHelper& soc_helper)
{
    epoch_t* epoch = epoch_allocator.get_epoch_t(soc_helper.get_overlay_start_address(core_id));
    for (BlobPtr stream_blob_ptr : epoch->active_streams)
    {
        if (stream_blob_ptr == 0)
        {
            break;
        }

        epoch_stream_info_t* stream = epoch_allocator.get_epoch_stream_info_t(stream_blob_ptr);
        if (stream->stream_id == stream_id)
        {
            return stream;
        }
    }
    log_assert(false, "Did not find requested stream_id among active streams.");
}

void EpochTBuilder::fill_epoch_stream_info_t_fork_idxs_all_cores(
    std::map<tt_cxy_pair, EpochAllocator>& epoch_alocators,
    const EpochBlobData& epoch_blob_data,
    const SoCHelper& soc_helper)
{
    for (const auto& [core_id, stream_blob_data] : epoch_blob_data.cores)
    {
        EpochAllocator& epoch_allocator = epoch_alocators.at(core_id);
        epoch_t* epoch = epoch_allocator.get_epoch_t(soc_helper.get_overlay_start_address(core_id));

        fill_epoch_stream_info_t_fork_idxs(epoch->active_streams, epoch_allocator, stream_blob_data);
    }
}

void EpochTBuilder::fill_epoch_stream_info_t_fork_idxs(
    const epoch_stream_info_t_ptr (&active_streams)[NOC_NUM_STREAMS],
    EpochAllocator& epoch_allocator,
    const CoreBlobData& core_blob_data)
{
    for (BlobPtr stream_blob_ptr : active_streams)
    {
        if (stream_blob_ptr == NullBlobPtr)
        {
            break;
        }
        epoch_stream_info_t* stream = epoch_allocator.get_epoch_stream_info_t(stream_blob_ptr);

        const StreamBlobData& stream_blob_data = core_blob_data.streams.at(stream->stream_id);
        const PhaseConfig& first_phase = *stream_blob_data.phases.begin()->second;
        PhaseConfigHelper first_phase_helper = PhaseConfigHelper(first_phase);

        for (auto* fork_stream : first_phase_helper.get_fork_streams())
        {
            StreamId fork_stream_id = fork_stream->get_stream_id();
            // Note: it will never hit NOC_NUM_STREAMS.
            for (int active_stream_idx = 0; active_stream_idx < NOC_NUM_STREAMS; active_stream_idx++)
            {
                BlobPtr active_stream_blob_ptr = active_streams[active_stream_idx];
                log_assert(
                    active_stream_blob_ptr != NullBlobPtr,
                    "Did not find requested fork_stream_id among active streams.");
                epoch_stream_info_t* active_stream = epoch_allocator.get_epoch_stream_info_t(active_stream_blob_ptr);
                if (active_stream->stream_id == fork_stream_id)
                {
                    stream->fork_idxs[stream->num_fork_streams++] = active_stream_idx;
                    break;
                }
            }
        }

        // If is_scatter_pack, then the stream itself is also included in fork_idxs.
        // But it shouldn't be included in num_fork_streams.
        if (first_phase_helper.get_is_scatter_pack())
        {
            stream->num_fork_streams--;
        }
    }
}

void EpochTBuilder::update_blob_base_addrs(
    std::map<tt_cxy_pair, EpochAllocator>& epoch_allocators,
    const EpochBlobData& epoch_blob_data,
    const SoCHelper& soc_helper)
{
    for (auto const& [core_id, core_blob_data] : epoch_blob_data.cores)
    {
        EpochAllocator& epoch_allocator = epoch_allocators.at(core_id);
        epoch_t* epoch = epoch_allocator.get_epoch_t(soc_helper.get_overlay_start_address(core_id));

        BlobPtr blob_start_addr = epoch_allocator.get_current_address();

        for (BlobPtr blob_stream_ptr : epoch->active_streams)
        {
            if (blob_stream_ptr == NullBlobPtr)
            {
                break;
            }
            epoch_stream_info_t* stream = epoch_allocator.get_epoch_stream_info_t(blob_stream_ptr);
            const StreamInfo& stream_info = core_blob_data.streams.at(stream->stream_id).info;
            stream->blob_start_addr = stream_info.blob_start_relative_offset + blob_start_addr;

            for (size_t i = 0; i < stream_info.phase_offsets.size(); i++)
            {
                scatter_pipe_state_t& scatter_pipe_state =
                    epoch_allocator.get_scatter_pipe_state_t(stream->scatter_pipe_state)[i];
                for (size_t bi = 0; bi < scatter_pipe_state.max_unroll; bi++)
                {
                    scatter_pipe_blob_t& scatter_pipe_blob =
                        epoch_allocator.get_scatter_pipe_blob_t(scatter_pipe_state.unroll_blobs)[bi];
                    scatter_pipe_blob.scatter_blob_base_addr += blob_start_addr;
                }
            }
        }
    }
}

void EpochTBuilder::fill_dram_streaming_header_addr(
    std::map<tt_cxy_pair, EpochAllocator>& epoch_allocators,
    const EpochBlobData& epoch_blob_data,
    const SoCHelper& soc_helper)
{
    for (const auto& [core_id, core_blob_data] : epoch_blob_data.cores)
    {
        EpochAllocator& epoch_allocator = epoch_allocators.at(core_id);
        epoch_t* epoch = epoch_allocator.get_epoch_t(soc_helper.get_overlay_start_address(core_id));
        NOCHelper noc_helper = soc_helper.get_noc_helper(core_id.chip);

        for (BlobPtr blob_stream_ptr : epoch->active_streams)
        {
            if (blob_stream_ptr == NullBlobPtr)
            {
                break;
            }
            epoch_stream_info_t* stream = epoch_allocator.get_epoch_stream_info_t(blob_stream_ptr);
            const StreamBlobData& stream_blob_data = core_blob_data.streams.at(stream->stream_id);

            if (stream->dram_io_info == NullBlobPtr)
            {
                continue;
            }

            epoch_stream_dram_io_info_t* epoch_stream_dram_io_info =
                epoch_allocator.get_epoch_stream_dram_io_info_t(stream->dram_io_info);
            int dram_io_state_idx = 0;
            BlobPtr next_dram_io_state_blob_ptr = epoch_stream_dram_io_info->dram_io_state;
            while (next_dram_io_state_blob_ptr != NullBlobPtr)
            {
                dram_io_state_t* next_dram_io_state = epoch_allocator.get_dram_io_state_t(next_dram_io_state_blob_ptr);
                const NcriscConfig& ncrisc_config = stream_blob_data.ncriscs[dram_io_state_idx];
                NcriscConfigHelper ncrisc_config_helper = NcriscConfigHelper(ncrisc_config);
                PhaseConfigHelper first_phase_helper = PhaseConfigHelper(*stream_blob_data.phases.begin()->second);

                if (ncrisc_config.dram_output.value_or(false) && ncrisc_config.dram_streaming.value_or(false))
                {
                    StreamNode* dest_stream = ncrisc_config.dram_streaming_dest.value();
                    EpochAllocator& dest_epoch_allocator = epoch_allocators.at(dest_stream->get_physical_location());
                    epoch_stream_info_t* dest_stream_info = find_epoch_stream_info_t(
                        dest_epoch_allocator,
                        dest_stream->get_physical_location(),
                        dest_stream->get_stream_id(),
                        soc_helper);

                    BlobPtr desired_header_addr =
                        dest_epoch_allocator.get_epoch_stream_dram_io_info_t(dest_stream_info->dram_io_info)
                            ->dram_io_state;

                    next_dram_io_state->l1_to_dram.dram_streaming_header_addr = {
                        ncrisc_config_helper.get_dram_streaming_header_addr(
                            first_phase_helper, noc_helper, desired_header_addr)};
                }
                next_dram_io_state_blob_ptr = next_dram_io_state->local.next;
                dram_io_state_idx++;
            }
        }
    }
}

void EpochTBuilder::fill_intermediates_stream_binary_instruction_list(
    std::map<tt_cxy_pair, EpochAllocator>& epoch_allocators, EpochBlobData& epoch_blob_data)
{
    for (auto& [core_id, core_blob_data] : epoch_blob_data.cores)
    {
        EpochAllocator& epoch_allocator = epoch_allocators.at(core_id);

        BlobPtr blob_start_addr = epoch_allocator.get_current_address();

        for (auto& [stream_id, stream_blob_data] : core_blob_data.streams)
        {
            StreamInfo& stream_info = stream_blob_data.info;
            for (const auto& [phase_id, phase] : stream_blob_data.phases)
            {
                PhaseConfigHelper phase_helper = PhaseConfigHelper(*phase);
                if (phase_helper.get_intermediate())
                {
                    stream_info.stream_binary_instruction_list.at(phase_id)[0].pop<uint32_t>();
                    stream_info.stream_binary_instruction_list.at(phase_id)[0].append(
                        PhaseBlobFiller::intermediates_blob_loop_dw(
                            stream_info.blob_start_relative_offset + blob_start_addr),
                        "STREAM_PHASE_AUTO_CFG_PTR_REG_INDEX popped:");
                }
            }
        }
    }
}

}  // namespace blobgen2