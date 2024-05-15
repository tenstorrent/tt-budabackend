// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "runtime.hpp"
#include "verif.hpp"
#include "epoch_q.h"

typedef std::pair<tt_target_dram, uint64_t> target_dram_addr;

struct ExternalUpdateBufferListInfo{
    std::vector<tt_queue_allocation_info> alloc_infos;  // List of Buffer DRAM Ch, Addr
    tt_target_dram dram_target;                         // DRAM Device, Ch, Subch of where this list is placed in DRAM 
    uint64_t dram_addr;                                 // DRAM Addr of where this list is placed in DRAM.
    bool skip_send;
};

struct VarInstCmdAndBinaries{
    epoch_queue::VarinstCmdInfo cmd_info;
    ExternalUpdateBufferListInfo ext_binary_info;
    std::set<tt_cxy_pair> unique_readers;
};


uint32_t get_32b_mask(uint16_t field_size_bytes);
void verify_supported_epoch_cmds(std::vector<VarInstCmdAndBinaries> &varinst_cmds_binaries);
void send_epoch_cmds_to_device(tt_runtime &runtime, std::vector<VarInstCmdAndBinaries> &varinst_cmds_binaries, int device_id);
void compute_expected_output(tt_runtime &runtime, std::vector<VarInstCmdAndBinaries> &varinst_cmds_binaries, int device_id, std::map<target_dram_addr, uint32_t> &dram_model);
bool compare_with_device(tt_runtime &runtime, std::map<target_dram_addr, uint32_t> &dram_model);
tt_target_dram get_target_dram_from_core(const buda_SocDescriptor *sdesc, int device_id, tt_xy_pair core);
void initialize_device_dram(tt_runtime &runtime, std::map<target_dram_addr, uint32_t> &dram_model);
int opcode_str_to_val(std::string opcode_str);
bool run_test(tt_runtime &runtime, std::vector<VarInstCmdAndBinaries> &varinst_cmds_binaries, int device_id, int test_num);

/*
Things to Test:
================
[x] Sizes of Fields (1, 2, 4 byte)
[x] Offset of Fields
[x] Multiple Fields
[x] Multiple Commands to same addr, 
different addresses.
[x] Different Opcodes including wrapping
Different cmds to different nearby addresses
*/


