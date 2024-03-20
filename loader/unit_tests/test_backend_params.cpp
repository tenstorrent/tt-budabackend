// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "gtest/gtest.h"
#include "test_unit_common.hpp"
#include "runtime/runtime_utils.hpp"
#include "runtime/runtime.hpp"

int get_num_harvested_rows(tt::ARCH arch) {
    auto devices_present = tt_cluster::detect_available_devices(TargetDevice::Silicon);
    if (devices_present.empty())
        return 0;

    auto arch_present = get_machine_arch();
    if (arch_present != arch)
        return 0;

    return get_param<int>("system-device0-num_harvested_rows");
}

TEST(BackendParamLib, CustomSOCDesc) {
    std::string grid_size = tt::backend::get_backend_param("Grayskull-T6-grid_size", "");
    std::string num_cores = tt::backend::get_backend_param("Grayskull-T6-num_cores", "");

    ASSERT_TRUE(grid_size == "12-10") << "Grid size for a non-harvested Grayskull should be 12-10!";
    ASSERT_TRUE(num_cores == "120") << "A non-harvested Grayskull should have 120 cores!";
    
    auto harvested_desc = tt::backend::get_custom_device_desc(tt::ARCH::GRAYSKULL, false, 6);
    
    grid_size = tt::backend::get_backend_param("Grayskull-T6-grid_size", harvested_desc.soc_desc_yaml);
    num_cores = tt::backend::get_backend_param("Grayskull-T6-num_cores", harvested_desc.soc_desc_yaml);

    ASSERT_TRUE(harvested_desc.harvesting_mask == 6) << "Invalid harvesting mask";
    ASSERT_TRUE(grid_size == "12-8") << "Grayskull grid size with harvesting mask 6 should be 12-8!";
    ASSERT_TRUE(num_cores == "96") <<  "Grayskull with harvesting mask 6 should have 120 cores!";
    
    auto single_core_desc = tt::backend::get_custom_device_desc(tt::ARCH::WORMHOLE_B0, false, 0, {1, 1});
    grid_size = tt::backend::get_backend_param("Wormhole_b0-T6-grid_size", single_core_desc.soc_desc_yaml);
    num_cores = tt::backend::get_backend_param("Wormhole_b0-T6-num_cores", single_core_desc.soc_desc_yaml);

    ASSERT_TRUE(grid_size == "1-1") << "Grid size for single core Grayskull should be 1x1!";
    ASSERT_TRUE(num_cores == "1") <<  "Single core Grayskull should have 1 core!";

    // Clear the params cache to avoid affecting other tests
    tt::backend::clear_backend_param_cache();
}

// Unspecified arch should either match default descriptor
// OR in the case of a harvested part, it needs to perform harvesting under the hood
TEST(BackendParamLib, APIUnspecifiedArch) {
    std::vector<tt::ARCH> arch_vec = {tt::ARCH::GRAYSKULL, tt::ARCH::WORMHOLE, tt::ARCH::WORMHOLE_B0};
    auto available_devices = tt_cluster::detect_available_devices(TargetDevice::Silicon);
    // Only values that may change based on descriptor are interesting, static values like hard coded values are not tested
    for (auto arch : arch_vec) {
        auto arch_name = get_arch_str(arch);
        auto sdesc = get_default_soc_desc(arch);

        // If arch under test matches machine arch, update harvesting info for expected calculation
        tt::param::DeviceDesc harvested_desc = tt::backend::get_custom_device_desc(arch, false, 0);
        if(std::find(available_devices.begin(), available_devices.end(), arch) != available_devices.end()) {
            harvested_desc = tt::backend::get_device_descs_for_available_devices()[0];
        }
        int num_harvested_rows = get_num_harvested_rows(arch);
        log_info(tt::LogTest, "Verifying arch: {}, harvested rows: {}", arch_name, num_harvested_rows);

        int num_chans = sdesc->get_dram_chan_map().size();
        int num_chans_param_lib = get_param<int>(arch_name + "-dram-num_channels", harvested_desc.soc_desc_yaml);
        ASSERT_EQ(num_chans, num_chans_param_lib);

        vector<uint16_t> t6_cores_per_chan = tt_epoch_dram_manager::get_num_cores_per_dram_channel(arch, sdesc->workers, sdesc->dram_core_channel_map, num_chans);
        vector<uint16_t> eth_cores_per_chan = tt_epoch_dram_manager::get_num_cores_per_dram_channel(arch, sdesc->ethernet_cores, sdesc->dram_core_channel_map, num_chans);

        // For unharvested part, check that param lib matches default backend dram carve out
        if (num_harvested_rows == 0) {
            for (int chan = 0; chan < num_chans; chan++) {
                uint64_t expected = tt_epoch_dram_manager::get_top_of_q_update_blobs(t6_cores_per_chan[chan], eth_cores_per_chan[chan]);
                uint64_t observed = get_param<uint64_t>(arch_name + "-dram-backend_reserved_chan" + std::to_string(chan), harvested_desc.soc_desc_yaml);
                log_info(tt::LogTest, "{} dram channel: {}, expected: {}, observed: {}", arch_name, chan, expected, observed);
                ASSERT_EQ(expected, observed);
            }
        }

        tt_xy_pair grid_size_expected = sdesc->worker_grid_size;
        tt_xy_pair grid_size_param_lib = get_param<tt_xy_pair>(arch_name + "-T6-grid_size", harvested_desc.soc_desc_yaml);

        int num_workers_expected = (int)sdesc->workers.size();
        int num_workers_param_lib = get_param<int>(arch_name + "-T6-num_cores", harvested_desc.soc_desc_yaml);

        grid_size_expected.y -= num_harvested_rows;
        num_workers_expected -= num_harvested_rows * grid_size_expected.x;

        ASSERT_EQ(num_workers_expected, num_workers_param_lib);
        ASSERT_EQ(grid_size_expected, grid_size_param_lib);

        // Clear the params cache to avoid affecting subsequent lookups
        tt::backend::clear_backend_param_cache();
    }
}

