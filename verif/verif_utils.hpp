// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "netlist/netlist.hpp"
#include "model/tensor.hpp"
#include "netlist/tt_backend.hpp"
#include "model/model.hpp"
#include "hlks/inc/hlk_api.h"
#include "common/base.hpp"
#include "common/data_binary_lib.hpp"
#include "verif_args.hpp"
#include "common/size_lib.hpp"
#include "common/env_lib.hpp"

namespace verif {
template<class Key, class Value>
const std::map<Value, Key> reverse(const std::map<Key, Value> &input_map) {
    std::map<Value, Key> output_map;
    for (std::pair<Key, Value> key_value_pair : input_map) {
        output_map[key_value_pair.second] = key_value_pair.first;
    }
    return output_map;
}

const std::map<DstMode, std::string> DST_MODE_TO_STRING = {
        {DstMode::Full, "Full"},
        {DstMode::Half, "Half"},
        {DstMode::Tile, "Tile"},
};
const std::map<std::string, DstMode> STRING_TO_DST_MODE = reverse(DST_MODE_TO_STRING);

const std::map<SfpuOp, std::string> SFPU_OP_TO_STRING = {
        {SfpuOp::Exp, "Exp"},
        {SfpuOp::Log, "Log"},
        {SfpuOp::Sqrt, "Sqrt"},
        {SfpuOp::Sigmoid, "Sigmoid"},
        {SfpuOp::Gelu, "Gelu"},
        {SfpuOp::GeluDerivative, "GeluDerivative"},
        {SfpuOp::Reciprocal, "Reciprocal"},
        {SfpuOp::Tanh, "Tanh"},
};
const std::map<std::string, SfpuOp> STRING_TO_SFPU_OP = reverse(SFPU_OP_TO_STRING);

const std::map<BinaryOp, std::string> BINARY_OP_TO_STRING = {
        {BinaryOp::Add, "Add"},
        {BinaryOp::Subtract, "Subtract"},
        {BinaryOp::Multiply, "Multiply"},
};
const std::map<std::string, BinaryOp> STRING_TO_BINARY_OP = reverse(BINARY_OP_TO_STRING);

const std::map<Dim, std::string> DIM_TO_STRING = {
        {Dim::None, "None"},
        {Dim::R, "R"},
        {Dim::C, "C"},
        {Dim::Z, "Z"},
        {Dim::RC, "RC"},
        {Dim::ZR, "ZR"},
};
const std::map<std::string, Dim> STRING_TO_DIM = reverse(DIM_TO_STRING);

const std::map<ReduceFunc, std::string> REDUCE_FUNC_TO_STRING = {
        {ReduceFunc::Sum, "Sum"},
        {ReduceFunc::Avg, "Avg"},
        {ReduceFunc::Max, "Max"},
};

const std::map<std::string, ReduceFunc> STRING_TO_REDUCE_FUNC = reverse(REDUCE_FUNC_TO_STRING);

const std::map<DataFormat, std::string> DATA_FORMAT_TO_STRING = {
        {DataFormat::Float32, "Float32"},
        {DataFormat::Float16_b, "Float16_b"},
        {DataFormat::Float16, "Float16"},
        {DataFormat::Bfp8_b, "Bfp8_b"},
        {DataFormat::Bfp8, "Bfp8"},
        {DataFormat::Bfp4_b, "Bfp4_b"},
        {DataFormat::Bfp4, "Bfp4"},
        {DataFormat::Bfp2_b, "Bfp2_b"},
        {DataFormat::Bfp2, "Bfp2"},
        {DataFormat::RawUInt32, "RawUInt32"},
        {DataFormat::RawUInt16, "RawUInt16"},
        {DataFormat::RawUInt8, "RawUInt8"},
        {DataFormat::Int8, "Int8"},
        {DataFormat::Invalid, "Invalid"},
};
const std::map<std::string, DataFormat> STRING_TO_DATA_FORMAT = reverse(DATA_FORMAT_TO_STRING);

const std::map<TargetDevice, std::string> TARGET_DEVICE_TO_STRING = {
        {TargetDevice::Model, "Model"},
        {TargetDevice::Versim, "Versim"},
        {TargetDevice::Silicon, "Silicon"},
        {TargetDevice::Golden, "Golden"},
};

const std::map<Action, std::string> ACTION_TO_STRING = {
    {Action::None, "None"},
    {Action::Slice, "Slice"},
    {Action::Stack, "Stack"},
};
const std::map<std::string, Action> STRING_TO_ACTION = reverse(ACTION_TO_STRING);

template <class T>
constexpr std::false_type always_false{};

template <class T>
T parse(std::string const &s) {
    if constexpr (std::is_same_v<T, std::uint32_t>) {
        return std::stoul(s, 0, 0);
    } else if constexpr (std::is_same_v<T, int>) {
        return std::stoi(s, 0, 0);
    } else if constexpr (std::is_same_v<T, std::uint64_t>) {
        return std::stoull(s, 0, 0);
    } else if constexpr (std::is_same_v<T, bool>) {
        return static_cast<bool>(std::stoi(s, 0, 0));
    } else if constexpr (std::is_same_v<T, std::string>) {
        return s;
    } else {
        static_assert(verif_args::always_false<T>, "No specialization for type");
    }
}

typedef struct buffer_info {
    string queue_name;
    int channel;
    int start_addr;
    int end_addr;
    int device_id;
} buffer_info;

enum ExternalBinaryMode {
    Constants = 0,
    AllInputs = 1,
    AllOutputs = 2,
};

void shift_addresses_by_offset(std::string input_filepath, std::string output_filepath, int offset);
std::unordered_map<int, std::vector<uint32_t>> parse_harvesting_masks(const std::string& harv_mask_str);
set<ExternalBinaryMode> cmdline_to_external_binary_mode(string all_modes);

void read_tensor_from_binary(
    string yaml_root, tt_queue_info queue_info, int entry_index, tt_tensor& tensor, const bool& init_rams, const uint &replicate_entries = 0);

void write_tensor_to_binary(string yaml_root, const std::string& q_name, int entry_index, tt_tensor& tensor);

// This verif function is a roundabout way, we get take tilized input and create raw-untilized tensors and get
// descriptor from backend before calling the push similar to how frontend would do this. This is to help catch FE
// integration issues
void push_tensor(tt_backend& backend, const string& q_name, tt_tensor* tensor, const int ptr = -1, const int timeout = 0);
void push_batched_tensor(tt_backend& backend, const string& q_name, tt_tensor* tensor, const bool is_activation, const int ptr = -1, const int timeout = 0, tt::tt_dram_io_desc q_desc_override = tt::tt_dram_io_desc(), bool untilize_as_flat_as_tt_tensor = false);
void push_tensors(tt_backend& backend, const string& q_name, std::vector<tt_tensor>& tensors, const int ptr = -1, const int timeout = 0);
void push_pytorch_tensor(tt_backend& backend, const string& q_name, tt_PytorchTensorDesc tensor, const bool is_activation, const int ptr = -1, const int timeout = 0, tt::tt_dram_io_desc q_desc_override = tt::tt_dram_io_desc());
// This verif function is a roundabout way, we get take tilized input and create raw-untilized tensors and get
// descriptor from backend before calling the push similar to how frontend would do this. This is to help catch FE
// integration issues
void get_and_pop_tensor(tt_backend& backend, const string& q_name, tt_tensor* tensor, const int ptr = -1, const int timeout = 0);
void get_and_pop_tensors(
    tt_backend& backend, const string& q_name, std::vector<tt_tensor>& tensors, const int ptr = -1, const int timeout = 0, bool pop_tensor = true);
void get_and_pop_flat_tensors(tt_backend& backend, const string& q_name, std::vector<float>& data_vector, const int ptr = -1, const int timeout = 0, std::uint32_t hstack_factor = 1, std::uint32_t vstack_factor = 1, bool stack_row_major = true);

void get_input_output_queue_names(const string &netlist_path, vector<string> &input_queues, vector<string> &output_queues);
}  // namespace verif


