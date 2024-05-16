// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "risc_epoch.h"
#ifdef PERF_DUMP
#include "risc_perf.h"
#endif
#ifndef ERISC
#include "context.h"
#endif
#include "tt_log.h"

// manages the epoch loading:
// loads the epoch_t structure from dram
// loads the kernels from dram
// initializes ncrisc, once epoch_t structure is loaded, copies whatever data is in L1 that needs to be reused into local data memory
// process dram_reads/writes until brisc tells you are done
// handles epilogue
void run_epoch(
    void (*risc_epoch_load)(uint64_t), void (*risc_kernels_load)(uint64_t), void (*risc_extra_overlay_blob_load)(uint64_t, uint32_t), void (*init_ncrisc_streams)(void*, uint32_t),
    bool skip_initial_epoch_dram_load, uint64_t dram_next_epoch_ptr, uint64_t dram_next_epoch_triscs_ptr, bool& skip_kernels,
#ifdef RISC_GSYNC_ENABLED
    volatile uint32_t &gsync_epoch, volatile uint32_t &epochs_in_progress,
#endif
#ifdef ERISC
    volatile uint32_t &epoch_id_to_send, volatile uint32_t &other_chip_epoch_id,
    uint32_t &num_input_streams, kernel_input_stream_state_t *input_stream_state,
    uint32_t &num_output_streams, kernel_output_stream_state_t *output_stream_state,
 #endif
    uint32_t &num_dram_input_streams, uint32_t &num_dram_output_streams, uint32_t &num_active_streams, uint32_t &num_active_dram_queues, uint32_t &num_dram_prefetch_streams,
    dram_q_state_t tt_l1_ptr *dram_q_state, dram_input_stream_state_t *dram_input_stream_state, dram_output_stream_state_t *dram_output_stream_state, active_stream_info_t tt_l1_ptr *active_stream_info,
    epoch_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_epoch_stream_info, active_stream_info_t tt_l1_ptr * tt_l1_ptr *dram_prefetch_active_stream_info, uint32_t dram_decouple_mask
) {
#ifdef PERF_DUMP
    risc::record_timestamp_at_offset(risc::perf_event::EPOCH, risc::EPOCH_START_OFFSET);
#endif
    if (!skip_initial_epoch_dram_load) {
#ifndef PERF_DUMP
        RISC_POST_STATUS(0x10000002);
#endif
        // loads epoch from dram
        risc_epoch_load(dram_next_epoch_ptr);
        uint32_t overlay_blob_extra_size = RISC_EPOCH_INFO_PTR->overlay_blob_extra_size;
        if (overlay_blob_extra_size > 0)
          risc_extra_overlay_blob_load(dram_next_epoch_ptr, overlay_blob_extra_size);
#ifndef ERISC
        // loads kernels (if needed) from dram
        skip_kernels = RISC_EPOCH_INFO_PTR->skip_kernels;

        if (!skip_kernels)
          risc_kernels_load(dram_next_epoch_triscs_ptr);
#endif
    }

    RISC_EPOCH_INFO_PTR->end_program = 0;
    if (RISC_EPOCH_INFO_PTR->overlay_valid) {
#ifdef PERF_DUMP
        call_with_cpu_flush((void *)risc::init_perf_dram_state, 0);
#endif
#if defined(ERISC) || defined(RISC_B0_HW)
        /*For Erisc init_ncrisc_streams is a pointer to init_erisc_streams()*/
        init_ncrisc_streams(0, dram_decouple_mask);
#else
        call_with_cpu_flush((void *)init_ncrisc_streams, (void *)dram_decouple_mask);
#endif

    // handling stream initialization 
#if defined(ERISC) || defined(RISC_B0_HW)
        risc_dram_stream_handler_init_l1(
            0,
#ifdef RISC_GSYNC_ENABLED
            gsync_epoch, epochs_in_progress,
#endif
#ifdef ERISC
            epoch_id_to_send, other_chip_epoch_id,
#endif
            num_dram_input_streams, num_dram_output_streams, num_active_streams, num_active_dram_queues, num_dram_prefetch_streams,
            dram_q_state, dram_input_stream_state, dram_output_stream_state, active_stream_info,
            (epoch_stream_info_t**)dram_prefetch_epoch_stream_info, (active_stream_info_t**)dram_prefetch_active_stream_info
        );
#ifdef ERISC
        //initialize input/output streams for Data Copy Op.
        risc_unpack_pack_stream_handler_init(
          num_input_streams, input_stream_state,
          num_output_streams, output_stream_state
        );
#endif
#else
        call_with_cpu_flush_args((void *)risc_dram_stream_handler_init_l1,
#ifdef RISC_GSYNC_ENABLED
          (void *) &gsync_epoch, (void *) &epochs_in_progress,
#endif
          (void *) &num_dram_input_streams, (void *) &num_dram_output_streams, (void *) &num_active_streams, (void *) &num_active_dram_queues, (void *) &num_dram_prefetch_streams,
          (void *) dram_q_state, (void *) dram_input_stream_state, (void *) dram_output_stream_state, (void *) active_stream_info,
          (void *) dram_prefetch_epoch_stream_info, (void *) dram_prefetch_active_stream_info
        );
#endif

#ifndef ERISC
        if (!skip_kernels)
          deassert_trisc_reset();
#endif

        // dram reads/writes until brisc tells you are done
        risc_dram_stream_handler_loop(
#ifdef RISC_GSYNC_ENABLED
          gsync_epoch, epochs_in_progress,
#endif
          num_dram_input_streams, num_dram_output_streams, num_active_streams, num_active_dram_queues, num_dram_prefetch_streams,
          dram_q_state, dram_input_stream_state, dram_output_stream_state, active_stream_info,
          dram_prefetch_epoch_stream_info, dram_prefetch_active_stream_info
#ifdef ERISC
          //Erisc needs to run input/output stream loops for Data Copy Op.
          ,
          num_input_streams, input_stream_state,
          num_output_streams, output_stream_state
#endif
        );

        // execute epilogue
#if defined(ERISC) || defined(RISC_B0_HW)
        risc_dram_stream_handler_epilogue_l1(
            0,
#ifdef RISC_GSYNC_ENABLED
            gsync_epoch, epochs_in_progress,
#endif
            num_dram_input_streams, num_dram_output_streams, num_active_streams, num_active_dram_queues, num_dram_prefetch_streams,
            dram_q_state, dram_input_stream_state, dram_output_stream_state, active_stream_info,
            dram_prefetch_epoch_stream_info, dram_prefetch_active_stream_info
        );
#else
        call_with_cpu_flush_args((void *)risc_dram_stream_handler_epilogue_l1,
#ifdef RISC_GSYNC_ENABLED
          (void *) &gsync_epoch, (void *) &epochs_in_progress,
#endif
          (void *) &num_dram_input_streams, (void *) &num_dram_output_streams, (void *) &num_active_streams, (void *) &num_active_dram_queues, (void *) &num_dram_prefetch_streams,
          (void *) dram_q_state, (void *) dram_input_stream_state, (void *) dram_output_stream_state, (void *) active_stream_info,
          (void *) dram_prefetch_epoch_stream_info, (void *) dram_prefetch_active_stream_info
        );
#endif

#ifndef ERISC
        if (!skip_kernels)
          assert_trisc_reset();
#endif
#ifdef PERF_DUMP
        risc::record_timestamp_at_offset(risc::perf_event::EPOCH, risc::EPOCH_END_OFFSET);
        call_with_cpu_flush((void *)risc::record_perf_dump_end, 0);
#endif
    }
}

