// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "common/io_lib.hpp"
#include "common/tti_lib.hpp"
#include "loader/tt_cluster.hpp"
#include "model/tensor.hpp"
#include "model/tilize_untilize_api/tilize.h"
#include "netlist/netlist_info_types.hpp"
#include "runtime_types.hpp"
#include "runtime_workload.hpp"
namespace tt::io {

// --------------------------------------------------------------------------------------
//! Common defines and types
// --------------------------------------------------------------------------------------
using tt_target_dram = std::tuple<int, int, int>;

static constexpr int QUEUE_HEADER_SIZE_BYTES = static_cast<uint32_t>(sizeof(tt_queue_header));
extern bool debug;

// --------------------------------------------------------------------------------------
//! Getter methods
//! Using user provided 'tag', returns cached object if hit or create new object if miss
// --------------------------------------------------------------------------------------

tt_runtime_workload *get_workload(const std::string tag);
template<Tilizer tilizer_backend>
DeviceTilizer<tilizer_backend> *get_tilizer(const std::string tag, const std::vector<tt_dram_io_desc> &dram_io_desc, tt_cluster* cluster, const tt::tt_PytorchTensorDesc& tensor, const Stride& stride = Stride(), const uint32_t tile_height = 32, const uint32_t tile_width = 32);
tt_cluster *get_cluster(const std::string tag, const tt::DEVICE &target_type);

void free_object_cache();
// --------------------------------------------------------------------------------------
//! Device IO API methods
// --------------------------------------------------------------------------------------
bool push_input_to_device(const tt::tt_dram_io_desc &q_desc, const tt::tt_TilizedTensorDesc &tilized_tensor, const int timeout_in_seconds, const int ptr = -1);
bool push_input_to_device(const tt::tt_dram_io_desc &q_desc, const tt::tt_PytorchTensorDesc &py_tensor_desc, const bool push_one, const int timeout_in_seconds, const int ptr = -1);
template<typename T>
void binarize_tensor(const T& tensor, const std::string& file_path);
template<typename T>
void debinarize_tensor(T& tensor, const std::string& file_path);
tt::tt_TilizedTensorDesc tilize_tensor(const tt::tt_dram_io_desc &q_desc, const tt::tt_PytorchTensorDesc &py_tensor_desc);
bool pop_output_from_device(const tt::tt_dram_io_desc &q_desc, const bool pop_one, const int timeout_in_seconds);
bool get_output_from_device(const tt::tt_dram_io_desc &q_desc, tt::tt_PytorchTensorDesc &py_tensor_desc, const bool get_one, const int timeout_in_seconds, const int ptr = -1);

// --------------------------------------------------------------------------------------
//! Utility methods
// --------------------------------------------------------------------------------------

bool is_queue_empty(const tt_dram_io_desc& desc, const tt_queue_info &queue_info, tt_cluster *cluster);
bool is_host_queue_empty(const tt_dram_io_desc &desc);
bool is_dram_queue_empty(const tt_dram_io_desc& desc, const tt_queue_info &queue_info, tt_cluster *cluster);

void translate_addresses(tt_dram_io_desc &io_desc);

std::vector<float> process_raw_untilized(const uint32_t* mem_vector, int vector_len, DataFormat format);
tt_tensor reconstruct_tensor_from_untilized(std::vector<uint32_t> &mem_vector, const tt_dram_io_desc &desc, bool batched, bool use_two_complement = false, DataFormat from_format = DataFormat::Invalid, uint32_t w_dim = 0);
tt_tensor reconstruct_tensor_from_untilized(const tt_dram_io_desc &desc, const tt::tt_PytorchTensorDesc &py_tensor_desc, bool batched, bool use_twos_complement = false, uint32_t w_dim_override = 0);

std::vector<uint32_t> pop_untilized_vector(const tt_dram_io_desc &desc, const tt_queue_info &queue_info, tt_cluster *cluster, const int timeout_in_seconds);

void push_queue_tilized_input(const tt_queue_info &queue_info, tt_tensor &input_tensor, tt_cluster *cluster,  const int timeout_in_seconds, int ram_ptr = -1, Dim ublock_scan = Dim::R);
tt_tensor pop_queue_tilized_output(const tt_queue_info &queue_info, tt_cluster *cluster, bool update_rdptr, int pop_count, Dim ublock_scan, const int timeout_in_seconds, int ram_ptr = -1);
tt_tensor pop_queue_tilized_output_sysmem(const tt_dram_io_desc &desc, const tt_queue_info &queue_info, tt_cluster *cluster, bool update_rdptr, int pop_count, Dim ublock_scan, const int timeout_in_seconds, int ram_ptr = -1);

// --------------------------------------------------------------------------------------
//! Cpp test environment IO methods
// --------------------------------------------------------------------------------------

//! Helper for pushing inputs into queues until full via HW tilizer
void push_host_inputs(std::vector<tt_dram_io_desc> &dram_io_desc, std::function<tt_tensor(tt_queue_info&, int)> get_tensor_callback, int num_pushes = -1, bool force_sw_tilize = false);
void push_host_input(tt_dram_io_desc &io, std::function<tt_tensor(tt_queue_info&, int)> get_tensor_callback, int num_pushes = -1, int ram_ptr = -1, bool force_sw_tilize = false);

//! Helper for retrieving outputs into queues until empty from HW untilized
void pop_outputs_to_tensor_queues(std::vector<tt_dram_io_desc> &dram_io_desc, const int timeout_in_seconds);
void pop_outputs_to_tqs_hw_untilize(std::vector<tt_dram_io_desc> &dram_io_desc, const int timeout_in_seconds);
void pop_outputs_to_tqs_sw_untilize(std::vector<tt_dram_io_desc> &dram_io_desc, const int timeout_in_seconds);

tt_tensor default_debug_tensor(tt_queue_info &queue_info, int entry_idx);

// Specialization of formatted tensor to pytorch desc conversions
template <> tt_PytorchTensorDesc get_pytorch_tensor_desc<float>(tt_tensor &tensor, aligned_vector<float> &host_storage, bool force_megarow);
template <> tt_PytorchTensorDesc get_pytorch_tensor_desc<uint16_t>(tt_tensor &tensor, aligned_vector<uint16_t> &host_storage, bool force_megarow);
template <> tt_PytorchTensorDesc get_pytorch_tensor_desc<int8_t>(tt_tensor &tensor, aligned_vector<int8_t> &host_storage, bool force_megarow);
// IO profiler methods
void finish_io_profiler();
void setup_io_perf_profiler(tt_io_info io_info);

}
