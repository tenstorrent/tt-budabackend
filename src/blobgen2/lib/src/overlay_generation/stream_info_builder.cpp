// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "overlay_generation/stream_info_builder.h"

#include "helpers/noc_helper.h"
#include "helpers/phase_config_helper.h"
#include "helpers/soc_helper.h"
#include "model/stream_graph/ncrisc_config.h"
#include "model/stream_graph/phase_config.h"
#include "overlay_blob/stream_info.h"
#include "overlay_generation/phase_blob_filler.h"

namespace blobgen2
{

void StreamInfoBuilder::fill_stream_infos(EpochBlobData& epoch_blob_data, const SoCHelper& soc_helper)
{
    for (auto& [core_id, core_blob_data] : epoch_blob_data.cores)
    {
        size_t curr_blob_relative_offset = 0;

        for (auto& [stream_id, stream_blob_data] : core_blob_data.streams)
        {
            fill_stream_info(
                stream_blob_data, epoch_blob_data, soc_helper, core_id, stream_id, curr_blob_relative_offset);
        }
    }
}

void StreamInfoBuilder::fill_stream_info(
    StreamBlobData& stream_blob_data,
    EpochBlobData& epoch_blob_data,
    const SoCHelper& soc_helper,
    const tt_cxy_pair& core_id,
    const StreamId& stream_id,
    size_t& curr_blob_relative_offset)
{
    NOCHelper noc_helper = soc_helper.get_noc_helper(core_id.chip);

    int curr_blob_start_offset = curr_blob_relative_offset;

    std::map<PhaseId, size_t> num_inc_per_phase = get_num_inc_per_phase(stream_blob_data.phases);
    std::map<uint32_t, PhaseId> scatter_idx_to_phase_id;

    // Gather previous and next phases for each phase. This is faster then using std::prev and std::next.
    std::unordered_map<PhaseConfig*, PhaseConfig*> prev_phases;
    std::unordered_map<PhaseConfig*, PhaseConfig*> next_phases;
    prev_phases.emplace(stream_blob_data.phases.begin()->second, nullptr);
    next_phases.emplace(stream_blob_data.phases.rbegin()->second, nullptr);
    PhaseConfig* prev_phase_iter = nullptr;
    for (auto& [phase_id, phase] : stream_blob_data.phases)
    {
        if (prev_phase_iter != nullptr)
        {
            prev_phases.emplace(phase, prev_phase_iter);
            next_phases.emplace(prev_phase_iter, phase);
        }
        prev_phase_iter = phase;
    }

    for (auto& [phase_id, phase] : stream_blob_data.phases)
    {
        PhaseConfigHelper current_phase_helper = PhaseConfigHelper(*phase);

        stream_blob_data.info.stream_binary_instruction_list.emplace(phase_id, std::vector<BlobSection>{});

        unsigned int mblock_base_addr = current_phase_helper.get_buf_base_addr();
        unsigned int mblock_size = current_phase_helper.get_mblock_size();

        std::optional<StreamNode*> dest_stream_node = current_phase_helper.get_dest_any_stream_optional();
        std::optional<PhaseConfig> dest_phase = dest_stream_node.has_value()
                                                    ? std::optional(*epoch_blob_data.find_phase_config_up_to_phase_id(
                                                          dest_stream_node.value()->get_physical_location(),
                                                          dest_stream_node.value()->get_stream_id(),
                                                          phase_id))
                                                    : std::nullopt;

        for (int mblock_buffering_idx = 0; mblock_buffering_idx < current_phase_helper.get_num_mblock_buffering();
             mblock_buffering_idx++)
        {
            unsigned int mblock_current_base_addr = mblock_base_addr + mblock_buffering_idx * mblock_size;
            if (stream_blob_data.info.dummy_phase_stream_binary_instruction_list.size() < mblock_buffering_idx + 1)
            {
                stream_blob_data.info.dummy_phase_stream_binary_instruction_list.emplace_back(
                    std::vector<BlobSection>{});
            }
            stream_blob_data.info.stream_binary_instruction_list.at(phase_id).emplace_back(0);

            uint32_t dummy_phase_addr = soc_helper.get_dummy_phase_address(core_id);

            stream_blob_data.info.stream_binary_instruction_list.at(phase_id)[mblock_buffering_idx].merge(
                PhaseBlobFiller::get_phase_blob(
                    core_id,
                    stream_id,
                    *phase,
                    prev_phases.at(phase),
                    next_phases.at(phase),
                    dest_phase,
                    stream_blob_data.ncriscs,
                    mblock_base_addr,
                    mblock_current_base_addr,
                    (current_phase_helper.has_dummy_phase() ||
                     stream_blob_data.info.dummy_phase_stream_binary_instruction_list[0].size() > 0) &&
                        next_phases.at(phase) == nullptr,
                    soc_helper.get_overlay_version(core_id.chip),
                    noc_helper));

            if (current_phase_helper.has_dummy_phase_receiver())
            {
                stream_blob_data.info.dummy_phase_stream_binary_instruction_list[mblock_buffering_idx].push_back(
                    PhaseBlobFiller::get_dummy_phase_blob(
                        core_id,
                        stream_id,
                        *phase,
                        dummy_phase_addr,
                        0,
                        false,
                        current_phase_helper.has_dummy_phase_receiver(),
                        soc_helper.get_overlay_version(core_id.chip),
                        noc_helper));
            }

            if (current_phase_helper.has_dummy_phase_sender())
            {
                // Dest dummy phase address can be different based on whether the core is ETH or not.
                uint32_t dest_dummy_phase_addr = soc_helper.get_dummy_phase_address(
                    current_phase_helper.get_dest_stream_nodes()[0]->get_physical_location());

                stream_blob_data.info.dummy_phase_stream_binary_instruction_list[mblock_buffering_idx].push_back(
                    PhaseBlobFiller::get_dummy_phase_blob(
                        core_id,
                        stream_id,
                        *phase,
                        dummy_phase_addr,
                        dest_dummy_phase_addr,
                        current_phase_helper.has_dummy_phase_sender(),
                        false,
                        soc_helper.get_overlay_version(core_id.chip),
                        noc_helper));
            }
        }
        if (current_phase_helper.get_intermediate())
        {
            // Infinite loop for intermediates - last word needs to be added below when we compute absolute
            // blob address insert placeholder here so length calculations are correct.
            // This is just to place the streams in some state where we can restart it after calling stream_reset. There
            // is no hw feature related to this. (its a hack)
            stream_blob_data.info.stream_binary_instruction_list.at(phase_id)[0].append(
                0xaabbccdd, "intermediate loop");
        }

        // TODO: refactor logic around prev_phase to make it clearer.
        if (prev_phases.at(phase) != nullptr)
        {
            PhaseConfig& prev_phase = *prev_phases.at(phase);

            if (current_phase_helper.is_pipe_scatter() && current_phase_helper.scatter_idx_change(prev_phase))
            {
                append_phase_offset(stream_blob_data.info, *phase, curr_blob_relative_offset);

                unsigned int phase_scatter_idx = phase->config.get_scatter_idx().value();

                bool has_prev_scatter_phase =
                    scatter_idx_to_phase_id.find(phase_scatter_idx) != scatter_idx_to_phase_id.end();

                if (has_prev_scatter_phase)
                {
                    auto& prev_scatter_phase_id = scatter_idx_to_phase_id[phase_scatter_idx];
                    auto& prev_scatter_phase = stream_blob_data.phases.at(prev_scatter_phase_id)->config;
                    stream_blob_data.info.cfg_header_dw[prev_scatter_phase_id] = PhaseBlobFiller::blob_header_dw(
                        stream_blob_data.info.stream_binary_instruction_list.at(phase_id)[0].dw_size(),
                        prev_scatter_phase.get_num_msgs().value(),
                        num_inc_per_phase[prev_scatter_phase_id]);
                    scatter_idx_to_phase_id.erase(phase_scatter_idx);
                }
            }
            else
            {
                stream_blob_data.info.cfg_header_dw[prev_phase.phase_id] = PhaseBlobFiller::blob_header_dw(
                    stream_blob_data.info.stream_binary_instruction_list.at(phase_id)[0].dw_size(),
                    prev_phase.config.get_num_msgs().value(),
                    num_inc_per_phase[prev_phase.phase_id]);
            }
        }
        else
        {
            stream_blob_data.info.blob_start_relative_offset = curr_blob_relative_offset;

            if (current_phase_helper.is_pipe_scatter())
            {
                append_phase_offset(stream_blob_data.info, *phase, curr_blob_relative_offset);
            }
            stream_blob_data.info.start_phase_num_cfg_regs =
                stream_blob_data.info.stream_binary_instruction_list.at(phase_id)[0].dw_size();
        }

        if (next_phases.at(phase) != nullptr)
        {
            auto& next_phase = *next_phases.at(phase);
            if (current_phase_helper.is_pipe_scatter() && current_phase_helper.scatter_idx_change(next_phase))
            {
                scatter_idx_to_phase_id[phase->config.get_scatter_idx().value()] = phase_id;
            }
        }
        else
        {
            fill_header_of_last_phase(stream_blob_data, *phase, scatter_idx_to_phase_id, num_inc_per_phase);
        }

        curr_blob_relative_offset += stream_blob_data.info.get_binary_instruction_size(phase_id);

        curr_blob_relative_offset += stream_blob_data.info.get_dummy_phase_instructions_size(
            current_phase_helper.has_dummy_phase_receiver(), current_phase_helper.has_dummy_phase_sender());
    }

    stream_blob_data.info.blob_size = curr_blob_relative_offset - curr_blob_start_offset;
    curr_blob_relative_offset +=
        stream_blob_data.info.blob_size *
        (PhaseConfigHelper(*stream_blob_data.phases.begin()->second).get_num_mblock_buffering() - 1);
}

std::map<PhaseId, size_t> StreamInfoBuilder::get_num_inc_per_phase(const std::map<PhaseId, PhaseConfig*>& phase_configs)
{
    std::map<PhaseId, size_t> num_inc_per_phase;

    // Set it to first phase_id, so num_inc_per_phase for the first phase will be 0.
    PhaseId prev_phase_id = std::begin(phase_configs)->first;
    for (auto& [phase_id, phase] : phase_configs)
    {
        PhaseConfigHelper phase_helper = PhaseConfigHelper(*phase);

        num_inc_per_phase[phase_id] = phase_id - prev_phase_id;
        if (phase_helper.is_phase_wrapped(prev_phase_id))
        {
            num_inc_per_phase[phase_id] = 0;
        }

        prev_phase_id = phase_id;
    }

    return num_inc_per_phase;
}

void StreamInfoBuilder::append_phase_offset(
    StreamInfo& stream_info, const PhaseConfig& phase, size_t curr_blob_relative_offset)
{
    unsigned int phase_scatter_idx = phase.config.get_scatter_idx().value();
    if (stream_info.phase_offsets.find(phase_scatter_idx) == stream_info.phase_offsets.end())
    {
        stream_info.phase_offsets[phase_scatter_idx] = {};
    }
    PhaseOffsetsForSingleDest& phase_offset = stream_info.phase_offsets.at(phase_scatter_idx);
    phase_offset.offsets.push_back(curr_blob_relative_offset);
    phase_offset.phase_num_cfg_regs.push_back(
        stream_info.stream_binary_instruction_list.at(phase.phase_id)[0].dw_size());
    phase_offset.num_unroll_iter = phase.config.get_num_unroll_iter().value();
}

void StreamInfoBuilder::fill_header_of_last_phase(
    StreamBlobData& stream_blob_data,
    const PhaseConfig& phase,
    const std::map<uint32_t, PhaseId>& scatter_idx_to_phase_id,
    const std::map<PhaseId, size_t>& num_inc_per_phase)
{
    PhaseConfigHelper current_phase_helper = PhaseConfigHelper(phase);

    for (auto& [scatter_idx, prev_scatter_phase_id] : scatter_idx_to_phase_id)
    {
        auto& prev_scatter_phase = stream_blob_data.phases.at(prev_scatter_phase_id)->config;
        stream_blob_data.info.cfg_header_dw[prev_scatter_phase_id] = PhaseBlobFiller::blob_header_dw(
            0, prev_scatter_phase.get_num_msgs().value(), num_inc_per_phase.at(prev_scatter_phase_id));
    }
    if (current_phase_helper.has_dummy_phase() ||
        stream_blob_data.info.dummy_phase_stream_binary_instruction_list[0].size() > 0)
    {
        stream_blob_data.info.cfg_header_dw[phase.phase_id] = PhaseBlobFiller::blob_header_dw(
            stream_blob_data.info.dummy_phase_stream_binary_instruction_list[0][0].dw_size(),
            current_phase_helper.get_num_msgs(),
            num_inc_per_phase.at(phase.phase_id));
    }
    else if (current_phase_helper.get_intermediate())
    {
        stream_blob_data.info.cfg_header_dw[phase.phase_id] = PhaseBlobFiller::blob_header_dw(
            stream_blob_data.info.stream_binary_instruction_list.at(phase.phase_id)[0].dw_size(),
            current_phase_helper.get_num_msgs(),
            num_inc_per_phase.at(phase.phase_id));
    }
    else
    {
        stream_blob_data.info.cfg_header_dw[phase.phase_id] = PhaseBlobFiller::blob_header_dw(
            0, current_phase_helper.get_num_msgs(), num_inc_per_phase.at(phase.phase_id));
    }
}

}  // namespace blobgen2