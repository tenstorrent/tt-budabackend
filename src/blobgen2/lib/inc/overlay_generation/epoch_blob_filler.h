// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "epoch.h"
#include "overlay_blob/epoch_blob_data.h"
#include "overlay_blob/typedef.h"

namespace pipegen2
{
struct L1BufferAllocationInfo;
}

namespace blobgen2
{

class NOCHelper;

using pipegen2::L1BufferAllocationInfo;
using pipegen2::NcriscConfig;
using pipegen2::PhaseConfig;

// Class used for filling the epoch blob trivially. It doesn't set all the fields, specifically pointers to other
// structures, or complex fields which need to be calculated only after all the structs are filled with basic info.
// It does fill most of the fields which can be calculated from phase and ncrisc configs.
class EpochBlobFiller
{
public:
    static void fill_stream_infos();

    static void fill_epoch_t(
        epoch_t* epoch,
        const int epoch_num,
        const CoreBlobData& core_blob_data,
        const std::map<uint32_t, uint32_t>& tile_size_and_address,
        const std::optional<dram_perf_info_t> perf_info,
        const std::optional<L1BufferAllocationInfo>& ncrisc_fallback_buffer,
        const bool is_eth_core);

    static void fill_epoch_t_empty(epoch_t* epoch);

    static void fill_epoch_stream_info_t(epoch_stream_info_t* stream_info, const StreamBlobData& stream_blob_data);

    static void fill_epoch_stream_dram_io_info_t(
        epoch_stream_dram_io_info_t* stream_dram_io_info, const StreamBlobData& stream_blob_data);

    static void fill_dram_io_state_t(
        dram_io_state_t* dram_io_state,
        const NcriscConfig& ncrisc_config,
        const PhaseConfig& first_phase,
        const NOCHelper& noc_helper);

    static void fill_dram_io_scatter_state_t(
        dram_io_scatter_state_t* dram_io_scatter_state,
        const NcriscConfig& ncrisc,
        const PhaseConfig& first_phase,
        const NOCHelper& noc_helper);

private:
    template <typename T>
    static void assign_or_check_same(T& dest, const std::optional<T> src)
    {
        if (src.has_value())
        {
            log_assert(dest == 0 || dest == src.value(), "Error! found different values for the same field");
            dest = src.value();
        }
    }

    static void assign_or_check_same(uint16_t& dest, const std::optional<uint32_t> src)
    {
        assign_or_check_same(dest, static_cast<std::optional<uint16_t>>(src));
    }
};

}  // namespace blobgen2