void gen_and_run_directed_test(tt_runtime &runtime, bool &pass, bool exit_on_fail, uint16_t opcode, int test_num){

    if (exit_on_fail && !pass){
        return;
    }

    log_info(tt::LogTest, std::string(120, '=').c_str());

    bool test_pass = true;
    std::vector<VarInstCmdAndBinaries> varinst_cmds_binaries; // Wrapper for commands, associated binaries, and reader core set.
    ExternalUpdateBufferListInfo ext_binary_info = {.alloc_infos={{0,0}}, .dram_target={0,0,0}, .dram_addr=0x0, .skip_send=false}; // Junk for tests that don't care.

    std::string desc;
    std::set<tt_cxy_pair> unique_readers;
    chip_id_t device_id = 0;
    std::size_t core_x = 1;
    std::size_t core_y = 1;

    const buda_SocDescriptor *sdesc   = &runtime.cluster->get_soc_desc(device_id);

    //////////////////////////////////////////////////////////////////////////
    // Generate the commands for each test                                  //
    //////////////////////////////////////////////////////////////////////////

    // First group of tests are similiar.
    if (test_num > 0 && test_num <= 11){

        // Some knobs that will be configured for different testcases.
        uint64_t addr               = 0x20000020;
        uint32_t operand_0          = 4;
        uint32_t operand_1          = 12;
        uint16_t update_mask        = 0b1 << 0;
        uint16_t field_size_bytes   = 2;
        uint16_t num_cmds           = 1;
        uint16_t buffer_dram_channel    = 0;
        uint16_t sync_loc_dram_channel  = 0;
        bool sync_loc_enable            = false;

        // Default opcode for these tests if not overridden on command line.
        opcode                          = (opcode == epoch_queue::VarinstCmdInvalid) ? epoch_queue::VarinstCmdIncWrap : opcode;
        // Convert from DRAM Channel to CoreXY
        tt_xy_pair buffer_dram_core     = sdesc->get_core_for_dram_channel(buffer_dram_channel, 0);
        tt_xy_pair sync_loc_dram_core   = sdesc->get_core_for_dram_channel(sync_loc_dram_channel, 0);

        unique_readers.insert({(std::size_t) device_id, core_x, core_y}); // Single chip, single core.

        // Consider making opcode a runtime argument for easier blasting out of tests by opcode.
        switch(test_num){
            case 1:
                desc = "Single 16 bit field at offset 0 incremented once";
                break;
            case 2:
                desc = "Single 16 bit field at offset 0 incremented multiple times, with wrapping.";
                num_cmds = 5;
                break;
            case 3:
                desc = "Single 16 bit field at offset 3 incremented once.";
                update_mask = 0b1 << 3;
                break;
            case 4:
                desc = "Single 16 bit field at offset 3 incremented multiple times, with wrapping.";
                num_cmds = 5;
                update_mask = 0b1 << 3;
                break;
            case 5:
                desc = "Single 8 bit field at offset 14 incremented once.";
                field_size_bytes = 1;
                update_mask = 0b1 << 14;
                break;
            case 6:
                desc = "Single 8 bit field at offset 14 incremented multiple times, with wrapping.";
                field_size_bytes = 1;
                update_mask = 0b1 << 14;
                num_cmds = 5;
                break;
            case 7:
                desc = "Multiple 16 bit fields at offset 0,1,8 incremented 3 times by 2.";
                field_size_bytes = 2;
                update_mask = 0b100000011; // 2 fields.
                operand_0 = 2;
                operand_1 = 10;
                num_cmds = 3;
                // Expected val for each field is: 0x6 (ie. 2 => 4 => 6)
                break;
            case 8:
                desc = "Multiple 16 bit fields at offset 0,1,3,6,15 incremented 6 times by 3, with wrapping at 14.";
                field_size_bytes = 2;
                update_mask = 0b1000000001001011; // 5 fields.
                operand_0 = 3;
                operand_1 = 14;
                num_cmds = 6;
                // Expected val for each field is: 0x4 (ie. 3 => 6 => 9 => 12 => 1 => 4)
                break;
            case 9:
                desc = "Multiple 32 bit fields at offset 0,1,3,6,8,15 incremented 6 times by 3, with wrapping at 14.";
                field_size_bytes = 4;
                update_mask = 0b1000000101001011; // 5 fields.
                operand_0 = 3;
                operand_1 = 14;
                num_cmds = 6;
                // Expected val for each field is: 0x4 (ie. 3 => 6 => 9 => 12 => 1 => 4)
                break;
            case 10:
                desc = "Multiple 16 bit fields at offset 0,2 incremented 3 times by 4, with wrapping at 10. Multiple Reader cores with sync loc in queue header loc.";
                field_size_bytes = 2;
                update_mask = 0b101; // 2 fields.
                operand_0 = 4;
                operand_1 = 10;
                num_cmds = 3;
                unique_readers.clear();
                for (int i=0; i<5; i++){
                    unique_readers.insert({(std::size_t) device_id, sdesc->workers.at(i).x, sdesc->workers.at(i).y}); // Single chip, multiple cores.
                }
                // Expected val for each field is: 0x6 (ie. 4 => 8 => 2)
                break;
            case 11:
                desc = "Multiple 16 bit fields at offset 0,2,4,5 incremented 3 times by 4, with wrapping at 10. Multiple Reader cores with sync loc in dedicated DRAM space.";
                field_size_bytes = 2;
                update_mask = 0b110101; // 4 fields.
                operand_0 = 4;
                operand_1 = 10;
                num_cmds = 3;
                unique_readers.clear();
                for (int i=0; i<5; i++){
                    unique_readers.insert({(std::size_t) device_id, sdesc->workers.at(i).x, sdesc->workers.at(i).y}); // Single chip, multiple cores.
                }
                sync_loc_enable = true;
                // Expected val for each field is: 0x6 (ie. 4 => 8 => 2)
                break;
            default:
                log_error("Not running a valid test_num.");
                break;
        }

        epoch_queue::VarinstCmdInfo base_cmd = { 
            .dram_addr_lower = static_cast<uint32_t>(addr & 0xFFFFFFFF), .dram_addr_upper = static_cast<uint16_t>((addr >> 32) & 0xFFFF), 
            .dram_core_x = static_cast<uint16_t>(buffer_dram_core.x & 0x3F), .dram_core_y = static_cast<uint16_t> (buffer_dram_core.y & 0x3F), 
            .cmd_type = epoch_queue::EpochCmdVarinst, .num_buffers = 0x1, .reader_index = 0x0, .num_readers = 0x1,
            .update_mask = update_mask, .opcode = opcode, .field_size_bytes = field_size_bytes, .operand_0 = operand_0, .operand_1 = operand_1,
            .sync_loc_enable = sync_loc_enable, .sync_loc_dram_core_x = static_cast<uint16_t>(sync_loc_dram_core.x & 0x3F), 
            .sync_loc_dram_core_y = static_cast<uint16_t>(sync_loc_dram_core.y & 0x3F), .sync_loc_index = 0,
        };

        // Copies on every command..this is less idea than a single set not tied to a command.
        for (int i = 0; i < num_cmds; i++){
            varinst_cmds_binaries.push_back({.cmd_info = base_cmd, .ext_binary_info=ext_binary_info, .unique_readers=unique_readers});
        }

    }else{
        // More complicated that are all a bit different.
        switch(test_num){
            case 100:
            {
                desc = "Multiple Commands each with multiple 16 bit fields incremented by different amount, with wrapping";

                // Default opcode for these tests if not overridden on command line.
                opcode                          = (opcode == epoch_queue::VarinstCmdInvalid) ? epoch_queue::VarinstCmdIncWrap : opcode;
                tt_xy_pair buffer_dram_core_xy  = sdesc->get_core_for_dram_channel(1, 0);
                tt_xy_pair sync_loc_dram_core   = sdesc->get_core_for_dram_channel(1, 0);

                epoch_queue::VarinstCmdInfo base_cmd = { 
                    .dram_addr_lower = 0x22000000, .dram_addr_upper = 0x0, 
                    .dram_core_x = static_cast<uint16_t>(buffer_dram_core_xy.x & 0x3F), .dram_core_y = static_cast<uint16_t> (buffer_dram_core_xy.y & 0x3F), 
                    .cmd_type = epoch_queue::EpochCmdVarinst, .num_buffers = 0x1, .reader_index = 0x0, .num_readers = 0x1,
                    .update_mask = 0b1000110000000011, .opcode = opcode, .field_size_bytes = 2, .operand_0 = 4, .operand_1 = 10,
                    .sync_loc_enable = true, .sync_loc_dram_core_x = static_cast<uint16_t>(sync_loc_dram_core.x & 0x3F), 
                    .sync_loc_dram_core_y = static_cast<uint16_t>(sync_loc_dram_core.y & 0x3F), .sync_loc_index = 0,
                };

                // TODO - change it per command.
                unique_readers.insert({(std::size_t) device_id, core_x, core_y}); // Single chip, single core.

                for (int i = 0; i < 3; i++){
                    varinst_cmds_binaries.push_back({.cmd_info=base_cmd, .ext_binary_info=ext_binary_info, .unique_readers=unique_readers});

                    epoch_queue::VarinstCmdInfo cmd1 = base_cmd;
                    cmd1.operand_0 = 7;
                    cmd1.operand_1 = 10;
                    varinst_cmds_binaries.push_back({.cmd_info=cmd1, .ext_binary_info=ext_binary_info, .unique_readers=unique_readers});

                }
               break;

            }
            case 101:
            case 102:
            case 103:
            case 104:
            {

                if (test_num == 101){
                    desc = "Multiple Commands each with different field sizes, incremented by different amount, with wrapping. Going from smaller field_size_bytes to bigger.";
                }else if (test_num == 102){
                    desc = "Multiple Commands each with different field sizes, incremented by different amount, with wrapping. Going from smaller field_size_bytes to bigger, back to smaller.";
                }else if (test_num == 103){
                    desc = "Multiple Commands each with different field sizes and update_masks, incremented by different amount, with wrapping. Going from smaller field_size_bytes to bigger, back to smaller.";
                }else if (test_num == 104){
                    desc = "Multiple Commands each with different field sizes and update_masks, incremented by different amount, with wrapping. Going from smaller field_size_bytes to bigger, back to smaller. Multiple reader cores w/ sync loc.";
                }
                // Default opcode for these tests if not overridden on command line.
                opcode                          = (opcode == epoch_queue::VarinstCmdInvalid) ? epoch_queue::VarinstCmdIncWrap : opcode;
                tt_xy_pair buffer_dram_core_xy  = sdesc->get_core_for_dram_channel(1, 0);
                tt_xy_pair sync_loc_dram_core   = sdesc->get_core_for_dram_channel(1, 0);

                epoch_queue::VarinstCmdInfo base_cmd = { 
                    .dram_addr_lower = 0x22000000, .dram_addr_upper = 0x0, 
                    .dram_core_x = static_cast<uint16_t>(buffer_dram_core_xy.x & 0x3F), .dram_core_y = static_cast<uint16_t> (buffer_dram_core_xy.y & 0x3F), 
                    .cmd_type = epoch_queue::EpochCmdVarinst, .num_buffers = 0x1, .reader_index = 0x0, .num_readers = 0x1,
                    .update_mask = 0b11, .opcode = opcode, .field_size_bytes = 4, .operand_0 = 4, .operand_1 = 10,
                    .sync_loc_enable = true, .sync_loc_dram_core_x = static_cast<uint16_t>(sync_loc_dram_core.x & 0x3F),
                    .sync_loc_dram_core_y = static_cast<uint16_t>(sync_loc_dram_core.y & 0x3F), .sync_loc_index = 0,
                };

                if (test_num < 104){
                    unique_readers.insert({(std::size_t) device_id, core_x, core_y}); // Single chip, single core.
                }else{
                    int num_readers = 3;
                    for (int i=0; i<num_readers; i++){
                        unique_readers.insert({(std::size_t) device_id, sdesc->workers.at(i).x, sdesc->workers.at(i).y}); // Single chip, multiple cores.
                    }
                }

                int num_cmd_loops = (test_num == 101) ? 1 : 3;
                for (int i = 0; i < num_cmd_loops; i++){

                    epoch_queue::VarinstCmdInfo cmd0 = base_cmd;

                    if ((test_num == 103 || test_num == 104) && i == num_cmd_loops-1){
                        cmd0.update_mask = 0b10;
                    }
                    varinst_cmds_binaries.push_back({.cmd_info=cmd0, .ext_binary_info=ext_binary_info, .unique_readers=unique_readers});

                    epoch_queue::VarinstCmdInfo cmd1 = base_cmd;
                    cmd1.operand_0 = 7;
                    cmd1.operand_1 = 10;
                    cmd1.field_size_bytes = 2;
                    varinst_cmds_binaries.push_back({.cmd_info=cmd1, .ext_binary_info=ext_binary_info, .unique_readers=unique_readers});

                    epoch_queue::VarinstCmdInfo cmd2 = base_cmd;
                    cmd2.operand_0 = 8;
                    cmd2.operand_1 = 10;
                    cmd2.field_size_bytes = 1;
                    varinst_cmds_binaries.push_back({.cmd_info=cmd2, .ext_binary_info=ext_binary_info, .unique_readers=unique_readers});

                }

               break;
            }

            case 105:
            case 106:
            case 107:
            case 108:
            {

                if (test_num == 105){
                    desc = "Simple 32B increment with external DRAM buffer list for num_buffers=5, num_cmds=2, num_readers=3, sync_loc_en=1";
                }else if (test_num == 106){
                    desc = "Simple 32B increment with external DRAM buffer list for num_buffers=5, num_cmds=3 (repeat x3 to reuse ext list), num_readers=3, sync_loc_en=1";
                }else if (test_num == 107){
                    desc = "Simple 32B increment with external DRAM buffer list for num_buffers=5, num_cmds=2, num_readers=1, sync_loc_en=1";
                }else if (test_num == 108){
                    desc = "Simple 32B increment with external DRAM buffer list for num_buffers=5, num_cmds=2, num_readers=3, sync_loc_en=0";
                }

                // TODO - Multiple commands sharing same external blob list - for loop for multiple commands.
                // TODO - Different reader cores per command, index into list.
                // TODO - Intelligence to re-use external binaries and not resend them.
                // TODO - Add to randoms.
                // TODO - Maybe use helper function to generate cmd + binary and takes num_buffers as argument to simplify this and future tests..

                int num_varinst_cmds_binaries   = test_num == 106 ? 3 : 2; // Number of EpochCmdVarinst that will be generated
                int num_readers                 = test_num == 107 ? 1 : 3;
                uint32_t buf_offset             = 0x0;
                int num_repeat_cmds             = test_num == 106 ? 3 : 1;
                bool sync_loc_enable            = test_num == 108 ? false : true;
                uint16_t sync_loc_index         = 0;
                uint16_t field_size_bytes       = 4;
                uint16_t update_mask            = test_num == 108 ? 0b101 : 0b1100000011000011;

                for (int i=0; i<num_varinst_cmds_binaries; i++){

                    // Set DRAM Location of External Update List/Binary that points to all DRAM Buffers.
                    int update_list_dram_ch                 = 0;
                    uint64_t update_list_addr               = 0x28000000 + (0x1000 * i);
                    tt_xy_pair update_list_dram_core_xy     = sdesc->get_core_for_dram_channel(update_list_dram_ch, 0);

                    // Set DRAM location of each buffer.
                    uint16_t num_buffers = 5;
                    std::vector<tt_queue_allocation_info> all_alloc_infos;

                    for (int buf_idx=0; buf_idx<num_buffers; buf_idx++){
                        uint32_t dram_buffer_addr = 0x20000000 + buf_offset; // Below func supports 32-bit only.
                        uint16_t dram_buffer_ch = 0x0;
                        all_alloc_infos.push_back({.channel=dram_buffer_ch, .address=dram_buffer_addr});
                        buf_offset += 0x100;
                    }
                    
                    // Put External Update Buffer List in wrapperstruct for sending to device and checking later.
                    ExternalUpdateBufferListInfo ext_binary_info = {    .alloc_infos = all_alloc_infos,
                                                                        .dram_target = {device_id, update_list_dram_ch, 0},
                                                                        .dram_addr = update_list_addr,
                                                                        .skip_send = false};

                    // Generate the actual EpochCmdVarinst command now, that points to External Update List DRAM location since num_buffers>1
                    opcode                          = (opcode == epoch_queue::VarinstCmdInvalid) ? epoch_queue::VarinstCmdIncWrap : opcode;
                    tt_xy_pair sync_loc_dram_core   = sdesc->get_core_for_dram_channel(1, 0);

                    epoch_queue::VarinstCmdInfo base_cmd = { 
                        .dram_addr_lower = static_cast<uint32_t>(update_list_addr & 0xFFFFFFFF), .dram_addr_upper = static_cast<uint16_t>((update_list_addr >> 32) & 0xFFFF), 
                        .dram_core_x = static_cast<uint16_t>(update_list_dram_core_xy.x & 0x3F), .dram_core_y = static_cast<uint16_t> (update_list_dram_core_xy.y & 0x3F), 
                        .cmd_type = epoch_queue::EpochCmdVarinst, .num_buffers = num_buffers, .reader_index = 0x0, .num_readers = 0x1,
                        .update_mask = update_mask, .opcode = opcode, .field_size_bytes = field_size_bytes, .operand_0 = 4, .operand_1 = 10,
                        .sync_loc_enable = sync_loc_enable, .sync_loc_dram_core_x = static_cast<uint16_t>(sync_loc_dram_core.x & 0x3F),
                        .sync_loc_dram_core_y = static_cast<uint16_t>(sync_loc_dram_core.y & 0x3F), .sync_loc_index = sync_loc_index++,
                    };

                    // Setup how many reader cores will read from these buffers.
                    for (int i=0; i<num_readers; i++){
                        unique_readers.insert({(std::size_t) device_id, sdesc->workers.at(i).x, sdesc->workers.at(i).y}); // Single chip, multiple cores.
                    }

                    // Repeat same EpochCmdVatinst command, and re-use (only send to device once) the external binary.
                    for (int i=0; i<num_repeat_cmds; i++){
                        // Push combined Cmd + Binary + Readers to wrapper struct.
                        ext_binary_info.skip_send = i > 0 ? true : false;
                        varinst_cmds_binaries.push_back({.cmd_info=base_cmd, .ext_binary_info=ext_binary_info, .unique_readers=unique_readers});
                    }
                }
                break;
            }
            default:
                log_error("Not running a valid test_num.");
                break;
        }
    }

    // Check that commands are supported/legal.
    verify_supported_epoch_cmds(varinst_cmds_binaries);
            
    //////////////////////////////////////////////////////////////////////////
    // Run the test now                                                     //
    //////////////////////////////////////////////////////////////////////////

    log_info(tt::LogTest, "Running Directed Test {} now. Description: \"{}\"", test_num, desc);

    if (varinst_cmds_binaries.size()){
        test_pass &= run_test(runtime, varinst_cmds_binaries, device_id, test_num); // FIXME ext_update_buf_infos
    }else{
        test_pass = false;
    }

    if (test_pass){
        log_info(tt::LogTest, "\tResult Test {} => PASSED", test_num);
    }else{
        log_error("\tResult Test {} => FAILED", test_num);
    }

    pass &= test_pass; // Overall pass/fail for all tests combined.

}

