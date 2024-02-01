// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <fstream>
#include <errno.h>

#include "tt_cluster.hpp"
#include "common/cache_lib.hpp"
#include "verif.hpp"
#include "runtime/runtime_utils.hpp"


// Pretty string hex version of values
std::string get_hex_vector(std::vector<uint32_t> &data) {
    std::stringstream s_hex_data;
    s_hex_data << "[";
    for (int i=0; i<data.size(); i++) {
        s_hex_data << "0x" << std::hex << data.at(i);
        if (i < data.size()-1) {
            s_hex_data << ",";
        }
    }
    s_hex_data << "]";
    return s_hex_data.str();
}


// Vec of specific size with random data.
std::vector<uint32_t> get_random_data_vec(int size_words = 8) {
    std::vector<uint32_t> vec;
    for (int i=0; i<size_words; i++) {
        vec.push_back((uint32_t)verif::random::tt_rnd_int(numeric_limits<int>::min(), numeric_limits<int>::max()));
    }
    return vec;
}

// Get random 32B aligned address between semi reasonable range.
uint64_t get_random_address(bool is_32b_aligned) {
    int aligned_in_bytes = is_32b_aligned ? 32 : 4; // Must be 4B aligned for epoch cmd write function
    const size_t min = 32 * 1024 * 1024;  // 32MB
    const size_t max = 512 * 1024 * 1024; // 512MB
    uint64_t addr = ((uint64_t) verif::random::tt_rnd_int(min, max) / aligned_in_bytes) * aligned_in_bytes;
    return addr;
}

bool dram_rdwr_check(tt_cluster *cluster, const tt_queue_info &q_info) {
    std::vector<uint32_t> tmp;
    std::vector<uint32_t> vec = get_random_data_vec(1);

    const tt_queue_allocation_info &buf = q_info.alloc_info.at(0);
    tt_target_dram dram = {q_info.target_device, buf.channel, 0};
    cluster->write_dram_vec(vec, dram, buf.address);

    bool allow_multiple_attempts = false;
    int attempt = 0;
    while (vec != tmp) {
        cluster->read_dram_vec(tmp, dram, buf.address, vec.size() * 4);
        attempt++;
        if (!allow_multiple_attempts && vec != tmp) {
            return false;
        }
    }
    log_info(tt::LogTest, "dram_rdwr_check wrote {} and read {}, passed in {} attempts\n", vec, tmp, attempt);
    return true;
}


