// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// Need to include all the components to compile a test
// netlist_parser/golden generation/runtime_api
#include <atomic>
#include <fstream>
#include <limits>
#include <thread>
#include <filesystem>

#include "common/tt_parallel_for.h"
#include "runtime.hpp"
#include "verif.hpp"

using namespace tt;
using namespace verif::comparison;
using namespace verif::stimulus;
using namespace verif::random;

// Meant for communication between the worker thread spawned and the main thread which checks this.
std::atomic<bool> done(false);
std::atomic<bool> pass(true);

static const std::string c_netlist_path_arg_name = "--netlist";
static const std::string c_output_dir_arg_name = "--outdir";
static const std::string c_bin_path_arg_name = "--bin-path";
static const std::string c_default_test_config_arg_name = "--default-test-config";
static const std::string c_seed_arg_name = "--seed";
static const std::string c_timeout_arg_name = "--timeout";
static const std::string c_opt_level_arg_name = "--O";
static const std::string c_run_silicon_arg_name = "--silicon";
static const std::string c_compare_with_saved_golden_arg_name = "--compare-with-saved-golden";
static const std::string c_vcd_dump_cores_arg_name = "--vcd-dump-cores";
static const std::string c_perf_thresh_arg_name = "--perf-thresh";
static const std::string c_backend_mode_arg_name = "--mode";
static const std::string c_determinism_test_arg_name = "--determinism-test";
static const std::string c_determinism_minibatch_count_arg_name = "--minibatch-count";
static const std::string c_help_arg_name = "--help";

static const std::string c_netlist_path_arg_default_value = "verif/graph_tests/netlists/netlist_binary_single_tile.yaml";
static const std::string c_default_test_config_arg_default_value = "verif/graph_tests/default_test_config.yaml";
static const std::string c_host_input_data_format_arg_default_value = "Float16_b";
static const bool c_run_silicon_arg_default_value = false;
static const bool c_compare_with_saved_golden_arg_default_value = false;
static const bool c_golden_dir_exists_arg_default_value = false;
static const int c_seed_arg_default_value = -1;
static const int c_timeout_arg_default_value = INT_MAX;
static const int c_opt_level_arg_default_value = 0;
static const int c_perf_thresh_arg_default_value = -1;
static const int c_determinism_minibatch_count_default_value = 50;
static const unsigned c_backend_mode_compile_and_run = 0;
static const unsigned c_backend_mode_compile_only = 1;
static const unsigned c_backend_mode_default_value = c_backend_mode_compile_and_run;
static const bool c_determinism_test_default_value = false;

std::string create_help_string()
{
    string help_string = "test_graph\n";

    help_string += "\t" + c_netlist_path_arg_name + " <>" +
        "\t\t\t: Path to netlist file (Default: " + c_netlist_path_arg_default_value + ")\n";

    help_string += "\t" + c_output_dir_arg_name + " <>" +
        "\t\t\t: Path to test output directory (Default: tt_build directory)\n";

    help_string += "\t" + c_bin_path_arg_name + " <>" +
        "\t\t\t: Path to input binaries, generated from FE\n";

    help_string += "\t" + c_default_test_config_arg_name + " <>" +
        "\t: Path to default test config file. Used only if test config is not specified in the netlist (Default: " +
        c_default_test_config_arg_default_value + ")\n";

    help_string += "\t" + c_seed_arg_name + " <>" +
        "\t\t\t: Seed for generating random input tensors. " +
        "Passing " + to_string(c_seed_arg_default_value) + " as the seed will set a default seed. " +
        "(Default: random seed)\n";

    help_string += "\t" + c_timeout_arg_name + " <>" +
        "\t\t\t: Timeout in Seconds (Default: " + to_string(c_timeout_arg_default_value) + " (no timeout))\n";

    help_string += "\t" + c_opt_level_arg_name + " <>" +
        "\t\t\t\t: Optimization level, lower for more checking, higher for caching and perf (Default: " +
        to_string(c_opt_level_arg_default_value) + ")\n";

    help_string += "\t" + c_run_silicon_arg_name +
        "\t\t\t: Run on Silicon (Default: Versim)\n";

    help_string += "\t" + c_vcd_dump_cores_arg_name + " <>" +
        "\t\t: Specify the string to pass to dump vcds to runtime (i.e \"3-1,1-1...\")\n";

    help_string += "\t" + c_compare_with_saved_golden_arg_name +
        "\t: Compare against golden file saved to disk, or save to disk if one doesnt exist\n";

    help_string += "\t" + c_perf_thresh_arg_name + " <>" +
        "\t\t: Graph performance threshold (Default: " +
        to_string(c_perf_thresh_arg_default_value) + " (no threshold))\n";

    help_string += "\t" + c_backend_mode_arg_name + " <>" +
        "\t\t\t: Mode of backend execution: " +
        to_string(c_backend_mode_compile_and_run) + " - compile and run, " +
        to_string(c_backend_mode_compile_only) + " - compile only " +
        "(Default: " + to_string(c_backend_mode_default_value) + ")\n";

    help_string += "\t" + c_determinism_test_arg_name +
        "\t\t: Run this netlist as determinism tests - only verify that it always produces the same outputs. " +
        "(Default: " + to_string(c_determinism_test_default_value) + ")\n";

    help_string += "\t" + c_determinism_minibatch_count_arg_name + " <>" +
        "\t\t: Define the number of minibatches for determinism tests " +
        "(Default: " + to_string(c_determinism_minibatch_count_default_value) + ")\n";

    help_string += "\t" + c_help_arg_name +
        "\t\t\t\t: Prints this message\n";

    return help_string;
}

