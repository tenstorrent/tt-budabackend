// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <chrono>
#include <fstream>

#include "runtime.hpp"
#include "tt_backend_api.hpp"
#include "verif.hpp"
#include <cstdlib>

tt_tensor get_tilized_tensor(tt_queue_info &queue_info, int entry_idx, bool batched, DataFormat host_format, uint32_t tile_height = 32, uint32_t tile_width = 32) {
    tt_tensor_metadata md;
    md.shape.rt = queue_info.dim.mblock_m * queue_info.dim.ublock_rt * queue_info.grid_size.r;
    md.shape.ct = queue_info.dim.mblock_n * queue_info.dim.ublock_ct * queue_info.grid_size.c;
    md.shape.z  = queue_info.dim.t;
    md.shape.w  = batched ? queue_info.input_count : 1;
    md.is_tilized = true;
    md.shape.tile_height = tile_height;
    md.shape.tile_width = tile_width;
    // Intentionaly ignore queue_info.data_format but instead
    // pretend that we're a host pytorch tensor using a different format
    md.data_format = host_format;

    tt_tensor tensor(md);

    tensor.init_to_tile_id(1.0f, entry_idx*16.0f);
    // tensor.init_to_xyz_values();
    return tensor;
}

tt_tensor get_tensor_from_binary(std::vector<int> input_shape, std::string binary_path, DataFormat host_format){
    tt_tensor_metadata md;
    md.shape.w = input_shape[0];
    md.shape.z = input_shape[1];
    md.shape.rt = input_shape[2];
    md.shape.ct = input_shape[3];
    md.data_format = host_format;

    tt_tensor tensor(md);
    std::vector<float> data_vector = {};
    tt::data_binary::read_file(binary_path, data_vector);
    tensor.fill_with_data(data_vector);
    tensor.set_is_tilized(false);
    tensor.tilize_inplace(true, false);
    tensor.set_is_tilized(true);
    return tensor;
}

std::vector<int> get_shape_for_shuffled_tensor(std::vector<int> input_shape, int stride){
    int shuffled_rt = ceil(static_cast<double>(input_shape[1] * stride * stride)/static_cast<double>(tt::constants::TILE_HEIGHT));
    int shuffled_ct = (input_shape[2] * input_shape[3] * tt::constants::TILE_HEIGHT) / (stride * stride);
    return {input_shape[0], 1, shuffled_rt, shuffled_ct};
}

void set_cpu_affinity(int cpu) {
    cpu_set_t cpuset;
    int cpu_core_pinned = cpu;
    if (cpu_core_pinned < 0) {
        cpu_core_pinned = verif::random::tt_rnd_int(0, sysconf(_SC_NPROCESSORS_ONLN)-1);
    }
    CPU_SET(cpu_core_pinned, &cpuset);  // set the bit that represents the pinned core
    sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    log_info(tt::LogTest, "SET_CPU_AFFINITY - thread is pinned to cpu_core: {}", cpu_core_pinned);

}

bool compare_tilized_tensors(tt_tensor &observed, tt_tensor &expected, bool verbose_compare) {

    bool match = observed == expected;

    for (int wi = 0; wi < expected.getw(); ++wi) {
        for (int zi = 0; zi < expected.getz(); ++zi) {
            for (int ri = 0; ri < expected.getrt(); ++ri) {
                for (int ci = 0; ci < expected.getct(); ++ci) {
                    if (!match && expected.tile_tensor[wi][zi][ri][ci] != observed.tile_tensor[wi][zi][ri][ci]) {
                        log_info(tt::LogTest, "Tile index [ci,ri,zi,wi] = [{},{},{},{}]", ci, ri, zi, wi);
                        log_info(tt::LogTest, "Diff\n{}", expected.tile_tensor[wi][zi][ri][ci].get_diff_string(observed.tile_tensor[wi][zi][ri][ci]));
                    }else if (verbose_compare){
                        log_info(tt::LogTest, "Tile index [ci,ri,zi,wi] = [{},{},{},{}]", ci, ri, zi, wi);
                        log_info(tt::LogTest, "Observed/Expected\n{}", expected.tile_tensor[wi][zi][ri][ci].get_string());
                    }
                }
            }
        }
    }

    if(match) log_info(tt::LogTest, "Tensors match");

    // Keep going and show more comparisons in verbose mode. Don't die here.
    if (!verbose_compare){
        log_assert(match, "Tile mismatch!");
    }
    return match;
}

int get_queue_size(tt_queue_info &queue_info, bool is_tilized, uint32_t tile_height = 32, uint32_t tile_width = 32) {
    int num_buffers = queue_info.grid_size.r * queue_info.grid_size.c;
    int buffer_size = tt::backend::get_io_size_in_bytes(
        queue_info.data_format,
        !is_tilized,
        queue_info.dim.ublock_ct,
        queue_info.dim.ublock_rt,
        queue_info.dim.mblock_m,
        queue_info.dim.mblock_n,
        queue_info.dim.t,
        queue_info.entries,
        tile_height,
        tile_width);
    return buffer_size * num_buffers;
}

