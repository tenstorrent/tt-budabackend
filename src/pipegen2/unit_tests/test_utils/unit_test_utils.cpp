// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "unit_test_utils.h"

#include <cstdlib>

namespace pipegen2
{
namespace unit_test_utils
{

bool is_built_for_arch(const std::string& arch_name)
{   
    const char* arch_env_var = std::getenv("ARCH_NAME");
    return arch_name == (arch_env_var ? arch_env_var : "grayskull");
}

} // namespace unit_test_utils
} // namespace pipegen2