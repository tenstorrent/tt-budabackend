// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "l1_address_map_api.h"

#include "common/buda_soc_descriptor.h"
#include "common/model/assert.hpp"
#include <array>



class ArchAddressMap {
  public:
    static inline AddressMap get(const tt::ARCH &arch) {
        switch (arch) {
            case tt::ARCH::GRAYSKULL: return AddressMap::ForGrayskull(); 
            case tt::ARCH::WORMHOLE: return AddressMap::ForWormhole(); 
            case tt::ARCH::WORMHOLE_B0: return AddressMap::ForWormhole(); 

            default: 
                log_assert(false, "Unsupported Arch");
                return AddressMap::ForUnsupportedArch();
        };

    }
    static inline AddressMap of(const tt::ARCH &arch) {
        return ArchAddressMap::get(arch);
    }
};