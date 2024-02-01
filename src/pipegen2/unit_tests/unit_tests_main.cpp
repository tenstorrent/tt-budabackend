// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <stdlib.h>

#include <gtest/gtest.h>

#include "utils/gtest_initializer.hpp"

int main(int argc, char **argv)
{
    setenv("LOGGER_LEVEL", "Disabled", false /* overwrite */);

    initialize_gtest(argc, argv);

    return RUN_ALL_TESTS();
}