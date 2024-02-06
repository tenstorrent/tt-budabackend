// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <fstream>

#include "tt_backend.hpp"
#include "tt_backend_api.hpp"
#include "common/param_lib.hpp"
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
    config.rtol = 0.15;
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

void push_intermed_inputs_to_golden(std::map<string, vector<tt_tensor>>& intermed_tensors, const netlist_workload_data& workload, std::shared_ptr<tt_backend> golden_backend, std::string netlist_name) {
    for(const auto &queue : workload.queues) {
        if(queue.second.my_queue_info.input.find("net2net") != string::npos) {
            log_assert(intermed_tensors.find(queue.second.my_queue_info.input) != intermed_tensors.end(), "Could not find input {} to queue {} in netlist {}. Check outputs from previous netlists.", queue.second.my_queue_info.input, queue.first, netlist_name);
            int input_count = (queue.second.my_queue_info.type == IO_TYPE::Queue) ? queue.second.my_queue_info.input_count : 1;
            for(int i = 0; i < input_count; i++){
                verif::push_tensor(*golden_backend, queue.first, &(intermed_tensors.at(queue.second.my_queue_info.input).at(i)), 0, 1);
            }
        }
    }
}

void populate_intermed_tensors_for_golden(std::map<string, vector<tt_tensor>>& intermed_tensors, const netlist_workload_data& workload, std::shared_ptr<tt_backend> golden_backend, std::vector<std::string>& output_qs) {
    for(const auto &queue : workload.queues){
        if (queue.first.find("net2net") != string::npos || std::find(output_qs.begin(), output_qs.end(), queue.first) != output_qs.end()) {
            intermed_tensors.insert({queue.first, {}});
            for(int i = 0; i < queue.second.my_queue_info.input_count; i++) {
                tt_tensor output = get_tensor_metadata_for_queue(golden_backend -> get_queue_descriptor(queue.first));
                intermed_tensors.at(queue.first).push_back(output);
                get_and_pop_tensor(*golden_backend, queue.first, &(intermed_tensors.at(queue.first).back()), 0);
            }
        }
    }
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);

    // Command line options
    int seed = 0;
    int pinned_cpu = 0;
    bool pass = true;
    std::string flat_inputs, flat_outputs, flat_netlists;

    std::tie(seed, args) = verif_args::get_command_option_int32_and_remaining_args(args, "--seed", 0);
    std::tie(flat_inputs, args) = verif_args::get_command_option_and_remaining_args(args, "--inputs", "op0_in0,op0_in1");
    std::tie(flat_outputs, args) = verif_args::get_command_option_and_remaining_args(args, "--outputs", "op1_dq_tilized,op1_dq_untilized,op1_hq_untilized");
    std::tie(flat_netlists, args) = verif_args::get_command_option_and_remaining_args(args, "--netlists", 
        "loader/tests/net_basic/netlist_eager_mm.yaml,"
        "loader/tests/net_basic/netlist_eager_unary.yaml"
    );
    std::tie(pinned_cpu, args) = verif_args::get_command_option_int32_and_remaining_args(args, "--cpu_id", 0);
    std::vector<std::string> input_qs, output_qs, op_netlists;
    tt::args::split_string_into_vector(input_qs, flat_inputs, ",");
    tt::args::split_string_into_vector(output_qs, flat_outputs, ",");
    tt::args::split_string_into_vector(op_netlists, flat_netlists, ",");

    tt::ARCH backend_arch = get_arch_from_string(tt::parse_env<string>("ARCH_NAME", "grayskull"));

    if(pinned_cpu > 0) {
        // Pin current thread to CPU for more consistent Push BW results
        int core_id = pinned_cpu;
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(core_id, &set);

        sched_setaffinity(0, sizeof(cpu_set_t), &set);
    }

    tt_rnd_set_seed(seed);

    // Create a single backend for all ops
    tt_runtime_config config(args);
    config.type = DEVICE::Silicon;
    config.arch = backend_arch;
    config.runtime_args = "--skip-io-init --skip-overlap-check"; // we will init IO manually using eager::io::init_queue

    verif_args::validate_remaining_args(args);

    // -----------------------------------------------
    // Backend Runtime API - create backend
    // -----------------------------------------------
    std::set<int> target_devices = {0};
    auto backend_base = tt_backend::create_eager_backend(config, target_devices);
    auto backend = std::dynamic_pointer_cast<tt_runtime>(backend_base);

    std::shared_ptr<tt_backend> golden_backend;
    // -----------------------------------------------
    // Backend IO API - init IO for all devices
    // -----------------------------------------------
    eager::io::initialize(config.arch, target_devices);

    // Iterate over netlists and init queues
    for (int op_idx = 0; op_idx < op_netlists.size(); op_idx++) {
        netlist_workload_data workload(op_netlists.at(op_idx));
        for (const auto &queue : workload.queues) {
            auto q_info = workload.queues.at(queue.first).my_queue_info;
            auto metadata = get_metadata_tensors(q_info, backend);
            tt_py_desc &chan_tensor = metadata.at("chan_tensor");
            tt_py_desc &addr_tensor = metadata.at("addr_tensor");

            // -----------------------------------------------
            // Backend IO API - initialize ptrs and num entries
            // -----------------------------------------------
            log_info(tt::LogTest, "Initializing {} queue {} on device {}", tt::LogVerif, q_info.loc, q_info.name, q_info.target_device);
            eager::io::init_queue(q_info.loc, q_info.target_device, chan_tensor, addr_tensor, q_info.entries);
        }
    }

    // Data structures to mimic host side inputs to TT Device and Golden
    std::map<string, vector<tt_tensor>> tensor_map;
    std::map<string, vector<tt_tensor>> golden_tensors;
    std::map<string, vector<tt_PytorchTensorDesc>> py_tensor_map;
    std::map<string, vector<aligned_vector<float>>> fp32_data;
    std::map<string, vector<aligned_vector<uint16_t>>> fp16_data;

    // Iterate over netlists and create backends
    for (int op_idx = 0; op_idx < op_netlists.size(); op_idx++)
    {
        bool first_op = (op_idx == 0);
        bool last_op = (op_idx == (op_netlists.size()-1));

        // Only first and last perform init and shutdown respectively
        netlist_workload_data workload(op_netlists.at(op_idx));

        // -----------------------------------------------
        // Backend Runtime API - compile netlist
        // -----------------------------------------------
        log_assert(backend->compile_netlist(op_netlists.at(op_idx)) == tt::DEVICE_STATUS_CODE::Success, "Expected netlist to be compiled successfuly");
        tt_backend_config golden_config = {.type = tt::DEVICE::Golden,
                                           .arch = backend_arch};

        // Create a new golden backend for every netlist.
        golden_backend = tt_backend::create(op_netlists.at(op_idx), golden_config);
        log_assert(golden_backend -> initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected golden backend to be initialized suddessfully");

        // Push inputs for first op
        if (first_op) {
            for (const auto &io_name : input_qs) {
                auto q_info = workload.queues.at(io_name).my_queue_info;
                auto metadata = get_metadata_tensors(q_info, backend);
                auto tensor_size_in_bytes = get_tensor_size_in_bytes(q_info, true);
                tt_py_desc &chan_tensor = metadata.at("chan_tensor");
                tt_py_desc &addr_tensor = metadata.at("addr_tensor");

                // Push the whole microbatch for input, and one element for rams
                int input_count = (q_info.type == IO_TYPE::Queue) ? q_info.input_count : 1;
                
                // Start storing input data for this queue
                tensor_map.insert({io_name, {}});
                py_tensor_map.insert({io_name, {}});
                fp32_data.insert({io_name, {}});
                fp16_data.insert({io_name, {}});

                // First initialize all tt_tensors/py_tensor_descs that need to be pushed
                for (int i=0; i<input_count; i++) {
                    tt_tensor input(get_tensor_metadata_from_tt_queue_info(q_info, false));
                    input = tt_rnd_float(-0.5, 1.0);
                    tensor_map[io_name].push_back(input);  // save for later golden computation
                    if(input.get_data_format() == DataFormat::Float32) {
                        fp32_data.at(io_name).push_back({});
                        py_tensor_map[io_name].push_back(io::get_pytorch_tensor_desc<float>(input, fp32_data.at(io_name).back()));
                    } else {
                        fp16_data.at(io_name).push_back({});
                        py_tensor_map[io_name].push_back(io::get_pytorch_tensor_desc<uint16_t>(input, fp16_data.at(io_name).back()));
                    }
                }

                auto push_start = std::chrono::high_resolution_clock::now();
                // Now push to golden and Silicon
                for(int i = 0; i < input_count; i++) {     
                    // -----------------------------------------------
                    // Backend IO API - init IO for all queues
                    // -----------------------------------------------
                    tt::eager::io::push_tensor(q_info.loc, q_info.target_device, chan_tensor, addr_tensor, py_tensor_map.at(io_name).at(i), q_info.type);
                }
                auto push_duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - push_start).count();
                float push_bw = ((float)input_count*tensor_size_in_bytes / (1024*1024*1024)) / ((float)push_duration / 1.0e6);
                
                log_info(tt::LogVerif, "Average Push Bandwidth for {} :  \t{:3.3f} GB/s", io_name, push_bw);

                for(int i = 0; i < input_count; i++) {
                    verif::push_tensor(*golden_backend, io_name, &(tensor_map.at(io_name).at(i)), 0, 1);
                }
            }   
        }
        else {
            // Golden backend was recreated for this netlist. Push netlist -> netlist inputs to this backend. tt_device already has these inputs in DRAM (assumption)
            push_intermed_inputs_to_golden(golden_tensors, workload, golden_backend, op_netlists.at(op_idx));
        }

        // Run programs in order specified in netlist
        for (const auto &program_name : workload.program_order) {
            // -----------------------------------------------
            // Backend Runtime API - run user specified program
            // -----------------------------------------------
            log_assert(backend->run_program(program_name, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to be run successfuly on target backend");
            log_assert(golden_backend->run_program(program_name, {}) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to be run successfuly on golden backend.");
        }
        // Pop netlist to netlist queues or output queues for golden
        populate_intermed_tensors_for_golden(golden_tensors, workload, golden_backend, output_qs);

        log_assert(golden_backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected golden backend to be closed succesfully");
        // Pop outputs for last op
        if (last_op) {
            for (const auto &io_name : output_qs) {
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
            // -----------------------------------------------
            // Backend Runtime API - destroy the backend
            // -----------------------------------------------
            log_assert(backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected device to be closed succesfully");
        }

    }
    // Verify outputs against golden
    VerifComparisonConfig comp_config = get_comp_config();
    for (const auto &io_name : output_qs) {
        for (int i = 0; i < tensor_map[io_name].size(); i++) {
            log_info(tt::LogTest, "Verifying {}[{}]", io_name, i);
            pass &= compare_tensor_data(golden_tensors.at(io_name).at(i), tensor_map[io_name].at(i), comp_config);
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

