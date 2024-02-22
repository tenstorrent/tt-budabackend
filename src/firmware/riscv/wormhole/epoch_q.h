// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#ifndef FW_COMPILE
#include "dram_address_map.h"
#include <stdexcept>
#endif

namespace epoch_queue {
/**
 * @brief Configuration parameters of the Epoch Queue on the "Silicon" device.
 */
    static constexpr std::int32_t EPOCH_Q_NUM_SLOTS = 256; // needs to match param with same name in ncrisc - parametrized by arch
    static constexpr std::int32_t EPOCH_Q_SLOT_SIZE = 64; // needs to match param with same name in ncrisc - parametrized by arch
    static constexpr std::int32_t GridSizeRow = 12;
    static constexpr std::int32_t GridSizeCol = 10;
    static constexpr std::int32_t EpochEndReached = 0xFFFFFFFF;

    static constexpr std::int32_t EPOCH_BIN_NUM_SLOTS = 32;   // used to store epoch binaries for epoch valid command
    static constexpr std::uint32_t POLLING_EPOCH_QUEUE_TAG = 0xACA0CAFE; // Special flag to disable l1wrptr shadow for epoch queue(s)

/** 
 * @brief Silicon device epoch queue command interpreted by NCRISC/ERISC FW.
 */
    enum EpochQueueCmd : uint32_t
    {
        EpochCmdValid = 0x1,
        EpochCmdNotValid = 0x2,
        EpochCmdIOQueueUpdate = 0x3,
        EpochCmdVarinst = 0x4,
        EpochCmdLoopStart = 0x5,
        EpochCmdLoopEnd = 0x6,
        EpochCmdEndProgram = 0xF,
    };

    struct IOQueueUpdateCmdInfo {
        uint64_t queue_header_addr;

        uint8_t num_buffers;
        uint8_t reader_index;
        uint8_t num_readers;
        // In full update mode: update_mask = 0xff
        uint8_t update_mask;

        uint32_t header[5]; // The first 5 words of the header
    };


    struct VarinstCmdInfo {
        // Word 0
        std::uint32_t dram_addr_lower       ;       // DRAM Address lower 32 bits. Like EpochCmdIOQueueUpdate could be queue header or binary queue. 
        // Word 1
        std::uint16_t dram_addr_upper       ;       // DRAM Address upper 16 bits.
        std::uint16_t dram_core_x           : 6 ;   // X core coordinate for target DRAM channel
        std::uint16_t dram_core_y           : 6 ;   // Y core coordinate for target DRAM channel
        std::uint16_t cmd_type              : 4 ;   // Set to EpochCmdVarInst for this command.
        // Word 2
        std::uint16_t num_buffers           : 8 ;   // Number of DRAM buffers to be updated
        std::uint16_t reader_index          : 8 ;   // Unique reader index for this core
        std::uint16_t num_readers           : 8 ;   // Total number of readers
        std::uint16_t __reserved_bits_2     : 8 ;

        // Word 3
        std::uint16_t update_mask           ;       // 16b mask to control which (up to 16) adjacent fields of specified size at target addr that the operation will be applied to.
        std::uint16_t opcode                : 3 ;   // Operation to run. (VarinstCmdOpcode)
        std::uint16_t field_size_bytes      : 3 ;   // Size of fields at target address in bytes that operation would be applied to. Maximum 4 bytes. 0 is invalid.
        std::uint16_t __reserved_bits_3     : 10 ;
        // Word 4
        std::uint32_t operand_0             ;       // First operand, eg. Amt when used with IncWrap
        // Word 5
        std::uint32_t operand_1             ;       // Second operand, eg. WrapAmt when used with IncWrap
        // Word 6
        std::uint16_t sync_loc_enable       : 1 ;   // Use reserved DRAM region for sync field location instead of queue_update_stride field position in Queue Header.
        std::uint16_t sync_loc_dram_core_x  : 6 ;   // X core coordinate for sync field location DRAM channel
        std::uint16_t sync_loc_dram_core_y  : 6 ;   // Y core coordinate for sync field location DRAM channel
        std::uint16_t __reserved_bits_6     : 3 ;
        std::uint16_t sync_loc_index        ;       // Indicates which sync field slot in reserved DRAM region by this cmd.
        // Word 7
        std::uint32_t __reserved_word_7     ;
    };

    // Used by FW to share common function for both flavors while not having to instantiate both structs.
    union EpochDramUpdateCmdInfo {
        IOQueueUpdateCmdInfo queue_update_info;
        VarinstCmdInfo varinst_cmd_info;
    };

    enum VarinstCmdOpcode {
    // enum class tt_epoch_cmd_varinst_opcode {
        VarinstCmdInvalid = 0x0,
        VarinstCmdIncWrap = 0x1,
        VarinstCmdInc = 0x2,
        VarinstCmdSet = 0x3,
        VarinstCmdAdd = 0x4,
        VarinstCmdMul = 0x5,
    };

    static constexpr std::int32_t EPOCH_Q_WRPTR_OFFSET = 4;
    static constexpr std::int32_t EPOCH_Q_RDPTR_OFFSET = 0;
    static constexpr std::int32_t EPOCH_Q_SLOTS_OFFSET = 64;

    static constexpr std::int32_t EPOCH_TABLE_ENTRY_SIZE_BYTES = EPOCH_Q_NUM_SLOTS*EPOCH_Q_SLOT_SIZE+EPOCH_Q_SLOTS_OFFSET;
    static constexpr std::int32_t QUEUE_UPDATE_BLOB_NUM_ENTRIES = 120;
    static constexpr std::int32_t QUEUE_UPDATE_BLOB_SIZE_BYTES = QUEUE_UPDATE_BLOB_NUM_ENTRIES * 8;

