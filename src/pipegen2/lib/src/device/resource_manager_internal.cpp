// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/resource_manager_internal.h"

// clang-format off
#include "logger.hpp"

#include "device/worker_core_resources_bh.h"
#include "device/worker_core_resources_gs.h"
#include "device/worker_core_resources_wh.h"
// clang-format on

namespace pipegen2
{
namespace resource_manager_internal
{

std::unique_ptr<WorkerCoreResources> create_worker_core_resources(
    tt::ARCH device_arch, const tt_cxy_pair& core_physical_location, const tt_cxy_pair& core_logical_location)
{
    switch (device_arch)
    {
        case tt::ARCH::GRAYSKULL:
            return std::make_unique<WorkerCoreResourcesGS>(core_physical_location, core_logical_location);
        case tt::ARCH::WORMHOLE:
        case tt::ARCH::WORMHOLE_B0:
            return std::make_unique<WorkerCoreResourcesWH>(core_physical_location, core_logical_location);
        case tt::ARCH::BLACKHOLE:
            return std::make_unique<WorkerCoreResourcesBH>(core_physical_location, core_logical_location);
        default:
            log_assert(false, "ResourceManager: Unsupported device architecture");
    }
}

std::unique_ptr<EthernetCoreResources> create_ethernet_core_resources(
    tt::ARCH device_arch, const tt_cxy_pair& core_physical_location)
{
    log_assert(
        device_arch == tt::ARCH::WORMHOLE || device_arch == tt::ARCH::WORMHOLE_B0 || device_arch == tt::ARCH::BLACKHOLE,
        "ResourceManager: Ethernet cores supported only on WH and BH architectures");

    return std::make_unique<EthernetCoreResources>(core_physical_location);
}

}  // namespace resource_manager_internal
}  // namespace pipegen2