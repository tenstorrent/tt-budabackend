// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <fstream>
#include <errno.h>

#include "tt_cluster.hpp"
#include "common/cache_lib.hpp"
#include "verif.hpp"
#include "tt_backend_api_types.hpp"
#include "verif.hpp"
#include "device/cpuset_lib.hpp"

// #include <hwloc.h>

// FIXME - Make this test or wrapper able to sweep over various things
struct transfer_info{
    std::string label = "";
    int target_device = -1;
    int dram_channel = -1;
    int dram_addr = -1;
    int size_mb = -1;
    int num_loops = -1;
    bool is_write = false;
    bool small_access = false;
};

typedef struct transfer_info transfer_info;

std::string get_indented_string(int length, std::string message) {

    // Issue int desired_length = std::max(length,  message.length());
    int desired_length = length > message.length() ? length : message.length();
    std::string indent(desired_length - message.length(), ' ');
    return message + indent;
}

// For nicer aligned formatted print statements of rd/wr metrics
std::string float_to_string(float val, int precision, std::string suffix ="", int indent_length=0){
    std::stringstream stream;
    stream << std::fixed << std::setprecision(precision) << val;
    std::string s = stream.str();
    if (suffix != ""){
        s += " " + suffix;
    }
    return get_indented_string(indent_length, s);
}

std::string int_to_hex_string(int val)
{
   std::stringstream stream;
   stream << hex << val;
   std::string hex_str = "0x" + stream.str();
   return get_indented_string(10, hex_str);
}

// Borrowed from runtime code, to keep a close eye on AICLK
void query_all_device_aiclks(tt_cluster *cluster, TargetDevice target_type, int aiclk_target, std::string loc) {
    if (target_type == TargetDevice::Silicon){

        std::map<int, int> freq_all_devices = cluster->get_all_device_aiclks();
        for (auto &device: freq_all_devices){
            auto device_id = device.first;
            auto aiclk = device.second;
            if (aiclk_target > 0 && aiclk < aiclk_target){
                log_fatal("Observed AICLK: {} for device_id: {} less than target_aiclk: {} at {}", aiclk, device_id, aiclk_target, loc);
            }else{
                log_info(tt::LogTest, "Observed AICLK: {} for device_id: {} matches/exceeds target aiclk: {} at {}", aiclk, device_id, aiclk_target, loc);
            }
        }
    }
}


