// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iostream>
#include <map>
#include <vector>
#include <array>
#include <algorithm>
#include <experimental/filesystem> // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>
#include <chrono>
#include <thread>
#include <functional>
#include "model.hpp"
#include "common/base.hpp"

#include "model/tt_rnd_util.hpp"
#include "utils/logger.hpp"
#include "epoch_q.h"

#include "common/model/tt_core.hpp"
#include "device/cpuset_lib.hpp"

using tt::TargetDevice;


using std::cout;
using std::endl;

namespace fs = std::experimental::filesystem;

using tt::DataFormat;
using tt::TargetDevice;
struct TestParams;

template<class Key, class Value>
const std::map<Value, Key> reverse_map(const std::map<Key, Value> &input_map) {
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
const std::map<std::string, DstMode> STRING_TO_DST_MODE = reverse_map(DST_MODE_TO_STRING);

const std::map<SfpuOp, std::string> SFPU_OP_TO_STRING = {
        {SfpuOp::Exp, "Exp"},
        {SfpuOp::Log, "Log"},
        {SfpuOp::Sqrt, "Sqrt"},
        {SfpuOp::Sigmoid, "Sigmoid"},
        {SfpuOp::Gelu, "Gelu"},
        {SfpuOp::GeluDerivative, "GeluDerivative"},
        {SfpuOp::Reciprocal, "Reciprocal"},
};
const std::map<std::string, SfpuOp> STRING_TO_SFPU_OP = reverse_map(SFPU_OP_TO_STRING);

const std::map<BinaryOp, std::string> BINARY_OP_TO_STRING = {
        {BinaryOp::Add, "Add"},
        {BinaryOp::Subtract, "Subtract"},
        {BinaryOp::Multiply, "Multiply"},
};
const std::map<std::string, BinaryOp> STRING_TO_BINARY_OP = reverse_map(BINARY_OP_TO_STRING);

const std::map<Dim, std::string> DIM_TO_STRING = {
        {Dim::None, "None"},
        {Dim::R, "R"},
        {Dim::C, "C"},
        {Dim::Z, "Z"},
        {Dim::RC, "RC"},
        {Dim::ZR, "ZR"},
};
const std::map<std::string, Dim> STRING_TO_DIM = reverse_map(DIM_TO_STRING);

const std::map<ReduceFunc, std::string> REDUCE_FUNC_TO_STRING = {
        {ReduceFunc::Sum, "Sum"},
        {ReduceFunc::Avg, "Avg"},
        {ReduceFunc::Max, "Max"},
};

const std::map<std::string, ReduceFunc> STRING_TO_REDUCE_FUNC = reverse_map(REDUCE_FUNC_TO_STRING);

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
        {DataFormat::Invalid, "Invalid"},
};
const std::map<std::string, DataFormat> STRING_TO_DATA_FORMAT = reverse_map(DATA_FORMAT_TO_STRING);

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
const std::map<std::string, Action> STRING_TO_ACTION = reverse_map(ACTION_TO_STRING);



inline vector<float> process_raw_untilized(const std::vector<uint32_t> &mem_vector, DataFormat format) {
    // converts an untilized vector to a format that can be consumed by tt_tile::tilize
    vector<float> rv;
    float wf;
    uint32_t wd;
    // FIXME:  init rv, reserve space
    for (uint32_t datum : mem_vector) {
        switch (format) {
            case DataFormat::Float16:
                log_fatal("Not yet implemented!");
                break;
            case DataFormat::Float16_b:
                //first datum, only look at the bottom 16 bits
                for (int i =0; i < 2; i++) {
                  wd = (datum & 0xffff) << 16;
                //   wf = *reinterpret_cast<float*>(&wd);
                  wf = *((float*)(&wd));
                  rv.push_back(wf);
                  datum >>= 16;
                }
                break;
            case DataFormat::Float32:
              wf = *reinterpret_cast<float*>(&datum);
              rv.push_back(wf);
              break;
            case DataFormat::Tf32:
              wf = *reinterpret_cast<float*>(&datum);
              rv.push_back(wf);
              break;

            default:
                log_fatal("Not yet implemented!");
                break;
        }
    }
    return rv;
}