// Generate a bunch of random tests. Randomizes most things that are interesting.
void gen_and_run_random_test(tt_runtime &runtime, bool &pass, bool exit_on_fail, int num_random_tests){

    chip_id_t device_id = 0;
    const buda_SocDescriptor *sdesc   = &runtime.cluster->get_soc_desc(device_id);

    // These can make things hairy. Make it easy to disable for testing.
    bool allow_fully_random_mask = true;
    bool allow_big_operands = true;
    std::map<int, int> allowed_field_size_bytes = {{1, 1}, {2, 2}, {3, 4}};

    for (int test_num = 0; test_num < num_random_tests; test_num++){

        std::vector<VarInstCmdAndBinaries> varinst_cmds_binaries; // Wrapper for commands, associated binaries, and reader core set.
        ExternalUpdateBufferListInfo ext_binary_info = {.alloc_infos={{0,0}}, .dram_target={0,0,0}, .dram_addr=0x0, .skip_send=false}; // Junk for tests that don't care.
        std::set<tt_cxy_pair> unique_readers;
        std::string desc;

        //////////////////////////////////////////////////////////////////////////
        // Generate the commands for each test                                  //
        //////////////////////////////////////////////////////////////////////////

        log_info(tt::LogTest, std::string(120, '=').c_str());

        int num_epoch_cmds = verif::random::tt_rnd_int(1,10);
        log_info(tt::LogTest, "Generating Random Test {}/{} with num_epoch_cmds: {}", test_num+1, num_random_tests, num_epoch_cmds);

        // Per test control. Most often, keep things simple.
        bool small_num_readers      = verif::random::tt_rnd_int(1,10) >= 2;
        bool small_operands         = !allow_big_operands || (verif::random::tt_rnd_int(1,10) >= 2);
        bool small_mask             = !allow_fully_random_mask || (verif::random::tt_rnd_int(1,10) >= 2);

        auto num_worker_cores       = sdesc->workers.size();
        uint16_t num_readers        = small_num_readers ? verif::random::tt_rnd_int(1,5) : verif::random::tt_rnd_int(1,num_worker_cores/4);

        while (unique_readers.size() < num_readers){
            auto random_reader_idx = verif::random::tt_rnd_int(0,num_worker_cores-1);
            tt_xy_pair reader_core = sdesc->workers.at(random_reader_idx);
            unique_readers.insert({(std::size_t) device_id, reader_core.x, reader_core.y}); // Single chip, multiple cores.
            log_debug(tt::LogTest, "Added to unique_readers =>  device_id: {} worker core: {} from random_reader_idx: {} num_readers: {} small_num_readers: {} num_worker_cores: {}",
                device_id, reader_core.str(), random_reader_idx, num_readers, small_num_readers, num_worker_cores);
        }

        std::ostringstream oss;
        oss << "num_epoch_cmds: " << num_epoch_cmds << " num_readers: " << num_readers << " small_num_readers: " << small_num_readers;
        oss << " small_operands: " << small_operands << " small_mask: " << small_mask;
        desc = oss.str();
        log_info(tt::LogTest, "Test: {}", desc); // Debug

        for (int i=0; i<num_epoch_cmds; i++){

            uint16_t opcode                 = verif::random::tt_rnd_int(1,6);
            uint16_t buffer_dram_channel    = verif::random::tt_rnd_int(0,7);   // FIXME - Use SOC Desc.
            uint16_t sync_loc_dram_channel  = verif::random::tt_rnd_int(0,7);
            uint32_t dram_addr_lower        = verif::random::tt_rnd_int(0x10000000, 0x20000000) & ~((uint32_t) 0x1F); // 32B aligned.
            uint16_t field_size_bytes       = allowed_field_size_bytes.at(verif::random::tt_rnd_int(1,3));
            uint32_t operand_0              = small_operands ? verif::random::tt_rnd_int(0, 64) : verif::random::tt_rnd_uint32t(numeric_limits<uint32_t>::min(), numeric_limits<uint32_t>::max());
            uint32_t operand_1              = small_operands ? verif::random::tt_rnd_int(0, 64) : verif::random::tt_rnd_uint32t(numeric_limits<uint32_t>::min(), numeric_limits<uint32_t>::max());
            uint16_t update_mask            = small_mask ? 0x1 << verif::random::tt_rnd_int(0,15) : verif::random::tt_rnd_int(numeric_limits<uint16_t>::min(), numeric_limits<uint16_t>::max());

            bool sync_loc_enable            = true; // Needs more work to remove hardcode.
            uint16_t num_buffers            = 0x1; // Needs more work to remove hardcode.
            tt_xy_pair buffer_dram_core_xy  = sdesc->get_core_for_dram_channel(buffer_dram_channel, 0);
            tt_xy_pair sync_loc_dram_core   = sdesc->get_core_for_dram_channel(sync_loc_dram_channel, 0);
            uint16_t sync_loc_index         = verif::random::tt_rnd_int(0,1023);

            // TODO: If sync loc is Queue Header position word5, make sure we don't touch that field.

            // Workarounds for illegal randoms
            if (opcode == epoch_queue::VarinstCmdIncWrap && operand_1 == 0)
                operand_1++;

            log_info(tt::LogTest, "\tGenerated random Epoch Command {}/{} with dram_addr_lower: 0x{:x} opcode: {} operand_0: {} operand_1: {} update_mask: {:016b} field_size_bytes: {} sync_loc_enable: {} idx: {}",
                i+1, num_epoch_cmds, dram_addr_lower, (int) opcode, operand_0, operand_1, update_mask, field_size_bytes, sync_loc_enable, sync_loc_index);

            epoch_queue::VarinstCmdInfo base_cmd = { 
                .dram_addr_lower = dram_addr_lower, .dram_addr_upper = 0x0, 
                .dram_core_x = static_cast<uint16_t>(buffer_dram_core_xy.x & 0x3F), .dram_core_y = static_cast<uint16_t> (buffer_dram_core_xy.y & 0x3F),
                .cmd_type = epoch_queue::EpochCmdVarinst, .num_buffers = num_buffers, .reader_index = 0x0, .num_readers = 0x1,
                .update_mask = update_mask, .opcode = opcode, .field_size_bytes = field_size_bytes, .operand_0 = operand_0, .operand_1 = operand_1,

                .sync_loc_enable = sync_loc_enable, .sync_loc_dram_core_x = static_cast<uint16_t>(sync_loc_dram_core.x & 0x3F),
                .sync_loc_dram_core_y = static_cast<uint16_t>(sync_loc_dram_core.y & 0x3F), .sync_loc_index = sync_loc_index,

            };

            varinst_cmds_binaries.push_back({.cmd_info=base_cmd, .ext_binary_info=ext_binary_info, .unique_readers=unique_readers});

        }

        // Check that commands are supported/legal.
        verify_supported_epoch_cmds(varinst_cmds_binaries);
                
        //////////////////////////////////////////////////////////////////////////
        // Run the test now                                                     //
        //////////////////////////////////////////////////////////////////////////

        log_info(tt::LogTest, "Running Random Test {} now. Description: \"{}\"", test_num, desc);

        bool test_pass = run_test(runtime, varinst_cmds_binaries, device_id, test_num);

        if (test_pass){
            log_info(tt::LogTest, "\tResult Test {} => PASSED", test_num);
        }else{
            log_error("\tResult Test {} => FAILED", test_num);
        }

        pass &= test_pass; // Overall pass/fail for all tests combined.
    }
}


