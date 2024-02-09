// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "test_config.h"

#include <fstream>

#include "experimental/filesystem"

tests::KernelType tests::get_kernel_type_from_string(std::string kernel_type_string) {
    if (KernelTypeMap.find(kernel_type_string) == KernelTypeMap.end()) {
        throw std::runtime_error("Cannot find kernel type: " + kernel_type_string);
    }
    return KernelTypeMap.at(kernel_type_string);
}
string tests::get_string_from_kernel_type(KernelType kernel_type) {
    for (const auto it : tests::KernelTypeMap) {
        if (it.second == kernel_type) {
            return it.first;
        }
    }
    throw std::runtime_error("Cannot find kernel type");
}

tests::TestConfig::TestConfig(tests::TestArgs test_args, std::string test_name, bool hlkc_test, bool perf_dump) :
    test_name(test_name), test_args(test_args), hlkc_test(hlkc_test), perf_dump(perf_dump), device(test_args.device) {}

tests::TestConfig::TestConfig(
    tests::TestArgs test_args, std::string test_name, YAML::Node test_descriptor, bool hlkc_test, bool perf_dump) :
    TestConfig(test_args, test_name, hlkc_test, perf_dump) {
    test_config_api::read_test_config_from_yaml(*this, test_descriptor);
}

std::string tests::test_config_api::get_uniquified_output_folder(tests::TestConfig &test_config) {
    if (test_config.output_folder == "") {
        test_config.output_folder = test_config.test_args.gitroot + "/llk_out/id_" + to_string(test_config.id) + "_" +
                                    test_config.test_name + to_string(std::hash<tests::TestConfig>{}(test_config)) +
                                    "/";
    }
    return test_config.output_folder;
}

void tests::test_config_api::update_tensor_config(
    TestConfig &test_config, std::string tensor_name, tests::TensorConfig &tensor_config) {
    test_config.tensor_configs[tensor_name] = tensor_config;
    // Reinitialize the stream config incase anthing was updated
    if (test_config.tensor_configs[tensor_name].stream_config.initialized) {
        initialize_stream_config(test_config.tensor_configs[tensor_name].stream_config);
    }
    // Updating a tensor config means the input dims change, so the overlay config can change.
    update_overlay_config_for_tensor(
        test_config.overlay_config, tensor_name, tensor_config.dims, tensor_config.data_format);
}

void tests::test_config_api::set_tensor_addresses(
    TestConfig &test_config, std::map<std::string, uint32_t> &tensor_address_map) {
    for (auto it : tensor_address_map) {
        auto tensor_name = it.first;
        auto tensor_address = it.second;
        test_config.tensor_configs[tensor_name].address = tensor_address;
    }
}
void tests::test_config_api::update_tensor_config(
    TestConfig &test_config, std::string tensor_name, llk::TensorDims dim, DataFormat data_format) {
    test_config.tensor_configs[tensor_name].dims = dim;
    test_config.tensor_configs[tensor_name].data_format = data_format;
    // Reinitialize the stream config incase anthing was updated
    if (test_config.tensor_configs[tensor_name].stream_config.initialized) {
        initialize_stream_config(test_config.tensor_configs[tensor_name].stream_config);
    }
    // Updating a tensor config means the input dims change, so the overlay config can change.
    update_overlay_config_for_tensor(test_config.overlay_config, tensor_name, dim, data_format);
}

void tests::test_config_api::update_tensor_config_for_streaming(
    tests::TensorConfig &tensor_config, std::string directory_path) {
    // Update and check when sreaming config and overlay config doesn't match.
    // TODO:
    // add the tiles and paths to the tensor_config
    tensor_config.stream_config.stream_buffer_address = tensor_config.address;
    uint32_t tile_id = 0;
    for (uint tile_id = 0; tile_id < tensor_config.dims.num_tiles(); tile_id++) {
        std::ostringstream filename_ss;
        filename_ss << std::setfill('0') << std::setw(8) << std::hex << tile_id << ".hex";
        std::string header_file_path = directory_path + tensor_config.name + "/header_memories/" + filename_ss.str();
        std::string data_file_path = directory_path + tensor_config.name + "/data_memories/" + filename_ss.str();

        std::experimental::filesystem::path header_path(header_file_path);
        std::experimental::filesystem::path data_path(data_file_path);
        if (not std::experimental::filesystem::exists(header_path.parent_path())) {
            std::experimental::filesystem::create_directories(header_path.parent_path());
        }
        if (not std::experimental::filesystem::exists(data_path.parent_path())) {
            std::experimental::filesystem::create_directories(data_path.parent_path());
        }
        tests::add_tile_to_stream_config(
            tensor_config.stream_config,
            data_file_path,
            header_file_path,
            static_cast<uint32_t>(
                llk::TensorDims(1, 1, 32, 32).num_bytes_when_assembled(tensor_config.data_format, false)),
            tensor_config.data_format,
            tile_id);
    }
}

