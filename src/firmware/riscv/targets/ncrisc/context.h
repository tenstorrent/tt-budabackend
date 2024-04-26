// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#ifndef _CONTEXT_H_
#define _CONTEXT_H_

#include <stdint.h>

//context size is 28 cpu registers.
//We do not save X0 which is 0
//We do not save SP which is X2. SP is saved in ContextInfo_t.
//We do not save GP which is X3.
//We do not save TP which is X4.
#define CONTEXT_SIZE ( 28 * 4 )
#define CONTEXT_COUNT ( 28 )

void init_riscv_context();

#ifdef __cplusplus
extern "C" {
#endif

extern void call_with_cpu_flush_args(
  void * pFunction,
#ifdef RISC_GSYNC_ENABLED
  void *gsync_epoch, void *epochs_in_progress,
#endif
  void *num_dram_input_streams, void *num_dram_output_streams, void *num_active_streams, void *num_active_dram_queues, void *num_dram_prefetch_streams,
  void *dram_q_state, void *dram_input_stream_state, void *dram_output_stream_state, void *active_stream_info,
  void *dram_prefetch_epoch_stream_info, void *dram_prefetch_active_stream_info
);

extern void call_with_cpu_flush_args2(
  void * pFunction,
  void *noc_read_scratch_buf, void *my_q_table_offset, void *my_q_rd_ptr, void *dram_next_epoch_ptr, void *loading_noc, void *varinst_cmd, void *loop_iteration
);

extern void call_with_cpu_flush_args3(
  void * pFunction,
  void *stream_id, void *phase_active, void *curr_phase_tiles_remaining_to_issue, void *curr_dram_input_stream_state, void *next_dram_q_issue,
  void *epoch_q_slots_remaining
);

extern void call_with_cpu_flush_args4(
  void * pFunction,
  void * num_active_dram_queues, void * dram_q_state, void * check_ptrs_flushed, void * all_ptrs_flushed
);

extern void call_with_cpu_flush(void *pFunction, void *dram_decouple_mask);
//extern void context_save(void);
extern void __attribute__((section("code_l1"))) context_restore(void);

#ifdef __cplusplus
}
#endif

#endif