// Run a test, which at this point is just a vectof of epoch commands.
bool run_test(tt_runtime &runtime, std::vector<VarInstCmdAndBinaries> &varinst_cmds_binaries, int device_id, int test_num){

    std::map<target_dram_addr, uint32_t> dram_model_output;   // expected output.

    compute_expected_output(runtime, varinst_cmds_binaries, device_id, dram_model_output);          // Model cmds to compute expected output. 
    initialize_device_dram(runtime, dram_model_output);                         // Use expected output addresses to initialize device DRAM.
    send_epoch_cmds_to_device(runtime, varinst_cmds_binaries, device_id);  // Send to device for processing, and compare against expected outputs.
    bool matching = compare_with_device(runtime, dram_model_output);            // Compare expected values with device and determine pass/fail for this test.
    return matching;
}



// Checking for unsupported cases.
void verify_supported_epoch_cmds(std::vector<VarInstCmdAndBinaries> &varinst_cmds_binaries){
    for (int i=0; i < varinst_cmds_binaries.size(); i++){
        auto &cmd = varinst_cmds_binaries.at(i).cmd_info;
        // Disallow some unsupported cases.
        log_assert(cmd.opcode != epoch_queue::VarinstCmdInvalid, "Invalid opcode");
        log_assert(cmd.field_size_bytes == 1 || cmd.field_size_bytes == 2 || cmd.field_size_bytes == 4, "invalid field_size_bytes. Must be 1, 2, or 4.");
        log_assert((cmd.dram_addr_lower % 32) == 0,"DRAM address 0x{:x} on cmd must be 32 Byte aligned",  cmd.dram_addr_lower);
        log_assert(!(cmd.opcode == 1 && cmd.operand_1 == 0), "Cannot have wrap value 0 for IncWrap");
        // Addresses of fields specified by field_size_bytes and update_mask must not overflow DRAM.
    }
}