const std::map<std::string, TargetDevice> STRING_TO_TARGET_DEVICE = reverse_map(TARGET_DEVICE_TO_STRING);

namespace tt {

tt_device *create_device(
    TargetDevice target_device, const tt_device_params &device_params, std::string device_descriptor_file_path);

template <typename T>
T round_up_div(T num, T div) {
    return (num + div - 1) / div;
}

template <typename T>
T align_up(T v, T alignment) {
    log_assert(alignment > 0, "Expected alignment greater than 0.");
    v -= 1;
    return v - (v % alignment) + alignment;
}

inline int select_smallest_divisible_factor(int number, int min_factor) {
    int factor = 0;  // search for legal block_dim

    for (int candidate_factor = min_factor; candidate_factor <= number; candidate_factor++) {
        if (number % candidate_factor == 0) {
            factor = candidate_factor;
            break;
        }
    }
    log_assert(factor > 0, "Expected factor > 0.");
    return factor;
}

inline int select_greatest_divisible_factor(int number, int max_factor) {
    int factor = 0; // search for legal block_dim

    for (int candidate_factor = max_factor; candidate_factor >= 1; candidate_factor--) {
        if (number % candidate_factor == 0) {
            factor = candidate_factor;
            break;
        }
    }
    log_assert(factor > 0, "Expected factor > 0.");
    return factor;
}

template <typename T>
int get_index_of_found_element_or_assert(vector<T>& container, T queried_element) {
    typename vector<T>::iterator it = find(container.begin(), container.end(), queried_element);
    log_assert(it != container.end() ,  "get_index_of_found_element_or_assert(..) could not find queried elemnent.");
    int found_op_args_index = distance(container.begin(), it);
    return found_op_args_index;
}

template <class T> constexpr std::false_type always_false{};

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
        static_assert(always_false<T>, "No specialization for type");
    }
}

template <typename T>
T env_as(char const *env_var, std::optional<T> const &default_value = std::nullopt) {
    char const *env_value = std::getenv(env_var);
    if (env_value) {
        return parse<T>(std::string(env_value));
    } else if (default_value) {
        return default_value.value();
    }
    return T{};
}

