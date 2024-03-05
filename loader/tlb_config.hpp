// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "common/buda_soc_descriptor.h"
#include "device/device_api.h"
#include "device/tt_arch_types.h"

namespace tt::tlb_config {

void configure_static_tlbs(
    tt::ARCH arch, const std::uint32_t& chip, const buda_SocDescriptor& sdesc, std::shared_ptr<tt_device> device);
// void activate_static_tlbs(std::shared_ptr<tt_device> device);
std::unordered_map<std::string, std::int32_t> get_dynamic_tlb_config(tt::ARCH arch);

}  // namespace tt::tlb_config