void compute_expected_output(tt_runtime &runtime, std::vector<VarInstCmdAndBinaries> &varinst_cmds_binaries, int device_id, std::map<target_dram_addr, uint32_t> &dram_model){

    log_info(tt::LogTest, "Inside compute_expected_output() now...");

    tt_cluster *cluster             = runtime.cluster.get();
    const buda_SocDescriptor *sdesc   = &cluster->get_soc_desc(device_id);

    for (int j=0; j<varinst_cmds_binaries.size(); j++){
        auto &wrap = varinst_cmds_binaries.at(j);
        auto &cmd = wrap.cmd_info;
        auto &bin = wrap.ext_binary_info;

        // Reconstruct 48 bit address from lower/upper bits for modeling purposes.
        uint64_t cmd_dram_addr          = (static_cast<uint64_t> (cmd.dram_addr_upper & 0xFFFF) << 32) | (cmd.dram_addr_lower & 0xFFFFFFFF);
        uint64_t buf_dram_addr          = cmd_dram_addr;
        tt_target_dram buf_dram_target  = get_target_dram_from_core(sdesc, device_id, {cmd.dram_core_x, cmd.dram_core_y});

        log_info(tt::LogTest, "Computing expected for EpochCmdVarinst j: {} cmd_dram_addr: 0x{:x} opcode: {} operand0: {} operand1: {} update_mask: {:016b} num_buffers: {}",
            j, cmd_dram_addr, (int) cmd.opcode, cmd.operand_0, cmd.operand_1, cmd.update_mask, (int) cmd.num_buffers);


        for (int buf_idx = 0; buf_idx < cmd.num_buffers; buf_idx++) {

            log_info(tt::LogTest, "Computing expected for EpochCmdVarinst j: {} buf_idx: {}", j, buf_idx);

            // If more than 1 buffer, the addresses are stored in external binary/list.
            if (cmd.num_buffers > 1){
                buf_dram_addr   = bin.alloc_infos.at(buf_idx).address;
                buf_dram_target = {device_id, bin.alloc_infos.at(buf_idx).channel, 0};
                log_info(tt::LogTest, "External Case buf_idx: {} buf_dram_addr: 0x{:x} dram_ch: {}", buf_idx, buf_dram_addr, bin.alloc_infos.at(buf_idx).channel);
            }

            // Iterate over all fields in update mask.
            for (int i = 0; i < sizeof(cmd.update_mask) * 8; i++) {
                if (cmd.update_mask & (1 << i)) {

                    // Calculate 32b aligned address that this field resides in
                    uint32_t field_offset_32b_aligned   = 4 * ((cmd.field_size_bytes * i) / 4);
                    uint64_t field_addr_32b_aligned     = buf_dram_addr + field_offset_32b_aligned;

                    // Calculate read mask for indexing which 8b or 16b chunk of 32b word the field resides at.
                    uint32_t field_shift_amt            = ((i * cmd.field_size_bytes * 8) % 32);
                    uint32_t field_mask_32b             = get_32b_mask(cmd.field_size_bytes);
                    uint32_t field_mask_shifted_32b     = field_mask_32b << field_shift_amt;

                    log_debug(tt::LogTest, "\tFor update_mask bit i: {} field_size_bytes: {} field_shift_amt: {} field_mask_32b: 0x{:x} field_mask_shifted_32b: 0x{:x} buf_dram_addr: 0x{:x} + field_offset_32b_aligned: 0x{:x} => field_addr_32b_aligned: 0x{:x}",
                        i, (int) cmd.field_size_bytes, field_shift_amt, field_mask_32b, field_mask_shifted_32b, buf_dram_addr, field_offset_32b_aligned, field_addr_32b_aligned);

                    target_dram_addr key = std::make_pair(buf_dram_target, field_addr_32b_aligned);

                    if (dram_model.find(key) == dram_model.end()){
                        // Zero initialize this addr if it hasn't been encountered for this test yet.
                        dram_model[key] = static_cast<uint32_t>(0x0);
                        log_debug(tt::LogTest, "\tInitialized DRAM Model for update_mask i: {} key dram_ch: {} base addr: 0x{:x} field_addr_32b_aligned: 0x{:x} to be 0x{:x}", 
                            i, std::get<1>(buf_dram_target), buf_dram_addr, field_addr_32b_aligned, dram_model[key]);
                    }

                    uint32_t &dest          = dram_model.at(key);
                    uint32_t rd_val_field   = (dest & field_mask_shifted_32b) >> field_shift_amt;
                    uint32_t wr_val_field   = 0; 

                    // Do the operation now based on opcode and operands.
                    switch (cmd.opcode){
                        case epoch_queue::VarinstCmdIncWrap:
                            wr_val_field = (rd_val_field + cmd.operand_0) % cmd.operand_1;
                            break;
                        case epoch_queue::VarinstCmdInc:
                            wr_val_field = (rd_val_field + cmd.operand_0);
                            break;
                        case epoch_queue::VarinstCmdSet:
                            wr_val_field = cmd.operand_0;
                            break;
                        case epoch_queue::VarinstCmdAdd:
                            wr_val_field = cmd.operand_0 + cmd.operand_1;
                            break;
                        case epoch_queue::VarinstCmdMul:
                            wr_val_field = cmd.operand_0 * cmd.operand_1;
                            break;
                    }

                    // Commit it to DRAM model in the appropriate field position.
                    dest = dest & ~field_mask_shifted_32b; // Clear field bits.
                    dest = dest | ((wr_val_field & field_mask_32b) << field_shift_amt); // Write field bits.

                    log_debug(tt::LogTest, "\tCalculated expected val: 0x{:08x} for addr: 0x{:08x} for epoch_cmd {:2d} update_mask bit i: {:2d} (from rd_val_field: {} opcode: {} operand0: {} operand1: {} wr_val_field: {})",
                        dest, field_addr_32b_aligned, j, i, rd_val_field, (int) cmd.opcode, cmd.operand_0, cmd.operand_1, wr_val_field);
                    
                }
            } // update_mask
        } // buffers
    } // cmds
}