namespace verif_filesystem {
namespace utils {
inline bool replace(std::string &string, const std::string &from, const std::string &to) {
    size_t start_pos = string.find(from);
    if (start_pos == std::string::npos) {
        return false;
    }
    string.replace(start_pos, from.length(), to);
    return true;
}

inline void replace_all(std::string &string, const std::string &from, const std::string &to) {
    while(replace(string, from, to));
}

inline std::string generate_random_string(const int size = 32) {
    std::string random_string;
    random_string.reserve(size);

    static const char alpha_numeric_characters[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    for (int i = 0; i < size; ++i) {
        random_string += alpha_numeric_characters[rand() % (sizeof(alpha_numeric_characters) - 1)];
    }
    return random_string;
}
}

inline std::string get_output_dir(const std::string &file_name, const std::vector<std::string> &args) {
    std::string stem = std::experimental::filesystem::path(file_name).stem().string();

    // Args that should NOT be used for uniquifying test build dir
    std::unordered_set<std::string> ignored_args = {
        "--skip-compile-backend",
        "--skip-compile-hlk-ops",
        "--skip-run-epoch",
        "--disable-unique-compiles",
        "--run-only",
    };
    // If the arg also has a value associated with it, skip the value as well as the arg
    std::unordered_set<std::string> ignored_args_with_values = {
        "--perf-op-mode",
        "--label",
        "--triplet-mode",
        "--reset-triplet-mode",
        "--perf-output-dir",
        "--insert-kernel-delay",
        "--reset-kernel-delay",
    };

    bool skip_value = false;
    bool found_label = false;
    string arg_combined = "";
    string override_output_dir = "";

    // Optional label per test that is preserved, ie. --label jan21_experiment4
    string label = "";

    for (int arg_index = 1; arg_index < args.size(); arg_index++) {

        auto arg = args.at(arg_index);
        if (skip_value) {
            skip_value = false;
            if (found_label){
                label = "_" + arg;
                found_label = false;
            }
            continue;
        }
        if (ignored_args.find(arg) != ignored_args.end()) {
            continue;
        }
        if (ignored_args_with_values.find(arg) != ignored_args_with_values.end()) {
            skip_value = true;
            found_label = (arg == "--label");
            continue;
        }
        if (arg.compare("--override-output-dir") == 0)
        {
            override_output_dir = args.at(arg_index + 1);
            break;
        }
        utils::replace_all(arg, "--", "");
        utils::replace_all(arg, "-", "_");

        arg_combined.append(arg);
    }

    std::size_t arg_combined_hash = std::hash<std::string>{}(arg_combined);

    string buda_out = parse_env<std::string>("BUDA_OUT", "./");;
    if (buda_out.back() != '/') {
        buda_out += "/";
    }

    std::string output_dir;
    if (override_output_dir.compare("") != 0)
    {
        output_dir = buda_out + "tt_build/" + override_output_dir;
    }
    else
    {
        output_dir = buda_out + "tt_build/" + stem + label + "_" + std::to_string(arg_combined_hash);
    }

    fs::path output_dir_path(output_dir);

    bool skip_compile_hlk_ops = verif_args::has_command_option(args, "--skip-compile-hlk-ops");
    bool skip_compile_backend = verif_args::has_command_option(args, "--skip-compile-backend");

    bool run_only = verif_args::has_command_option(args, "--run-only");

    if (skip_compile_backend) {
        log_assert(fs::exists(output_dir_path), "output dir does not exist");
        log_info(tt::LogVerif, "skip_compile_backend flag is set. Not deleting the existing output directory: {}", output_dir_path);
    }

    if (skip_compile_hlk_ops) {
        log_assert(fs::exists(output_dir_path), "output dir does not exist");
        log_info(tt::LogVerif, "skip_compile_hlk_ops flag is set. Not deleting the existing output directory: {}", output_dir_path);
    }
    if (run_only) {
        log_assert(fs::exists(output_dir_path), "In run_only mode, output directory {} must already exist.", output_dir_path);
        log_info(tt::LogVerif, "run_only flag is set. Not deleting the existing output directory: {}", output_dir_path);
    }
    if (fs::exists(output_dir_path) && (!skip_compile_hlk_ops && !skip_compile_backend && !run_only)) {
        log_info(tt::LogVerif, "Deleting existing output_dir: {}",  output_dir_path);
        fs::remove_all(output_dir_path);
    }
    if (!fs::exists(output_dir_path)) {
        fs::create_directories(output_dir_path);
    }

    // We need to pass the output dir to gstate so that it is available to various components of the compiler and env,
    // so they have a location to dump output files to. That requires every test to be changed... In the meantime,
    // we'll set a global variable and have gstate fall back to this if one isn't set explicitly
    g_output_dir = output_dir;

    // Some post-processing scripts will use this
    log_info(tt::LogVerif, "using output_dir: {}", output_dir);;

    return output_dir;
}

inline std::string randomize_output_dir(const std::string &output_dir) {
    std::string random_output_dir = output_dir;
    random_output_dir.append("_");
    random_output_dir.append(utils::generate_random_string());
    return random_output_dir;
}

} 

// FIXME: Probably move this to proper place later
ostream& operator<<(ostream& os, const tt_tensor_metadata& t);
