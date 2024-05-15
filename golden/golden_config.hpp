// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "common/base.hpp"
#include "netlist/tt_backend_api_types.hpp"
namespace tt::golden {
struct tt_golden_config : tt::tt_backend_config {
    bool en_quantize_golden = false;  // Quantize golden results
};

inline tt_golden_config get_golden_config(const tt::tt_backend_config &base_config) {
    log_assert(
        base_config.type == tt::DEVICE::Golden,
        "Golden Config can only be used if the backend type is golden");
    tt_golden_config golden_config;
    tt::tt_backend_config &copy_to_config = golden_config;
    copy_to_config = base_config;
    golden_config.en_quantize_golden = not golden_config.ignore_data_format_precision;
    return golden_config;
};
}  // namespace tt::golden
