// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/tt_silicon_driver_common.hpp"
#include "tt_xy_pair.h"
#include "tt_device.h"

bool TLB_DATA::check(const TLB_OFFSETS offset) {
    return local_offset > ((1 << (offset.x_end - offset.local_offset)) - 1) |
            x_end > ((1 << (offset.y_end - offset.x_end)) - 1) |
            y_end > ((1 << (offset.x_start - offset.y_end)) - 1) |
            x_start > ((1 << (offset.y_start - offset.x_start)) - 1) |
            y_start > ((1 << (offset.noc_sel - offset.y_start)) - 1) |
            noc_sel > ((1 << (offset.mcast - offset.noc_sel)) - 1) |
            mcast > ((1 << (offset.ordering - offset.mcast)) - 1) |
            ordering > ((1 << (offset.linked - offset.ordering)) - 1) |
            linked > ((1 << (offset.static_vc - offset.linked)) - 1) |
            static_vc > ((1 << (offset.static_vc_end - offset.static_vc)) - 1);
}

std::optional<std::uint64_t> TLB_DATA::apply_offset(const TLB_OFFSETS offset) {
    if (check(offset)) { return std::nullopt; }

    return local_offset << offset.local_offset |
            x_end << offset.x_end |
            y_end << offset.y_end |
            x_start << offset.x_start |
            y_start << offset.y_start |
            noc_sel << offset.noc_sel |
            mcast << offset.mcast |
            ordering << offset.ordering |
            linked << offset.linked |
            static_vc << offset.static_vc;
}

std::optional<std::tuple<std::uint32_t, std::uint32_t>> tt_SiliconDevice::describe_tlb(tt_xy_pair coord) {
    auto tlb_index = map_core_to_tlb(tt_xy_pair(coord.x, coord.y));
    if (tlb_index < 0) {
        return std::nullopt;
    }

    return describe_tlb(tlb_index);
}

std::string TensixSoftResetOptionsToString(TensixSoftResetOptions value) {
    std::string output;

    if((value & TensixSoftResetOptions::BRISC) != TensixSoftResetOptions::NONE) {
        output += "BRISC | ";
    }
    if((value & TensixSoftResetOptions::TRISC0) != TensixSoftResetOptions::NONE) {
        output += "TRISC0 | ";
    }
    if((value & TensixSoftResetOptions::TRISC1) != TensixSoftResetOptions::NONE) {
        output += "TRISC1 | ";
    }
    if((value & TensixSoftResetOptions::TRISC2) != TensixSoftResetOptions::NONE) {
        output += "TRISC2 | ";
    }
    if((value & TensixSoftResetOptions::NCRISC) != TensixSoftResetOptions::NONE) {
        output += "NCRISC | ";
    }
    if((value & TensixSoftResetOptions::STAGGERED_START) != TensixSoftResetOptions::NONE) {
        output += "STAGGERED_START | ";
    }

  if(output.empty()) {
    output = "UNKNOWN";
  } else {
    output.erase(output.end() - 3, output.end());
  }

  return output;
}

TensixSoftResetOptions operator|(TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) {
    return static_cast<TensixSoftResetOptions>(
        static_cast<std::underlying_type<TensixSoftResetOptions>::type>(lhs) |
        static_cast<std::underlying_type<TensixSoftResetOptions>::type>(rhs)
    );
}

TensixSoftResetOptions operator&(TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) {
    return static_cast<TensixSoftResetOptions>(
        static_cast<std::underlying_type<TensixSoftResetOptions>::type>(lhs) &
        static_cast<std::underlying_type<TensixSoftResetOptions>::type>(rhs)
    );
}

bool operator!=(TensixSoftResetOptions lhs, TensixSoftResetOptions rhs) {
    return
        static_cast<std::underlying_type<TensixSoftResetOptions>::type>(lhs) !=
        static_cast<std::underlying_type<TensixSoftResetOptions>::type>(rhs);
}
