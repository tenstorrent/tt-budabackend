// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _RTOS_H_
#define _RTOS_H_

#include <stdint.h>

#define RTOS_MAX_TASKS 4
#define MAIN_TASK_INDEX 0
#define TASK_1_INDEX 1
#define TASK_2_INDEX 2
#define TASK_3_INDEX 3
#define STACK_TOP 0xFFB01000
#define STACK_SIZE 1024
//context size is 28 cpu register.
//We do not save X0 which is 0
//We do not save GP which is X3. GP is never modified once initialized in the code.
//We do not save SP which is X2. SP is saved in TaskInfo_t.
//We do not save TP which is X4.
#define TASK_CONTEXT_SIZE ( 28 * 4 )
#define TASK_CONTEXT_COUNT ( 28 )

void rtosAddTask( void * task_func, uint32_t stack_ptr, uint8_t task_index, uint8_t priority) __attribute__((noinline));

#ifdef __cplusplus
extern "C" {
#endif

void RtosScheduler(void);
extern void rtos_context_switch(void);

#ifdef __cplusplus
}
#endif

#endif