struct test_args
{
    std::string netlist_path;
    std::string output_dir;
    std::string bin_path;
    std::string default_test_config_path;
    std::string golden_dir_path;
    std::string stim_bin_path;
    std::string vcd_dump_cores;
    std::string host_input_data_format = c_host_input_data_format_arg_default_value;
    int seed = c_seed_arg_default_value;
    int timeout = c_timeout_arg_default_value;
    int opt_level = c_opt_level_arg_default_value;
    int perf_thresh = c_perf_thresh_arg_default_value;
    bool run_silicon = c_run_silicon_arg_default_value;
    bool compare_with_saved_golden = c_compare_with_saved_golden_arg_default_value;
    bool golden_dir_exists = c_golden_dir_exists_arg_default_value;
    perf::PerfDesc perf_desc;
    unsigned backend_mode = c_backend_mode_default_value;
    int determinism_minibatch_count = c_determinism_minibatch_count_default_value;
    bool determinism_test = c_determinism_test_default_value;
};

struct test_parameters
{
    int minibatch_count;
    int microbatch_count;
    int microbatch_size;
    int zero_grad;
};

test_args parse_test_args(std::vector<std::string> input_args)
{
    test_args args;

    try
    {
        bool help = false;
        std::tie(help, input_args) = verif_args::has_command_option_and_remaining_args(input_args, c_help_arg_name);
        if (help)
        {
            log_info(tt::LogTest, "Usage Help:\n{}", create_help_string());
            exit(0);
        }

        std::tie(args.netlist_path, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, c_netlist_path_arg_name, c_netlist_path_arg_default_value);

        std::tie(args.output_dir, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, c_output_dir_arg_name, verif_filesystem::get_output_dir(__FILE__, input_args));

        std::tie(args.bin_path, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, c_bin_path_arg_name, "");

        std::tie(args.default_test_config_path, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, c_default_test_config_arg_name, c_default_test_config_arg_default_value);

        std::tie(args.seed, input_args) = verif_args::get_command_option_uint32_and_remaining_args(
            input_args, c_seed_arg_name, c_seed_arg_default_value);

        std::tie(args.timeout, input_args) = verif_args::get_command_option_int32_and_remaining_args(
            input_args, c_timeout_arg_name, c_timeout_arg_default_value);

        std::tie(args.opt_level, input_args) = verif_args::get_command_option_int32_and_remaining_args(
            input_args, c_opt_level_arg_name, c_opt_level_arg_default_value);

        std::tie(args.run_silicon, input_args) = verif_args::has_command_option_and_remaining_args(
            input_args, c_run_silicon_arg_name);

        std::tie(args.compare_with_saved_golden, input_args) = verif_args::has_command_option_and_remaining_args(
            input_args, c_compare_with_saved_golden_arg_name);

        std::tie(args.vcd_dump_cores, input_args) = verif_args::get_command_option_and_remaining_args(
            input_args, c_vcd_dump_cores_arg_name, "");

        std::tie(args.perf_thresh, input_args) = verif_args::get_command_option_int32_and_remaining_args(
            input_args, c_perf_thresh_arg_name, c_perf_thresh_arg_default_value);

        std::tie(args.backend_mode, input_args) = verif_args::get_command_option_uint32_and_remaining_args(
            input_args, c_backend_mode_arg_name, c_backend_mode_default_value);

        std::tie(args.determinism_minibatch_count, input_args) = verif_args::get_command_option_uint32_and_remaining_args(
            input_args, c_determinism_minibatch_count_arg_name, c_determinism_minibatch_count_default_value);

        std::tie(args.determinism_test, input_args) = verif_args::has_command_option_and_remaining_args(
            input_args, c_determinism_test_arg_name);

        args.perf_desc = perf::PerfDesc(input_args, args.netlist_path);
        verif_args::validate_remaining_args(input_args);
    }
    catch (const std::exception& e)
    {
        log_error("{}", e.what());
        log_error("Usage Help:\n{}", create_help_string());
        exit(1);
    }

    return args;
}