// Given DRAM addresses that will be written to by epoch commands, initialize them before we run on device.
void initialize_device_dram(tt_runtime &runtime, std::map<target_dram_addr, uint32_t> &dram_model){

    log_debug(tt::LogTest, "\tStarting initialize_device_dram() now.");
    tt_cluster *cluster = runtime.cluster.get();

    for (auto &d : dram_model){
        tt_target_dram dram_target  = d.first.first;
        uint64_t dram_addr          = d.first.second;
        uint32_t fill_val           = 0x0;

        // Instead of single word, do 7 more for good measure. This is needed when sync_loc (rd_stride) is in word5 and sync_loc_enable=false
        // too, otherwise old data may be left there.
        vector<uint32_t> wr_vec(8, fill_val); // 32 bytes, will init single words x 8. 
        cluster->write_dram_vec({wr_vec}, dram_target, dram_addr);
            log_info(tt::LogTest, "\tInitialized Device {} DRAM ch: {} subch: {} addr: 0x{:x} => 0x{:08x}", std::get<0>(dram_target), std::get<1>(dram_target), std::get<2>(dram_target), dram_addr, fill_val);
    }
}

// For each DRAM address in expected outputs DRAM model, read device and compare against expected.
bool compare_with_device(tt_runtime &runtime, std::map<target_dram_addr, uint32_t> &dram_model){

    log_debug(tt::LogTest, "\tStarting compare_with_device() now.");

    tt_cluster *cluster = runtime.cluster.get();
    bool matching       = true;

    // Wait for epoch commands to be finished by device.
    runtime.wait_for_idle();

    for (auto &d : dram_model){
        tt_target_dram dram_target  = d.first.first;
        log_debug(tt::LogTest, "\t\tExpectedVal for Device: {:2d} ch: {:1d} subch: {:1d} Addr: 0x{:08x} => 0x{:08x}", 
            std::get<0>(dram_target), std::get<1>(dram_target), std::get<2>(dram_target), d.first.second, d.second);
    }

    // Future Improvement: Zero initialize wider range (base_addr + 64 bytes), and compare this same range to make sure other bits were not modified.
    // But problem, we don't have those other values not modified recorded in expected dram model. TODO - Update initialization of DRAM model to zero 
    // out 64 bytes after DRAM base start with zeros.  Then would iterate over epoch_cmds vector instead of DRAM model, probably.

    for (auto &d : dram_model){

        tt_target_dram dram_target  = d.first.first;
        uint64_t dram_addr          = d.first.second;
        uint32_t expected_val       = d.second;
        int read_size_in_bytes      = 4; // single uint32_t word.

        vector<uint32_t> rd_vec;
        cluster->read_dram_vec(rd_vec, dram_target, dram_addr, read_size_in_bytes);
        log_assert(rd_vec.size() == 1, "Expected to only read a single 32b word");
        uint32_t device_dram_rd_val = rd_vec.at(0);

        if (device_dram_rd_val == expected_val){
            log_info(tt::LogTest, "\t\t{:8s} between Device: 0x{:08x} vs Expected: 0x{:08x} (from device: {} DRAM ch: {} subch: {} addr: 0x{:x})",
            "Match", device_dram_rd_val, expected_val, std::get<0>(dram_target), std::get<1>(dram_target), std::get<2>(dram_target), dram_addr);
        }else{
            log_error("\t\t{:8s} between Device: 0x{:08x} vs Expected: 0x{:08x} (from device: {} DRAM ch: {} subch: {} addr: 0x{:x})",
            "Mismatch", device_dram_rd_val, expected_val, std::get<0>(dram_target), std::get<1>(dram_target), std::get<2>(dram_target), dram_addr);
            matching = false;
        }
    }

    return matching;
}

