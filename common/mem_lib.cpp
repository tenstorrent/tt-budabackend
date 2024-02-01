// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "common/mem_lib.hpp"

namespace tt {
namespace mem {

std::unordered_map<void*, std::shared_ptr<std::vector<uint32_t>>> host_shared_memory;

}  // namespace mem
}  // namespace tt