tt::DEVICE_MODE get_device_mode(unsigned int backend_mode)
{
    tt::DEVICE_MODE mode;
    switch (backend_mode)
    {
        case c_backend_mode_compile_and_run:
            mode = tt::DEVICE_MODE::CompileAndRun;
            break;
        case c_backend_mode_compile_only:
            mode = tt::DEVICE_MODE::CompileOnly;
            break;
        default:
            mode = tt::DEVICE_MODE::CompileAndRun;
            break;
    }
    return mode;
}

void setup_seed(test_args& args)
{
    if (args.seed == c_seed_arg_default_value)
    {
        log_info(tt::LogTest, "Generating random test seed since '--seed' is unspecified or '--seed {}' was specified.",
                 c_seed_arg_default_value);
        args.seed = tt_gen_seed();
    }

    tt_rnd_set_seed(args.seed);
}

void setup_compare_with_saved_golden(test_args& args)
{
    if (args.compare_with_saved_golden)
    {
        int ext_indx = args.netlist_path.size() - 1;

        while (ext_indx > 0 && args.netlist_path[ext_indx] != '.')
        {
            ext_indx--;
        }

        if (ext_indx == 0)
        {
            log_fatal("Invalid netlist path");
        }

        args.stim_bin_path = args.netlist_path.substr(0, ext_indx) + "_stim_" + std::to_string(args.seed);
        args.golden_dir_path = args.netlist_path.substr(0, ext_indx) + "_golden_" + std::to_string(args.seed);
        std::filesystem::path golden_dir{args.golden_dir_path.c_str()};
        args.golden_dir_exists = std::filesystem::exists(golden_dir);
    }
}

test_parameters read_test_parameters(const VerifTestConfig& test_config, const test_args& args)
{
    test_parameters test_params =
    {
        .minibatch_count  = verif::test_config::get<int>(test_config.test_args, "minibatch_count", 1),
        .microbatch_count = verif::test_config::get<int>(test_config.test_args, "microbatch_count", 1),
        .microbatch_size  = verif::test_config::get<int>(test_config.test_args, "microbatch_size", 1),
        .zero_grad        = verif::test_config::get<int>(test_config.test_args, "zero_grad", 1)
    };

    if (args.determinism_test)
    {
        test_params.minibatch_count = args.determinism_minibatch_count;
    }

    log_info(tt::LogTest, "minibatch_count  = {}", test_params.minibatch_count);
    log_info(tt::LogTest, "microbatch_count = {}", test_params.microbatch_count);
    log_info(tt::LogTest, "microbatch_size  = {}", test_params.microbatch_size);
    log_info(tt::LogTest, "zero_grad        = {}", test_params.zero_grad);

    return test_params;
}

