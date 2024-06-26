// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include <stddef.h>
#include "context.h"

typedef struct ContextInfo {
	    uint32_t *Sp;
	        void *FuncPtr;
} ContextInfo_t;

ContextInfo_t riscvContext __attribute__((section ("data_noinit")));
ContextInfo_t *pContext __attribute__((section ("data_noinit")));


void init_riscv_context()
{
	    pContext = &riscvContext;
}
