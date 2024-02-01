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

std::vector<uint32_t> get_filled_data_vec(int size_words = 8, int start_val = 0) {
    std::vector<uint32_t> vec;
    for (int i=0; i<size_words; i++) {
        vec.push_back(start_val + (i*1));
    }
    return vec;
}

// Get random 32B aligned address between semi reasonable range.
uint64_t get_random_address(bool is_32b_aligned, int min_addr_mb = 32 , int max_addr_mb = 512) {
    int aligned_in_bytes = is_32b_aligned ? 32 : 4; // Must be 4B aligned for epoch cmd write function
    const size_t min = min_addr_mb * 1024 * 1024;
    const size_t max = max_addr_mb * 1024 * 1024;
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
bool random_dram_wr_wr_rd_check_basic(std::string test_name, int device_id, tt_cluster *cluster, const int num_loops) {
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

        for (int j=0; j<num_writes; j++) {
            // New feature in ETH FW 6.4 and current preferred solution. Without use_write_ordering: hazards.
            bool ordered_with_prev = use_write_ordering ? j>0 : false;
            data_vec = get_random_data_vec(size_words);
            all_writes_data.push_back(data_vec);
            cluster->write_dram_vec(data_vec, buf_dram, buf_addr, true, true, true, false, ordered_with_prev);
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


// Updated version for #2388 epoch cmd races, with memory model, multiple ordered chains, other traffic, and delay checking
// until end of test to avoid having to call flush between every ordered-write-chain.
bool random_dram_wr_wr_rd_check_stress(std::string test_name, int device_id, tt_cluster *cluster, const int num_loops) {
    std::vector<uint32_t> tmp;
    uint32_t target_dram_channel = 0;

    std::map<uint64_t, uint32_t> memory; // 64-bit address to 32 bit data.
    auto &sdesc = cluster->get_soc_desc(device_id);
    int target_addr_mb_min = 6; // Narrow range to keep test reasonably quick.
    int target_addr_mb_max = 8;

    // New feature in ETH FW 6.4 and current preferred solution.
    const bool use_write_ordering = true;
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
    log_info(tt::LogTest, "Starting num_loops: {} for device_id: {} test_name: {} ", num_loops, device_id, test_name);

    for (int i=0; i<num_loops; i++) {

        // Randomize some params. Not necessary to hit race that inspired this test,
        // but do it anyways because who knows what the future holds...
        bool use_32b_aligned_addr   = true;
        uint64_t buf_addr_target    = get_random_address(use_32b_aligned_addr, target_addr_mb_min, target_addr_mb_max);
        int num_writes              = verif::random::tt_rnd_int(2,16);
        int size_words              = use_32b_aligned_addr ? (verif::random::tt_rnd_int(0,10) <= 8 ? verif::random::tt_rnd_int(1,8) : verif::random::tt_rnd_int(32,128)) : 1;

        // Do multiple ordered write chains to the same adddress. They were getting out of order for netlists.
        int num_ordered_write_chains = verif::random::tt_rnd_int(1,7);

        for (int chain_idx=0; chain_idx<num_ordered_write_chains; chain_idx++) {

            // Small chance for ordered write-chains to target different address and different core (DRAM ch) other than ch0 (checking enabled)
            uint64_t buf_addr       = verif::random::tt_rnd_int(0,10) < 3 ? get_random_address(use_32b_aligned_addr, target_addr_mb_min, target_addr_mb_max) : buf_addr_target;
            uint32_t dram_channel   = (verif::random::tt_rnd_int(0,10) < 3) ?  verif::random::tt_rnd_int(1,sdesc.get_num_dram_channels()-1) : target_dram_channel;

            for (int j=0; j<num_writes; j++) {

                // Used for ordering with ETW FW 6.3 and earlier via flush. In this stress test, multiple ordered write chains
                // will often target same address. Could be that first write of current chain reaches dest before final write
                // of previous chain, must use ordered_with_prev=true to avoid.
                bool ordered_with_prev = use_write_ordering ? true : false;
                size_words =  use_32b_aligned_addr ? (verif::random::tt_rnd_int(0,10) <= 7 ? verif::random::tt_rnd_int(1,8) : verif::random::tt_rnd_int(32,128)) : 1;

                // Data that makes debugging failures a bit easier.
                uint32_t data_base = ((chain_idx * 0x1100) << 16) | ((j * 0x1100) & 0xFFFF);
                std::vector<uint32_t> data_vec = get_filled_data_vec(size_words, data_base);
                tt_target_dram buf_dram = {device_id, dram_channel, 0};

                // Make sure Final write of test is not issued with last=false, otherwise it will not get written to device (unless erisc cmd q hits full)
                bool final_write_of_test = (j == (num_writes-1)) && (chain_idx == (num_ordered_write_chains-1)) && (i == (num_loops-1));
                bool last_send_epoch_cmd = !final_write_of_test && (verif::random::tt_rnd_int(0,10) < 7) ? false : true;

                cluster->write_dram_vec(data_vec, buf_dram, buf_addr, true, true, last_send_epoch_cmd, false, ordered_with_prev);

                // Only model/check channel 0. The other writes to other channels are just to exercise eth fw routing.
                if (dram_channel == 0) {
                    // Store data in memory model for readback/checking later.
                    // Byte is 8 bits.  Word is 4-Bytes (32-bits).  CMD is 8-Words.
                    for (int k=0; k<size_words; k++) {
                        uint64_t addr_64b   = static_cast<uint64_t>(buf_addr + (k*sizeof(uint32_t)));  // byte aligned, so each word is x4
                        uint32_t addr_lo    = static_cast<uint32_t>(addr_64b & 0xFFFFFFFF);
                        uint32_t addr_hi    = static_cast<uint32_t>((addr_64b >> 32) & 0xFFFFFFFF);
                        uint32_t data       = data_vec.at(k);
                        log_debug(tt::LogTest, "{} - DeviceId: {} LoopIdx: {} WriteIdx: {} WordIdx: {} Writing Data: 0x{:x} to => addr_upper: 0x{:x} addr_lower: 0x{:x}",
                            __FUNCTION__, device_id, i, j, k, data, addr_hi, addr_lo);
                        memory[addr_64b] = data;
                    }
                }
            }
        }

        log_debug(tt::LogTest, "loop {}/{} Finished with chains: {} num_writes: {}", i+1, num_loops, num_ordered_write_chains, num_writes);
    }

    // Read back and ensure data matches last written data. With race condition, it did not match.
    cluster->wait_for_non_mmio_flush(); // Must flush to ensure read is ordered with write.

    auto num_mem_words_to_check = memory.size();
    int mismatches = 0;
    int idx = 1;
    log_info(tt::LogTest, "Reading back memory now for {} unique words written to memory for device_id: {}.", num_mem_words_to_check, device_id);

    for (auto &mem: memory) {
        uint64_t address = mem.first;
        uint32_t data = mem.second;
        std::vector<uint32_t> tmp;
        tt_target_dram buf_dram = {device_id, target_dram_channel, 0};
        cluster->read_dram_vec(tmp, buf_dram, address, sizeof(uint32_t));
        log_assert(tmp.size() == 1, "Returned too many data in read");

        if (tmp.at(0) != data) {
            log_warning(tt::LogTest, "{} - word {}/{} device_id: {} addr: 0x{:x} FAILED! wrote 0x{:x} and read 0x{:x}", __FUNCTION__, idx, num_mem_words_to_check, device_id, address, data, tmp.at(0));
            mismatches++;
        }
        idx++;
    }

    auto elapsed_time_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time);
    bool passed = mismatches == 0;

    log_info(tt::LogTest, "{}! {}/{} words matching for {} loops for device_id: {}. Test took elapsed_time: {} ms",
        passed ? "PASSED" : "FAILED", num_mem_words_to_check-mismatches, num_mem_words_to_check, num_loops, device_id, elapsed_time_in_ms.count());

    return passed;

}

// Updated version with memory model and delay checking until end of test to avoid having to call flush between every ordered-write-chain.
bool epoch_write_benchmark(std::string test_name, int device_id, tt_cluster *cluster, const int num_epochs) {

    std::vector<uint32_t> tmp;
    uint32_t target_dram_channel = 0;

    std::map<uint64_t, uint32_t> memory; // 64-bit address to 32 bit data.
    auto &sdesc = cluster->get_soc_desc(device_id);
    auto num_dram_channnels = sdesc.get_num_dram_channels();

    // New feature in ETH FW 6.4 and current preferred solution.
    const bool use_write_ordering = true;
    std::chrono::high_resolution_clock::time_point start_time = std::chrono::high_resolution_clock::now();
    log_info(tt::LogTest, "Starting num_epochs: {} for device_id: {} test_name: {} ", num_epochs, device_id, test_name);

    bool use_32b_aligned_addr   = true;
    int num_cores_dram_writes = sdesc.ethernet_cores.size() + sdesc.workers.size();
    int num_writes_per_core = 2; // cmd, wrptr.

    // Collect all cores, used for sending L1 writes (to mimmic l1 shadow wrptr updates)
    std::unordered_set<tt_xy_pair> all_cores;
    all_cores.insert(sdesc.ethernet_cores.begin(), sdesc.ethernet_cores.end());
    // all_cores.insert(sdesc.workers.begin(), sdesc.workers.end());

    for (int i=0; i<num_epochs; i++) {

        // Create epoch command covering 96 cores with pair of CMD, WRPTR
        uint64_t buf_addr = get_random_address(use_32b_aligned_addr, 6, 8); // 6MB to 8MB region.

        for (int core_idx=0; core_idx<num_cores_dram_writes; core_idx++) {

            uint32_t dram_channel = core_idx % num_dram_channnels;
            tt_target_dram buf_dram = {device_id, dram_channel, 0};

            // Mimic epch {cmd,wrptr} pair of writes to a DRAM channel per worker/eth core.
            for (int j=0; j<num_writes_per_core; j++) {
                bool last_send_epoch_cmd    = true; // opt: j == num_writes_per_core-1;
                int size_words              = j == 0 ? 12 : 4;
                bool ordered_with_prev      = use_write_ordering ? j>0 : false;
                std::vector<uint32_t> data_vec(size_words, 0xDEADBEEF);

                // log_info(tt::LogTest, "epoch: {} core_idx: {} write_idx: {} DRAM Write to buf_addr: 0x{:x} dram_channel: {} last_send_epoch_cmd: {}", i, core_idx, j, buf_addr, dram_channel, last_send_epoch_cmd);
                cluster->write_dram_vec(data_vec, buf_dram, buf_addr, true, true, last_send_epoch_cmd, false, ordered_with_prev);
            }
        }

        // Send l1-shadow-wrptr updates. Just to eth cores is what typical runtime flow does today.
        int core_idx = 0;
        int l1_addr = 0x9004;
        std::vector<uint32_t> data_vec(1, 0xCAFEBABE);

        for (auto &core: all_cores) {
            tt_cxy_pair chip_core_l1(device_id, core);
            bool last_send_epoch_cmd = core_idx++ < all_cores.size() - 1 ? false : true;
            cluster->write_dram_vec(data_vec, chip_core_l1, l1_addr, true, true, last_send_epoch_cmd);
        }
    }

    auto elapsed_time_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time);
    log_info(tt::LogTest, "Finished num_epochs: {} for device_id: {} test_name: {} took elapsed_time_in_ms: {} ms", num_epochs, device_id, test_name, elapsed_time_in_ms.count());

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
            if (test == "write_ordering_multichip_basic") {
                tt_device_params default_params;
                test_loops = get_test_loops(test, num_loops);
                tt_cluster *cluster = new tt_cluster;
                cluster->open_device(arch, target_type, target_device_ids, sdesc_file, cluster_desc_file, true, false, true);
                cluster->start_device(default_params);
                for (auto &device_id: target_device_ids) {
                    log_assert(random_dram_wr_wr_rd_check_basic(test, device_id, cluster, test_loops), "random_dram_wr_wr_rd_check_basic failed");
                }
                cluster->close_device();
                delete cluster;
            }

            // Updated for race in #2388. Keep simpler version of test around too, it is still useful.
            if (test == "write_ordering_multichip_stress") {
                tt_device_params default_params;
                test_loops = get_test_loops(test, num_loops);
                tt_cluster *cluster = new tt_cluster;
                cluster->open_device(arch, target_type, target_device_ids, sdesc_file, cluster_desc_file, true, false, true);
                cluster->start_device(default_params);
                for (auto &device_id: target_device_ids) {
                    log_assert(random_dram_wr_wr_rd_check_stress(test, device_id, cluster, test_loops), "random_dram_wr_wr_rd_check_stress failed");
                }
                cluster->close_device();
                delete cluster;
            }

            // Roughly mimic sending many epochs (cmds, wrptr to all cores, plus l1-shadow-wrptr update to eth cores).
            // Around 540ms for nbx2 remote chip for 1000 epochs.
            // Finished num_epochs: 1000 for device_id: 1 test_name: epoch_writes_multichip_benchmark took elapsed_time_in_ms: 538 ms
            if (test == "epoch_writes_multichip_benchmark") {
                tt_device_params default_params;
                test_loops = get_test_loops(test, num_loops);
                tt_cluster *cluster = new tt_cluster;
                cluster->open_device(arch, target_type, target_device_ids, sdesc_file, cluster_desc_file, true, false, true);
                cluster->start_device(default_params);
                for (auto &device_id: target_device_ids) {
                    log_assert(epoch_write_benchmark(test, device_id, cluster, test_loops), "epoch_write_benchmark failed");
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

