// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "device_data.hpp"
#include "device/device_api.h"
#include "common/buda_soc_descriptor.h"
#include <cstdint>

namespace tt::tlb_config {
    #ifdef ARCH_GRAYSKULL
    const std::vector<uint32_t> EPOCH_CMD_QUEUE_TLBS = {DEVICE_DATA.TLB_BASE_INDEX_2M + 2, DEVICE_DATA.TLB_BASE_INDEX_2M + 3, DEVICE_DATA.TLB_BASE_INDEX_2M + 4, DEVICE_DATA.TLB_BASE_INDEX_2M + 5, DEVICE_DATA.TLB_BASE_INDEX_2M + 6, DEVICE_DATA.TLB_BASE_INDEX_2M + 7, DEVICE_DATA.TLB_BASE_INDEX_2M + 8, DEVICE_DATA.TLB_BASE_INDEX_2M + 9};
    static constexpr uint32_t DYNAMIC_TLB_COUNT = 16;
    static constexpr unsigned int MEM_SMALL_READ_WRITE_TLB  = DEVICE_DATA.TLB_BASE_INDEX_2M + 1;
    static constexpr unsigned int DYNAMIC_TLB_BASE_INDEX    = DEVICE_DATA.MEM_LARGE_READ_TLB + 1;

    #else 
    const std::vector<uint32_t> EPOCH_CMD_QUEUE_TLBS = {DEVICE_DATA.TLB_BASE_INDEX_1M + 96, DEVICE_DATA.TLB_BASE_INDEX_1M + 97, DEVICE_DATA.TLB_BASE_INDEX_1M + 98, DEVICE_DATA.TLB_BASE_INDEX_1M + 99, DEVICE_DATA.TLB_BASE_INDEX_1M + 100, DEVICE_DATA.TLB_BASE_INDEX_1M + 101, DEVICE_DATA.TLB_BASE_INDEX_1M + 102, DEVICE_DATA.TLB_BASE_INDEX_1M + 103,
                                                        DEVICE_DATA.TLB_BASE_INDEX_1M + 104, DEVICE_DATA.TLB_BASE_INDEX_1M + 105, DEVICE_DATA.TLB_BASE_INDEX_1M + 106, DEVICE_DATA.TLB_BASE_INDEX_1M + 107, DEVICE_DATA.TLB_BASE_INDEX_1M + 108, DEVICE_DATA.TLB_BASE_INDEX_1M + 109, DEVICE_DATA.TLB_BASE_INDEX_1M + 110, DEVICE_DATA.TLB_BASE_INDEX_1M + 111, 
                                                        DEVICE_DATA.TLB_BASE_INDEX_1M + 112, DEVICE_DATA.TLB_BASE_INDEX_1M + 113};
    static constexpr uint32_t DYNAMIC_TLB_COUNT = 16;
    static constexpr unsigned int MEM_SMALL_READ_WRITE_TLB  = DEVICE_DATA.TLB_BASE_INDEX_2M + 1;
    static constexpr uint32_t DYNAMIC_TLB_BASE_INDEX = DEVICE_DATA.MEM_LARGE_READ_TLB + 1;

    #endif
    std::int32_t get_static_tlb_index(tt_xy_pair target);
    void configure_static_tlbs(const std::uint32_t& chip, const buda_SocDescriptor& sdesc, std::shared_ptr<tt_device> device);
    //void activate_static_tlbs(std::shared_ptr<tt_device> device);
    std::unordered_map<std::string, std::int32_t> get_dynamic_tlb_config();
}