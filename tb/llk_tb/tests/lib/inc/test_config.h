// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <map>
#include <string>

#include "test_args.h"
#include "test_comparison_config.h"
#include "test_kernel_params.h"
#include "test_overlay_config.h"
#include "test_stream_config.h"
#include "test_tensor_config.h"
#include "utils/hash.h"
#include "yaml-cpp/yaml.h"

using namespace std;

namespace tests {

//! KernelType is meant as a enumeration for the type of kernels supported in yaml/config
//  Helper functions help keep track of the string form expected in YAML
enum KernelType { UNPACK, MATH, PACK };
static const map<string, tests::KernelType> KernelTypeMap = {
    {"unpack-kernel", tests::KernelType::UNPACK},
    {"math-kernel", tests::KernelType::MATH},
    {"pack-kernel", tests::KernelType::PACK},
};
KernelType get_kernel_type_from_string(string kernel_type_string);
string get_string_from_kernel_type(KernelType kernel_type);

//! TestConfig specification for a specific core.
//    These are calculated from kernel_parameters which is ctest/test_writer defined
//    and kernel names which can be user specified or read from YAML
struct SingleCoreTestConfig {
    //! Kernel name has two fields for each kernel type, "base" for param file name generation and "full" which is the
    //! full name of kernel
    map<KernelType, map<string, string>> kernel_names = {
        {KernelType::UNPACK, {{"base", ""}, {"full", ""}}},
        {KernelType::MATH, {{"base", ""}, {"full", ""}}},
        {KernelType::PACK, {{"base", ""}, {"full", ""}}},
    };
    //! Kernel params has a map<string, string> for each kernel type.
    map<KernelType, KernelParams> kernel_parameters = {
        {KernelType::UNPACK, KernelParams()},
        {KernelType::MATH, KernelParams()},
        {KernelType::PACK, KernelParams()},
    };
};

//! TestConfig - Config Structure
struct TestConfig {
    //////////////////////////////////////////////
    // Global Configs (consistent across tests) //
    //////////////////////////////////////////////
    //! Global args from command line
    TestArgs test_args;
    string test_name;
    string output_folder;
    string device = "";
    bool hlkc_test = false;
    bool perf_dump = false;
    //! Seed used for stimulus and expected generation.
    int seed = 0;
    //! Config id used in regression mode to differentiate yaml dumps with config
    int id = 0;

    //! Configs that are specified per core. contains kernel names and the related params
    /*!  Helper functions are available to read in the kernel names from yaml descriptor,
         kernel_parameters have helper functions which can set it specifically
    */
    map<llk::xy_pair, SingleCoreTestConfig> core_configs;

    //! Comparison Configuration that specifies the flags for comparison and controls how it is compared
    /*!  Helper functions are available to read in the config from yaml descriptor, and to automatically get updated
         if the tensor config changes
    */
    ComparisonConfig comparison_config;
    //! Overlay Configuration that specifies the flags for programming the overlay generation script.
    /*!  Helper functions are available to read in the config from yaml descriptor, and to automatically get updated
         if the tensor config changes
    */
    OverlayConfig overlay_config;  // TODO: For now we don't support multi-core overlay config
    //! Tensor Configuration which specifies the dimensions/data format/stimulus for the tensor
    /*!  Helper functions are available to read in the config from yaml descriptor
     */
    map<string, TensorConfig> tensor_configs;  // TODO: For now we don't support multi-core tensor input or outputs yet
    //! Extra Configs which test writer can add to and use in there test.
    map<string, string> extra_config;