namespace args {

std::string strip(std::string const &s);

inline std::string get_command_option(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<std::string> &default_value = std::nullopt) {
    std::vector<std::string>::const_iterator option_pointer = std::find(std::begin(args), std::end(args), option);
    if (option_pointer != std::end(args) and option_pointer++ != std::end(args)) {
        return *option_pointer;
    }
    if (not default_value.has_value()) {
        throw std::runtime_error("Option not found!");
    }
    return default_value.value();
}

inline std::uint32_t get_command_option_uint32(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<std::uint32_t> &default_value = std::nullopt) {
    std::string param;
    if (default_value.has_value()) {
        param = get_command_option(args, option, std::to_string(default_value.value()));
    } else {
        param = get_command_option(args, option);
    }
    return std::stoul(param, 0, 0);
}

inline std::int32_t get_command_option_int32(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<std::uint32_t> &default_value = std::nullopt) {
    std::string param;
    if (default_value.has_value()) {
        param = get_command_option(args, option, std::to_string(default_value.value()));
    } else {
        param = get_command_option(args, option);
    }
    return std::stoi(param, 0, 0);
}

inline double get_command_option_double(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<double> &default_value = std::nullopt) {
    std::string param;
    if (default_value.has_value()) {
        param = get_command_option(args, option, std::to_string(default_value.value()));
    } else {
        param = get_command_option(args, option);
    }
    return std::stod(param, 0);
}

inline bool has_command_option(const std::vector<std::string> &args, const std::string &option) {
    std::vector<std::string>::const_iterator option_pointer = std::find(std::begin(args), std::end(args), option);
    return option_pointer != std::end(args);
}

std::tuple<std::string, std::vector<std::string>> get_command_option_and_remaining_args(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<std::string> &default_value = std::nullopt);

std::tuple<std::uint32_t, std::vector<std::string>> get_command_option_uint32_and_remaining_args(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<std::uint32_t> &default_value = std::nullopt);

std::tuple<std::int32_t, std::vector<std::string>> get_command_option_int32_and_remaining_args(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<std::uint32_t> &default_value = std::nullopt);

std::tuple<double, std::vector<std::string>> get_command_option_double_and_remaining_args(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<double> &default_value = std::nullopt);

std::tuple<bool, std::vector<std::string>> has_command_option_and_remaining_args(
    const std::vector<std::string> &args, const std::string &option);

template <class T>
inline void split_string_into_vector(
    std::vector<T> &output_vector, const std::string input_command, const char *delimiter) {
    std::string input_command_modified = input_command;
    if (!input_command_modified.empty()) {
        size_t current_pos = input_command_modified.find(delimiter);
        while (current_pos != std::string::npos) {
            output_vector.push_back(parse<T>(input_command_modified.substr(0, current_pos)));
            input_command_modified.erase(0, current_pos + 1);
            current_pos = input_command_modified.find(delimiter);
        }
        if (!input_command_modified.empty()) {
            output_vector.push_back(parse<T>(input_command_modified));
        }
    }
}

template <class T>
T get(
    const std::vector<std::string> &args,
    const std::string &option,
    const std::optional<T> &default_value = std::nullopt) {
    std::string param;
    if (default_value.has_value()) {
        if constexpr (std::is_same_v<T, std::string>) {
            param = get_command_option(args, option, default_value.value());
        } else {
            param = get_command_option(args, option, std::to_string(default_value.value()));
        }
    } else {
        param = get_command_option(args, option);
    }

    return parse<T>(param);
}

void validate_remaining_args(const std::vector<std::string>& remaining_args);

}  // namespace args

namespace filesystem {
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
        "--compile-only",
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
        if (arg.compare("--outdir") == 0)
        {
            override_output_dir = args.at(arg_index + 1);
            break;
        }
        utils::replace_all(arg, "--", "");
        utils::replace_all(arg, "-", "_");

        arg_combined.append(arg);
    }

    std::size_t arg_combined_hash = std::hash<std::string>{}(arg_combined);

    string buda_out;
    if (getenv("BUDA_OUT")) {
        buda_out = getenv("BUDA_OUT");
    } else { 
        buda_out = "./";
    }
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

    bool skip_compile_hlk_ops = tt::args::has_command_option(args, "--skip-compile-hlk-ops");
    bool skip_compile_backend = tt::args::has_command_option(args, "--skip-compile-backend");
    bool run_only = tt::args::has_command_option(args, "--run-only");

    if (skip_compile_backend) {
        log_assert(fs::exists(output_dir_path), "Output dir does not exist.");
        log_info(tt::LogModel, "skip_compile_backend flag is set. Not deleting the existing output directory: {}", output_dir_path);
    }

    if (skip_compile_hlk_ops) {
        log_assert(fs::exists(output_dir_path), "Output dir does not exist.");
        log_info(tt::LogModel, "skip_compile_hlk_ops flag is set. Not deleting the existing output directory: {}",  output_dir_path);
    }
    if (run_only) {
        log_assert(fs::exists(output_dir_path), "Output dir does not exist.");
        log_warning(tt::LogModel, "run_only flag is set. Not deleting the existing output directory: {}", output_dir_path);
    }
    if (fs::exists(output_dir_path) && (!skip_compile_hlk_ops && !skip_compile_backend && !run_only)) {
        log_info(tt::LogModel, "Deleting existing output_dir: {}", output_dir_path);
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
    log_info(tt::LogModel, "using output_dir: {}", output_dir);

    return output_dir;
}

} // namespace filesystem

namespace utils {
    std::string pretty_print_bytes(uint64_t bytes);
} // namespace utils

} // namespace tt