void tests::test_config_api::update_stream_buffer_size_for_tensor(
    TestConfig &test_config, string tensor_name, uint32_t num_tiles) {
    if (test_config.tensor_configs[tensor_name].stream_config.initialized) {
        test_config.tensor_configs[tensor_name].stream_config.max_tiles_for_buffer = num_tiles;
    }
    update_overlay_config(test_config.overlay_config, tensor_name + "_data_buf_size", to_string(num_tiles));
}

void tests::test_config_api::update_stimulus_config_in_tensor_config(
    TestConfig &test_config, string tensor_name, string key, string value) {
    // Updating a tensor config means the input dims change, so the overlay config can change.
    test_config.tensor_configs[tensor_name].stimulus_config[key] = value;
}

void tests::test_config_api::update_stimulus_config_in_tensor_config(
    TestConfig &test_config, string tensor_name, map<string, string> stimulus_config) {
    for (auto &it : stimulus_config) {
        update_stimulus_config_in_tensor_config(test_config, tensor_name, it.first, it.second);
    }
}

void tests::test_config_api::read_tensor_config_from_yaml(TestConfig &test_config, YAML::Node &test_descriptor) {
    if (not test_descriptor["test-config"]["tensor-config"]) {
        throw std::runtime_error("test-config.tensor-config not defined");
    }
    for (auto const &it : test_descriptor["test-config"]["tensor-config"]) {
        auto tensor_string = it.first.as<std::string>();
        auto tensor_dims_vector = it.second["dims"].as<std::vector<int>>();
        auto data_format = llk::get_data_format(it.second["data-format"].as<std::string>());
        test_config.tensor_configs[tensor_string] =
            TensorConfig(llk::TensorDims(tensor_dims_vector), data_format, tensor_string);

        if (it.second["stimulus-config"]) {
            read_stimulus_config_from_yaml(test_config.tensor_configs[tensor_string], it.second["stimulus-config"]);
        }
        if (it.second["stream-config"]) {
            initialize_stream_config(test_config.tensor_configs[tensor_string].stream_config);
            read_stream_config_from_yaml(test_config.tensor_configs[tensor_string], it.second["stream-config"]);
        }
    }
}
std::vector<tests::StreamConfig> tests::test_config_api::get_stream_configs(TestConfig &test_config) {
    std::vector<StreamConfig> stream_configs = {};
    for (auto const &it : test_config.tensor_configs) {
        auto tensor_string = it.first;
        auto tensor_config = it.second;
        if (tensor_config.stream_config.initialized) {
            stream_configs.push_back(tensor_config.stream_config);
        }
    }
    return stream_configs;
}
void tests::test_config_api::read_extra_config_from_yaml(TestConfig &test_config, YAML::Node &test_descriptor) {
    for (auto const &it : test_descriptor["test-config"]["extra-config"]) {
        auto key_string = it.first.as<std::string>();
        auto val_string = it.second.as<std::string>();
        test_config.extra_config[key_string] = val_string;
    }
}
void tests::test_config_api::read_test_config_from_yaml(TestConfig &test_config, YAML::Node &test_descriptor) {
    if (test_descriptor["test-config"]["seed"]) {
        test_config.seed = test_descriptor["test-config"]["seed"].as<int>();
    } else if (test_config.test_args.force_seed and (not test_config.test_args.regression_mode)) {
        test_config.seed = test_config.test_args.seed;  // directed config test
    } else {
        test_config.seed = rand();
    }
    if (test_descriptor["test-config"]["device"]) {
        test_config.device = test_descriptor["test-config"]["device"].as<string>(); // override from yaml
    } 
    // Read extra configs
    read_extra_config_from_yaml(test_config, test_descriptor);
    // Read input/output tensor dims
    read_tensor_config_from_yaml(test_config, test_descriptor);
    // Fill up overlay_config
    read_overlay_config_from_yaml(test_config.overlay_config, test_descriptor);
    update_overlay_config(test_config, "chip", test_config.device);
    // Fill up comparison_config
    read_comparison_config_from_yaml(test_config.comparison_config, test_descriptor);

    // Create Names Map
    if (not test_descriptor["kernels-config"]) {
        throw std::runtime_error("kernel-config needs to be defined within the yaml file");
    }
    for (auto const &it : test_descriptor["kernels-config"]) {
        auto coord = llk::format_node(it.first.as<std::string>());
        auto kernel_config_per_coord = it.second;

        std::vector<KernelType> kernel_types = {KernelType::UNPACK, KernelType::MATH, KernelType::PACK};

        for (auto const kernel_type : kernel_types) {
            std::string kernel_type_string = tests::get_string_from_kernel_type(kernel_type);
            if (not kernel_config_per_coord[kernel_type_string]) {
                throw std::runtime_error(kernel_type_string + " needs to be defined within the yaml file");
            }
            std::string base_kernel_name = kernel_config_per_coord[kernel_type_string]["base-name"].as<std::string>();
            std::string full_kernel_name = base_kernel_name;
            if (kernel_config_per_coord[kernel_type_string]["full-name"]) {
                full_kernel_name = kernel_config_per_coord[kernel_type_string]["full-name"].as<std::string>();
            } else {
                full_kernel_name = base_kernel_name;
                std::vector<std::string> subop_strings;
                // Generate full name for kernel from yaml
                if (not test_config.hlkc_test and kernel_config_per_coord[kernel_type_string]["sub-ops"]) {
                    subop_strings =
                        kernel_config_per_coord[kernel_type_string]["sub-ops"].as<std::vector<std::string>>();
                }
                for (const auto subop_string : subop_strings) {
                    full_kernel_name += "_" + subop_string;
                }
            }
            test_config.core_configs[coord].kernel_names[kernel_type] = {
                {"base", base_kernel_name},
                {"full", full_kernel_name},
            };
        }
    }
}