// Push an extra garbage tensor to device in unexpected times, to ensure timeout is seen and no impact to device. Avoid assert if timeout is disabled.
void do_extra_push_hit_timeout(tt_dram_io_desc &io_desc, tt_queue_info& queue_info, bool batched_push, int push_timeout_seconds){
    log_info(tt::LogTest, "Going to do an extra push that should trigger timeout. batched_push: {} push_timeout_seconds: {}", batched_push, push_timeout_seconds);
    aligned_vector<uint16_t> host_stored_tensor_garbage;
    tt_tensor input_new = get_tilized_tensor(queue_info, 0xBEEF, batched_push, queue_info.data_format);
    tt::tt_PytorchTensorDesc py_tensor_desc_new = tt::io::get_pytorch_tensor_desc<uint16_t>(input_new, host_stored_tensor_garbage);
    auto push_status = tt::backend::push_input(io_desc, py_tensor_desc_new, !batched_push, push_timeout_seconds);
    log_assert(push_timeout_seconds == 0 || push_status == tt::DEVICE_STATUS_CODE::TimeoutError, "Did not see the expected TimeoutError on extra push");
}

// Get and pop too much or when output io queue is empty, to ensure timeout is seen and no impact to device. Avoid assert if timeout is disabled.
void do_extra_get_pop_hit_timeout(tt_dram_io_desc &io_desc, bool batched_push, int get_timeout_seconds, int pop_timeout_seconds){
    log_info(tt::LogTest, "Going to do an extra get and pop that should each trigger timeout. batched_push: {} get_timeout_seconds: {} pop_timeout_seconds: {}", batched_push, get_timeout_seconds, pop_timeout_seconds);

    tt::tt_PytorchTensorDesc py_desc;
    auto get_status = tt::backend::get_output(io_desc, py_desc, !batched_push, get_timeout_seconds);
    log_assert(get_timeout_seconds == 0 || get_status == tt::DEVICE_STATUS_CODE::TimeoutError, "Did not see the expected TimeoutError on extra get");

    auto pop_status = tt::backend::pop_output(io_desc, !batched_push, pop_timeout_seconds);
    log_assert(pop_timeout_seconds == 0 || pop_status == tt::DEVICE_STATUS_CODE::TimeoutError, "Did not see the expected TimeoutError on extra pop");

    // Don't need to call free_tensor, since no tensor returned.
    log_assert(py_desc.ptr == nullptr, "Did not expect tensor to be returned since timeout");
}

// Ensure HW Tilizer was used in tests that intended it to be used, when using automatic enablement of hw-tilize.
void ensure_sw_tilize_used(tt_backend_config &config, vector<tt_dram_io_desc> &input_io_desc){
    for (const auto& desc: input_io_desc) {
        bool hw_tilizer_exists = false;
        if (config.type == tt::DEVICE::Silicon){
            hw_tilizer_exists = tt_object_cache<DeviceTilizer<Tilizer::FastTilizeMMIOPush>>::exists(desc.queue_name) or tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::exists(desc.queue_name);
        }else if (config.type == tt::DEVICE::Versim){
            hw_tilizer_exists = tt_object_cache<DeviceTilizer<Tilizer::FastTilizeDevicePush>>::exists(desc.queue_name);
        }else{
            log_assert(false, "Unsupported Backend (config.type) for DeviceTilizer. Must be Silicon or Versim only.");
        }
        log_info(tt::LogTest, "hw_tilizer_exists: {} for queue_name: {}", hw_tilizer_exists, desc.queue_name);
        log_assert(!hw_tilizer_exists, "SW Tilizer was expected to be used in this test but was not.");
    }
}

void setup_temp_queue_for_conv(tt_queue_info& temp_queue_info, const tt_queue_info& queue_info, std::vector<int>& input_shape, tt::DataFormat data_format = tt::DataFormat::Invalid) {
    temp_queue_info.dim.ublock_rt = 1;
    temp_queue_info.dim.ublock_ct = 1;
    temp_queue_info.dim.mblock_m = input_shape[3];
    temp_queue_info.dim.mblock_n = input_shape[2];
    temp_queue_info.dim.t = input_shape[1];
    temp_queue_info.input_count = queue_info.input_count;
    temp_queue_info.grid_size.r = 1;
    temp_queue_info.grid_size.c = 1;
    if(data_format != tt::DataFormat::Invalid) {
        temp_queue_info.data_format = data_format;
    } else {
        temp_queue_info.data_format = queue_info.data_format;
    }
}

string flatten_input_args(vector<string> input_args) {
    string input_args_str = "";
    for (auto arg: input_args) {
        input_args_str += arg + " ";
    }
    return input_args_str;
}