// Consecutive writes to a DRAM  on remote chip, should be ordered. Read back should return last data written. Do it many times.
// Inspired by #2018 epooch cmd queue DRAM cmd/wrptr race to remote chips.
bool random_dram_wr_wr_rd_check(std::string test_name, int device_id, tt_cluster *cluster, const int num_loops) {
    std::vector<uint32_t> tmp;
    tt_target_dram buf_dram = {device_id, 0, 0};

    const bool use_write_ordering = true;
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
    log_info(tt::LogTest, "Starting num_loops: {} for device_id: {} test_name: {} ", num_loops, device_id, test_name);

    for (int i=0; i<num_loops; i++) {

        // Randomize some params. Not necessary to hit race that inspired this test,
        // but do it anyways because who knows what the future holds...
        bool use_32b_aligned_addr   = verif::random::tt_rnd_int(0,9) <= 7 ? true : false;
        uint64_t buf_addr           = get_random_address(use_32b_aligned_addr);
        int num_writes              = verif::random::tt_rnd_int(2,16);
        int size_words              = use_32b_aligned_addr ? (verif::random::tt_rnd_int(0,10) <= 7 ? verif::random::tt_rnd_int(1,8) : verif::random::tt_rnd_int(32,128)) : 1;
        // log_info(tt::LogTest, "test: {} device_id: {} loop: {} use_32b_aligned_addr: {} num_writes: {} size_words:{} buf_addr: 0x{:x}",
        //    test_name, device_id, i, use_32b_aligned_addr, num_writes, size_words, buf_addr);

        std::vector<uint32_t> data_vec;
        std::vector<std::vector<uint32_t>> all_writes_data;

        if (use_write_ordering) {
            // New feature in ETH FW 6.4 and current preferred solution.
            for (int j=0; j<num_writes; j++) {
                data_vec = get_random_data_vec(size_words);
                all_writes_data.push_back(data_vec);
                cluster->write_dram_vec(data_vec, buf_dram, buf_addr, true, true, true, false, j>0);
            }
        } else {
            // No write ordering hazard workarounds.
            for (int j=0; j<num_writes; j++) {
                data_vec = get_random_data_vec(size_words);
                all_writes_data.push_back(data_vec);
                cluster->write_dram_vec(data_vec, buf_dram, buf_addr, true, true, true, false, false);
            }
        }

        // Read back and ensure data matches last written data. With race condition, it did not match.
        cluster->wait_for_non_mmio_flush(); // Must flush to ensure read is ordered with write.
        cluster->read_dram_vec(tmp, buf_dram, buf_addr, data_vec.size() * 4);

        if (tmp != data_vec) {
            log_warning(tt::LogTest, "{} test {}/{} use_32b_aligned_addr: {} num_writes: {} size_words: {} device_id: {} addr: 0x{:x} FAILED! wrote {} and read {}",
                __FUNCTION__, i+1, num_loops, use_32b_aligned_addr, num_writes, size_words, device_id, buf_addr, get_hex_vector(data_vec), get_hex_vector(tmp));

            log_info(tt::LogTest, "Order of writes:");
            for (int i=0; i<all_writes_data.size(); i++) {
                log_info(tt::LogTest, "i: {} data: {}",i, get_hex_vector(all_writes_data.at(i)));
            }
            return false;
        } else {
            log_debug(tt::LogTest, "{} test {}/{} use_32b_aligned_addr: {} num_writes: {} size_words: {} device_id: {} addr: 0x{:x} PASSED! wrote {} and read {}",
                __FUNCTION__, i+1, num_loops, use_32b_aligned_addr, num_writes, size_words, device_id, buf_addr, get_hex_vector(data_vec), get_hex_vector(tmp));
        }
    }

    auto elapsed_time_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time);
    log_info(tt::LogTest, "Finished num_loops: {} for device_id: {} test_name: {} took elapsed_time_in_ms: {} ms", num_loops, device_id, test_name, elapsed_time_in_ms.count());

    return true;
}


int get_test_loops(const std::string &test, int default_loops) {
    if (test == "multi_inst_concurrent") {
        return default_loops < 16 ? default_loops : 16;  // Issue 524, limit the amount of concurrent instances
    }
    return default_loops;
}

