// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef PERF_DUMP
#include <l1_address_map.h>

#include "perf_events_target_inputs.h"
#include "perf_lib/scratch_api.h"

#ifndef INTERMED_DUMP
#define INTERMED_DUMP 0
#endif

#ifndef PERF_DUMP_CONCURRENT
#define PERF_DUMP_CONCURRENT 0
#endif

#pragma GCC diagnostic ignored "-Wunused-function"

static constexpr uint32_t PERF_DUMP_END_SIGNAL = 0xbeeff00d;
static constexpr uint32_t PERF_CNT_DUMP_ENTRY_SIZE = 16; // Entry size in bytes

#if PERF_DUMP_LEVEL == 0
static constexpr int32_t TRISC_PERF_BUF_SIZE = l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_0;
#else
static constexpr int32_t TRISC_PERF_BUF_SIZE = l1_mem::address_map::UNPACK_PACK_PERF_BUF_SIZE_LEVEL_1;
#endif

#endif