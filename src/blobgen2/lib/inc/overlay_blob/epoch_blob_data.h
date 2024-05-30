// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

// #include <cstdint>

#include "model/stream_graph/ncrisc_config.h"
#include "model/stream_graph/phase_config.h"
#include "overlay_blob/stream_info.h"
#include "utils/logger.hpp"

// #include "model/typedefs.h"

namespace blobgen2
{

using pipegen2::NcriscConfig;
using pipegen2::PhaseConfig;
using pipegen2::PhaseId;
using pipegen2::StreamId;

struct StreamBlobData
{
    std::map<PhaseId, PhaseConfig*> phases;
    std::vector<NcriscConfig> ncriscs;
    StreamInfo info;
    StreamId stream_id;
};

struct CoreBlobData
{
    std::map<StreamId, StreamBlobData> streams;
};

struct EpochBlobData
{
    std::map<tt_cxy_pair, CoreBlobData> cores;

    // Find phase config up to the provided phase id. The provided phase id is included in the search.
    PhaseConfig* find_phase_config_up_to_phase_id(
        const tt_cxy_pair& core_id, const StreamId stream_id, const PhaseId& phase_id)
    {
        std::map<PhaseId, PhaseConfig*>& phases = cores.at(core_id).streams.at(stream_id).phases;

        // std::upper_bound returns first element > phase_id. So by taking the previous element, we get the last
        // element <= phase_id.
        auto phase_it = phases.upper_bound(phase_id);
        log_assert(phase_it != phases.begin(), "Expected at least one phase than or equal to the provided phase id");
        return std::prev(phase_it)->second;
    }
};

}  // namespace blobgen2