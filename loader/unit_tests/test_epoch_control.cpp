// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "gtest/gtest.h"
#include "test_unit_common.hpp"
#include <algorithm>

// Create epoch queue in chip0 dram0 at 0x1000000
tt_epoch_queue create_and_init_epoch_queue(
    tt_cluster *cluster,
    int &num_slots,
    int &wc_window_size,
    tt_xy_pair &routing_core,
    bool mmio_chip,
    unordered_map<std::string, int> &num_cmds_per_binary,
    unordered_map<int, int> &num_cmds_per_epoch_id,
    unordered_map<std::string, int> &num_cmds_per_update_blob) {

    log_trace(tt::LogTest, "Creating Epoch Cmd Queue with num_slots: {} wc_window_size: {}", num_slots, wc_window_size);
    // Create epoch queue in chip0 dram0 at 0x1000000
    tt_epoch_queue eq(
        num_slots,
        epoch_queue::EPOCH_Q_SLOT_SIZE,
        routing_core,
        mmio_chip,
        &num_cmds_per_binary,
        &num_cmds_per_epoch_id,
        &num_cmds_per_update_blob);
    eq.set_chip_id(0);
    eq.set_dram_chan(0);
    eq.set_dram_addr(0x1000000);
    eq.set_dram_subchannel(0);
    eq.set_l1_shadow_addr(l1_mem::address_map::NCRISC_L1_EPOCH_Q_BASE);
    eq.set_wc_window_size(wc_window_size);

    // Test worker epoch queue status
    eq.init_queue_ptrs(cluster);
    // ASSERT_TRUE(eq.is_active()) << "Epoch queue should be active";
    // ASSERT_EQ(eq.get_num_slots(), num_slots) << "Epoch queue should have " << num_slots << " slots";

    EXPECT_TRUE(eq.is_active()) << "Epoch queue should be active";
    EXPECT_EQ(eq.get_num_slots(), num_slots) << "Epoch queue should have " << num_slots << " slots";

    return eq;

}

// Test direct push to DRAM, full/empty states and wrapping
TEST(EpochControl, EpochQueueDirectDRAMPush) {
    unordered_map<std::string, int> num_cmds_per_binary;
    unordered_map<int, int> num_cmds_per_epoch_id;
    unordered_map<std::string, int> num_cmds_per_update_blob;

    // Create 1x1 cluster
    tt_xy_pair routing_core(0, 0);
    auto cluster = quiet_call([&] {
        auto sdesc_path = test_path() + "device_descriptors/grayskull_1x1_arch.yaml";
        return get_cluster(tt::ARCH::GRAYSKULL, tt::TargetDevice::Versim, {0}, sdesc_path);
    });

    // Create dummy epoch command
    int cmd_words = epoch_queue::EPOCH_Q_SLOT_SIZE / sizeof(uint32_t);
    vector<uint32_t> cmd_blob(cmd_words, 0);
    auto cmd = std::make_shared<tt_hex>(cmd_blob, tt_hex_type::Misc, routing_core, "cmd");
    bool mmio_chip = true;

    // Test EQ of different sizes
    vector<int> num_slots_sweep = {1, 2, 4, 7, 13, 32, 64, 4000};
    for (int num_slots : num_slots_sweep) {

        int wc_window_size = 0; // WC disabled.
        tt_epoch_queue eq = create_and_init_epoch_queue(cluster.get(), num_slots, wc_window_size, routing_core, mmio_chip, num_cmds_per_binary, num_cmds_per_epoch_id, num_cmds_per_update_blob);

        int num_wraparounds = 4;
        for (int i = 0; i < num_wraparounds; i++) {
            for (int j = 0; j < num_slots; j++) {
                eq.push_command(cmd, cluster.get());
            }
            ASSERT_TRUE(eq.is_full_dram(cluster.get())) << "Epoch queue should be full";

            for (int j = 0; j < num_slots; j++) {
                pop_epoch_queue(eq, cluster.get());
                ASSERT_EQ(eq.occupancy_dram(cluster.get()), num_slots - j - 1);
            }
            ASSERT_TRUE(eq.is_empty_dram(cluster.get())) << "Epoch queue should be empty";
        }
    }
}