// Explicitly specified arch should always match the specified descriptor
// even if the current machine is not harvested or has as different grid size
TEST(BackendParamLib, APIWormholeHarvestedArch) {
    std::vector<std::pair<tt::ARCH, std::string>> arch_descriptions = {
        {tt::ARCH::GRAYSKULL, tt::buda_home() + "device/grayskull_10x12.yaml"},
        {tt::ARCH::WORMHOLE, tt::buda_home() + "device/wormhole_8x10.yaml"},
        {tt::ARCH::WORMHOLE_B0, tt::buda_home() + "device/wormhole_b0_8x10.yaml"},
        {tt::ARCH::WORMHOLE_B0, tt::buda_home() + "device/wormhole_b0_9x8_harvested.yaml"},
        {tt::ARCH::WORMHOLE_B0, tt::buda_home() + "device/wormhole_b0_1x1.yaml"}
    };

    // Test that queried values match the defaults backend flow uses
    // Static values that don't change based on descriptors are not tested here
    for (const auto& [arch, sdesc_path] : arch_descriptions) {
        auto arch_name = get_arch_str(arch);
        auto sdesc = load_soc_descriptor_from_file(arch, sdesc_path);
        log_debug(tt::LogTest, "Param DB loaded {} parameters using {}", get_param<int>("*", sdesc_path), sdesc_path);

        int num_chans = sdesc->get_dram_chan_map().size();
        int num_chans_param_lib = get_param<int>(arch_name + "-dram-num_channels");
        ASSERT_EQ(num_chans, num_chans_param_lib);

        vector<uint16_t> t6_cores_per_chan = tt_epoch_dram_manager::get_num_cores_per_dram_channel(arch, sdesc->workers, sdesc->dram_core_channel_map, num_chans);
        vector<uint16_t> eth_cores_per_chan = tt_epoch_dram_manager::get_num_cores_per_dram_channel(arch, sdesc->ethernet_cores, sdesc->dram_core_channel_map, num_chans);
        for (int chan = 0; chan < num_chans; chan++) {
            uint64_t expected = tt_epoch_dram_manager::get_top_of_q_update_blobs(t6_cores_per_chan[chan], eth_cores_per_chan[chan]);
            uint64_t observed = get_param<uint64_t>(arch_name + "-dram-backend_reserved_chan" + std::to_string(chan), sdesc_path);
            ASSERT_EQ(expected, observed);
        }

        int num_workers = (int)sdesc->workers.size();
        int num_workers_param_lib = get_param<int>(arch_name + "-T6-num_cores", sdesc_path);
        ASSERT_EQ(num_workers, num_workers_param_lib);

        tt_xy_pair grid_size = sdesc->worker_grid_size;
        tt_xy_pair grid_size_param_lib = get_param<tt_xy_pair>(arch_name + "-T6-grid_size", sdesc_path);
        ASSERT_EQ(grid_size, grid_size_param_lib);
    }

    // Clear the params cache to avoid affecting other tests
    tt::backend::clear_backend_param_cache();
}

TEST(BackendParamLib, ExplicitSOCDescAPI) {
    tt_runtime_config config;
    config.mode = DEVICE_MODE::CompileOnly;
    config.type = tt::DEVICE::Silicon;
    config.arch = ARCH::WORMHOLE_B0;
    config.output_dir = "./harvesting_test/";
    config.soc_descriptor_path = "device/wormhole_b0_1x1.yaml";
    config.harvested_rows = {{0, {1}}}; // 1 row harvested
    tt_runtime target_backend(config, {0});
    auto sdesc = load_soc_descriptor_from_yaml(get_soc_desc_path(0, true));
    auto overlay_sdesc = load_soc_descriptor_from_yaml(get_soc_desc_path(0));
    ASSERT_EQ(sdesc -> workers.size(), 1) << "No harvesting should be performed on the 1x1 descriptor when it is expilictly passed in.";
    ASSERT_EQ(overlay_sdesc -> workers.size(), 1) << "No harvesting should be performed on the 1x1 descriptor when it is expilictly passed in.";
    ASSERT_EQ(overlay_sdesc -> workers.at(0).x, 18) << "Overlay descriptors should use NOC translation.";
    ASSERT_EQ(overlay_sdesc -> workers.at(0).y, 18) << "Overlay descriptors should use NOC translation.";
    ASSERT_EQ(get_soc_desc_path(0, true),  "./harvesting_test//device_desc_runtime/0.yaml") << "Runtime desc should be in device_desc_runtime/";
    ASSERT_EQ(get_soc_desc_path(0, false),  "./harvesting_test//device_descs/0.yaml") << "Overlay desc should be in device_descs/.";
    target_backend.finish();
}