std::shared_ptr<tt_backend> create_target_backend(const test_args& args, tt::ARCH backend_arch)
{
    tt_backend_config target_config =
    {
        .type = args.run_silicon ? tt::DEVICE::Silicon : tt::DEVICE::Versim,
        .arch = backend_arch,
        .mode = get_device_mode(args.backend_mode),
        .optimization_level = args.opt_level,
        .output_dir = args.output_dir,
    };

    std::shared_ptr<tt_backend> target_backend = tt_backend::create(args.netlist_path, target_config);

    // Performance counter setup
    std::shared_ptr<tt_runtime> target_runtime = std::dynamic_pointer_cast<tt_runtime>(target_backend);
    target_runtime->config.perf_desc = args.perf_desc;

    if (args.vcd_dump_cores != "")
    {
        target_runtime->config.device_params.vcd_dump_cores =
            verif_args::get_vcd_dump_cores_from_arg_string(args.vcd_dump_cores);
    }

    log_assert(target_backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Target Backend to get initialized");

    return target_backend;
}

std::shared_ptr<tt_backend> create_golden_backend(const test_args& args, tt::ARCH backend_arch)
{
    tt_backend_config golden_config =
    {
        .type = tt::DEVICE::Golden,
        .arch = backend_arch,
        .mode = get_device_mode(args.backend_mode),
        .optimization_level = args.opt_level,
        .output_dir=args.output_dir
    };

    std::shared_ptr<tt_backend> golden_backend = tt_backend::create(args.netlist_path, golden_config);

    log_assert(golden_backend->initialize() == tt::DEVICE_STATUS_CODE::Success, "Expected Golden Backend to get initialized");

    return golden_backend;
}

void read_queue_names(const VerifTestConfig& test_config,
                      const netlist_workload_data& workload,
                      std::map<string, tt_queue_info>& input_queue_names,
                      std::map<string, tt_queue_info>& verif_queue_names)
{
    const bool training_mode = workload.programs.size() > 1;
    for (const auto& io : workload.queues)
    {
        const tt_queue_info& q_info = io.second.my_queue_info;

        if (q_info.name.find("e2e") != std::string::npos)
        {
            continue;
        }

        if (q_info.input != "HOST")
        {
            verif_queue_names.insert({q_info.name, q_info});
        }

        // In training mode, we want to initialize RAM queues from the host, and
        // also verify their content at the end of minibatch. RAM queues are
        // typically reserved for parameters and their gradients. In this mode,
        // parameter queues don't have HOST as the input, but instead they are
        // fed by an operation from optimizer graph.
        if (q_info.input == "HOST" || (q_info.input != "HOST" && training_mode && q_info.type == IO_TYPE::RandomAccess))
        {
            input_queue_names.insert({q_info.name, q_info});
        }
    }

    log_assert(!input_queue_names.empty(), "There are no input queues in a netlist");
    log_assert(!verif_queue_names.empty(), "There are no queues for verification in a netlist");
}

void read_sparse_stimulus(VerifTestConfig& test_config, const netlist_workload_data& workload)
{
    for (auto &stimulus_config_iter : test_config.stimulus_configs)
    {
        if (stimulus_config_iter.second.type == StimulusType::Sparse ||
            stimulus_config_iter.second.type == StimulusType::SparseEncoding ||
            stimulus_config_iter.second.type == StimulusType::SparseEncodingNonzeroCounts)
        {
            sparse::get_op_info(&stimulus_config_iter.second, workload);
        }
    }
}

bool is_static_queue(const netlist_workload_data& workload, const string& q_name)
{
    // Search for queue over all programs. Assume that queue will have the same properties in each
    // program.
    // Queue is static if prologue is set to true for that queue or its read pointers are set to zero.
    for (const auto& program_iter : workload.programs)
    {
        netlist_program program = program_iter.second;

        std::vector<std::pair<string, int>> vars;
        for (const tt_instruction_info& instr_info : program.get_program_trace())
        {
            if (instr_info.opcode != INSTRUCTION_OPCODE::Var)
            {
                continue;
            }

            vars = instr_info.vars;
        }

        for (const tt_instruction_info& instr_info : program.get_program_trace())
        {
            if (instr_info.opcode != INSTRUCTION_OPCODE::Execute)
            {
                continue;
            }

            for (const tt_queue_setting_info& queue_settings_info : instr_info.queue_settings)
            {
                if (q_name != queue_settings_info.name)
                {
                    continue;
                }

                // RAM queues have only rd_ptr_global.
                int rd_ptr_local = queue_settings_info.rd_ptr_local == "" ? 0 : -1;
                int rd_ptr_global = -1;
                for (const auto &var : vars)
                {
                    if (var.first == queue_settings_info.rd_ptr_local)
                    {
                        rd_ptr_local = var.second;
                    }

                    if (var.first == queue_settings_info.rd_ptr_global)
                    {
                        rd_ptr_global = var.second;
                    }
                }

                return queue_settings_info.prolog == "true" || (rd_ptr_local == 0 && rd_ptr_global == 0);
            }
        }
    }

    return false;
}

void dump_queue_entry(const tt_tensor& tensor,
                      const test_args& args,
                      const std::string& origin,
                      const std::string& q_name,
                      const int entry_id)
{
        const std::string filename = args.output_dir + "/" + origin + "_" + q_name +
                                     "_entry_" + std::to_string(entry_id) +
                                     "_seed_" + std::to_string(args.seed) +
                                     ".txt";
        log_info(tt::LogTest, "Writing data of {} queue {}, entry {} into file {}", origin, q_name, entry_id, filename);
        tensor.dump(filename);
}

void fill_input_queue(const test_args& args,
                      const std::string& q_name,
                      const tt_queue_info& q_info,
                      const int microbatch_count,
                      const int minibatch_id,
                      const netlist_workload_data& workload,
                      const std::unordered_map<std::string, VerifStimulusConfig>& stimulus_configs,
                      const std::shared_ptr<tt_backend>& target_backend,
                      const std::shared_ptr<tt_backend>& golden_backend,
                      std::map<std::string, std::map<int, tt_tensor>>& input_queue_cache) {

    const int default_ram_ptr = 0;
    tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(q_name);
    tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);

    // It is needed to push num_entries tensors to each input queue.
    // For activation queues (prologue=false) num_entries is equal to a
    // minibatch worth of inputs (input_count * microbatch_count).
    // For static queues (prologue=true or dram pointers are set to zero)
    // capacity (entries) of the queue is 1 regardless of minibatch size,
    // and in those cases push queue.entries tensors.
    int q_capacity = q_desc.bufq_num_slots;
    bool is_static_q = is_static_queue(workload, q_name);
    int num_entries = is_static_q ? q_capacity : q_desc.input_count * microbatch_count;

    log_assert(num_entries > 0, "Number of entries must be greater than zero. For queue {}, num_entries is {}",
                q_name, num_entries);

    log_assert(q_capacity >= num_entries, "Cannot push {} entries to the queue {} with capacity of {} entries.",
                num_entries, q_name, q_capacity);

    // Calculate num_pushes entries to push to the queue.
    // For activation queues always push a minibatch worth of inputs (num_entries).
    // For static queues push entries only before processing the first minibatch.
    // TODO: This is okay for inference mode only, in training mode we will have to
    // push updated weights/biases for each minibatch.
    int num_pushes = (is_static_q && minibatch_id > 0) ? 0 : num_entries;

    for (int push_id = 0; push_id < num_pushes; ++push_id)
    {
        tt_tensor io_tensor(md);
        if (args.golden_dir_exists)
        {
            std::string stim_yaml_path = args.stim_bin_path + "/";
            verif::read_tensor_from_binary(stim_yaml_path, q_info,
                                            num_pushes * minibatch_id + push_id,
                                            io_tensor, true);
        }
        else if (args.bin_path != "" &&
                    tt::data_binary::does_file_exists(args.bin_path + "/", q_name, num_pushes * minibatch_id + push_id))
        {
            log_info(tt::LogTest, "Loading binary for queue {} from bin-path {}", q_name, args.bin_path + "/");

            verif::read_tensor_from_binary(args.bin_path + "/", q_info,
                                            num_pushes * minibatch_id + push_id,
                                            io_tensor, false);
        }
        else if (args.determinism_test)
        {
            if (minibatch_id == 0)
            {
                generate_tensor(get_config(q_name, stimulus_configs), io_tensor);
                input_queue_cache[q_name][push_id] = io_tensor;
            }
            else
            {
                io_tensor = input_queue_cache[q_name][push_id];
            }
        }
        else
        {
            generate_tensor(get_config(q_name, stimulus_configs), io_tensor);
            if (args.compare_with_saved_golden && !args.golden_dir_exists)
            {
                std::string stim_yaml_path = args.stim_bin_path + "/";
                std::filesystem::create_directories(args.stim_bin_path);
                verif::write_tensor_to_binary(stim_yaml_path, q_name, num_pushes * minibatch_id + push_id, io_tensor);
            }
        }

        log_info(tt::LogTest, "Pushing entry {}/{} into queue {} with capacity {}",
                    push_id + 1, num_pushes, q_name, q_capacity);

        // Uncomment for debug purposes.
        // dump_queue_entry(io_tensor, args, "input", q_name, push_id + 1);

        verif::push_tensor(*target_backend, q_name, &io_tensor, default_ram_ptr);

        if(!args.golden_dir_exists && !args.determinism_test)
        {
            verif::push_tensor(*golden_backend, q_name, &io_tensor, default_ram_ptr);
        }
    }
}

