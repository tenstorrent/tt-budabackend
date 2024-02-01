// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _PKERNELS_H_
#define _PKERNELS_H_

#include "tensix.h"
#include "noc_parameters.h"
#include "risc_attribs.h"
#include "epoch.h"
#include "stream_interface.h"

extern "C" {
void __attribute__((interrupt)) __attribute__((used)) __attribute__((noinline)) pkernel_main();
}

#endif