// Function for processing epoch management
// Uses NOC to load data from DRAM
void run_dram_queue_update(
    void * pFunction, volatile uint32_t *noc_read_scratch_buf, uint64_t& my_q_table_offset, uint32_t& my_q_rd_ptr, uint64_t& dram_next_epoch_ptr, uint8_t& loading_noc, bool &varinst_cmd, uint32_t &loop_iteration
) {
    epoch_queue::EpochDramUpdateCmdInfo cmd;

    // Desire to re-use this code mostly as-is for existing EpochCmdIOQueueUpdate and new EpochCmdVarinst which are very similar in what they do.
    // EpochCmdVarinst will share same synchronization behavior but do RMW on DRAM instead of straight replace of header contents. And, the
    // new command is in it's own struct with different set of fields. Didn't want to template this function which would cause code size to grow, 
    // so instead pull out the common variables between the two cmd/struct types in union here, and set them at runtime based on which cmd is used.
    uint64_t cmd_info_queue_header_addr;
    uint16_t cmd_info_num_buffers;
    uint8_t cmd_info_num_readers;
    uint8_t cmd_info_reader_index;

    if (varinst_cmd){
        risc_get_epoch_varinst_info(cmd.varinst_cmd_info, noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, dram_next_epoch_ptr);
        // Hacky: reconstruct 64b addr+dram_cores from bitfield struct to reduce FW code changes.
        cmd_info_queue_header_addr  = (cmd.varinst_cmd_info.dram_addr_lower & 0xFFFFFFFF);
        cmd_info_queue_header_addr |= static_cast<uint64_t>(cmd.varinst_cmd_info.dram_addr_upper & 0xFFFF) << 32;
        cmd_info_queue_header_addr |= static_cast<uint64_t>(cmd.varinst_cmd_info.dram_core_x & 0x3F) << 48;
        cmd_info_queue_header_addr |= static_cast<uint64_t>(cmd.varinst_cmd_info.dram_core_y & 0x3F) << 54;
        cmd_info_num_buffers        = cmd.varinst_cmd_info.num_buffers;
        cmd_info_num_readers        = cmd.varinst_cmd_info.num_readers;
        cmd_info_reader_index       = cmd.varinst_cmd_info.reader_index;

        // Set/Add/Mul instructions (since a constant operand are used) only make sense to execute once on first iteration. Save time.
        if ((cmd.varinst_cmd_info.opcode == epoch_queue::VarinstCmdSet || 
             cmd.varinst_cmd_info.opcode == epoch_queue::VarinstCmdAdd || 
             cmd.varinst_cmd_info.opcode == epoch_queue::VarinstCmdMul) && loop_iteration != 0){
            return;
        }

    } else{

        risc_get_epoch_update_info(cmd.queue_update_info, noc_read_scratch_buf, my_q_table_offset, my_q_rd_ptr, dram_next_epoch_ptr);
        cmd_info_queue_header_addr  = cmd.queue_update_info.queue_header_addr;
        cmd_info_num_buffers        = cmd.queue_update_info.num_buffers;
        cmd_info_num_readers        = cmd.queue_update_info.num_readers;
        cmd_info_reader_index       = cmd.queue_update_info.reader_index;
    }

    bool allocate_queue = !varinst_cmd && (cmd.queue_update_info.update_mask == 0);
    bool single_sync_mode = allocate_queue || (varinst_cmd && cmd.varinst_cmd_info.sync_loc_enable);

    // When in device loop, allow queue updates only on first loop, unless allocate (global sync) cmd.
    if (!varinst_cmd && !allocate_queue && loop_iteration != 0) {
        return;
    }

    if (cmd_info_num_buffers > 1) {
        uint64_t dram_addr_offset;
        uint32_t dram_coord_x;
        uint32_t dram_coord_y;
        risc_get_noc_addr_from_dram_ptr_l1_with_l1_ptr((volatile uint32_t tt_l1_ptr *)(&(cmd_info_queue_header_addr)), dram_addr_offset, dram_coord_x, dram_coord_y);
        uint64_t queue_addr_ptr = NOC_XY_ADDR(NOC_X(dram_coord_x), NOC_Y(dram_coord_y), dram_addr_offset);
        ncrisc_noc_fast_read_any_len_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, queue_addr_ptr, 
#ifdef ERISC
                                        eth_l1_mem::address_map::OVERLAY_BLOB_BASE + sizeof(epoch_t), 
#else
                                        l1_mem::address_map::OVERLAY_BLOB_BASE + sizeof(epoch_t), 
#endif
                                        cmd_info_num_buffers*8, NCRISC_HEADER_RD_TRID);
    }