void tests::test_config_api::update_overlay_config(TestConfig &test_config, std::string key, std::string value) {
    update_overlay_config(test_config.overlay_config, key, value);
}

void tests::test_config_api::update_extra_config(TestConfig &test_config, std::string key, std::string value) {
    test_config.extra_config[key] = value;
}

void tests::test_config_api::clear_kernel_parameters(
    TestConfig &test_config, llk::xy_pair coord, tests::KernelType kernel_type, std::string key, std::string value) {
    clear_param_mapping(test_config.core_configs[coord].kernel_parameters[kernel_type]);
}

void tests::test_config_api::update_kernel_parameters(
    TestConfig &test_config, llk::xy_pair coord, tests::KernelType kernel_type, std::string key, std::string value) {
    add_param_mapping(test_config.core_configs[coord].kernel_parameters[kernel_type], key, value);
}

void tests::test_config_api::update_kernel_parameters(
    TestConfig &test_config,
    llk::xy_pair coord,
    tests::KernelType kernel_type,
    std::map<std::string, std::string> param_mappings) {
    add_param_mapping(test_config.core_configs[coord].kernel_parameters[kernel_type], param_mappings);
}

void tests::test_config_api::update_kernel_parameters(
    TestConfig &test_config, llk::xy_pair coord, tests::KernelType kernel_type, tests::KernelParams kernel_params) {
    test_config.core_configs[coord].kernel_parameters[kernel_type] = kernel_params;
}

void tests::test_config_api::update_kernel_used(
    TestConfig &test_config,
    llk::xy_pair coord,
    tests::KernelType kernel_type,
    std::string base_kernel_name,
    std::string full_kernel_name) {
    test_config.core_configs[coord].kernel_names[kernel_type] = {
        {"base", base_kernel_name},
        {"full", full_kernel_name},
    };
}

void tests::test_config_api::update_comparison_config(TestConfig &test_config, ComparisonConfig comparison_config) {
    test_config.comparison_config = comparison_config;
}

std::string tests::test_config_api::generate_config_summary_string(TestConfig &test_config) {
    std::string result = "test_name=" + test_config.test_name;
    for (auto it : test_config.tensor_configs) {
        result = result + "_#_" + it.first + "_dims(w,z,y,x)=" + it.second.dims.unformatted_str();
        result = result + "_#_" + it.first + "_format=" + llk::to_str(it.second.data_format);
    }
    for (auto it : test_config.extra_config) {
        result = result + "_#_" + it.first + "=" + it.second;
    }
    result = result + "_#_seed=" + to_string(test_config.seed);
    result = result + "_#_output_hash=" + to_string(std::hash<tests::TestConfig>{}(test_config));
    return result;
}
std::map<std::string, std::string> tests::test_config_api::generate_fields_from_config_summary_string(
    std::string config_summary_string) {
    std::string start_delim = ": ";
    std::string delim = "_#_";
    std::string header_delim = "=";
    std::map<std::string, std::string> fields;

    auto start = config_summary_string.find(start_delim) + start_delim.length();
    auto end = config_summary_string.find(delim);
    while (end != std::string::npos) {
        std::string field_string = config_summary_string.substr(start, end - start);
        auto header_separator = field_string.find(header_delim);

        std::string key = field_string.substr(0, header_separator);
        std::string value = field_string.substr(header_separator + header_delim.length());
        start = end + delim.length();
        end = config_summary_string.find(delim, start);
        fields[key] = value;
    }
    // Last field
    std::string field_string = config_summary_string.substr(start);
    auto header_separator = field_string.find(header_delim);

    std::string key = field_string.substr(0, header_separator);
    std::string value = field_string.substr(
        header_separator + header_delim.length(), (int)field_string.size() - header_separator - header_delim.length());

    fields[key] = value;
    return fields;
}
std::vector<std::string> tests::test_config_api::get_keys_from_summary_string(std::string config_summary_string) {
    std::vector<std::string> keys;
    for (auto &it : generate_fields_from_config_summary_string(config_summary_string)) {
        keys.push_back(it.first);
    }
    return keys;
}

