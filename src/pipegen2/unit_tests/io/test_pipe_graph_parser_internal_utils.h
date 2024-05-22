// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
// clang-format off
#include "model/pipe_graph/pg_buffer.h"
#include "model/pipe_graph/pg_pipe.h"

#include <gtest/gtest.h>
// clang-format on

using namespace pipegen2;

// Compares that two pipes are the same by comparing their attributes.
void compare_pipe_attributes(const PGPipe& pipe1, const PGPipe& pipe2);

// Compares that two buffers are the same by comparing their attributes.
void compare_buffer_attributes(const PGBuffer& buffer1, const PGBuffer& buffer2);