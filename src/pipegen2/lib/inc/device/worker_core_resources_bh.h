// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device/worker_core_resources_wh.h"

namespace pipegen2
{

class WorkerCoreResourcesBH : public WorkerCoreResourcesWH
{
public:
    WorkerCoreResourcesBH(const tt_cxy_pair& core_physical_location) : WorkerCoreResourcesWH(core_physical_location) {}
};

} // namespace pipegen2