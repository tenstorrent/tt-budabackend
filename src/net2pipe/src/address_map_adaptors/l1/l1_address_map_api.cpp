// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "address_map_adaptors/l1/l1_address_map_api.h"

AddressMap AddressMap::ForUnsupportedArch() {
    return AddressMap(
        tt::ARCH::Invalid,
        L1AddressMap{.MAX_SIZE = -1, .FW_BLOCK_SIZE = -1},
        EthernetL1AddressMap{.MAX_SIZE = -1, .FW_BLOCK_SIZE = -1},
        ArchConstants{.MAX_MCAST_STREAMS_PER_CORE=-1}
    );
}
