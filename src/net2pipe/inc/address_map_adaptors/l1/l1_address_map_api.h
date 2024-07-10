// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/buda_soc_descriptor.h"

#include <cstdint>

struct L1AddressMap {
    std::int32_t MAX_SIZE;
    std::int32_t FW_BLOCK_SIZE;
    std::int32_t DATA_BUFFER_SPACE_BASE;
    std::int32_t TILE_HEADER_BUFFER_SIZE;
};

struct EthernetL1AddressMap {
    std::int32_t MAX_SIZE;
    std::int32_t FW_BLOCK_SIZE;
    std::int32_t DATA_BUFFER_SPACE_BASE;
    std::int32_t TILE_HEADER_BUFFER_SIZE;
};

struct ArchConstants {
  std::int32_t MAX_MCAST_STREAMS_PER_CORE;
};

class AddressMap {
  private:
    AddressMap(const tt::ARCH &_arch, const L1AddressMap &_l1, const EthernetL1AddressMap &_ethernet_l1, const ArchConstants &_arch_constants) :
        arch(_arch),
        l1(_l1),
        ethernet_l1(_ethernet_l1),
        arch_constants(_arch_constants)
    {}

  public:
    const tt::ARCH arch;
    const L1AddressMap l1;
    const EthernetL1AddressMap ethernet_l1;
    const ArchConstants arch_constants;


    static AddressMap ForGrayskull();
    static AddressMap ForWormhole();
    static AddressMap ForBlackhole();

    static AddressMap ForUnsupportedArch();
};