    TestConfig(TestArgs test_args, string test_name, bool hlkc_test = false, bool perf_dump = false);
    TestConfig(
        TestArgs test_args,
        string test_name,
        YAML::Node test_descriptor,
        bool hlkc_test = false,
        bool perf_dump = false);
};

namespace test_config_api {

//! get a unique output folder based off the hash of the test_config structure
std::string get_uniquified_output_folder(TestConfig &test_config);
//! Read test-config.tensor-config sections from a yaml
void read_tensor_config_from_yaml(TestConfig &test_config, YAML::Node &test_descriptor);
//! Read test-config.extra-config sections from a yaml
void read_extra_config_from_yaml(TestConfig &test_config, YAML::Node &test_descriptor);
//! Read the test-config section from a yaml.  Top-level function to call if a yaml configuration needs to be pulled
//! into the config
void read_test_config_from_yaml(TestConfig &test_config, YAML::Node &test_descriptor);

//! Get the vector of fifo configurations from the tensor configs
std::vector<StreamConfig> get_stream_configs(TestConfig &test_config);

//! Add or set the overlay config using {key, value} map
void update_overlay_config(TestConfig &test_config, std::string key, std::string value);

//! Add or set the extra config using {key, value} map
void update_extra_config(TestConfig &test_config, std::string key, std::string value);

//! Update tensor addresses
void set_tensor_addresses(TestConfig &test_config, std::map<std::string, uint32_t> &tensor_address_map);
//! Update the buffer size for the stream/overlay
void update_stream_buffer_size_for_tensor(TestConfig &test_config, string tensor_name, uint32_t num_tiles);
//! Update the tensor configration under the tensor name. Will overrite if it exists.
void update_tensor_config(TestConfig &test_config, string tensor_name, tests::TensorConfig &tensor_config);
//! Update the tensor configration with the tiles for streaming
void update_tensor_config_for_streaming(tests::TensorConfig &tensor_config, std::string directory_path);
//! Update only the dimensions and data_format for a specific tensor config.
/*! Will create an empty tensor config with the dim and data_format if it doesn't exist.  Will update if tensor_name
 * exists
 */
void update_tensor_config(TestConfig &test_config, string tensor_name, llk::TensorDims dim, DataFormat data_format);
//! Update a specific field of the stimulus config in a tensor_config
void update_stimulus_config_in_tensor_config(TestConfig &test_config, string tensor_name, string key, string value);
//! Update only the stimulus config in a tensor_config
void update_stimulus_config_in_tensor_config(
    TestConfig &test_config, string tensor_name, map<string, string> stimulus_config);

//! Clear all kernel parameter mappings
void clear_kernel_parameters(
    TestConfig &test_config, llk::xy_pair coord, tests::KernelType kernel_type, std::string key, std::string value);
//! Update Kernal parameters using {key, value} pair.  Assign if it exists or create if it doesn't
void update_kernel_parameters(
    TestConfig &test_config, llk::xy_pair coord, tests::KernelType kernel_type, std::string key, std::string value);
//!  Update Kernal parameters using a map with {key, value} pairs.  Assign all pairs to the kernel_parameter mapping
//!  exists or create if it doesn't
void update_kernel_parameters(
    TestConfig &test_config,
    llk::xy_pair coord,
    tests::KernelType kernel_type,
    std::map<std::string, std::string> param_mappings);
//! Override the specified kernel type's kernel param mappings
void update_kernel_parameters(
    TestConfig &test_config, llk::xy_pair coord, tests::KernelType kernel_type, tests::KernelParams kernel_params);

//! Override the kernel name used (Will need to provide the full name)
void update_kernel_used(
    TestConfig &test_config,
    llk::xy_pair coord,
    tests::KernelType kernel_type,
    string base_kernel_name,
    string full_kernel_name);
//! Override the comparison config used
void update_comparison_config(TestConfig &test_config, ComparisonConfig comparison_config);

//! Get a std::string which summarizes the configuration
std::string generate_config_summary_string(TestConfig &test_config);

//! Get the fields map from the config_summary_string
std::map<std::string, std::string> generate_fields_from_config_summary_string(std::string config_summary_string);

//! Get the headers from the config_summary_string
std::vector<std::string> get_keys_from_summary_string(std::string config_summary_string);

//! Get the values from config summary string
std::vector<std::string> get_values_from_summary_string(
    std::string config_summary_string, std::vector<std::string> keys);

//! Dump a yaml file which can be targetted for yaml
void dump_config_as_yaml(TestConfig &test_config, std::string filepath);
}  // namespace test_config_api

}  // namespace tests

namespace std {
template <>
struct hash<tests::TestConfig> {
    inline size_t operator()(const tests::TestConfig &test_config) const {
        // size_t value = your hash computations over x
        size_t current_hash = test_config.seed;
        ::hash_combine(current_hash, test_config.id);
        ::hash_combine(current_hash, test_config.test_name);
        return current_hash;
    }
};
}  // namespace std