// Test pushing batch of epoch q cmds to intermediate host queue (WC buffer) with it sized to match number of slots.
TEST(EpochControl, EpochQueueIntermediateHostPush) {
    unordered_map<std::string, int> num_cmds_per_binary;
    unordered_map<int, int> num_cmds_per_epoch_id;
    unordered_map<std::string, int> num_cmds_per_update_blob;

    // Create 1x1 cluster
    tt_xy_pair routing_core(0, 0);
    auto cluster = quiet_call([&] {
        auto sdesc_path = test_path() + "device_descriptors/grayskull_1x1_arch.yaml";
        return get_cluster(tt::ARCH::GRAYSKULL, tt::TargetDevice::Versim, {0}, sdesc_path);
    });

    // Create dummy epoch command
    int cmd_words = epoch_queue::EPOCH_Q_SLOT_SIZE / sizeof(uint32_t);
    vector<uint32_t> cmd_blob(cmd_words, 0);
    auto cmd = std::make_shared<tt_hex>(cmd_blob, tt_hex_type::Misc, routing_core, "cmd");
    bool mmio_chip = true;

    // Test EQ of different sizes, prime non-pow-2 numbers are more intesresting
    vector<int> num_slots_sweep = {1, 3, 5, 11, 17, 53, 64, 4000};
    for (int num_slots : num_slots_sweep) {
        int wc_window_size = num_slots;

        tt_epoch_queue eq = create_and_init_epoch_queue(cluster.get(), num_slots, wc_window_size, routing_core, mmio_chip, num_cmds_per_binary, num_cmds_per_epoch_id, num_cmds_per_update_blob);
        int effective_wc_window_size = std::min(wc_window_size, host_mem::address_map::ETH_ROUTING_BLOCK_SIZE / epoch_queue::EPOCH_Q_SLOT_SIZE); // API limitation for max 1024 bytes over ethernet.
        log_info(tt::LogTest, "Starting test for num_slots: {} effective_wc_window_size: {}", num_slots, effective_wc_window_size);
        int num_wraparounds = 4;

        for (int i = 0; i < num_wraparounds; i++) {
            for (int j = 0; j < num_slots; j++) {
                eq.push_command(cmd, cluster.get());
                log_trace(tt::LogTest, "Pushed command for i: {} j: {} host occupancy: {} after push and possible flush.", i, j, eq.occupancy());
                ASSERT_TRUE(eq.occupancy() < effective_wc_window_size) << "Epoch Cmd Queue (Host/WC) exceeded limit"; // Don't bother with full modeling.
            }

            ASSERT_TRUE(eq.is_full_dram(cluster.get())) << "Epoch queue in DRAM should be full";
            ASSERT_EQ(eq.occupancy(), 0) << "Epoch Cmd Qeue (Host/WC) occupancy should be zero."; // Flushed on final slot automatically.

            for (int j = 0; j < num_slots; j++) {
                pop_epoch_queue(eq, cluster.get());
                ASSERT_EQ(eq.occupancy_dram(cluster.get()), num_slots - j - 1);
            }
            ASSERT_TRUE(eq.is_empty_dram(cluster.get())) << "Epoch queue should be empty";
        }
    }
}


// Do it for GS to start
// Single feature flag in code to guard this behavior for write combine path.

// Ideas to test:
// Different epoch queue sizes
// Flush at different points


