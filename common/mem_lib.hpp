// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

namespace tt {
//! Helper function for memory purposes
namespace mem {

extern std::unordered_map<void*, std::shared_ptr<std::vector<uint32_t>>> host_shared_memory;

}  // namespace mem
}  // namespace tt