// Issue configurable size looped read or write to device DRAM for measuring performance.
uint64_t dram_read_or_write(tt_cluster *cluster, const transfer_info &xfer_info, bool warm_up) {

    if (xfer_info.size_mb == 0){
        return 0;
    }

    std::thread::id sys_local_thread_id = std::this_thread::get_id();
    log_debug(tt::LogTest, "dram_read_or_write() is_write: {} for size_mb: {} addr: {} channel: {} target_device: {} loops: {} tid: {}", 
    xfer_info.is_write, xfer_info.size_mb,  xfer_info.dram_addr,  xfer_info.dram_channel,  xfer_info.target_device, xfer_info.num_loops, sys_local_thread_id);

    int bytes_per_datum = 4; // uint32
    int rd_wr_bytes = xfer_info.size_mb * (1024 * 1024);
    int num_elements = rd_wr_bytes / bytes_per_datum;

    std::vector<uint32_t> vec(num_elements, (uint32_t)verif::random::tt_rnd_int(numeric_limits<int>::min(), numeric_limits<int>::max()));
    tt_target_dram dram = {xfer_info.target_device, xfer_info.dram_channel, 0};

    // Warm up loops.
    if (warm_up){

        auto warm_up_loops = xfer_info.num_loops > 1 ? xfer_info.num_loops / 2 : xfer_info.num_loops;
        if (xfer_info.is_write){
            for (int i=0; i<warm_up_loops; i++){
                cluster->write_dram_vec(vec, dram, xfer_info.dram_addr, xfer_info.small_access);
            }
        }else{
            for (int i=0; i<warm_up_loops; i++){
                cluster->read_dram_vec(vec, dram, xfer_info.dram_addr, rd_wr_bytes, xfer_info.small_access);
            }
        }

    }

    // Read or Write === 
    auto start_time = std::chrono::high_resolution_clock::now();

    if (xfer_info.is_write){
        for (int i=0; i<xfer_info.num_loops; i++){
            auto cpu_id = sched_getcpu();
            log_debug(tt::LogTest, "Doing transfer {} [DeviceId: {} Ch {}] on CPU: {}", xfer_info.label, xfer_info.target_device, xfer_info.dram_channel, cpu_id);
            cluster->write_dram_vec(vec, dram, xfer_info.dram_addr, xfer_info.small_access);
        }
    }else{
        for (int i=0; i<xfer_info.num_loops; i++){
            cluster->read_dram_vec(vec, dram, xfer_info.dram_addr, rd_wr_bytes, xfer_info.small_access);
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    // Metrics ===
    auto total_size_mb =  xfer_info.size_mb *  xfer_info.num_loops;
    float bandwidth_gb = ((float) total_size_mb / (1024)) / ((float) duration / 1.0e6);

    auto bw_gb_str          = float_to_string(bandwidth_gb, 2, "GB/s");
    auto bw_mb_str          = float_to_string(bandwidth_gb * 1024, 2, "MB/s", 13);
    auto duration_s_str     = float_to_string(duration/1.0e6, 2, "sec");
    auto size_mb_str        = float_to_string(total_size_mb, 2, "MB", 13);
    auto addr_str           = int_to_hex_string(xfer_info.dram_addr);
    auto rd_wr_str          = xfer_info.is_write ? "Writes" : "Reads ";
    auto spacer             = total_size_mb < 100 ? "\t" : "\t";  // Try to make prints line up.
    auto cpu_id             = sched_getcpu();

    log_info(tt::LogTest, "Transfer {} perf [DeviceId: {} {} @ Addr {} Ch {}] => BW: {} ({})\tDuration: {} ({} us) CPU: {}", xfer_info.label, xfer_info.target_device, size_mb_str, addr_str, xfer_info.dram_channel, bw_mb_str, bw_gb_str, duration_s_str, duration, cpu_id);

    return duration;

}


// Go through transfers and compute some interesting overall perf metrics across all threads/transfers per device, and all devices. Return true is passing perf checks.
bool compute_and_check_overall_perf(const std::set<chip_id_t> &target_device_ids, const std::vector<transfer_info> &transfers, std::vector<uint64_t> runtime_per_thread, float per_device_write_bw_gb_target, float per_device_read_bw_gb_target){

    // Take per-thread runtimes and compute max-runtime per-device. Assumes they all start at the same time (reasonable..)
    std::map<chip_id_t, uint64_t> max_runtime_per_device_id;
    uint64_t max_thread_runtime = 0;

    for (auto &target_device : target_device_ids){
        max_runtime_per_device_id.insert({target_device, 0});
    }

    auto num_threads = transfers.size();
    log_assert(num_threads == runtime_per_thread.size(), "mismatch in sizes");

    for (int local_thread_id = 0; local_thread_id < num_threads; local_thread_id++){
        auto thread_runtime = runtime_per_thread.at(local_thread_id);
        auto device_id      = transfers.at(local_thread_id).target_device;
        max_runtime_per_device_id.at(device_id) = max_runtime_per_device_id.at(device_id) > thread_runtime ? max_runtime_per_device_id.at(device_id) : thread_runtime;
        max_thread_runtime = thread_runtime > max_thread_runtime ? thread_runtime : max_thread_runtime;
    }

    auto max_thread_runtime_str_sec = float_to_string(max_thread_runtime/1.0e6, 2);

    log_info(tt::LogTest, "Test Longest Thread Time: {} seconds ({} us)", max_thread_runtime_str_sec, max_thread_runtime);


    std::map<chip_id_t, int> write_size_mb_per_device;
    std::map<chip_id_t, int> read_size_mb_per_device;

    for (auto &transfer_info : transfers){
        if (transfer_info.is_write){
            write_size_mb_per_device[transfer_info.target_device] += transfer_info.num_loops * transfer_info.size_mb; 
        }else{
            read_size_mb_per_device[transfer_info.target_device] += transfer_info.num_loops * transfer_info.size_mb; 
        }
    }

    float all_devices_combined_write_size_gb = 0;
    float all_devices_combined_read_size_gb = 0;

    int perf_fails = 0;

    for (auto &target_device : target_device_ids){
        auto device_duration            = max_runtime_per_device_id.at(target_device);
        auto device_duration_str_sec    = float_to_string(device_duration/1.0e6, 2);

        auto combined_write_size_gb     = (float) write_size_mb_per_device[target_device] / 1024;
        auto combined_read_size_gb      = (float) read_size_mb_per_device[target_device] / 1024;
        float combined_write_bw_gb      = combined_write_size_gb/ ((float) device_duration / 1.0e6);
        float combined_read_bw_gb       = combined_read_size_gb / ((float) device_duration / 1.0e6);
        auto combined_write_size_gb_str = float_to_string(combined_write_size_gb, 2, "GB");
        auto combined_read_size_gb_str  = float_to_string(combined_read_size_gb, 2, "GB");

        auto combined_write_bw_gb_str   = float_to_string(combined_write_bw_gb, 2, "GB/s");
        auto combined_read_bw_gb_str    = float_to_string(combined_read_bw_gb, 2, "GB/s");

        log_info(tt::LogTest, "All Threads  ({} seconds) perf for DeviceId: {}\t => WriteSize: {}   CombinedWriteBW: {}\tReadSize: {}   CombinedReadBW: {}", device_duration_str_sec, target_device, combined_write_size_gb_str, combined_write_bw_gb_str, combined_read_size_gb_str, combined_read_bw_gb_str);
    
        all_devices_combined_write_size_gb += combined_write_size_gb;
        all_devices_combined_read_size_gb += combined_read_size_gb;

        // Check perf results per device against targets.
        if (combined_write_bw_gb < per_device_write_bw_gb_target){
            log_error("Perf Fail: DeviceId: {} Combined (all threads) Write BW: {} GB/s is less than expected Target: {} GB/s", target_device, combined_write_bw_gb, per_device_write_bw_gb_target);
            perf_fails++;
        }

        if (combined_read_bw_gb < per_device_read_bw_gb_target){
            log_error("Perf Fail: DeviceId: {} Combined (all threads) Read  BW: {} GB/s is less than expected Target: {} GB/s", target_device, combined_read_bw_gb, per_device_read_bw_gb_target);
            perf_fails++;
        }

    }

    // Combined results covering all devices if more than 1.
    // if (target_device_ids.size() > 1){

        float all_combined_write_bw_gb      = all_devices_combined_write_size_gb/ ((float) max_thread_runtime / 1.0e6);
        float all_combined_read_bw_gb       = all_devices_combined_read_size_gb / ((float) max_thread_runtime / 1.0e6);

        auto all_devices_combined_write_size_gb_str     = float_to_string(all_devices_combined_write_size_gb, 2, "GB");
        auto all_devices_combined_read_size_gb_str      = float_to_string(all_devices_combined_read_size_gb, 2, "GB");
        auto all_combined_write_bw_gb_str               = float_to_string(all_combined_write_bw_gb, 2, "GB/s");
        auto all_combined_read_bw_gb_str                = float_to_string(all_combined_read_bw_gb, 2, "GB/s");
        log_info(tt::LogTest, "Overall Test ({} seconds) perf for All {} Devices\t => WriteSize: {}   CombinedWriteBW: {}\tReadSize: {}   CombinedReadBW: {}", max_thread_runtime_str_sec, target_device_ids.size(), all_devices_combined_write_size_gb_str, all_combined_write_bw_gb_str, all_devices_combined_read_size_gb_str, all_combined_read_bw_gb_str);

    // }

    return perf_fails == 0;

}


int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool pass = true;
    int seed;

    uint32_t rd_addr, wr_addr, small_addr;
    int rd_ch, wr_ch, small_ch;
    int read_size_mb, write_size_mb, small_size_mb, fast_tilize_size_mb;
    int read_loops, write_loops, small_loops, fast_tilize_loops;

    std::string target_device;
    int aiclk_target;
    bool small_access_thread;
    bool fast_tilize_thread = false;
    bool use_all_devices;
    bool no_threads;
    float per_device_write_bw_gb_target, per_device_read_bw_gb_target;
    bool warm_up;
    int thread_multiplier = 1;

    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(seed, args)         = verif_args::get_command_option_int32_and_remaining_args(args, "--seed", -1);
    std::tie(aiclk_target, args)  =  verif_args::get_command_option_uint32_and_remaining_args(args, "--aiclk-target", 1200); // Purely for checking.

    // Thread 1 (Large Writes)
    std::tie(wr_addr, args)         = verif_args::get_command_option_uint32_and_remaining_args(args, "--wr-addr", 0x08000000);
    std::tie(wr_ch, args)           = verif_args::get_command_option_uint32_and_remaining_args(args, "--wr-ch", 1);
    std::tie(write_size_mb, args)   = verif_args::get_command_option_uint32_and_remaining_args(args, "--write-size-mb", 64);
    std::tie(write_loops, args)     = verif_args::get_command_option_uint32_and_remaining_args(args, "--write-loops", 100);

    // Thread 2 (Large Reads)
    std::tie(rd_addr, args)         = verif_args::get_command_option_uint32_and_remaining_args(args, "--rd-addr", 0x18000000);
    std::tie(rd_ch, args)           = verif_args::get_command_option_uint32_and_remaining_args(args, "--rd-ch",2 );
    std::tie(read_size_mb, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--read-size-mb", 64);
    std::tie(read_loops, args)      = verif_args::get_command_option_uint32_and_remaining_args(args, "--read-loops", 1);

    // Thread 3 (Small Access Write)
    std::tie(small_access_thread, args) =  verif_args::has_command_option_and_remaining_args(args, "--small-access-thread");
    std::tie(small_addr, args)          = verif_args::get_command_option_uint32_and_remaining_args(args, "--small-addr", 0x28000000);
    std::tie(small_ch, args)            = verif_args::get_command_option_uint32_and_remaining_args(args, "--small-ch",3 );
    std::tie(small_size_mb, args)       = verif_args::get_command_option_uint32_and_remaining_args(args, "--small-size-mb", 64);
    std::tie(small_loops, args)         = verif_args::get_command_option_uint32_and_remaining_args(args, "--small-loops", 100);

    // Thread 4 Fast Tilize (doesn't use static TLB's with cluster->write_mem_vec() path) FIXME
    // std::tie(fast_tilize_thread, args)  =  verif_args::has_command_option_and_remaining_args(args, "--fast-tilize-thread");
    // std::tie(fast_tilize_size_mb, args)   = verif_args::get_command_option_uint32_and_remaining_args(args, "--fast-tilize-size-mb", 64);
    // std::tie(fast_tilize_loops, args)     = verif_args::get_command_option_uint32_and_remaining_args(args, "--fast-tilize-loops", 100);

    std::tie(target_device, args)       = verif_args::get_command_option_and_remaining_args(args, "--target-device", "0"); // Comma separated list
    std::tie(use_all_devices, args)     = verif_args::has_command_option_and_remaining_args(args, "--use-all-devices");
    std::tie(no_threads, args)          = verif_args::has_command_option_and_remaining_args(args, "--no-threads");
    std::tie(warm_up, args)             = verif_args::has_command_option_and_remaining_args(args, "--warm-up");

    // Make more threads. This many per type (large write, large read, small, etc.)
    std::tie(thread_multiplier, args)         = verif_args::get_command_option_uint32_and_remaining_args(args, "--thread-multiplier", 1);

    std::vector<std::string> target_device_str_vector;
    verif_args::split_string_into_vector(target_device_str_vector, target_device, ",");


    // Perf Checking to fail test if per-device combined (across all threads) write/read throughput is less than target
    std::tie(per_device_write_bw_gb_target, args) = verif_args::get_command_option_double_and_remaining_args(args, "--per-device-write-bw-gb-target", 0);
    std::tie(per_device_read_bw_gb_target, args) = verif_args::get_command_option_double_and_remaining_args(args, "--per-device-read-bw-gb-target", 0);

    verif_args::validate_remaining_args(args);

    auto test_start = std::chrono::high_resolution_clock::now();

    // Randomization seeding
    if (seed == -1) {
        seed = verif::random::tt_gen_seed();
        log_info(tt::LogTest, "Unspecified cmdline --seed , generated random seed {}", seed);
    }
    verif::random::tt_rnd_set_seed(seed);

    const TargetDevice target_type = TargetDevice::Silicon; // Test is for silicon only.
    const tt::ARCH arch = tt::ARCH::GRAYSKULL; // FIXME

    const std::string sdesc_file = get_soc_description_file(arch, target_type);
    bool has_input_q = false;

    // Can use comma separated list of device_ids, or all devices reserved
    std::set<chip_id_t> target_device_ids;

    if (use_all_devices){
        auto target_device_ids_vector = tt_SiliconDevice::detect_available_device_ids();
        for (auto &device_id: target_device_ids_vector){
            target_device_ids.insert(device_id);
        }
    }else{
        for (auto &device_id: target_device_str_vector){
            target_device_ids.insert(stoi(device_id));
        }
    }

    // Push thread info into an array and just keep dispatching threads.
    std::vector<transfer_info> transfers;

    for (auto &target_device : target_device_ids){

        for (int i=0; i< thread_multiplier; i++){

            std::string suffix  = thread_multiplier > 1 ? "_thread_" + std::to_string(i) : "";
            int indent_amt      = thread_multiplier ? 28 : 20;

            // Setup Write and Read Info into self contained structs.
            if (write_size_mb > 0){
                transfer_info t0_xfer;
                t0_xfer.label            = get_indented_string(indent_amt, "LargeWrite" + suffix);
                t0_xfer.size_mb          = write_size_mb;
                t0_xfer.dram_channel     = wr_ch;
                t0_xfer.dram_addr        = wr_addr;
                t0_xfer.target_device    = target_device;
                t0_xfer.num_loops        = write_loops;
                t0_xfer.is_write         = true;
                t0_xfer.small_access     = false;
                transfers.push_back(t0_xfer);
            }

            if (read_size_mb > 0){
                transfer_info t1_xfer;
                t1_xfer.label            = get_indented_string(indent_amt, "LargeRead" + suffix);
                t1_xfer.size_mb          = read_size_mb;
                t1_xfer.dram_channel     = rd_ch;
                t1_xfer.dram_addr        = rd_addr;
                t1_xfer.target_device    = target_device;
                t1_xfer.num_loops        = read_loops;
                t1_xfer.is_write         = false;
                t1_xfer.small_access     = false;
                transfers.push_back(t1_xfer);
            }

            if (small_access_thread && small_size_mb > 0 ){
                transfer_info t2_xfer;
                t2_xfer.label            = get_indented_string(indent_amt, "SmallAccessWrite" + suffix);
                t2_xfer.size_mb          = small_size_mb;
                t2_xfer.dram_channel     = small_ch;
                t2_xfer.dram_addr        = small_addr;
                t2_xfer.target_device    = target_device;
                t2_xfer.num_loops        = small_loops;
                t2_xfer.is_write         = true;
                t2_xfer.small_access     = true;
                transfers.push_back(t2_xfer);
            }

            // if (fast_tilize_thread &&  fast_tilize_size_mb > 0){
            //     transfer_info t3_xfer;
            //     t3_xfer.label            = get_indented_string(indent_amt, "FastTilizeWrite" + suffix);
            //     t3_xfer.size_mb          = fast_tilize_size_mb;
            //     t3_xfer.dram_channel     = 0;
            //     t3_xfer.dram_addr        = 0x30000000;
            //     t3_xfer.target_device    = target_device;
            //     t3_xfer.num_loops        = fast_tilize_loops;
            //     t3_xfer.is_write         = true;
            //     t3_xfer.small_access     = false;
            //     transfers.push_back(t3_xfer);
            // }

        }
    }

    // Ring topology for GS cluster requires no gaps in target_device_ids used, so fill them in now even though no xfers will go to them.
    int starting_device_id  = *target_device_ids.begin();
    int ending_device_id    = *target_device_ids.rbegin();
    int num_enabled_devices = target_device_ids.size();

    if ((starting_device_id + num_enabled_devices - 1) != ending_device_id){
        log_info(tt::LogTest, "Gaps detected in target_device_ids, filling them in now to use all device_ids between: starting_device_id: {} ending_device_id: {}", starting_device_id, ending_device_id);
    }

    std::set<chip_id_t> cluster_target_device_ids;

    for (int i=starting_device_id; i < ending_device_id+1; i++){
        cluster_target_device_ids.insert(i);
    }

    try {
        tt_cluster *cluster = new tt_cluster;
        cluster->open_device(arch, target_type, cluster_target_device_ids, sdesc_file, "", true, false, true);
        cluster->start_device({.init_device = true}); // Must be true otherwise AICLK is low.

        log_info(tt::LogTest, "Device(s) started, launching {} transfers {} (targeting {} devices)...", transfers.size(), no_threads ? "serially" : "concurrently", target_device_ids.size());

        auto num_threads = transfers.size();
        std::vector<std::thread> threads(num_threads);
        std::vector<uint64_t> runtime_per_thread(num_threads);

        if (warm_up){
            log_info(tt::LogTest, "Note: Warm-up enabled. Each thread will warm up with reads/writes, time not reflected in per-thread perf results, only overall test time.");
        }

        // Serial, or Concurrent Threads for Reads, Writes, SmallAccess, FastTilize
        if (no_threads){
            for (auto &transfer_info : transfers){
                auto duration_us = dram_read_or_write(cluster, transfer_info, warm_up);
            }
        }else{

            int local_thread_id = 0;


            for (auto &transfer_info : transfers){

                threads[local_thread_id] = std::thread([&threads, &runtime_per_thread, local_thread_id, cluster, transfer_info, warm_up]{
         
                    std::thread::id sys_thread_id = std::this_thread::get_id();
                    usleep(local_thread_id * 1000); // Stagger debug prints a bit.

                    // This is typically in IO API functions, but they aren't used in this test.
                    tt::cpuset::tt_cpuset_allocator::bind_thread_to_cpuset(cluster->get_cluster_desc(), transfer_info.target_device);

                    auto duration_us = dram_read_or_write(cluster, transfer_info, warm_up);
                    runtime_per_thread[local_thread_id] = duration_us;
                });

                local_thread_id++;

            }
            for (auto &thread : threads){
                thread.join();
            }
        }


        pass &= compute_and_check_overall_perf(target_device_ids, transfers, runtime_per_thread, per_device_write_bw_gb_target, per_device_read_bw_gb_target);

        query_all_device_aiclks(cluster,TargetDevice::Silicon, aiclk_target, "end_of_test");
        cluster->close_device();
        delete cluster;

        
    } catch (const std::exception& e) {
        pass = false;
        // Capture the exception error message
        log_error("{}", e.what());
        // Capture system call errors that may have returned from driver/kernel
        log_error("System error message: {}", std::strerror(errno));
    }

    auto test_end = std::chrono::high_resolution_clock::now();
    auto test_duration = std::chrono::duration_cast<std::chrono::microseconds>(test_end - test_start).count();
    auto duration_str_sec = float_to_string(test_duration/1.0e6, 2);
    log_info(tt::LogTest, "Test Elapsed Time: {} seconds ({} us)", duration_str_sec, test_duration);

    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }
    return 0;
}