    static constexpr std::int32_t DRAM_EPOCH_METADATA_BASE = 6 * 1024 * 1024 + 100 * 1024;
    static constexpr std::int32_t DRAM_EPOCH_METADATA_LIMIT = 8 * 1024 * 1024;
    // Epoch queues start at DRAM_EPOCH_METADATA_LIMIT - sizeof(All epoch queues on the chip)
    static constexpr std::int32_t EPOCH_TABLE_DRAM_ADDR = DRAM_EPOCH_METADATA_LIMIT-GridSizeCol*GridSizeRow*EPOCH_TABLE_ENTRY_SIZE_BYTES;
    static constexpr std::int32_t EPOCH_ALLOC_QUEUE_SYNC_SLOTS = 256;
    static constexpr std::int32_t EPOCH_ALLOC_QUEUE_SYNC_ADDR = (EPOCH_TABLE_DRAM_ADDR) - (EPOCH_Q_SLOT_SIZE * EPOCH_ALLOC_QUEUE_SYNC_SLOTS);

    // Functions used to get dynamically programmed epoch_q_num_slots/epoch_bin_num_slots in runtime code
    // Don't use these in FW code, since they are not used and cause a build failure for NCRISC/ERISC
    #ifndef FW_COMPILE
    // Epoch command queue static asserts
    static_assert(EPOCH_TABLE_DRAM_ADDR % NOC_ADDRESS_ALIGNMENT == 0);
    static_assert(DRAM_EPOCH_METADATA_LIMIT % NOC_ADDRESS_ALIGNMENT == 0);
    // Epoch alloc queue static asserts
    static_assert(EPOCH_ALLOC_QUEUE_SYNC_ADDR >= DRAM_EPOCH_METADATA_BASE);
    static_assert(EPOCH_ALLOC_QUEUE_SYNC_ADDR % NOC_ADDRESS_ALIGNMENT == 0);
    static_assert(DRAM_EPOCH_METADATA_BASE ==  dram_mem::address_map::DRAM_EACH_BANK_CONST_BLOB_ADDR + dram_mem::address_map::DRAM_EACH_BANK_CONST_BLOB_SIZE);
    // If the number of slots used for external queue update blobs/allocate blobs is not specified, use the same num slots as epoch command queue
    static constexpr std::int32_t IO_QUEUE_UPDATE_NUM_SLOTS = EPOCH_Q_NUM_SLOTS;
    static std::int32_t get_epoch_q_num_slots() {
        if(std::getenv("TT_BACKEND_EPOCH_QUEUE_NUM_SLOTS")) {
            return atoi(std::getenv("TT_BACKEND_EPOCH_QUEUE_NUM_SLOTS"));
        }
        else {
            return EPOCH_Q_NUM_SLOTS;
        }
    }
    static std::int32_t get_epoch_table_entry_size_bytes() {
        return get_epoch_q_num_slots() * EPOCH_Q_SLOT_SIZE + EPOCH_Q_SLOTS_OFFSET;
    }
    static std::int32_t get_epoch_table_dram_addr() {
        return DRAM_EPOCH_METADATA_LIMIT - GridSizeCol * GridSizeRow * get_epoch_table_entry_size_bytes();
    }
    static int32_t get_epoch_alloc_queue_sync_addr() {
        int32_t addr =  get_epoch_table_dram_addr() - (EPOCH_Q_SLOT_SIZE * EPOCH_ALLOC_QUEUE_SYNC_SLOTS);
        if (addr < DRAM_EPOCH_METADATA_BASE) {
            throw std::runtime_error("Epoch alloc queue sync addr must be above DRAM_EPOCH_METADATA_BASE");
        }
        return addr;
    }
    static std::int32_t get_epoch_bin_num_slots() {
        if(std::getenv("TT_BACKEND_EPOCH_BIN_NUM_SLOTS")) {
            return atoi(std::getenv("TT_BACKEND_EPOCH_BIN_NUM_SLOTS"));
        }
        return EPOCH_BIN_NUM_SLOTS;
    }
    static std::int32_t get_epoch_io_queue_update_num_slots() {
        if(std::getenv("TT_BACKEND_IO_QUEUE_UPDATE_NUM_SLOTS")) {
            return atoi(std::getenv("TT_BACKEND_IO_QUEUE_UPDATE_NUM_SLOTS"));
        }
        return IO_QUEUE_UPDATE_NUM_SLOTS;
    }
    static std::int32_t get_queue_update_blob_num_entries() {
        if(std::getenv("TT_BACKEND_QUEUE_UPDATE_BLOB_NUM_ENTRIES")) {
            return atoi(std::getenv("TT_BACKEND_QUEUE_UPDATE_BLOB_NUM_ENTRIES"));
        }
        return QUEUE_UPDATE_BLOB_NUM_ENTRIES;
    }
    static std::int32_t get_queue_update_blob_size_bytes() {
        // Align this up to 64 Bytes (should be aligned with default constant values but may not be the case when these values are configured by env vars)
        std::int32_t queue_update_blob_size = get_queue_update_blob_num_entries() * 8;
        return (queue_update_blob_size % 64 == 0) ? queue_update_blob_size : (queue_update_blob_size + 64 - (queue_update_blob_size % 64));
    }
    #endif
    
    typedef struct EpochQHeader {
        uint32_t rd_ptr;
        uint32_t wr_ptr;
        uint32_t padding[6];
    } EpochQHeader_t;

} // namespace epoch_queue
