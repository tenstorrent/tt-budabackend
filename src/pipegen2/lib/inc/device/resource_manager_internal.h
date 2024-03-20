#pragma once

#include <memory>

#include "device/tt_arch_types.h"
#include "device/tt_xy_pair.h"

#include "device/ethernet_core_resources.h"
#include "device/worker_core_resources.h"

namespace pipegen2
{
namespace resource_manager_internal
{

// Creates appropriate worker core resources for a given device architecture and worker core location.
std::unique_ptr<WorkerCoreResources> create_worker_core_resources(tt::ARCH device_arch,
                                                                  const tt_cxy_pair& core_physical_location);

// Creates appropriate ethernet core resources for a given device architecture and ethernet core location.
std::unique_ptr<EthernetCoreResources> create_ethernet_core_resources(tt::ARCH device_arch,
                                                                      const tt_cxy_pair& core_physical_location);

} // namespace resource_manager_internal
} // namespace pipegen2