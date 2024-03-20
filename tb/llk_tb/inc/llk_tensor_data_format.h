// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <map>
#include <string>

#include "tensix_types.h"
namespace llk {

std::string to_str(const DataFormat inType);
DataFormat get_data_format(const std::string in_data_format);
float num_bytes(const DataFormat inType);
int num_bytes_extra(const DataFormat inType);

}  // namespace llk