int main(int argc, char** argv)
{
    std::vector<std::string> args(argv, argv + argc);

    bool pass = true;

    int num_loops, seed;
    std::string netlist_path;
    std::string cmdline_arg;
    std::vector<std::string> tests;
    std::string cluster_desc_file = "";
    const std::string output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    std::tie(num_loops, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-loops", 100);
    std::tie(seed, args)         = verif_args::get_command_option_int32_and_remaining_args(args, "--seed", -1);
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_tilize_untilize/tilize_fp16b_untilize_fp16.yaml");
    std::tie(cluster_desc_file, args) = verif_args::get_command_option_and_remaining_args(args, "--cluster-desc", "");
    std::tie(cmdline_arg, args) = verif_args::get_command_option_and_remaining_args(args, "--tests", "multi_instance_test1");
    verif_args::split_string_into_vector(tests, cmdline_arg, ",");

    verif_args::validate_remaining_args(args);

    // Randomization seeding
    if (seed == -1) {
        seed = verif::random::tt_gen_seed();
        log_info(tt::LogTest, "Unspecified cmdline --seed , generated random seed {}", seed);
    }
    verif::random::tt_rnd_set_seed(seed);

    netlist_workload_data workload(netlist_path);
    const TargetDevice target_type = TargetDevice::Silicon;
    const tt::ARCH arch = static_cast<tt::ARCH>(workload.device_info.arch);
    const std::string sdesc_file = get_soc_description_file(arch, target_type);

    //std::string cluster_desc_file = "";
    if(cluster_desc_file == "") {
        if(arch == tt::ARCH::WORMHOLE || arch == tt::ARCH::WORMHOLE_B0) {
            generate_cluster_desc_yaml(output_dir);
            cluster_desc_file = output_dir + "/cluster_desc.yaml";
        }
    }
    
    bool has_input_q = false;
    auto target_device_ids = workload.compute_target_device_ids();

    tt_queue_info input_q;
    for (const auto &queue : workload.queues) {
        if (queue.second.my_queue_info.input == "HOST") {
            has_input_q = true;
            input_q = queue.second.my_queue_info;
            log_info(tt::LogTest, "Found input queue {}", input_q.name);
            break;
        }
    }

    try {
        int test_loops = num_loops;
        for (const std::string &test : tests) {
            // Construct and destruct driver within a loop
            if (test == "multi_inst_sequential") {
                test_loops = get_test_loops(test, num_loops);
                for (int i=0; i<test_loops; i++) {
                    log_info(tt::LogTest, "-- Run driver test {} loop {} of {}", test, i+1, test_loops);
                    tt_cluster *cluster = new tt_cluster;
                    std::string tag = test + "_driver_inst_" + std::to_string(i);
                    cluster->open_device(arch, target_type, target_device_ids, sdesc_file, cluster_desc_file);
                    cluster->start_device({.init_device = false});
                    if (has_input_q) log_assert(dram_rdwr_check(cluster, input_q), "dram_rdwr_check failed");
                    delete cluster;
                }
            }

            // Construct driver in a loop, keep they all in memory
            if (test == "multi_inst_concurrent") {
                test_loops = get_test_loops(test, num_loops);
                for (int i=0; i<test_loops; i++) {
                    log_info(tt::LogTest, "-- Run driver test {} loop {} of {}", test, i+1, test_loops);
                    tt_cluster *cluster = new tt_cluster;
                    std::string tag = test + "_driver_inst_" + std::to_string(i);
                    cluster->open_device(arch, target_type, target_device_ids, sdesc_file, cluster_desc_file, true, false, true);
                    cluster->start_device({.init_device = false});
                    tt_object_cache<tt_cluster>::set(tag, cluster);
                    if (has_input_q) log_assert(dram_rdwr_check(cluster, input_q), "dram_rdwr_check failed");
                }
                // Desctruct and deallocate all driver at once
                for (int i=0; i<test_loops; i++) {
                    std::string tag = test + "_driver_inst_" + std::to_string(i);
                    tt_object_cache<tt_cluster>::destroy(tag);
                }
            }

            // Init and shutdown driver within a loop
            if (test == "init_shutdown_sequential") {
                tt_device_params default_params;
                test_loops = get_test_loops(test, num_loops);
                for (int i=0; i<test_loops; i++) {
                    log_info(tt::LogTest, "-- Run driver test {} loop {} of {}", test, i+1, test_loops);
                    tt_cluster *cluster = new tt_cluster;
                    std::string tag = test + "_driver_inst_" + std::to_string(i);
                    cluster->open_device(arch, target_type, target_device_ids, sdesc_file, cluster_desc_file, true, false, true);
                    cluster->start_device(default_params);
                    if (has_input_q) log_assert(dram_rdwr_check(cluster, input_q), "dram_rdwr_check failed");
                    cluster->close_device();
                    delete cluster;
                }
            }

            // Create and Delete driver within a loop. Used for concurrent processes testing.
            if (test == "init_sequential") {
                tt_device_params default_params;
                test_loops = get_test_loops(test, num_loops);
                for (int i=0; i<test_loops; i++) {
                    log_info(tt::LogTest, "-- Run driver test {} loop {} of {}", test, i+1, test_loops);
                    tt_cluster *cluster = new tt_cluster;
                    cluster->open_device(arch, target_type, target_device_ids, sdesc_file, cluster_desc_file);
                    delete cluster;
                }

            }

            // Test consecutive writes to all chips (local and remote) Was a race bug (#2018) for remote chips.
            if (test == "write_ordering_multichip") {
                tt_device_params default_params;
                test_loops = get_test_loops(test, num_loops);
                tt_cluster *cluster = new tt_cluster;
                cluster->open_device(arch, target_type, target_device_ids, sdesc_file, cluster_desc_file);
                cluster->start_device(default_params);
                for (auto &device_id: target_device_ids) {
                    log_assert(random_dram_wr_wr_rd_check(test, device_id, cluster, test_loops), "dram_rdwr_check failed");
                }
                cluster->close_device();
                delete cluster;
            }

        }
    } catch (const std::exception& e) {
        pass = false;
        // Capture the exception error message
        log_error("{}", e.what());
        // Capture system call errors that may have returned from driver/kernel
        log_error("System error message: {}", std::strerror(errno));
    }

    if (pass) {
        log_info(tt::LogTest, "Test Passed");
    } else {
        log_fatal("Test Failed");
    }
    return 0;
}