TEST(EpochControl, EpochQueueIntermediateHostPushWriteCombine) {
    unordered_map<std::string, int> num_cmds_per_binary;
    unordered_map<int, int> num_cmds_per_epoch_id;
    unordered_map<std::string, int> num_cmds_per_update_blob;

    // Create 1x1 cluster
    tt_xy_pair routing_core(0, 0);
    auto cluster = quiet_call([&] {
        auto sdesc_path = test_path() + "device_descriptors/grayskull_1x1_arch.yaml";
        return get_cluster(tt::ARCH::GRAYSKULL, tt::TargetDevice::Versim, {0}, sdesc_path);
    });

    // Create dummy epoch command
    int cmd_words = epoch_queue::EPOCH_Q_SLOT_SIZE / sizeof(uint32_t);
    vector<uint32_t> cmd_blob(cmd_words, 0);
    auto cmd = std::make_shared<tt_hex>(cmd_blob, tt_hex_type::Misc, routing_core, "cmd");

    // Test EQ of different sizes, prime non-pow-2 numbers are more intesresting

    // Enable the WC feature. 0=Disabled, N=NEntires. Maxes out at num_slots.
    vector<int> num_slots_sweep = {1, 5, 17, 53, 64};
    vector<int> wc_window_size_sweep = {1, 3, 8, 32, 41};
    bool mmio_chip = false;

    for (int num_slots : num_slots_sweep) {
        for (int wc_window_size : wc_window_size_sweep) {

            if (wc_window_size > num_slots ) {
                log_info(tt::LogTest, "wc_window_size: {} num_slots: {} not valid - num_slots must be larger. Skipping...", wc_window_size, num_slots);
                continue;
            }

            int effective_wc_window_size = std::min(wc_window_size, host_mem::address_map::ETH_ROUTING_BLOCK_SIZE / epoch_queue::EPOCH_Q_SLOT_SIZE); // API limitation for max 1024 bytes over ethernet.
            log_info(tt::LogTest, "Starting test for num_slots: {} effective_wc_window_size: {}", num_slots, effective_wc_window_size);

            tt_epoch_queue eq = create_and_init_epoch_queue(cluster.get(), num_slots, wc_window_size, routing_core, mmio_chip, num_cmds_per_binary, num_cmds_per_epoch_id, num_cmds_per_update_blob);

            int occ_wc_buffer = 0;
            int occ_dram = 0;

            int num_wraparounds = 4;
            for (int i = 0; i < num_wraparounds; i++) {

                // Fill host intermediate epoch cmd queue.
                for (int j = 0; j < num_slots; j++) {
                    log_trace(tt::LogTest, "Pushing cmd j: {} num_slots: {}", j, num_slots);
                    eq.push_command(cmd, cluster.get());

                    // Model WC-Enabled and WC-Disabled cases 
                    if (wc_window_size != 0) {
                        occ_wc_buffer += 1;
                        bool is_eq_full         = occ_wc_buffer == num_slots;
                        bool is_wc_window_full  = occ_wc_buffer == effective_wc_window_size;
                        bool is_final_eq_slot   = occ_dram + occ_wc_buffer == num_slots;
                        bool do_flush           = is_eq_full || is_wc_window_full || is_final_eq_slot;
                        log_trace(tt::LogTest, "get_wr_ptr: {} occ_wc_buffer: {} is_final_eq_slot: {} do_flush: {}", eq.get_wr_ptr(), occ_wc_buffer, is_final_eq_slot, do_flush);

                        if (do_flush) {
                            log_trace(tt::LogTest, "Modelling flush to device since occ_wc_buffer: {} exceeds wc_window_size: {}", occ_wc_buffer, wc_window_size);
                            occ_dram += occ_wc_buffer;
                            occ_wc_buffer = 0;
                            // Real flush should make sure DRAM can hold it.
                        }
                        ASSERT_TRUE(eq.occupancy() < effective_wc_window_size) << "Epoch Cmd Queue (Host/WC) exceeded limit";
                    } else {
                        occ_dram += 1;
                    }

                    log_trace(tt::LogTest, "Finished slot j: {} wc_window_size: {} num_slots: {} occ_wc_buffer: {} occ_dram: {}",
                        j, wc_window_size, num_slots, occ_wc_buffer, occ_dram);

                    ASSERT_EQ(eq.occupancy(), occ_wc_buffer) << "Host Epoch Queue WC buffer incorrect occupancy";
                    ASSERT_EQ(eq.occupancy_dram(cluster.get()), occ_dram) << "DRAM Epoch Queue incorrect occupancy";

                }
            
                // After reaching end of epoch queue, everything should be flushed.
                ASSERT_TRUE(occ_wc_buffer == 0) << "Occupancy of WC buffer should be zero";
                ASSERT_TRUE(eq.is_full_dram(cluster.get())) << "DRAM Epoch Queue should be full";

                // Pop entire epoch cmd queue in DRAM and assert empty.
                for (int j = 0; j < num_slots; j++) {
                    pop_epoch_queue(eq, cluster.get());
                    ASSERT_EQ(eq.occupancy_dram(cluster.get()), num_slots - j - 1);
                    occ_dram -= 1;
                }
                ASSERT_TRUE(eq.is_empty_dram(cluster.get())) << "DRAM Epoch Queue should be empty";

            }

        }
    }

}

