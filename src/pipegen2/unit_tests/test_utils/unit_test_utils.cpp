// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "unit_test_utils.h"

#include <cstdlib>
#include <string>

#include "device/tt_soc_descriptor.h"

namespace pipegen2
{
namespace unit_test_utils
{

void verify_exception_message_regex(const std::exception& ex, const std::string& expected_error_message_regex)
{
    EXPECT_THAT(ex.what(), ::testing::MatchesRegex(expected_error_message_regex));
}

tt::ARCH get_build_arch()
{
    const char* arch_env_var = std::getenv("ARCH_NAME");
    // If there is no such env var, grayskull is default.
    const std::string arch_name = (arch_env_var ? arch_env_var : "grayskull");

    return get_arch_name(arch_name);
}

} // namespace unit_test_utils
} // namespace pipegen2