// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "tti_lib.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "model/base_defs.h"
#include "utils/logger.hpp"

#include <experimental/filesystem> // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>
namespace fs = std::experimental::filesystem;  // see comment above

namespace {

std::string get_flat_filename(const std::string &path) {
    std::string filename = path.substr(path.find_last_of('/') + 1);
    std::replace(filename.begin(), filename.end(), '.', '_');
    return filename;
}
std::string strip_prefix(const std::string &full_string, const std::string &delimiter) {
    std::string stripped = full_string;
    size_t pos = full_string.rfind(delimiter);
    if (pos != std::string::npos && pos + 1 < full_string.length()) {
        stripped = full_string.substr(pos + 1);
    }
    return stripped;
}

std::string get_enum(const json_node &node) {
    std::string enum_str = "";
    if (node.find("__enum__") == node.end()) {
        enum_str = node.get<std::string>();
    } else {
        enum_str = node["__enum__"].get<std::string>();
    }
    return strip_prefix(enum_str, ".");
}

std::string find_file(const std::string &filename, const std::string &path) {
    for (const auto &entry : fs::recursive_directory_iterator(path)) {
        if (fs::is_regular_file(entry) && entry.path().filename() == filename) {
            return entry.path().string();
        }
    }
    log_fatal("Failed to find {} under {}", filename, path);
    return "";
}


bool is_zip_file(const std::string &path) {
    std::vector<char> buffer(4);
    std::ifstream file(path, std::ios::binary);
    bool is_zip = false;
    if (file.read(buffer.data(), buffer.size())) {
        is_zip = buffer[0] == 0x50 && buffer[1] == 0x4b &&
                buffer[2] == 0x03 && buffer[3] == 0x04;
    }
    else {
        log_fatal("Unable to read from file {}", path);
    }
    file.close();
    return is_zip;
}

void uncompress_image(const std::string &path, const std::string &output_path) { 
    log_debug(tt::LogBackend, "Uncompressing pre-compiled image {} to {}", path, output_path);
    std::stringstream cmd;
    std::string log = output_path + "/" + get_flat_filename(path) + ".log ";

    if(is_zip_file(path)) {
        // unzip zipped file here
        cmd << "unzip " << path << " -d " << output_path;
        auto result = tt::run_command(cmd.str(), log, "");
        if (!result.success) {
            log_fatal("Running uncompress image command failed: {}, error_message: {}", cmd.str(), result.message);
        }
    }
    else {
        // untar tarred file here
        cmd << "tar -xf " << path << " -C " << output_path;
        auto result = tt::run_command(cmd.str(), log, "");
        if (!result.success) {
            log_fatal("Running untar image command failed: {}, error_message: {}", cmd.str(), result.message);
        }
    }
}

void set_envvars(const json &envvars) {
    // do not overwrite existing envvars
    constexpr int overwrite = 0;
    for (auto &it : envvars.items()) {
        std::string key = it.key();
        std::string value = it.value().get<std::string>();
        setenv(key.c_str(), value.c_str(), overwrite);
    }
}

}  // namespace