TEST(BackendParamLib, AutomaticSOCDescAPI) {
    tt_runtime_config config;
    config.mode = DEVICE_MODE::CompileOnly;
    config.type = tt::DEVICE::Silicon;
    config.arch = ARCH::WORMHOLE_B0;
    config.output_dir = "./harvesting_test/";
    config.harvested_rows = {{0, {1}}}; // 1 row harvested
    tt_runtime target_backend(config, {0});
    auto sdesc = load_soc_descriptor_from_yaml(get_soc_desc_path(0, true));

    ASSERT_EQ(sdesc -> workers.size(), 72) << "Harvesting on the default WH descriptor should be performed.";
    ASSERT_EQ(get_soc_desc_path(0, true),  "./harvesting_test//device_desc_runtime/0.yaml") << "Expected a harvested runtime descriptor.";
    target_backend.finish();
}

TEST(BackendParamLib, WorkerCoordTranslation) {
    auto device_yaml = test_path() + "device_descriptors/wormhole_b0_8x10.yaml";

    // Add some test cases that are corners of the grid, and hit the dram rows
    std::vector<std::tuple<std::uint32_t, std::uint32_t>> logical = {
        {0,0}, {2,2}, {4,1}, {7,9}
    };
    std::vector<std::tuple<std::uint32_t, std::uint32_t>> physical = {
        {1,1}, {3,3}, {6,2}, {9,11}
    };

    for (int i = 0; i < physical.size(); i++) {
        // Test logical to physical
        {
            auto expected = physical[i];
            auto observed = tt::backend::translate_coord_logical_to_routing(device_yaml, logical[i]);
            log_debug(
                tt::LogTest,
                "Translated logical ({},{}) to physical ({},{})",
                std::get<0>(logical[i]),
                std::get<1>(logical[i]),
                std::get<0>(observed),
                std::get<1>(observed));
            ASSERT_EQ(expected, observed);
        }
        // Test physical to logical
        {
            auto expected = logical[i];
            auto observed = tt::backend::translate_coord_routing_to_logical(device_yaml, physical[i]);
            log_debug(
                tt::LogTest,
                "Translated phyiscal ({},{}) to logical ({},{})",
                std::get<0>(physical[i]),
                std::get<1>(physical[i]),
                std::get<0>(observed),
                std::get<1>(observed));
            ASSERT_EQ(expected, observed);
        }
    }
}

TEST(BackendParamLib, DramCoordTranslation) {
    auto device_yaml = test_path() + "device_descriptors/wormhole_b0_8x10.yaml";
    std::string device_param = "wormhole_b0-dram-core_xy";

    // Add some test cases that are corners of the grid, and hit the dram rows
    std::vector<std::tuple<std::uint32_t, std::uint32_t>> dram = {
        {0, 0}, {0, 1}, {0, 2}, {2, 0}, {2, 1}, {2, 2}, {5, 0}, {5, 1}, {5, 2}};
    std::vector<std::tuple<std::uint32_t, std::uint32_t>> physical = {
        {0, 0}, {0, 1}, {0, 11}, {5, 0}, {5, 1}, {5, 11}, {5, 5}, {5, 6}, {5, 7}
    };



    for (int i = 0; i < dram.size(); i++) {
        // Test DRAM to physical
        {
            std::string dram_param = device_param + 
                "_chan" + std::to_string(std::get<0>(dram[i])) + 
                "_subchan" + std::to_string(std::get<1>(dram[i]));
            auto expected = physical[i];

            // parse string "x-y" into tuple {x,y}
            auto value = tt::backend::get_backend_param(dram_param, device_yaml);
            auto delimeter = value.find("-");
            auto x_str = value.substr(0, delimeter);
            auto y_str = value.substr(delimeter + 1, std::string::npos);
            auto observed = std::make_tuple<std::uint32_t, std::uint32_t>(std::stoi(x_str, 0, 0), std::stoi(y_str, 0, 0));

            log_debug(
                tt::LogTest,
                "Translated DRAM chan={} subchan={} to physical ({},{})",
                std::get<0>(dram[i]),
                std::get<1>(dram[i]),
                std::get<0>(observed),
                std::get<1>(observed));
            ASSERT_EQ(expected, observed);
        }
    }
}
