// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/typedefs.h"
#include "stream_config.h"

namespace pipegen2
{
    struct PhaseConfig
    {
        PhaseId phase_id;
        StreamConfig config;

        PhaseConfig()
        {
        }

        PhaseConfig(PhaseId pid) : phase_id(pid), config(StreamConfig())
        {
        }

        PhaseConfig(PhaseId pid, StreamConfig&& config) : phase_id(pid), config(std::move(config))
        {
        }

        bool operator<(const PhaseConfig& other) const
        {
            return phase_id < other.phase_id;
        }
    };
}