namespace tt {

tt_device_image::tt_device_image(const std::string &path, const ::std::string &output_path) : output_path(output_path) {
    if (!fs::exists(path)) {
        log_fatal("TTI file {} does not exist", path);
    }
    if (fs::exists(output_path)) {
        fs::remove_all(output_path);
    }
    fs::create_directory(output_path);
    uncompress_image(path, output_path);

    model_path = output_path + "/unzipped_tti";
    if (!fs::exists(model_path)) {
        log_warning(tt::LogBackend, "TTI model path {} does not exist, performing search for device.json to locate the model path", model_path);
        model_path = find_file("device.json", output_path);
    }

    std::string env_json_path = get_model_path("compile_and_runtime_config.json");
    std::ifstream env_json(env_json_path);
    if (!env_json.is_open()) {
        log_fatal("Failed to open json file {}", env_json_path);
    }
    set_envvars(json::parse(env_json));

    std::string meta_json_path = get_model_path("device.json");
    std::ifstream meta_json(meta_json_path);
    if (!meta_json.is_open()) {
        log_fatal("Failed to open json file {}", meta_json_path);
    }
    metadata = json::parse(meta_json);
    constant_names = metadata["compiled_graph_state"]["ordered_constant_node_names"].get<std::set<std::string>>();
    parameter_names = metadata["compiled_graph_state"]["ordered_parameter_node_names"].get<std::set<std::string>>();
    populate_io_prestride_map();
}

void tt_device_image::populate_io_prestride_map() {
    const auto& input_names = get_graph_input_names();
    uint32_t transform_idx = 0;
    for (auto transform : metadata["compiled_graph_state"]["ordered_input_runtime_tensor_transforms"]) {
        if(transform["type"].get<std::string>() == "Prestride") {
            const auto shape = transform["original_shape"]["dims_"].get<std::vector<std::uint32_t>>();
            const auto stride_y = transform["stride_height"].get<std::uint32_t>();
            const auto stride_x = transform["stride_width"].get<std::uint32_t>();
            log_assert(stride_x == stride_y, "Cannot support prestriding with stride_x != stride_y");
            prestride_info info = {shape[1], shape[2], shape[3], stride_x};
            prestride_transform_per_input.insert({input_names.at(transform_idx), info});
        }
        transform_idx++;
    }
}

std::string tt_device_image::arch() const {
    static std::string arch_str = "";
    if (arch_str == "")
        arch_str = get_enum(metadata["arch"]);
    return arch_str;
}

std::string tt_device_image::backend() const {
    static std::string backend_str = "";
    if (backend_str == "")
        backend_str = get_enum(metadata["devtype"]);
    return backend_str;
}

std::string tt_device_image::get_netlist_path() const {
    // Get the raw netlist path from device.json and extract the netlist filename from it
    // We expect the netlist to be in the main model directory
    fs::path raw_netlist_path(metadata["compiled_graph_state"]["netlist_filename"].get<std::string>());
    std::string netlist_name = raw_netlist_path.filename();
    return get_model_path(netlist_name);
}

std::string tt_device_image::get_model_path(const std::string &relative_path) const {
    return model_path + "/" + relative_path;
}

std::vector<std::string> tt_device_image::get_graph_input_names() const {
    return metadata["compiled_graph_state"]["ordered_input_names"].get<std::vector<std::string>>();
}
std::vector<std::string> tt_device_image::get_graph_output_names() const {
    return metadata["compiled_graph_state"]["ordered_output_names"].get<std::vector<std::string>>();
}

std::vector<std::string> tt_device_image::get_graph_constant_and_parameter_names() const {
    std::vector<std::string> names = {};
    names.insert(names.end(), constant_names.begin(), constant_names.end());
    names.insert(names.end(), parameter_names.begin(), parameter_names.end());
    return names;
}

tt_device_image::tensor_meta tt_device_image::get_tensor_meta(const std::string &name) const {
    if (parameter_names.find(name) != parameter_names.end()) {
        const auto &node = metadata["compiled_graph_state"]["post_const_eval_parameters"][name];
        return get_tensor_meta(node);
    } else if (constant_names.find(name) != constant_names.end()) {
        const auto &node = metadata["compiled_graph_state"]["post_const_eval_constants"][name];
        return get_tensor_meta(node);
    } else {
        log_fatal("Failed to find serialized tensor {} in device.json", name);
    }
    return {};
}

tt_device_image::tensor_meta tt_device_image::get_tensor_meta(const json_node &node) const {
    if (node.find("bin") == node.end()) {
        log_fatal("Failed to find bin in node {}, pre-compiled model must be generated with PYBUDA_TTI_BACKEND_FORMAT=1",
            node.dump());
    }
    std::string bin_name = node["bin"].get<std::string>();

    if(bin_name.find(".tbin") == std::string::npos) {
        return {
            get_model_path(node["bin"].get<std::string>()),
            node["itemsize"].get<uint32_t>(),
            get_enum(node["format"]),
            node["shape"].get<std::vector<uint32_t>>(),
            node["strides"].get<std::vector<uint32_t>>(),
            node["dim"].get<uint32_t>(),
        };
    } else if(bin_name.find(".tbin") != std::string::npos) {
        return {
            get_model_path(node["bin"].get<std::string>()),
            node["num_buffers"].get<std::uint32_t>(),
            get_enum(node["format"]),
            {},
            {},
            node["buffer_size_bytes"].get<std::uint32_t>(),
        };
    } else {
        log_fatal(
            "Invalid binary format. Expected tensors in .bin or .tbin files. Found {}",
            bin_name);
        return {};
    }
}

std::unordered_map<std::string, tt_device_image::prestride_info> tt_device_image::get_io_prestride_map() const {
    return prestride_transform_per_input;
}
}  // namespace tt