std::vector<std::string> tests::test_config_api::get_values_from_summary_string(
    std::string config_summary_string, std::vector<std::string> keys) {
    auto fields = generate_fields_from_config_summary_string(config_summary_string);
    std::vector<std::string> values;
    for (auto &key : keys) {
        if (fields.find(key) != fields.end()) {
            values.push_back(fields[key]);
        } else {
            throw std::runtime_error("Cannot get value from config summary string for key:" + key);
        }
    }
    return values;
}

void tests::test_config_api::dump_config_as_yaml(TestConfig &test_config, std::string filepath) {
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "test-config";
    out << YAML::BeginMap;
    out << YAML::Key << "seed";
    out << YAML::Value << to_string(test_config.seed);
    out << YAML::Key << "device";
    out << YAML::Value << test_config.device;
    out << YAML::Key << "extra-config";
    out << test_config.extra_config;
    out << YAML::Key << "tensor-config";
    out << YAML::BeginMap;
    for (auto &it : test_config.tensor_configs) {
        out << YAML::Key << it.first;
        out << YAML::BeginMap;
        out << YAML::Key << "dims";
        out << it.second.dims.get_vector();
        out << YAML::Key << "data-format";
        out << YAML::Value << llk::to_str(it.second.data_format);
        out << YAML::Key << "stimulus-config";
        out << it.second.stimulus_config;
        if (it.second.stream_config.initialized) {
            out << YAML::Key << "stream-config";
            out << it.second.stream_config;
        }
        out << YAML::EndMap;
    }
    out << YAML::EndMap;
    out << test_config.comparison_config;
    out << YAML::EndMap;

    out << YAML::Key << "kernels-config";
    out << YAML::BeginMap;
    out << YAML::Key << "0-0";
    out << YAML::BeginMap;
    out << YAML::Key << "unpack-kernel";
    out << YAML::BeginMap;
    out << YAML::Key << "base-name";
    out << YAML::Value << test_config.core_configs[llk::xy_pair(0, 0)].kernel_names[KernelType::UNPACK]["base"];
    out << YAML::Key << "full-name";
    out << YAML::Value << test_config.core_configs[llk::xy_pair(0, 0)].kernel_names[KernelType::UNPACK]["full"];
    out << YAML::EndMap;
    out << YAML::Key << "math-kernel";
    out << YAML::BeginMap;
    out << YAML::Key << "base-name";
    out << YAML::Value << test_config.core_configs[llk::xy_pair(0, 0)].kernel_names[KernelType::MATH]["base"];
    out << YAML::Key << "full-name";
    out << YAML::Value << test_config.core_configs[llk::xy_pair(0, 0)].kernel_names[KernelType::MATH]["full"];
    out << YAML::EndMap;
    out << YAML::Key << "pack-kernel";
    out << YAML::BeginMap;
    out << YAML::Key << "base-name";
    out << YAML::Value << test_config.core_configs[llk::xy_pair(0, 0)].kernel_names[KernelType::PACK]["base"];
    out << YAML::Key << "full-name";
    out << YAML::Value << test_config.core_configs[llk::xy_pair(0, 0)].kernel_names[KernelType::PACK]["full"];
    out << YAML::EndMap;
    out << YAML::EndMap;
    out << YAML::EndMap;

    out << test_config.overlay_config;
    out << YAML::EndMap;
    std::experimental::filesystem::path yaml_path(filepath);
    if (not std::experimental::filesystem::exists(yaml_path.parent_path())) {
        std::experimental::filesystem::create_directories(yaml_path.parent_path());
    }
    std::ofstream yaml_output(filepath);
    yaml_output << out.c_str();
    return;
}