// Send vector of epoch commands for a given test to device. Automatically fill in sync_loc_index, reader_index, num_readers.
void send_epoch_cmds_to_device(tt_runtime &runtime, std::vector<VarInstCmdAndBinaries> &varinst_cmds_binaries, int device_id){

    log_info(tt::LogTest, "\tStarting send_epoch_cmds_to_device() now.");

    tt_cluster *cluster             = runtime.cluster.get();
    const buda_SocDescriptor *sdesc   = &cluster->get_soc_desc(device_id);

    int sync_loc_index = 0;
    auto num_cmds = varinst_cmds_binaries.size();
    for (int i=0; i < num_cmds; i++){
        auto &cmd_wrap   = varinst_cmds_binaries.at(i);
        auto &cmd       = cmd_wrap.cmd_info;
        auto &binary    = cmd_wrap.ext_binary_info;

        if (cmd.sync_loc_enable){
            cmd.sync_loc_index = sync_loc_index++;
        }

        log_info(tt::LogTest, "\tSending EpochCmdVarinst {}/{} dram_addr: 0x{:04x}{:08x} field_size_bytes: {} update_mask: {:016b} opcode: {} operand_0: {} operand_1: {} sync_loc_ena: {} sync_loc_index: {}",
            i+1, num_cmds, cmd.dram_addr_upper, cmd.dram_addr_lower, (int) cmd.field_size_bytes, cmd.update_mask, (int) cmd.opcode, cmd.operand_0, cmd.operand_1, (bool) cmd.sync_loc_enable, cmd.sync_loc_index);

        // First write the external data to DRAM for binary (DRAM addr+locs) if it's used (num_buf>1)
        if (cmd.num_buffers > 1){
            // FIXME - Consider replacing this with whatever ends up being used by runtime.
            // TODO - Write to loader report, or leverage that code (needs hex structure?)
            if (binary.skip_send){
                log_info(tt::LogTest, "\t\tSkipping Sending Binary for num_buffers: {} to Device {} DRAM ch: {} subch: {} addr: 0x{:x}", 
                (int) cmd.num_buffers, std::get<0>(binary.dram_target), std::get<1>(binary.dram_target), std::get<2>(binary.dram_target), binary.dram_addr);
            }else{

                log_info(tt::LogTest, "\t\tWriting Binary for num_buffers: {} to Device {} DRAM ch: {} subch: {} addr: 0x{:x}", 
                (int) cmd.num_buffers, std::get<0>(binary.dram_target), std::get<1>(binary.dram_target), std::get<2>(binary.dram_target), binary.dram_addr);

                std::vector<uint32_t> binary_data = tt_epoch_loader::get_q_update_read_binaries(
                    {.loc = QUEUE_LOCATION::DRAM, .alloc_info = binary.alloc_infos}, *sdesc);
                cluster->write_dram_vec(binary_data, binary.dram_target, binary.dram_addr);

            }
        }


        // Push one command to each core for each buffer in the queue.
        uint8_t reader_index = 0;
        for (const tt_cxy_pair &reader_cxy: cmd_wrap.unique_readers) {
            cmd.num_readers = cmd_wrap.unique_readers.size();
            cmd.reader_index = reader_index++;
            log_info(tt::LogTest, "\t\tSending EpochCmdVarinst to reader: {}", reader_cxy.str());
            runtime.loader->send_epoch_cmd_varinst(cmd, reader_cxy.chip, {reader_cxy.x, reader_cxy.y});
        }
    }

    // For WFI per-test befor read from device, must increment gen_id otherwise WFI does nothing.
    auto &ctrl = runtime.loader->get_epoch_ctrl(device_id);
    ctrl.update_curr_gen_id(ctrl.get_next_gen_id());

    log_info(tt::LogTest, "\tDone send_epoch_cmds_to_device() now.");
}

