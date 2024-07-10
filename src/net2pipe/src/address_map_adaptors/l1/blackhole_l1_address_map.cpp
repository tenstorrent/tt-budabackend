// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "address_map_adaptors/l1/l1_address_map_api.h"
#include "src/firmware/riscv/blackhole/l1_address_map.h"
#include "src/firmware/riscv/blackhole/eth_l1_address_map.h"
#include "net2pipe_constants.h"

AddressMap AddressMap::ForBlackhole() {
    return AddressMap(
        tt::ARCH::BLACKHOLE,
        L1AddressMap{.MAX_SIZE = l1_mem::address_map::MAX_SIZE, .FW_BLOCK_SIZE = l1_mem::address_map::FW_L1_BLOCK_SIZE, .DATA_BUFFER_SPACE_BASE = l1_mem::address_map::DATA_BUFFER_SPACE_BASE, .TILE_HEADER_BUFFER_SIZE=l1_mem::address_map::TILE_HEADER_BUFFER_SIZE},
        EthernetL1AddressMap{.MAX_SIZE = eth_l1_mem::address_map::MAX_SIZE, .FW_BLOCK_SIZE = eth_l1_mem::address_map::FW_L1_BLOCK_SIZE, .DATA_BUFFER_SPACE_BASE = eth_l1_mem::address_map::DATA_BUFFER_SPACE_BASE,  .TILE_HEADER_BUFFER_SIZE=eth_l1_mem::address_map::TILE_HEADER_BUFFER_SIZE},
        ArchConstants{.MAX_MCAST_STREAMS_PER_CORE=MAX_MCAST_STREAMS_PER_CORE}
    );
}