int main(int argc, char **argv) {
    // Use 1MB DMA buffer size for reads. For writes DMA is still disabled.
    // This is providing 15x speed up on poping output queues from dram.
    // This is safe to use in test_pybuda_api since we are poping outputs from a single thread.
    setenv("TT_PCI_DMA_BUF_SIZE", "1048576", false);
    std::vector<std::string> args(argv, argv + argc);

    bool pass = true;
    string input_args_flattened = flatten_input_args(args);
    std::string output_dir;
    bool run_only;

    int opt_level, num_pushes;
    int num_extra_gets; // For performance benchmarking device reads.
    int push_duration_thresh, pop_duration_thresh;
    int push_timeout_seconds, pop_timeout_seconds, get_timeout_seconds;
    int num_loops;
    bool run_silicon, batched_push, skip_tile_check;
    bool io_api_timeout_test;
    bool sw_tilize_test;
    bool conv_perf_checking;
    std::string netlist_path;
    std::string arch_name;
    std::string host_format_;
    int seed;
    int allowed_perf_fails;
    std::string input_tensor_binary = "";
    std::string expected_tensor_binary = "";
    std::vector<int> input_shape;
    int stride;
    std::string cmd_line_args;
    float average_push_bw = 0;
    float min_push_bw = 0;
    float max_push_bw = 0;
    float target_push_bw_for_conv = 0;
    bool skip_gets = false;
    bool unaligned_conv = false;
    bool verbose_compare = false;
    bool disable_min_loops = false;
    // For testing fractional tile support
    uint32_t tile_height = 32;
    uint32_t tile_width = 32;

    std::tie(output_dir, args) = verif_args::get_command_option_and_remaining_args(args, "--outdir", "");
    if (output_dir == "") {
        output_dir = verif_filesystem::get_output_dir(__FILE__, args);
    }

    std::tie(run_only, args)     = verif_args::has_command_option_and_remaining_args(args, "--run-only");
    std::tie(seed, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--seed", 0);
    std::tie(opt_level, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--O", 2);
    std::tie(num_pushes, args)   = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-pushes", 1);
    std::tie(num_loops, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-loops", 1);
    std::tie(batched_push, args) = verif_args::has_command_option_and_remaining_args(args, "--batched-push");
    std::tie(run_silicon, args)  = verif_args::has_command_option_and_remaining_args(args, "--silicon");
    std::tie(netlist_path, args) = verif_args::get_command_option_and_remaining_args(args, "--netlist", "loader/tests/net_tilize_untilize/tilize_fp16b_untilize_fp16.yaml");
    std::tie(arch_name, args)    = verif_args::get_command_option_and_remaining_args(args, "--arch", "grayskull");
    std::tie(host_format_, args) = verif_args::get_command_option_and_remaining_args(args, "--host-data-format", "QueueFormat");
    std::tie(input_tensor_binary, args) = verif_args::get_command_option_and_remaining_args(args, "--input_tensor_binary", "");
    std::tie(expected_tensor_binary, args) = verif_args::get_command_option_and_remaining_args(args, "--expected_tensor_binary", "");
    std::tie(cmd_line_args, args) = verif_args::get_command_option_and_remaining_args(args, "--input-tensor-shape", "");
    verif_args::split_string_into_vector(input_shape, cmd_line_args, ",");
    std::tie(stride, args) = verif_args::get_command_option_int32_and_remaining_args(args, "--stride", -1);
    std::tie(conv_perf_checking, args) = verif_args::has_command_option_and_remaining_args(args, "--conv-perf-checking");
    // For API timeout testing, we will puroposely hit a timeout, and handle it gracefully. For other tests, use get timeout only.
    std::tie(io_api_timeout_test, args)     = verif_args::has_command_option_and_remaining_args(args, "--io-api-timeout-test");
    std::tie(push_timeout_seconds, args)    = verif_args::get_command_option_uint32_and_remaining_args(args, "--push-timeout-seconds", io_api_timeout_test ? 1 : 0);
    std::tie(pop_timeout_seconds, args)     = verif_args::get_command_option_uint32_and_remaining_args(args, "--pop-timeout-seconds", io_api_timeout_test ? 1 : 0);
    std::tie(get_timeout_seconds, args)     = verif_args::get_command_option_uint32_and_remaining_args(args, "--get-timeout-seconds", io_api_timeout_test ? 1 : 1);
    std::tie(target_push_bw_for_conv, args) = verif_args::get_command_option_double_and_remaining_args(args, "--target-push-bw-for-conv", 0);
    std::tie(unaligned_conv, args)          = verif_args::has_command_option_and_remaining_args(args, "--unaligned-conv");
    std::tie(tile_height, args)             = verif_args::get_command_option_uint32_and_remaining_args(args, "--tile-height", 32);
    std::tie(tile_width, args)             = verif_args::get_command_option_uint32_and_remaining_args(args, "--tile-width", 32);
    log_info(tt::LogTest, "push_timeout_seconds: {}  pop_timeout_seconds: {}  get_timeout_seconds: {} " , push_timeout_seconds, pop_timeout_seconds, get_timeout_seconds);

    std::tie(sw_tilize_test, args) = verif_args::has_command_option_and_remaining_args(args, "--sw-tilize-test");

    log_assert(arch_name == "grayskull" || arch_name == "wormhole", "Invalid arch");

    std::tie(skip_tile_check, args)      = verif_args::has_command_option_and_remaining_args(args, "--skip-tile-check");
    std::tie(skip_gets, args)            = verif_args::has_command_option_and_remaining_args(args, "--skip-gets");
    std::tie(push_duration_thresh, args) = verif_args::get_command_option_uint32_and_remaining_args(args, "--push-duration-thresh", -1);
    std::tie(pop_duration_thresh, args)  = verif_args::get_command_option_uint32_and_remaining_args(args, "--pop-duration-thresh", -1);
    std::tie(num_extra_gets, args)       = verif_args::get_command_option_uint32_and_remaining_args(args, "--num-extra-gets", 0);
    std::tie(disable_min_loops, args)    = verif_args::has_command_option_and_remaining_args(args, "--disable-min-loops");

    // CI cluster machines have some random variability (other jobs running?) causing occasional perf fails. Since we run multiple loops
    // of test with perf checking enabled, allow small number (20%) to fail to hide random machine/cpu blips.
    std::tie(allowed_perf_fails, args)   = verif_args::get_command_option_uint32_and_remaining_args(args, "--allowed-perf-fails", num_loops/5);
    std::tie(verbose_compare, args) = verif_args::has_command_option_and_remaining_args(args, "--verbose-compare");

    // Strip out perf arguments here, they will be passed directly to backend object for further parsing.
    perf::PerfDesc perf_desc;
    perf_desc = perf::PerfDesc(args, netlist_path);

    verif_args::validate_remaining_args(args);

    // Push/Pop/Get API timeout test relies on batched_push=false
    log_assert(!io_api_timeout_test || !batched_push, "Expected batched_push==false when io_api_timeout_test=1");
    if(sw_tilize_test) setenv("TT_BACKEND_FORCE_SW_TILIZE", "1", 1);

    bool perf_check = (push_duration_thresh > 0) or (pop_duration_thresh > 0);
    int perf_check_loop_start = (num_loops/2)-1;
    if (perf_check) {
        log_assert(disable_min_loops || num_loops >= 10, "Perf check requires > 10 loops for CPU freq ramp");
         // By default don't use this method to pin CPU unless one is passed via env-var or -1 is passed (for random cpu)
        if (std::getenv("SET_CPU_AFFINITY")){
            int cpu_id = atoi(std::getenv("SET_CPU_AFFINITY"));
            set_cpu_affinity(cpu_id); 
        }
    }

    // if (input_tensor_binary.size()) {
    //     num_loops = 1;
    // }

    // Backend setup
    tt_backend_config config = {
        .type = run_silicon ? tt::DEVICE::Silicon : tt::DEVICE::Versim,
        .arch = arch_name == "grayskull" ? tt::ARCH::GRAYSKULL : tt::ARCH::WORMHOLE,
        .optimization_level = opt_level,
        .output_dir = output_dir,
        .perf_desc_args=input_args_flattened,
        };

    if (run_only) {
        config.mode = DEVICE_MODE::RunOnly;
    }

    std::shared_ptr<tt_backend> backend_api = tt_backend::create(netlist_path, config);
    tt_backend &backend = *backend_api.get();
    tt_runtime_workload &workload = *tt::io::get_workload(netlist_path);

    // ----------------------------------------------------------------
    // PyBuda API for initializing backend
    // ----------------------------------------------------------------
    log_assert(backend.initialize() == tt::DEVICE_STATUS_CODE::Success, "initialize failed");

    // ----------------------------------------------------------------
    // PyBuda API for querying IO descriptors
    // ----------------------------------------------------------------
    vector<tt_dram_io_desc> input_io_desc;
    for (const auto& it : workload.queues) {
        if (it.second.my_queue_info.input == "HOST") {
            tt_dram_io_desc io_desc = backend.get_queue_descriptor(it.first);
            tt::backend::translate_addresses(io_desc);
            input_io_desc.push_back(io_desc);
        }
    }

    std::vector<tt_tensor> expected_output;
    tt_cluster *cluster = tt::io::get_cluster(netlist_path, config.type);
    int perf_fails = 0;
    std::vector<float> perf_fails_bw = {};

    for (int loop=0; loop<num_loops; loop++) {
        log_info(tt::LogTest, "Running loop: {} of num_loops: {} in test now...", loop, num_loops);

        int64_t push_bytes = 0;
        int64_t push_duration = 0;
        int push_size;
        log_info(tt::LogTest, "Pushing all inputs...");

        for (tt_dram_io_desc &io_desc : input_io_desc) {
            tt_queue_info& queue_info = workload.queues[io_desc.queue_name].my_queue_info;
            push_size = batched_push ? queue_info.input_count : 1;

            if(stride > 0){
                io_desc.s_descriptor.stride = stride;
                for(int x = 0; x < stride; x++){
                    for(int y = 0; y < stride; y++){
                        io_desc.s_descriptor.xy_offsets.push_back({x, y});
                    }
                }
            }
            
            // By default use the queue format for no translation unless specific format passed on cmd line.
            DataFormat host_format   = host_format_ == "QueueFormat" ? queue_info.data_format : verif::STRING_TO_DATA_FORMAT.at(host_format_);

            // ----------------------------------------------------------------
            // PyBuda API for size calculations
            // ----------------------------------------------------------------
            int tensor_size;
            tt_queue_info temp_queue_info;
            if(conv_perf_checking) {
                // If performing perf checking on convolution shuffler, use the original (unshuffled) tensor size to compute push BW
                
                std::vector<int> aligned_shape = input_shape;
                if(unaligned_conv) {
                    // If activation is unaligned, approximate the tensor size by rounding down to nearest multiple of tile size (conservative BW)
                    aligned_shape[2] = (aligned_shape[3] / 32);
                    aligned_shape[3] = (aligned_shape[3] / 32);
                }
                setup_temp_queue_for_conv(temp_queue_info, queue_info, aligned_shape, host_format);
                tensor_size = get_tensor_size_in_bytes(temp_queue_info, true);
            }
            else{
                bool is_tilized = workload.has_tilized_data(queue_info.name);
                int queue_size  = get_queue_size(queue_info, is_tilized, tile_height, tile_width); // uses pybuda api
                tensor_size = get_tensor_size_in_bytes(queue_info, is_tilized);
                int header_size = queue_info.grid_size.r * queue_info.grid_size.c * tt::io::QUEUE_HEADER_SIZE_BYTES;
                log_assert(tensor_size * queue_info.entries + header_size == queue_size, "Tensor and queue size mismatch"); // check size consistency manual vs. pybuda api
            }
            
            std::vector<tt_PytorchTensorDesc> pytorch_tensors = {};
            std::vector<aligned_vector<float>> fp32_data = {};
            std::vector<aligned_vector<uint16_t>> fp16_data = {};
            std::vector<aligned_vector<int8_t>> int8_data = {};
            for(int i = 0; i < num_pushes; i++) {
                // Setup host side data to push. Do this outside the main push loop, to prevent potential cache misses from affecting push BW.
                if(!(unaligned_conv || host_format == tt::DataFormat::Int8)) {
                    tt_tensor input;
                    if(input_tensor_binary.size()) {
                        log_assert(input_shape.size(), "Input shape not specified");
                        log_assert(expected_tensor_binary.size(), "Expected binary not specified");
                        log_assert(stride > 0, "Stride not specified");
                        input = get_tensor_from_binary(input_shape, input_tensor_binary, host_format);
                        expected_output.push_back(get_tensor_from_binary(get_shape_for_shuffled_tensor(input_shape, stride), expected_tensor_binary, host_format));
                    }
                    else if(conv_perf_checking) {
                        input = get_tilized_tensor(temp_queue_info, i, batched_push, host_format, tile_height, tile_width);
                    }
                    else{
                        input = get_tilized_tensor(queue_info, i, batched_push, host_format, tile_height, tile_width);
                        expected_output.push_back(input);
                    }

                    if (verbose_compare){
                        std::cout << "Pushing Input Tensor" << std::endl;
                        input.dump();
                    }

                    if(input.get_data_format() == DataFormat::Float32) {
                        fp32_data.push_back({});
                        pytorch_tensors.push_back(tt::io::get_pytorch_tensor_desc<float>(input, fp32_data.back()));
                    }
                    else if(input.get_data_format() == DataFormat::Int8) {
                        int8_data.push_back({});
                        pytorch_tensors.push_back(tt::io::get_pytorch_tensor_desc<int8_t>(input, int8_data.back()));
                    }
                    else {
                        fp16_data.push_back({});
                        pytorch_tensors.push_back(tt::io::get_pytorch_tensor_desc<uint16_t>(input, fp16_data.back()));
                    }
                    pytorch_tensors.back().owner = tt::OWNERSHIP::Frontend;
                }
                else {
                    if(unaligned_conv) {
                        tt_PytorchTensorDesc tensor_to_shuffle;
                        if(host_format == DataFormat::Float32) {
                            fp32_data.push_back(aligned_vector<float>(input_shape[0] * input_shape [1] * input_shape[2] * input_shape[3], 0));
                            for(int i = 0; i < fp32_data.size(); i++) {
                                fp32_data.back()[i] = i % 100; // Bound data between 0 & 99
                            }
                            tensor_to_shuffle.ptr = fp32_data.back().data();
                        }
                        else {
                            fp16_data.push_back(aligned_vector<uint16_t>(input_shape[0] * input_shape [1] * input_shape[2] * input_shape[3], 0));
                            for(int i = 0; i < fp16_data.size(); i++) {
                                fp16_data.back()[i] = i % 100;
                            }
                            tensor_to_shuffle.ptr = fp16_data.back().data();
                        }
                        tt::io::set_py_tensor_metadata(tensor_to_shuffle,{static_cast<uint32_t>(input_shape[0]), static_cast<uint32_t>(input_shape[1]), static_cast<uint32_t>(input_shape[2]), static_cast<uint32_t>(input_shape[3])}, sizeof(float), tt::PY_TENSOR_DIMS, host_format);
                        tensor_to_shuffle.owner = tt::OWNERSHIP::Frontend;
                        pytorch_tensors.push_back(tensor_to_shuffle);
                    }
                    else {
                        // Infer the Pytorch tensor shape from the q descriptor
                        input_shape = {static_cast<int32_t>(io_desc.input_count), static_cast<int32_t>(io_desc.t), static_cast<int32_t>(io_desc.bufq_grid_dim_r * io_desc.ublock_rt * io_desc.mblock_m * 32), static_cast<int32_t>(io_desc.bufq_grid_dim_c * io_desc.ublock_ct * io_desc.mblock_n * 32)};
                        tt_PytorchTensorDesc input_tensor; // What we push to device
                        tt_PytorchTensorDesc output_tensor; // Expected Fp32 data (after reading back from device)
                        int8_data.push_back(aligned_vector<int8_t>(input_shape[0] * input_shape [1] * input_shape[2] * input_shape[3], 0));
                        fp32_data.push_back(aligned_vector<float>(input_shape[0] * input_shape [1] * input_shape[2] * input_shape[3], 0));
                        for(int i = 0; i < int8_data.back().size(); i++) {
                            int8_data.back()[i] = -127 + i % 255; // Bound int8 data between -127 & 127
                            fp32_data.back()[i] = -127 + i % 255;
                        }
                        input_tensor.ptr = int8_data.back().data();
                        output_tensor.ptr = fp32_data.back().data();
                        tt::io::set_py_tensor_metadata(input_tensor,{static_cast<uint32_t>(input_shape[0]), static_cast<uint32_t>(input_shape[1]), static_cast<uint32_t>(input_shape[2]), static_cast<uint32_t>(input_shape[3])}, sizeof(uint8_t), tt::PY_TENSOR_DIMS, host_format);
                        tt::io::set_py_tensor_metadata(output_tensor,{static_cast<uint32_t>(input_shape[0]), static_cast<uint32_t>(input_shape[1]), static_cast<uint32_t>(input_shape[2]), static_cast<uint32_t>(input_shape[3])}, sizeof(uint32_t), tt::PY_TENSOR_DIMS, tt::DataFormat::Float32);
                        pytorch_tensors.push_back(input_tensor);
                        expected_output.push_back(tt::io::reconstruct_tensor_from_untilized(io_desc, output_tensor, batched_push));
                    }
                }
            }

            for (int i=0; i<num_pushes; i++) {

                // Before final push, try and push way too much (batched_push=true) and ensure timeout is hit, with no device impact. Don't do
                // this until timeout is actually greater than zero since it would result in hang and not any asserts.
                if (io_api_timeout_test && (push_timeout_seconds > 0) && i == (num_pushes - 1)){
                    do_extra_push_hit_timeout(io_desc, queue_info, true, push_timeout_seconds);
                }

                // ----------------------------------------------------------------
                // PyBuda API for pushing input
                // ----------------------------------------------------------------
                push_bytes += push_size * tensor_size;
                auto push_start = std::chrono::high_resolution_clock::now();
                auto push_status = tt::backend::push_input(io_desc, pytorch_tensors.at(i), !batched_push, push_timeout_seconds);
                
                log_assert(push_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on this push");
                // tt::io::push_queue_tilized_input(queue_info, input, cluster); // sw tilize for debug
                
                auto push_end = std::chrono::high_resolution_clock::now();
                push_duration += std::chrono::duration_cast<std::chrono::microseconds>(push_end - push_start).count();

                // After final push, try to push again a little bit too much (batched_push=false), and ensure timeout still hit. Don't count this in duration.
                if (io_api_timeout_test && (push_timeout_seconds > 0) && i == (num_pushes - 1)){
                    do_extra_push_hit_timeout(io_desc, queue_info, false, push_timeout_seconds);
                }
            }
        }

        // Ensure SW-Tilizer was correctly picked for tests that expect it.
        if (sw_tilize_test){
            ensure_sw_tilize_used(config, input_io_desc);
        }

        float push_bandwidth = ((float)push_bytes / (1024*1024*1024)) / ((float)push_duration / 1.0e6);
        if(loop) average_push_bw += push_bandwidth;
        if(loop) max_push_bw = push_bandwidth > max_push_bw ? push_bandwidth : max_push_bw;
        if(loop) min_push_bw = min_push_bw == 0 || push_bandwidth < min_push_bw ? push_bandwidth : min_push_bw;

        bool push_perf_check_ena        = (push_duration_thresh > 0 && loop >= perf_check_loop_start) || (target_push_bw_for_conv > 0 && loop); //Enable perf check for conv tests after the initial (setup) loop
        std::string push_perf_check_str = push_perf_check_ena ? "PerfCheckEnabled" : ""; // For filtering results
        log_info(tt::LogTest, "Push Size:         \t{} MB", push_bytes/(1024 * 1024));
        log_info(tt::LogTest, "Push Elapsed Time: \t{} us {}", push_duration, push_perf_check_str);
        log_info(tt::LogTest, "Push Bandwidth:    \t{:3.3f} GB/s {}", push_bandwidth, push_perf_check_str);
        log_info(tt::LogTest, "Batch Size: {}", push_size);
        log_info(tt::LogTest, "Push Samples/sec: \t{:3.3f}", ((float)push_size) / ((float)push_duration / 1.0e6));
        if (push_perf_check_ena) {
            if (push_duration > push_duration_thresh || (push_bandwidth < target_push_bw_for_conv)) {
                if (perf_fails < allowed_perf_fails){
                    perf_fails++;
                    if(target_push_bw_for_conv){
                        if(push_bandwidth < target_push_bw_for_conv) {
                            perf_fails_bw.push_back(push_bandwidth);
                            log_warning(tt::LogTest, "Push BW for conv {} is less than perf threshold {} GB/s. Ignoring since allowed_perf_fails: {}", push_bandwidth, target_push_bw_for_conv, allowed_perf_fails);
                        }
                    }
                    else{
                        log_warning(tt::LogTest, "Push duration {} us failed perf threshold {} us! Ignoring since allowed_perf_fails: {}", push_duration, push_duration_thresh, allowed_perf_fails);
                    }
                }else{
                    if(!conv_perf_checking) {
                        pass = false;
                        log_error("Push duration {} us failed perf threshold {} us!", push_duration, push_duration_thresh);
                    }
                }
            }
        }

        // ----------------------------------------------------------------
        // PyBuda API for running programs
        // ----------------------------------------------------------------

        log_info(tt::LogTest, "Done pushing inputs, going to run programs...");

        for (std::string program_name : workload.program_order) {
            log_assert(backend.run_program(program_name, {}) == tt::DEVICE_STATUS_CODE::Success, "run_program failed");
        }
        backend.wait_for_idle();

        // ----------------------------------------------------------------
        // PyBuda API for querying IO descriptors
        // ----------------------------------------------------------------
        vector<tt_dram_io_desc> output_io_desc;
        for (const auto& it : workload.queues) {
            if (it.second.my_queue_info.input != "HOST") {
                tt_dram_io_desc io_desc = backend.get_queue_descriptor(it.first);
                tt::backend::translate_addresses(io_desc);
                output_io_desc.push_back(io_desc);
            }
        }

        int64_t pop_bytes = 0;
        int64_t pop_duration = 0;
        int64_t pop_size = 0;
        log_info(tt::LogTest, "Popping all outputs...");

        bool popped_output_tiles = false;
        auto num_outputs = expected_output.size(); // Used to identify final get/pop.

        for (tt_dram_io_desc &io_desc : output_io_desc) {
            int output_idx = 0;

            tt_queue_info& queue_info = workload.queues[io_desc.queue_name].my_queue_info;
            int batch_size = batched_push ? queue_info.input_count : 1;

            // ----------------------------------------------------------------
            // PyBuda API for size calculations
            // ----------------------------------------------------------------
            bool is_tilized = workload.has_tilized_data(queue_info.name);
            int queue_size  = get_queue_size(queue_info, is_tilized, tile_height, tile_width); // uses pybuda api
            int tensor_size = get_tensor_size_in_bytes(queue_info, is_tilized);
            int header_size = queue_info.grid_size.r * queue_info.grid_size.c * tt::io::QUEUE_HEADER_SIZE_BYTES;
            log_assert(tensor_size * queue_info.entries + header_size == queue_size, "Tensor and queue size mismatch"); // check size consistency manual vs. pybuda api
            pop_size = batch_size * tensor_size; // Each pop is this size. Multiple pops may happen.

            while (!tt::io::is_queue_empty(io_desc, queue_info, cluster)) {

                // Before final get/pop, try to get/pop way too much (batched_push=true), and ensure timeout is hit.
                if (io_api_timeout_test && (output_idx == (num_outputs - 1))){
                    do_extra_get_pop_hit_timeout(io_desc, true, get_timeout_seconds, pop_timeout_seconds);
                }

                // ----------------------------------------------------------------
                // PyBuda API for popping output
                // ----------------------------------------------------------------
                tt::tt_PytorchTensorDesc py_desc;

                pop_bytes += pop_size;
                auto pop_start = std::chrono::high_resolution_clock::now();
                // log_info(tt::LogTest, "Calling get_output() for output_idx: {} for queue: {} batched_push: {} pop_size: {} bytes", output_idx, queue_info.name, batched_push, pop_size);

                auto get_status = skip_gets ? tt::DEVICE_STATUS_CODE::Success : tt::backend::get_output(io_desc, py_desc, !batched_push, get_timeout_seconds);

                if (!skip_gets){
                    // For device read performance benchmarking.
                    for (int i=0; i<num_extra_gets; i++){
                        log_info(tt::LogTest, "Doing extra get #{} Output #{} for queue: {}", i, output_idx, queue_info.name);
                        get_status = tt::backend::get_output(io_desc, py_desc, !batched_push, get_timeout_seconds);
                        log_assert(get_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on get");
                        pop_bytes += pop_size;
                    }
                }

                auto pop_end = std::chrono::high_resolution_clock::now();
                pop_duration += std::chrono::duration_cast<std::chrono::microseconds>(pop_end - pop_start).count();
                auto pop_status = tt::backend::pop_output(io_desc, !batched_push, pop_timeout_seconds);
                popped_output_tiles = true;

                log_assert(get_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on get");
                log_assert(pop_status == tt::DEVICE_STATUS_CODE::Success, "Expected to see backend status Success on pop");

                if ((stride > 0 && !conv_perf_checking) || !skip_tile_check) {
                    // Mimic python runtime to extract output tensor from pytorch tensor descriptor
                    log_info(tt::LogTest, "Comparing observed and expected outputs...");
                    tt_tensor observed = tt::io::reconstruct_tensor_from_untilized(io_desc, py_desc, batched_push);
                    tt_tensor expected = expected_output.at(output_idx);

                    // Compare against expected output
                    bool tensor_match = compare_tilized_tensors(observed, expected, verbose_compare);
                    pass &= tensor_match;
                    log_info(tt::LogTest, "Output #{} for queue: {} : {}", output_idx, queue_info.name, tensor_match ? "PASSED" : "FAILED");
                }

                if (!skip_gets){
                    tt::backend::free_tensor(py_desc);
                }
                output_idx++;
            }

            // log_assert(expected_output.size() == output_idx, "Did not compare against all expected_output tensors.");

            // After final get/pop, try to get/pop a little bit too much (batched_push=false), and ensure timeout still hit.
            if (io_api_timeout_test){
                do_extra_get_pop_hit_timeout(io_desc, false, get_timeout_seconds, pop_timeout_seconds);
            }
        }

        // Make sure we popped at least something from some output queue.
        log_assert(popped_output_tiles, "Finished test but did not pop any output tiles");

        // Can't do any perf checking or reporting if we skipped getting the data.
        if (!skip_gets){
            float pop_bandwidth = ((float)pop_bytes / (1024*1024*1024)) / ((float)pop_duration / 1.0e6);

            bool pop_perf_check_ena         = pop_duration_thresh > 0 && loop >= perf_check_loop_start;
            std::string pop_perf_check_str  = pop_perf_check_ena ? "PerfCheckEnabled" : ""; // For filtering results.
            log_info(tt::LogTest, "Pop Elapsed Time: \t{} us\t({} seconds) {}", pop_duration, (pop_duration/1.0e6), pop_perf_check_str);
            log_info(tt::LogTest, "Pop Size:         \t{} MB\t\t({} MB per pop) {}", pop_bytes/(1024 * 1024), pop_size/(1024 * 1024), pop_perf_check_str);
            log_info(tt::LogTest, "Pop Bandwidth:    \t{:3.3f} GB/s\t({} MB/s) {}", pop_bandwidth, pop_bandwidth * 1024, pop_perf_check_str);

            if (pop_perf_check_ena) {
                if (pop_duration > pop_duration_thresh) {
                    if (perf_fails < allowed_perf_fails){
                        perf_fails++;
                        log_warning(tt::LogTest, "Pop duration {} us failed perf threshold {} us! Ignoring since allowed_perf_fails: {}", pop_duration, pop_duration_thresh, allowed_perf_fails);
                    }else{
                        pass = false;
                        log_error("Pop duration {} us failed perf threshold {} us!", pop_duration, pop_duration_thresh);
                    }
                }
            }
        }


    }
    if(target_push_bw_for_conv) {
        float avg_push_bw_ignoring_fails = (average_push_bw - std::reduce(perf_fails_bw.begin(), perf_fails_bw.end())) / (num_loops - 1 - perf_fails_bw.size());
        if(avg_push_bw_ignoring_fails < target_push_bw_for_conv) {
            pass = false;
            log_error("Average Push Bandwidth: {:3.3f} GB/s is less than target: {:3.3f} GB/s. (Min: {:3.3f}  Max: {:3.3f}  Delta: {:3.3f})", average_push_bw / (num_loops - 1), target_push_bw_for_conv, min_push_bw, max_push_bw, max_push_bw - min_push_bw);
        }
        else{
            log_info(tt::LogTest, "Average Push Bandwidth: {:3.3f} GB/s (Min: {:3.3f}  Max: {:3.3f}  Delta: {:3.3f})", average_push_bw / (num_loops - 1), min_push_bw, max_push_bw, max_push_bw - min_push_bw);
        }
    }
    else {
        log_info(tt::LogTest, "Average Push Bandwidth: {:3.3f} GB/s (Min: {:3.3f}  Max: {:3.3f}  Delta: {:3.3f})", average_push_bw / (num_loops - 1), min_push_bw, max_push_bw, max_push_bw - min_push_bw);
    }
    // ----------------------------------------------------------------
    // PyBuda API for stopping backend
    // ----------------------------------------------------------------
    backend.finish();
    log_assert(pass, "Test failed");
    return 0;
}