#ifdef ERISC
    uint32_t header_addr = eth_l1_mem::address_map::OVERLAY_BLOB_BASE + sizeof(epoch_t) +  MAX_DRAM_QUEUES_TO_UPDATE*8 + MAX_DRAM_QUEUES_TO_UPDATE*sizeof(dram_io_state_t);
#else
    uint32_t header_addr = l1_mem::address_map::OVERLAY_BLOB_BASE + sizeof(epoch_t) +  MAX_DRAM_QUEUES_TO_UPDATE*8 + MAX_DRAM_QUEUES_TO_UPDATE*sizeof(dram_io_state_t);
#endif  
 
    volatile uint32_t tt_l1_ptr *header_addr_ptr = (volatile uint32_t tt_l1_ptr *)header_addr;


    if (allocate_queue) {
        header_addr_ptr[0] = 0;
        header_addr_ptr[1] = 0;
        header_addr_ptr[2] = 0;
        header_addr_ptr[3] = 0;
        header_addr_ptr[4] = 0;
    } else if (!varinst_cmd){
        header_addr_ptr[0] = cmd.queue_update_info.header[0];
        header_addr_ptr[1] = cmd.queue_update_info.header[1];
        header_addr_ptr[2] = cmd.queue_update_info.header[2];
        header_addr_ptr[3] = cmd.queue_update_info.header[3];
        header_addr_ptr[4] = cmd.queue_update_info.header[4];
    }
    header_addr_ptr[5] = 0;
    header_addr_ptr[6] = 0;
    header_addr_ptr[7] = 0;

    uint8_t state[MAX_DRAM_QUEUES_TO_UPDATE-1];
    for (uint32_t i = 0; i < cmd_info_num_buffers; i++) {
      state[i] = 0;
    }

    bool all_done = false;
    while (!all_done) {
        all_done = true;

#ifdef ERISC
        // Avoid deadlock of eth routing core getting stuck in sync loop
        risc_context_switch();
#endif
        while (!ncrisc_noc_reads_flushed_l1(loading_noc, NCRISC_HEADER_RD_TRID));
        while (!ncrisc_noc_nonposted_writes_flushed_l1(loading_noc, NCRISC_WR_DEF_TRID));

        for (int k = 0; k < cmd_info_num_buffers; k++) {
            if (state[k] == 4)
                continue;

            if (single_sync_mode) {
                if (k > 0) {  // skip since remaining are allocated all at once
                    state[k] = state[0];
                    continue;
                }
            }

    #ifdef ERISC
            volatile uint64_t tt_l1_ptr * queue_addr_l1 = (volatile uint64_t tt_l1_ptr *)(eth_l1_mem::address_map::OVERLAY_BLOB_BASE + sizeof(epoch_t));
    #else
            volatile uint64_t tt_l1_ptr * queue_addr_l1 = (volatile uint64_t tt_l1_ptr *)(l1_mem::address_map::OVERLAY_BLOB_BASE + sizeof(epoch_t));
    #endif

            bool has_multi_buffers = cmd_info_num_buffers > 1;

            uint64_t dram_addr_offset;
            uint32_t dram_coord_x;
            uint32_t dram_coord_y;
            if (has_multi_buffers) {
                volatile uint32_t tt_l1_ptr *queue_addr = (volatile uint32_t tt_l1_ptr *)&queue_addr_l1[k];
                risc_get_noc_addr_from_dram_ptr_l1_with_l1_ptr(queue_addr, dram_addr_offset, dram_coord_x, dram_coord_y);
            } else {
                volatile uint32_t *queue_addr = (uint32_t *)&(cmd_info_queue_header_addr);
                risc_get_noc_addr_from_dram_ptr(queue_addr, dram_addr_offset, dram_coord_x, dram_coord_y);
            }

            uint64_t queue_addr_ptr = NOC_XY_ADDR(NOC_X(dram_coord_x), NOC_Y(dram_coord_y), dram_addr_offset);
            uint64_t queue_sync_ptr;  // location used for stride sync
            if (allocate_queue) {
                queue_sync_ptr = NOC_XY_ADDR(
                    NOC_X(cmd.queue_update_info.header[0]),
                    NOC_Y(cmd.queue_update_info.header[1]),
                    EPOCH_ALLOC_QUEUE_SYNC_ADDRESS + cmd.queue_update_info.header[2] * epoch_queue::EPOCH_Q_SLOT_SIZE);
            } else if (varinst_cmd && cmd.varinst_cmd_info.sync_loc_enable){
                queue_sync_ptr = NOC_XY_ADDR(
                    NOC_X(cmd.varinst_cmd_info.sync_loc_dram_core_x),
                    NOC_Y(cmd.varinst_cmd_info.sync_loc_dram_core_y),
                    EPOCH_ALLOC_QUEUE_SYNC_ADDRESS + cmd.varinst_cmd_info.sync_loc_index * epoch_queue::EPOCH_Q_SLOT_SIZE);
            }else{
                queue_sync_ptr = queue_addr_ptr;
            }
            uint32_t l1_ptr_addr = ((uint32_t)queue_addr_l1) + MAX_DRAM_QUEUES_TO_UPDATE*8 + k*sizeof(dram_io_state_t);
            volatile dram_io_state_t tt_l1_ptr *l1_ptrs = (volatile dram_io_state_t tt_l1_ptr *)l1_ptr_addr;

            if (state[k] == 0 || state[k] == 2) {
                RISC_POST_STATUS(0x11D00001);
                while (!ncrisc_noc_fast_read_ok_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF));
                ncrisc_noc_fast_read_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, queue_sync_ptr, l1_ptr_addr, DRAM_HEADER_SIZE, NCRISC_HEADER_RD_TRID);
                /*
                *((volatile int *)0x8) = queue_sync_ptr;
                *((volatile int *)0xc) = queue_sync_ptr>>32;
                *((volatile int *)0x10) = l1_ptr_addr;
                *((volatile int *)0x14) = allocate_queue;
                *((volatile int *)0x18) = has_multi_buffers;
                *((volatile int *)0x1c) = cmd.queue_update_info.reader_index;
                *((volatile int *)0x20) = cmd.queue_update_info.num_readers;
                */
                if (state[k] == 2)
                    state[k] = 3;
                else
                    state[k] = 1;
                all_done = false;
            } else if (state[k] == 1) {
                RISC_POST_STATUS(0x11D00002);
                uint32_t total_readers = cmd_info_num_readers;
                bool has_multi_readers = total_readers > 1;
                uint32_t reader_index = cmd_info_reader_index;
                uint32_t rd_stride = l1_ptrs->dram_to_l1.rd_queue_update_stride; 

                if (!has_multi_readers || reader_index == rd_stride) {
                    if (!has_multi_readers || reader_index == total_readers-1) {
                        if (varinst_cmd){
                            run_varinst_cmd_dram_rmw(cmd.varinst_cmd_info, queue_addr_ptr, noc_read_scratch_buf);
                        }else{
                            if (allocate_queue || cmd.queue_update_info.update_mask == 0xff) {
                                while (!ncrisc_noc_fast_write_ok_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF));
                                ncrisc_noc_fast_write_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(header_addr_ptr[0])), queue_addr_ptr, DRAM_HEADER_SIZE,
                                                        DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
                            } else {
                                for (int m = 0; m < 8; m++) {
                                    bool update_word = (cmd.queue_update_info.update_mask >> m) & 0x1;
                                    if (update_word) {
                                        while (!ncrisc_noc_fast_write_ok_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF));
                                        ncrisc_noc_fast_write_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(header_addr_ptr[m])), queue_addr_ptr + 4*m, 4,
                                                            DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
                                    }
                                }
                            }
                        }
                        if (single_sync_mode) {
                            if (has_multi_buffers) {
                                // allocate remaining buffers as well
                                for (int b = 1; b < cmd_info_num_buffers; b++) {
                                    volatile uint32_t tt_l1_ptr *queue_addr = (uint32_t tt_l1_ptr *)&queue_addr_l1[b];
                                    risc_get_noc_addr_from_dram_ptr_l1_with_l1_ptr(queue_addr, dram_addr_offset, dram_coord_x, dram_coord_y);
                                    queue_addr_ptr = NOC_XY_ADDR(NOC_X(dram_coord_x), NOC_Y(dram_coord_y), dram_addr_offset);
                                    while (!ncrisc_noc_fast_write_ok_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF));
                                    if (varinst_cmd){
                                        run_varinst_cmd_dram_rmw(cmd.varinst_cmd_info, queue_addr_ptr, noc_read_scratch_buf);
                                    }else{
                                        ncrisc_noc_fast_write_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(header_addr_ptr[0])), queue_addr_ptr, DRAM_HEADER_SIZE,
                                                                DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
                                    }
                                }
                            }
                            while (!ncrisc_noc_nonposted_writes_flushed_l1(loading_noc, NCRISC_WR_DEF_TRID));
                        }
                    }

                    if (has_multi_readers) {
                        if (reader_index == total_readers-1) {
                            l1_ptrs->l1_to_dram.wr_queue_update_stride = DRAM_STRIDE_WRAP_BIT + 0;
                            l1_ptrs->dram_to_l1.rd_queue_update_stride = DRAM_STRIDE_WRAP_BIT + 0;
                        } else {
                            l1_ptrs->l1_to_dram.wr_queue_update_stride = reader_index + 1;
                            l1_ptrs->dram_to_l1.rd_queue_update_stride = reader_index + 1;
                        }
                        // Reg poll loop, flushed immediately
                        while (!ncrisc_noc_fast_write_ok_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF));
                        ncrisc_noc_fast_write_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_queue_update_stride)), queue_sync_ptr+DRAM_BUF_QUEUE_UPDATE_STRIDE_OFFSET, 2,
                                              DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
                    }

                    state[k] = 2;
                    all_done = false;
                } else {
                    state[k] = 0;
                    all_done = false;
                    continue;
                }
            } else if (state[k] == 3) {
                RISC_POST_STATUS(0x11D00003);
                uint32_t total_readers = cmd_info_num_readers;
                bool has_multi_readers = total_readers > 1;
                uint32_t reader_index = cmd_info_reader_index;
                uint32_t rd_stride = l1_ptrs->dram_to_l1.rd_queue_update_stride;

                if (!has_multi_readers || (reader_index + DRAM_STRIDE_WRAP_BIT) == rd_stride) {
                    if (has_multi_readers) {
                        if (reader_index == total_readers-1) {
                            l1_ptrs->l1_to_dram.wr_queue_update_stride = 0;
                            l1_ptrs->dram_to_l1.rd_queue_update_stride = 0;
                        } else {
                            l1_ptrs->l1_to_dram.wr_queue_update_stride = DRAM_STRIDE_WRAP_BIT + reader_index + 1;
                            l1_ptrs->dram_to_l1.rd_queue_update_stride = DRAM_STRIDE_WRAP_BIT + reader_index + 1;
                        }
                        // Reg poll loop, flushed immediately
                        while (!ncrisc_noc_fast_write_ok_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF));
                        ncrisc_noc_fast_write_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(l1_ptrs->l1_to_dram.wr_queue_update_stride)), queue_sync_ptr+DRAM_BUF_QUEUE_UPDATE_STRIDE_OFFSET, 2,
                                              DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
                    }

                    state[k] = 4;
                } else {
                    state[k] = 2;
                    all_done = false;
                    continue;
                }
            }
        }

    }

    while (!ncrisc_noc_reads_flushed_l1(loading_noc, NCRISC_HEADER_RD_TRID));
    while (!ncrisc_noc_nonposted_writes_flushed_l1(loading_noc, NCRISC_WR_DEF_TRID));
}

