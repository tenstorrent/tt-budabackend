// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdexcept>

namespace tt {
namespace error_types {


class timeout_error : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

}  // namespace error_types
}  // namespace tt