// Given field_size_bytes (1,2,4) get the associated mask into 32b word.
uint32_t get_32b_mask(uint16_t field_size_bytes){

    uint32_t word_mask_32b = 0x0;

    switch (field_size_bytes){
        case 1:
            word_mask_32b = 0x000000FF;
            break;
        case 2:
            word_mask_32b = 0x0000FFFF;
            break;
        case 4:
            word_mask_32b = 0xFFFFFFFF;
            break;
        default:
            log_fatal("unsupported word_mask_32b size.");
            break;
    } 

    return word_mask_32b;

}

// Convert from DRAM Core XY to Target DRAM (Chip, Ch, SubCb)
tt_target_dram get_target_dram_from_core(const buda_SocDescriptor *sdesc, int device_id, tt_xy_pair core) {
    if (sdesc->dram_core_channel_map.find(core) != sdesc->dram_core_channel_map.end()){
        return std::tuple_cat(std::make_tuple(device_id), sdesc->dram_core_channel_map.at(core));
        // return sdesc->dram_core_channel_map.at(core);
    }else{
        log_fatal("dram channel not found for core {}", core.str());
        return {0,0,0};
    }
}


int str_to_varinst_cmd_opcode(std::string opcode_str){

    std::map<std::string, int> instr_opcode_to_cmd_opcode_map = {
        {"Invalid",     epoch_queue::VarinstCmdInvalid},
        {"IncWrap",     epoch_queue::VarinstCmdIncWrap},
        {"Inc",         epoch_queue::VarinstCmdInc},
        {"Mul",         epoch_queue::VarinstCmdMul},
        {"Add",         epoch_queue::VarinstCmdAdd},
        {"Set",         epoch_queue::VarinstCmdSet},
    };

    auto it = instr_opcode_to_cmd_opcode_map.find(opcode_str);
    if (it != instr_opcode_to_cmd_opcode_map.end()) {
        return it->second;
    } else {
        log_error("varinst instr opcode str {} not supported.", opcode_str);
        return epoch_queue::VarinstCmdInvalid;
    }
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool run_silicon = true;
    int opt_level, test_num;
    std::string netlist_path;
    bool exit_on_fail = false;
    std::string opcode_str;
    bool random_test = false;
    int num_random_tests;
    int seed;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(opt_level, args)           = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 4);
    std::tie(test_num, args)            = verif_args::get_command_option_uint32_and_remaining_args(args, "--test-num", -1); // Default is all tests.
    std::tie(netlist_path, args)        = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_basic/netlist_unary_multicore.yaml");
    std::tie(exit_on_fail, args)        = verif_args::has_command_option_and_remaining_args(args, "--exit-on-fail");
    std::tie(opcode_str, args)          = verif_args::get_command_option_and_remaining_args(args, "--opcode", "Invalid"); // Optional, easy way to blast out more tests.
    std::tie(random_test, args)         = verif_args::has_command_option_and_remaining_args(args, "--random"); 
    std::tie(num_random_tests, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-random-tests", 1); // Default is 1 test.
    std::tie(seed, args)                = verif_args::get_command_option_int32_and_remaining_args(args, "--seed", -1);

    // Randomization seeding
    if (seed == -1) {
        seed = verif::random::tt_gen_seed();
        log_info(tt::LogTest, "Unspecified cmdline --seed , generated random seed {}", seed);
    }
    verif::random::tt_rnd_set_seed(seed);

    perf::PerfDesc perf_desc(args, netlist_path);
    verif_args::validate_remaining_args(args);
    log_assert(!(random_test && test_num != -1), "cannot use --random and --test-num together, pick one");

    uint16_t opcode = str_to_varinst_cmd_opcode(opcode_str);

    // Runtime setup
    tt_runtime_config config = get_runtime_config(perf_desc, output_dir, opt_level, run_silicon);
    tt_runtime runtime(netlist_path, config);
    tt_runtime_workload &workload = *runtime.get_workload();

    log_assert(runtime.initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to be initialized successfully");

    vector<tt_dram_io_desc> input_io_desc = runtime.get_host_input_io_desc();
    tt::io::push_host_inputs(input_io_desc, &tt::io::default_debug_tensor);

    for (auto const& program : workload.programs) {
        runtime.run_program(program.first, {});
    }

    bool pass = true;    

    if (random_test){
        gen_and_run_random_test(runtime, pass, exit_on_fail, num_random_tests);
    }else{
    
        // Run all tests by default. Update as more tests are added.
        if (test_num == -1){
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 1);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 2);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 3);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 4);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 5);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 6);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 7);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 8);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 9);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 10);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 11);

            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 100);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 101);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 102);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 103);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 104);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 105);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 106);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 107);
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, 108);

        }else{
            gen_and_run_directed_test(runtime, pass, exit_on_fail, opcode, test_num);
        }

    }

    runtime.finish();

    // Handle Pass/Fail 
    if (pass){
        log_info(tt::LogTest, "All Tests Pass.");
        return 0;
    }else{
        if (exit_on_fail){
            log_error("A test failed. Early exited on first failure. See above.");
        }else{
            log_error("Some Test(s) Fail. See above.");
        }
        return 1;
    }

}

