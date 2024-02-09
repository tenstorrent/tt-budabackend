// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "golden_config.hpp"
#include "golden_workload_data.hpp"

namespace tt::golden::io {
golden_workload_data *get_workload(const std::string tag);
tt_golden_config *get_config(const std::string tag);

void free_object_cache();

//! Interface Functions for queues
void push_input(
    const tt::tt_dram_io_desc &q_desc, 
    const tt::tt_TilizedTensorDesc &tilized_tensor, 
    const int timeout_in_seconds, 
    const int ram_ptr);
void push_input(
    const tt::tt_dram_io_desc &q_desc,
    const tt::tt_PytorchTensorDesc &py_tensor_desc,
    const bool push_one,
    const int timeout_in_seconds,
    const int ptr);
void pop_output(const tt::tt_dram_io_desc &q_desc, const bool pop_one, const int timeout_in_seconds);
void get_output(
    const tt::tt_dram_io_desc &q_desc,
    tt::tt_PytorchTensorDesc &py_tensor_desc,
    const bool get_one,
    const int timeout_in_seconds,
    const int ptr);

//! Debug Interface Functions for queues -- Used by backend tests -- Will be REMOVED once the other paths are
//! supported
void push_input(
    const std::string netlist_path, const tt_queue_info &q_info, std::shared_ptr<tt_tensor> input, const int ptr);
std::shared_ptr<tt_tensor> pop_output(const std::string netlist_path, const tt_queue_info &q_info);
std::shared_ptr<tt_tensor> get_output(const std::string netlist_path, const tt_queue_info &q_info, const int ptr);

void convert_tt_tensor_to_py_tensor_desc(
    const std::string netlist_path,
    std::shared_ptr<tt_tensor> tensor,
    const tt_dram_io_desc &q_desc,
    tt::tt_PytorchTensorDesc &py_tensor_desc,
    const bool batched);

}  // namespace tt::golden::io