void fill_input_queues(const test_args& args,
                       const std::map<string, tt_queue_info>& input_queue_names,
                       const int microbatch_count,
                       const int minibatch_id,
                       const netlist_workload_data& workload,
                       const std::unordered_map<std::string, VerifStimulusConfig>& stimulus_configs,
                       const std::shared_ptr<tt_backend>& target_backend,
                       const std::shared_ptr<tt_backend>& golden_backend,
                       std::map<std::string, std::map<int, tt_tensor>>& input_queue_cache)
{
    std::vector<std::pair<string, int>> queue_name_and_seed;
    queue_name_and_seed.reserve(input_queue_names.size());
    int thread_seed = args.seed + 1;
    for (const auto& [q_name, q_info] : input_queue_names) {
        queue_name_and_seed.emplace_back(std::make_pair(q_name, thread_seed++));
    }

    tt::parallel_for(queue_name_and_seed.begin(), queue_name_and_seed.end(), [&](const std::pair<string, int>& pair)
    {
        const tt_queue_info& q_info = input_queue_names.at(pair.first);

        log_info(tt::LogTest, "Setting seed for input queue {} thread to: {}", pair.first, pair.second);
        tt_rnd_set_seed(pair.second);
        fill_input_queue(args, pair.first, q_info, microbatch_count, minibatch_id,
                         workload, stimulus_configs, target_backend, golden_backend, input_queue_cache);
    }, tt::cpuset::get_allowed_num_threads());
}