// Execute the opcode w/ operands. for EpochCmdVarinst on configurable size and nubmer of fields offset from a DRAM address.
void run_varinst_cmd_dram_rmw(epoch_queue::VarinstCmdInfo &varinst_cmd_info, uint64_t &queue_addr_ptr, volatile uint32_t *noc_read_scratch_buf){
    // Read the 64 Byte aligned header into the 64 Byte aligned noc_read_scratch_buf
    const int num_varinst_words = 16; // rename, make it something to do with words per txn or something.
    const int read_size         = num_varinst_words * sizeof(uint32_t);
    const int write_size        = sizeof(uint32_t);

    int field_offset_1B = 0;

    // Step 1 : Read 64B chunk from DRAM to L1 and wait for read to complete. This is safe, since we're reading from a 64B aligned address into a 64B aligned address
    ncrisc_noc_fast_read_any_len_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, queue_addr_ptr, (uint32_t)(noc_read_scratch_buf), read_size, NCRISC_HEADER_RD_TRID);
    while (!ncrisc_noc_reads_flushed_l1(loading_noc, NCRISC_HEADER_RD_TRID));

    // Step 2 : Modify appropriate fields (up to 16 per 64B chunk) of configurable size.
    for (int i = 0; i < 16; i++) {

        bool update_word = (varinst_cmd_info.update_mask >> i) & 0x1;
        if (update_word) {

            uint16_t field_offset_4B        = field_offset_1B >> 2;
            uint32_t field_shift_amt        = (field_offset_1B << 3) & 0x1F; // Number of bits to shift by into 4B word to reach field.
            uint32_t field_mask_4B          = varinst_cmd_info.field_size_bytes == 2 ? 0xFFFF : varinst_cmd_info.field_size_bytes == 1 ? 0xFF : 0xFFFFFFFFF;
            uint32_t field_mask_shifted_4B  = field_mask_4B << field_shift_amt;

            // TODO - Could improve by avoiding datacopies, and using references.
            uint32_t rd_val_field = (noc_read_scratch_buf[field_offset_4B] & field_mask_shifted_4B) >> field_shift_amt;
            uint32_t wr_val_field = 0;

            if (varinst_cmd_info.opcode == epoch_queue::VarinstCmdIncWrap){
                // Can't use modulo, do wrapping manually. This isn't efficient for the case where existing data is 
                // many times larget than wrap value. Could be optimized, but this shouldn't be common.
                wr_val_field = rd_val_field + varinst_cmd_info.operand_0;
                while (wr_val_field >= varinst_cmd_info.operand_1) {
                    wr_val_field -= varinst_cmd_info.operand_1;
                }
            }else if (varinst_cmd_info.opcode == epoch_queue::VarinstCmdInc){
                wr_val_field = rd_val_field + varinst_cmd_info.operand_0;
            }else if (varinst_cmd_info.opcode == epoch_queue::VarinstCmdSet){
                wr_val_field = varinst_cmd_info.operand_0;
            }else if (varinst_cmd_info.opcode == epoch_queue::VarinstCmdAdd){
                wr_val_field = varinst_cmd_info.operand_0 + varinst_cmd_info.operand_1;
            }else if (varinst_cmd_info.opcode == epoch_queue::VarinstCmdMul){
                // Multiply operator not available. This seems to work...
                for (uint32_t j = 0; j < 32; ++j) {
                    if (varinst_cmd_info.operand_1 & (1u << j)) {
                        wr_val_field += (varinst_cmd_info.operand_0 << j);
                    }
                }
            }

            noc_read_scratch_buf[field_offset_4B] = noc_read_scratch_buf[field_offset_4B] & ~field_mask_shifted_4B;
            noc_read_scratch_buf[field_offset_4B] = noc_read_scratch_buf[field_offset_4B] | ((wr_val_field & field_mask_4B) << field_shift_amt);
            // TT_LOG("run_varinst_cmd_dram_rmw queue_addr_ptr: 0x{:x} field_size_bytes: {} num_buffers: {} num_readers: {} reader_index: {} => updated i: {} opcode: {} rd_val_field: 0x{:x} wr_val_field: 0x{:x}",
            // queue_addr_ptr, varinst_cmd_info.field_size_bytes, varinst_cmd_info.num_buffers, varinst_cmd_info.num_readers, varinst_cmd_info.reader_index, i, varinst_cmd_info.opcode, rd_val_field, wr_val_field);

            // Step 3 : Write updated fields back to DRAM. Not entire 64B chunk, but just updated fields, one at a time. Since the L1 src address has the same relative addr as the DRAM dest addr (wrt a 64B alignment, this is safe) 
            // This is a bit dangerous, write back entire 4B word that may have been partially (1B or 2B) updated. It works to solve Rdptr/Wrptr modification race though since
            // they are in different 4B words.
            while (!ncrisc_noc_fast_write_ok_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF));
            ncrisc_noc_fast_write_l1(loading_noc, NCRISC_SMALL_TXN_CMD_BUF, (uint32_t)(&(noc_read_scratch_buf[field_offset_4B])), queue_addr_ptr + field_offset_1B, write_size,
                                    DRAM_PTR_UPDATE_VC, false, false, 1, NCRISC_WR_DEF_TRID);
        }

        field_offset_1B += varinst_cmd_info.field_size_bytes;
    }
}