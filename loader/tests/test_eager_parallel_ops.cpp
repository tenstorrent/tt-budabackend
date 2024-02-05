// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "tt_backend.hpp"
#include "tt_backend_api.hpp"
#include "runtime.hpp"
#include "runtime/runtime_eager_io.hpp"
#include "utils.hpp"
#include "verif.hpp"

using namespace verif;
using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;
using namespace tt;

VerifComparisonConfig get_comp_config() {
    VerifComparisonConfig config;
    config.type = ComparisonType::AllCloseHw;
    config.atol = 0.01;
    config.rtol = 0.01;
    config.check_pct = 0.99;
    config.check_pcc = 0.99;
    return config;
}

// Helpers to mimic python side pytorch tensor creation
std::unordered_map<std::string, tt_py_desc> get_metadata_tensors(const tt_queue_info &q_info, std::shared_ptr<tt_runtime> backend) {
    std::unordered_map<std::string, tt_py_desc> metadata_tensors;

    // allocate host side storage for chan and addr tensor data
    int num_elements = q_info.alloc_info.size();
    auto chan_data = backend->get_workload()->allocate_untilized_memory(num_elements);
    auto addr_data = backend->get_workload()->allocate_untilized_memory(num_elements);

    for (int i = 0; i < num_elements; i++) {
        chan_data->at(i) = q_info.alloc_info.at(i).channel;
        addr_data->at(i) = q_info.alloc_info.at(i).address;
    }
    int chip_r = 1, chip_c = 1, tile_w = 1, tile_h = 1;
    metadata_tensors.insert({"chan_tensor", io::get_pytorch_tensor_desc_from_array<uint32_t>(chan_data->data(), chip_r, chip_c, q_info.grid_size.r, q_info.grid_size.c, DataFormat::RawUInt32, tt::PY_TENSOR_DIMS, tile_w, tile_h)});
    metadata_tensors.insert({"addr_tensor", io::get_pytorch_tensor_desc_from_array<uint32_t>(addr_data->data(), chip_r, chip_c, q_info.grid_size.r, q_info.grid_size.c, DataFormat::RawUInt32, tt::PY_TENSOR_DIMS, tile_w, tile_h)});
    return metadata_tensors;
}
tt_py_desc get_empty_tensor(const tt_queue_info &q_info) {
    tt_py_desc empty_tensor;
    int chip_r = 1, chip_c = 1;
    int tile_h = q_info.grid_size.r * q_info.dim.mblock_m * q_info.dim.ublock_rt;
    int tile_w = q_info.grid_size.c * q_info.dim.mblock_n * q_info.dim.ublock_ct;
    if (q_info.data_format == DataFormat::Float32)
        empty_tensor = io::get_pytorch_tensor_desc_from_array<uint32_t>(nullptr, chip_r, chip_c, tile_h, tile_w, q_info.data_format);
    else
        empty_tensor = io::get_pytorch_tensor_desc_from_array<uint16_t>(nullptr, chip_r, chip_c, tile_h, tile_w, q_info.data_format);
    return empty_tensor;
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);

    // Mimic host side backing memory for data tensors
    aligned_vector<float> fp32_storage;
    aligned_vector<uint16_t> fp16_storage;

    // Command line options
    int seed = 0;
    bool pass = true;
    std::string flat_inputs, flat_outputs, netlist;

    std::tie(seed, args) = verif_args::get_command_option_int32_and_remaining_args(args, "--seed", 0);
    std::tie(flat_inputs, args) = verif_args::get_command_option_and_remaining_args(args, "--inputs", "in");
    std::tie(flat_outputs, args) = verif_args::get_command_option_and_remaining_args(args, "--outputs", "out");
    std::tie(netlist, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_basic/netlist_parallel_unary.yaml");

    std::vector<std::string> input_qs, output_qs;
    tt::args::split_string_into_vector(input_qs, flat_inputs, ",");
    tt::args::split_string_into_vector(output_qs, flat_outputs, ",");

    tt_rnd_set_seed(seed);

    tt::ARCH backend_arch = get_arch_from_string(tt::parse_env<string>("ARCH_NAME", "grayskull"));

    // Create a single backend for all ops
    tt_runtime_config config(args);
    config.type = DEVICE::Silicon;
    config.arch = backend_arch;
    config.runtime_args = "--skip-io-init --skip-overlap-check"; // we will init IO manually using eager::io::init_queue

    verif_args::validate_remaining_args(args);

    // -----------------------------------------------
    // Backend Runtime API - create backend
    // -----------------------------------------------
    std::set<int> target_devices = {0,1};
    auto backend_base = tt_backend::create_eager_backend(config, target_devices);
    auto backend = std::dynamic_pointer_cast<tt_runtime>(backend_base);

    // -----------------------------------------------
    // Backend IO API - init IO for all devices
    // -----------------------------------------------
    eager::io::initialize(config.arch, target_devices);

    netlist_workload_data workload(netlist);
    for (const auto &queue : workload.queues) {
        auto q_info = workload.queues.at(queue.first).my_queue_info;
        auto metadata = get_metadata_tensors(q_info, backend);
        tt_py_desc &chan_tensor = metadata.at("chan_tensor");
        tt_py_desc &addr_tensor = metadata.at("addr_tensor");

        // -----------------------------------------------
        // Backend IO API - initialize ptrs and num entries
        // -----------------------------------------------
        log_info(tt::LogTest, "Initializing {} queue {} on device {}", q_info.loc, q_info.name, q_info.target_device);
        eager::io::init_queue(q_info.loc, q_info.target_device, chan_tensor, addr_tensor, q_info.entries);
    }

    std::map<string, vector<tt_tensor>> tensor_map;

    // -----------------------------------------------
    // Backend Runtime API - compile netlist
    // -----------------------------------------------
    log_assert(backend->compile_netlist(netlist) == tt::DEVICE_STATUS_CODE::Success, "Expected netlist to be compiled succesfully.");

    for (const auto &name : input_qs) {
        for (int d : target_devices) {
            auto io_name = name + "." + std::to_string(d);
            auto q_info = workload.queues.at(io_name).my_queue_info;
            auto metadata = get_metadata_tensors(q_info, backend);
            tt_py_desc &chan_tensor = metadata.at("chan_tensor");
            tt_py_desc &addr_tensor = metadata.at("addr_tensor");

            // Push the whole microbatch for input, and one element for rams
            int input_count = (q_info.type == IO_TYPE::Queue) ? q_info.input_count : 1;

            for (int i=0; i<input_count; i++) {
                tt_tensor input(get_tensor_metadata_from_tt_queue_info(q_info, false));
                input = tt_rnd_float(-0.5, 1.0);
                tensor_map[io_name].push_back(input);  // save for later golden computation

                tt_py_desc data_tensor = input.get_data_format() == DataFormat::Float32 ? 
                    io::get_pytorch_tensor_desc<float>(input, fp32_storage) :
                    io::get_pytorch_tensor_desc<uint16_t>(input, fp16_storage);
                
                // -----------------------------------------------
                // Backend IO API - init IO for all queues
                // -----------------------------------------------
                tt::eager::io::push_tensor(q_info.loc, q_info.target_device, chan_tensor, addr_tensor, data_tensor, q_info.type);
            }
        }
    }

    // Run programs in order specified in netlist
    for (const auto &program_name : workload.program_order) {
        // -----------------------------------------------
        // Backend Runtime API - run user specified program
        // -----------------------------------------------
        log_assert(backend->run_program(program_name, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to be run successfully on target backend");
    }

    // Pop outputs for last op
    for (const auto &name : output_qs) {
        for (int d : target_devices) {
            auto io_name = name + "." + std::to_string(d);
            auto q_info = workload.queues.at(io_name).my_queue_info;
            auto metadata = get_metadata_tensors(q_info, backend);
            tt_py_desc &chan_tensor = metadata.at("chan_tensor");
            tt_py_desc &addr_tensor = metadata.at("addr_tensor");
            tt_py_desc data_tensor = get_empty_tensor(q_info);

            // Push the whole microbatch
            for (int i=0; i<q_info.input_count; i++) {
                // -----------------------------------------------
                // Backend IO API - init IO for all queues
                // -----------------------------------------------
                bool untilized_output = workload.has_untilized_data(q_info.name);
                eager::io::get_tensor(q_info.loc, q_info.target_device, chan_tensor, addr_tensor, data_tensor, q_info.type, 0, untilized_output);
                eager::io::pop_tensor(q_info.loc, q_info.target_device, chan_tensor, addr_tensor);

                // save for checking against golden
                tt_tensor output = tt::io::reconstruct_tensor_from_untilized(backend->get_queue_descriptor(io_name), data_tensor, false);
                tensor_map[io_name].push_back(output);

                // -----------------------------------------------
                // Backend IO API - free host memory (optional)
                // a good pratice to reduce mem footprint, as backend garbage collection doesn't occur
                // until eager::io::finish() is explicitly called on the process used for IO
                // user needs to either delay the free until ref count is 0 or clone it before free
                // -----------------------------------------------
                tt::backend::free_tensor(data_tensor);
            }
        }
    }

    // -----------------------------------------------
    // Backend Runtime API - destroy the backend
    // -----------------------------------------------
    log_assert(backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected device to be closed succesfully");

    // Verify outputs against golden
    VerifComparisonConfig comp_config = get_comp_config();
    for (const auto &name : output_qs) {
        for (int d : target_devices) {
            auto io_name = name + "." + std::to_string(d);
            auto input_name = input_qs[0] + "." + std::to_string(d);
            for (int i = 0; i < tensor_map[io_name].size(); i++) {
                log_info(tt::LogTest, "Verifying {}[{}]", io_name, i);
                tt_tensor expected = tensor_map[input_name].at(i);
                pass &= compare_tensor_data(expected, tensor_map[io_name].at(i), comp_config);
            }
        }
    }
    if (pass) {
        log_info(tt::LogTest, "Test Passed -- Ringo!");
    } else {
        log_fatal("Test Failed -- Dillinger!");
    }

    // -----------------------------------------------
    // Backend IO API - finish IO process
    // -----------------------------------------------
    eager::io::finish();
    return 0;
}