void run_programs(const test_args& args,
                  const test_parameters& test_params,
                  const netlist_workload_data& workload,
                  const std::shared_ptr<tt_backend>& target_backend,
                  const std::shared_ptr<tt_backend>& golden_backend)
{
    std::map <std::string, std::string> program_parameters =
    {
        {"$p_microbatch_count", std::to_string(test_params.microbatch_count)},
        {"$p_microbatch_size", std::to_string(test_params.microbatch_size)},
        {"$p_zero_grad", std::to_string(test_params.zero_grad)}
    };

    // In case of determinism tests we don't need to run golden.
    const bool should_run_golden_backend = (!args.golden_dir_exists && !args.determinism_test);

    for (const std::string& program_name : workload.program_order)
    {
        log_info(tt::LogTest, "Running program {}", program_name);

        log_assert(target_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on target backend");

        if(should_run_golden_backend)
        {
            log_assert(golden_backend->run_program(program_name, program_parameters) == tt::DEVICE_STATUS_CODE::Success, "Expected programs to execute successfuly on golden backend");
        }
    }

    // When accessing RAM outputs, there are no pointers for HOST to poll for occupancy
    // since there is no way know when data has been committed to DRAM, let's insert explicit WFI
    log_assert(target_backend->wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI to execute successfuly on target backend");

    if(should_run_golden_backend)
    {
        log_assert(golden_backend->wait_for_idle() == tt::DEVICE_STATUS_CODE::Success, "Expected WFI execute successfuly on golden backend");
    }
}

void verify_queue_entry(const test_args& args,
                        const std::string& q_name,
                        const tt_queue_info& q_info,
                        const tt_tensor_metadata& md,
                        const bool is_static_q,
                        int entry_id,
                        int num_entries,
                        int minibatch_id,
                        const std::shared_ptr<tt_backend>& target_backend,
                        const std::shared_ptr<tt_backend>& golden_backend,
                        const std::unordered_map<std::string, VerifComparisonConfig>& comparison_configs,
                        std::map<string, std::map<int, std::shared_ptr<tt_tensor>>>& output_queue_cache)
{
    const int q_ptr = is_static_q ? 0 : -1;
    std::shared_ptr<tt_tensor> observed = std::make_shared<tt_tensor>(md);
    verif::get_and_pop_tensor(*target_backend, q_name, observed.get(), q_ptr);

    std::shared_ptr<tt_tensor> expected = std::make_shared<tt_tensor>(md);

    // Collect observed tensor and potentially save it in output queue cache for determinism tests.
    if (args.determinism_test)
    {
        if (minibatch_id == 0)
        {
            output_queue_cache[q_name][entry_id] = observed;
        }
    }
    else if (args.golden_dir_exists)
    {
        std::string golden_yaml_path = args.golden_dir_path + "/";
        verif::read_tensor_from_binary(golden_yaml_path, q_info,
                                       num_entries * minibatch_id + entry_id,
                                       *expected, true);
    }
    else
    {
        verif::get_and_pop_tensor(*golden_backend, q_name, expected.get(), q_ptr);
        if (args.compare_with_saved_golden && !args.golden_dir_exists)
        {
            std::string golden_yaml_path = args.golden_dir_path + "/";
            std::filesystem::create_directories(args.golden_dir_path);
            verif::write_tensor_to_binary(golden_yaml_path, q_name, num_entries * minibatch_id + entry_id, *expected);
        }
    }

    // Uncomment for debug purposes.
    // dump_queue_entry(*observed.get(), args, "device", q_name, entry_id + 1);
    // dump_queue_entry(*expected.get(), args, "golden", q_name, entry_id + 1);

    log_info(tt::LogTest, "Checking entry {}/{} for output queue {}", entry_id + 1, num_entries, q_name);

    // For determinism tests, we want to make sure that the netlist run always produces the same output.
    // Otherwise, simply compare target backend output with golden output.
    if (args.determinism_test)
    {
        if (minibatch_id > 0)
        {
            const auto determinism_test_config = verif::comparison::get_config("determinism_test", comparison_configs);
            if(not compare_tensors(*output_queue_cache[q_name][entry_id], *observed, determinism_test_config))
            {
                pass = false;
                log_error("Non Deterministic Output: Entry idx={} for output={} in run={} Mismatched with initial output",
                          entry_id, q_name, minibatch_id);
            }
        }
    }
    else if (not compare_tensors(*expected, *observed, verif::comparison::get_config(q_name, comparison_configs)))
    {
        pass = false;
        log_error("Entry {}/{} for output queue {} is mismatched", entry_id + 1, num_entries, q_name);
    }
}

void verify_queues(const test_args& args,
                   const std::map<string, tt_queue_info>& verif_queue_names,
                   const netlist_workload_data& workload,
                   int microbatch_count,
                   int minibatch_id,
                   const std::shared_ptr<tt_backend>& target_backend,
                   const std::shared_ptr<tt_backend>& golden_backend,
                   const std::unordered_map<std::string, VerifComparisonConfig>& comparison_configs,
                   std::map<string, std::map<int, std::shared_ptr<tt_tensor>>>& output_queue_cache)
{
    for (const auto& it : verif_queue_names)
    {
        const string& q_name = it.first;
        const tt_queue_info& q_info = it.second;
        tt_dram_io_desc q_desc = target_backend->get_queue_descriptor(q_name);
        tt_tensor_metadata md = get_tensor_metadata_for_queue(q_desc);

        const bool is_static_q = is_static_queue(workload, q_name);
        int num_entries = is_static_q ? 1 : q_desc.input_count * microbatch_count;

        log_info(tt::LogTest, "Validating {} entries of output queue {}", num_entries, q_name);

        for (int entry_id = 0; entry_id < num_entries; ++entry_id)
        {
            verify_queue_entry(args, q_name, q_info, md, is_static_q, entry_id, num_entries, minibatch_id,
                               target_backend, golden_backend, comparison_configs, output_queue_cache);
        }
    }
}

void test_on_minibatches(const test_args& args,
                         const VerifTestConfig& test_config,
                         const test_parameters& test_params,
                         const netlist_workload_data& workload,
                         const std::map<string, tt_queue_info>& input_queue_names,
                         const std::map<string, tt_queue_info>& verif_queue_names,
                         const std::shared_ptr<tt_backend>& target_backend,
                         const std::shared_ptr<tt_backend>& golden_backend)
{
    std::map<std::string, std::map<int, tt_tensor>> input_queue_cache;
    std::map<string, std::map<int, std::shared_ptr<tt_tensor>>> output_queue_cache;

    for (int minibatch_id = 0; minibatch_id < test_params.minibatch_count; ++minibatch_id)
    {
        log_info(tt::LogTest, "Started minibatch: {}/{} ", minibatch_id + 1, test_params.minibatch_count);

        fill_input_queues(args, input_queue_names, test_params.microbatch_count, minibatch_id, workload,
                          test_config.stimulus_configs, target_backend, golden_backend, input_queue_cache);

        run_programs(args, test_params, workload, target_backend, golden_backend);

        verify_queues(args, verif_queue_names, workload, test_params.microbatch_count, minibatch_id,
                      target_backend, golden_backend, test_config.comparison_configs, output_queue_cache);
    }

    log_info(tt::LogTest, "Completed all minibatches");
}

void teardown_backends(const std::shared_ptr<tt_backend>& target_backend,
                       const std::shared_ptr<tt_backend>& golden_backend)
{
    log_assert(target_backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected target device to close successfuly");
    log_assert(golden_backend->finish() == tt::DEVICE_STATUS_CODE::Success, "Expected golden device to close successfuly");

    log_info(tt::LogTest, "Backends teardown finished");
}

void dump_performance_summary(const std::shared_ptr<tt_backend>& target_backend, int perf_thresh)
{
    std::shared_ptr<tt_runtime> target_runtime = std::dynamic_pointer_cast<tt_runtime>(target_backend);
    std::vector<tt::EpochPerfInfo> perf_info = target_runtime->get_perf_info();

    for (const tt::EpochPerfInfo& epoch_perf_info : perf_info)
    {
        log_info(tt::LogTest, "Program = {}, Graph = {}, Tensors/s = {:.2f}, Cycles/tensor = {:.2f} ",
                 epoch_perf_info.program_name, epoch_perf_info.graph_name,
                 epoch_perf_info.num_inputs_per_second, epoch_perf_info.num_cycles_per_input);

        if (epoch_perf_info.num_inputs_per_second < perf_thresh)
        {
            pass = false;
            log_error("Perf check FAILED, actual perf {:.2f} is less than perf threshold {} Tensors/s",
                      epoch_perf_info.num_inputs_per_second, perf_thresh);
        }
    }
}

void run_test(test_args args)
{
    log_info(tt::LogTest, "Graph test started");

    setup_seed(args);

    netlist_workload_data workload(args.netlist_path);
    log_assert(workload.programs.size() >= 1, "Must have a program in netlist!");

    std::shared_ptr<tt_backend> target_backend = create_target_backend(args, workload.device_info.arch);
    std::shared_ptr<tt_backend> golden_backend = create_golden_backend(args, workload.device_info.arch);

    if (args.backend_mode == c_backend_mode_compile_and_run)
    {
        setup_compare_with_saved_golden(args);

        VerifTestConfig test_config = verif::test_config::read_from_yaml_file(args.netlist_path,
                                                                              args.default_test_config_path);
        test_parameters test_params = read_test_parameters(test_config, args);

        std::map<string, tt_queue_info> input_queue_names;
        std::map<string, tt_queue_info> verif_queue_names;

        read_queue_names(test_config, workload, input_queue_names, verif_queue_names);
        read_sparse_stimulus(test_config, workload);

        test_on_minibatches(args, test_config, test_params, workload, input_queue_names,
                            verif_queue_names, target_backend, golden_backend);
    }

    teardown_backends(target_backend, golden_backend);

    if (args.backend_mode == c_backend_mode_compile_and_run)
    {
        // Perf info is available only after the runtime->finish() call.
        dump_performance_summary(target_backend, args.perf_thresh);
    }

    done = true;
}

int main(int argc, char** argv) {
    // Use 1MB DMA buffer size for reads. For writes DMA is still disabled.
    // This is providing 15x speed up on poping output queues from dram.
    // This is safe to use in test_graph since we are poping outputs from a single thread.
    setenv("TT_PCI_DMA_BUF_SIZE", "1048576", false);
    tt::assert::register_segfault_handler();

    test_args args = parse_test_args(std::vector<std::string>(argv, argv + argc));

    // Run test in separate thread, and make main thread wait for its completion or timeout.
    std::thread test(run_test, args);

    double elapsed_seconds = 0;
    while (!done and (elapsed_seconds < args.timeout))
    {
        std::this_thread::sleep_for(100ms);
        elapsed_seconds += 0.1;
    }
    log_info(tt::LogTest, "Elapsed Time: {}s", elapsed_seconds);

    if (done)
    {
        test.join();
    }
    else
    {
        log_error("TIMEOUT ERROR");
        std::terminate();
        exit(1);
    }

    if (pass)
    {
        if (args.backend_mode == c_backend_mode_compile_and_run)
        {
            if (args.determinism_test)
            {
                log_info(tt::LogTest, "Determinism Test Passed");
            }
            else
            {
                log_info(tt::LogTest, "Test Passed");
            }
        }
        else
        {
            log_info(tt::LogTest, "Test Compilation Passed");
        }
    }
    else
    {
        log_fatal("Test Failed");
    }
}

