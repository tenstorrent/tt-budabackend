// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "tt_backend_api_types.hpp"
#include "perf_lib/op_model/op_model.hpp"
#include "common/param_lib.hpp"

/**
 * @file
 * This file contains functions related to backend IO API
 */

namespace tt::backend {
/**
 * @brief Write a Pytorch Tensor input to queue/ram
 * \param q_desc Queue descriptor
 * \param py_tensor_desc Pytorch Tensor descriptor
 * \param push_one Push one entry or a batch of entries, batch size is the outer most dimension of the tensor
 * \param timeout_in_seconds Timeout in seconds for the push operation
 * \param ptr Pointer to the ram address, must be provided when accessing ram type memory. Ignored for queue type memory
 * @return DEVICE_STATUS_CODE indicating success/failure/timeout
 */
tt::DEVICE_STATUS_CODE push_input(
    const tt::tt_dram_io_desc &q_desc,
    const tt::tt_PytorchTensorDesc &py_tensor_desc,
    const bool push_one,
    const int timeout_in_seconds,
    const int ptr = -1);

/**
 * @brief Write a Tilized Tensor input to queue/ram. When batch > 1, the full 4D tensor is written to device
 * \param q_desc Queue descriptor
 * \param tilized_tensor_desc Tilized Tensor descriptor
 * \param timeout_in_seconds Timeout in seconds for the push operation
 * \param ptr Pointer to the ram address, must be provided when accessing ram type memory. Ignored for queue type memory
 * @return DEVICE_STATUS_CODE indicating success/failure/timeout
 */
tt::DEVICE_STATUS_CODE push_input(
    const tt::tt_dram_io_desc &q_desc,
    const tt::tt_TilizedTensorDesc &tilized_tensor_desc,
    const int timeout_in_seconds,
    const int ptr = -1);

/**
 * @brief Popping output from queue
 * Only frees entries from a queue, for reading data use get_output. Cannot be performed on ram.
 * \param q_desc Queue descriptor
 * \param pop_one Pop one entry or a batch of entries, batch size is the outer most dimension of the tensor
 * \param timeout_in_seconds Timeout in seconds for the pop operation
 * @return DEVICE_STATUS_CODE indicating success/failure/timeout
 */
tt::DEVICE_STATUS_CODE pop_output(const tt::tt_dram_io_desc &q_desc, const bool pop_one, const int timeout_in_seconds);

/**
 * @brief Reading output to queue/ram, ie. peek without ptr updates
 * \param q_desc Queue descriptor
 * \param py_tensor_desc Tensor descriptor
 * \param get_one Get one entry or a batch of entries, batch size is the outer most dimension of the tensor
 * \param timeout_in_seconds Timeout in seconds for the get operation
 * \param ptr Pointer to the ram address, must be provided when accessing ram type memory. Ignored for queue type memory
 * @return DEVICE_STATUS_CODE indicating success/failure/timeout
 */
tt::DEVICE_STATUS_CODE get_output(
    const tt::tt_dram_io_desc &q_desc,
    tt::tt_PytorchTensorDesc &py_tensor_desc,
    const bool get_one,
    const int timeout_in_seconds,
    const int ptr = -1);

/**
 * @brief Address translation API for queues
 * Translates from a local device relative address to a system level user-space address
 * If the device does not expose memory to be MMIO mapped and accessed by the host, this function will fail
 * \param q_desc Queue descriptor
 */
tt::DEVICE_STATUS_CODE translate_addresses(tt::tt_dram_io_desc &q_desc);

/**
 * @brief NOC logical-to-routing translation API using (x, y) coordinates
 * Translates from a NOC logical coordinate to a routing coordinate
 * \param device_yaml Path to the device yaml file
 * \param coord NOC logical coordinate
 * @return NOC routing coordinate
 */
std::tuple<std::uint32_t, std::uint32_t> translate_coord_logical_to_routing(
    const std::string &device_yaml, const std::tuple<std::uint32_t, std::uint32_t> &coord);

/**
 * @brief NOC routing-to-logical translation API using (x, y) coordinates
 * Translates from a NOC routing coordinate to a logical coordinate
 * \param device_yaml Path to the device yaml file
 * \param coord NOC routing coordinate
 * @return NOC logical coordinate
 */
std::tuple<std::uint32_t, std::uint32_t> translate_coord_routing_to_logical(
    const std::string &device_yaml, const std::tuple<std::uint32_t, std::uint32_t> &coord);

/**
 * @brief TT Backend Utility API to allocate persistent memory for a tensor. 
 * This memory is to be freed by calling free_tensor during runtime, or can be automatically freed
 * during program teardown.
 * \param tensor_format Data Format of the Tensor
 * \param num_elements Number of elements in the tensor
 * @return A shared pointer to a slab of memory (represented as a uint32_t vector). This memory is tracked by backend.
 */ 
std::shared_ptr<std::vector<uint32_t>> allocate_memory_for_tensor(const tt::DataFormat tensor_format, const uint32_t num_elements);

/**
 * @brief Memory management API for deallocating tensors
 * Locates and frees the dynamically allocated memory for backend owned tensors,
 * asserts if the tensor is not backend owned, ie. not meant to be managed by the backend
 */
template<typename T>
tt::DEVICE_STATUS_CODE free_tensor(T &tensor_desc);

/**
 * @brief Get the size of a single datum with a specific data format in bytes
 * \param data_format Data Format to compute the size of
 * @return The size of a single datum with a specific data format 
 */ 
uint32_t sizeof_format(const DataFormat data_format);

/**
 * @brief Initialization and configuration steps for the current process
 */
tt::DEVICE_STATUS_CODE initialize_child_process(const std::string &output_dir);

/**
 * @brief Post-processing and cleanup steps for the current process
 */
tt::DEVICE_STATUS_CODE finish_child_process();

/**
 * @brief Backend size calculation utility
 * @return bytes for a given tilized/untilized tile
 */
int get_tile_size_in_bytes(
    const DataFormat data_formati, const bool is_untilized, const int tile_height, const int tile_width);

/**
 * @brief Backend size calculation utility
 * @return bytes for a given tilized/untilized tensor
 */
int get_entry_size_in_bytes(
    const DataFormat data_formati,
    const bool is_untilized,
    const int ublock_ct,
    const int ublock_rt,
    const int mblock_m,
    const int mblock_n,
    const int t,
    const int tile_height = 32,
    const int tile_width = 32);

/**
 * @brief Backend size calculation utility, performs (get_entry_size_in_bytes() * entries + queue header)
 * @return bytes for a given tilized/untilized queue/ram
 */
int get_io_size_in_bytes(
    const DataFormat data_formati,
    const bool is_untilized,
    const int ublock_ct,
    const int ublock_rt,
    const int mblock_m,
    const int mblock_n,
    const int t,
    const int entries,
    const int tile_height = 32,
    const int tile_width = 32);

/**
 * @brief Aligns input address to the next address device R/W accessible address
 */
uint32_t get_next_aligned_address(const uint32_t address);

/**
 * @brief Scan and detect available devices on the machine
 * @return devices by type ordered by device id
 */
std::vector<tt::ARCH> detect_available_devices(bool only_detect_mmio = true);

/**
 * @brief Generates a vector of DeviceDesc objects ordered by device_id.
 * @return A vector of DeviceDesc's, with indices matching detect_available_devices.
 */ 
std::vector<tt::param::DeviceDesc> get_device_descs_for_available_devices(const std::string& out_dir = "./tt_build");

/**
 * @brief Generates a DeviceDesc object based on the device configuration passed in.
 * Useful for offline compile to force target device configuration in the absence of a silicon platform
 * @return A DeviceDesc object with the device configuration passed in.
 */
tt::param::DeviceDesc get_custom_device_desc(
                      tt::ARCH arch, 
                      bool mmio = false, 
                      std::uint32_t harvesting_mask = 0, 
                      std::pair<int, int> grid_dim = {0, 0},
                      const std::string& out_dir = "./tt_build");

/**
 * @brief Generates a cluster network descriptor file based on the current silicon platform
 * @return A path to the generated cluster network descriptor stored in Budabackend root
 */
std::string get_device_cluster_yaml();

/**
 * @brief Generates a cluster network descriptor file based on the current silicon platform
 * \param out_dir Directory where the cluster descriptor will be emitted
 * @return A path to the generated cluster network descriptor
 */
std::string get_device_cluster_yaml_v2(const std::string& out_dir = "");

/**
 * @brief Query method for various backend parameters
 * key = "<Arch>-<Scope>-<Name>" and is case-incensitive.
 * key = "*" is a special keyword that will dump the whole key-value store to stdout
 * Eg. key = "Grayskull-DRAM-channel_capacity" will returns num bytes as a string
 * eth_cluster_file_path = path to eth cluster file for wormhole machines
 * @return looked up value in string form, user can cast it to the appropriate type
 */
std::string get_backend_param(
    const std::string &key,
    const std::string &soc_desc_file_path = "",
    const std::string &eth_cluster_file_path = "",
    const std::string &runtime_params_ref_path = "",
    const bool save = false);

/**
 * @brief Clear the backend parameter cache
 * \param out_dir Output Directory used for workload. This stores the device descriptors 
 * which will get cleared.
 */
void clear_backend_param_cache(const std::string& out_dir = "./tt_build");

/**
 * @brief Clear the backend parameter cache
 */
void clear_backend_param_cache_v2();

/**
 * @brief Query method for OP's execution cycles
 * OP model is based on measured data from the backend
 * using idealized input and output bandwidth assuming no backpressure or starvation
 * the result is what you would expect in a single core run
 * @return number of cycles for the OP
 */
uint32_t get_op_model_execution_cycles(const tt_op_model_desc &op_desc);
uint32_t get_op_model_param(const tt_op_model_desc &op_desc, const std::string &param_name);

/**
 * @brief Configures a host profiler for the current thread
 */
tt::DEVICE_STATUS_CODE setup_backend_profiler(const std::string &output_dir);

/**
 * @brief Record a custom event for the current thread's host profiler
 */
tt::DEVICE_STATUS_CODE record_backend_event(const std::string &event_label);

/**
 * @brief Tilize a Pytorch Tensor based on the format and blocking configuration
 * specified in the queue descriptor
 * \param q_desc Queue descriptor
 * \param py_tensor_desc Pytorch Tensor to Tilize
 */
tt::tt_TilizedTensorDesc tilize_tensor(const tt::tt_dram_io_desc &q_desc, const tt::tt_PytorchTensorDesc &py_tensor_desc);

/**
 * @brief Binarize a Pytorch or Tilized Tensor to disk
 * \param tensor Tensor to binarize
 * \param file_path File that will contain the binarized tensor
 */
template<typename T>
void binarize_tensor(const T& tensor, const std::string& file_path);

/**
 * @brief Read data into Pytorch or Tilized Tensor from disk. Tensor metadata
 * needs to be initialized separately.
 * Warning: This function assumes that Pybuda will appropriately frre_tensor for 
 * this data. Backend does not keep track of memory allocated in this function.
 * \param tensor Tensor to fill up
 * \param file_path File that contains the binarized tensor
 */
template<typename T>
void debinarize_tensor(T& tensor, const std::string& file_path);
}  // namespace tt::backend
