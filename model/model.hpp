// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <deque>
#include <random>
#include <chrono>
#include <string>
#include <optional>
#include <cmath>
#include <unordered_map>
#include <experimental/filesystem> // clang6 requires us to use "experimental", g++ 9.3 is fine with just <filesystem>
#include "common/base.hpp"
#include "model/tensor.hpp"
#include "perf_lib/perf_descriptor.hpp"
#include "hlks/inc/hlk_api.h"
#include "device/device_api.h"
#include "tile.hpp"
#include "buffer.hpp"
#include "model/op.hpp"
#include "dram.hpp"
#include <functional>

// IO
using std::ifstream;
using std::ofstream;
using std::stringstream;
using std::cout;

// Containers
using std::deque;
using std::string;
using std::vector;
using std::pair;

using std::chrono::high_resolution_clock;

namespace fs = std::experimental::filesystem; // see comment above

class tt_core;
class tt_hex;
struct tt_comparison_config;


namespace tt
{
    class tt_op;
}

using tt::DataFormat;
using tt::DimMask;
using tt::DimMask_C;
using tt::DimMask_NONE;
using tt::DimMask_R;
using tt::DimMask_W;
using tt::DimMask_Z;
using tt::EpochInstOpCode;
using tt::EpochType;
//using tt::MathFidelity;
using tt::TargetDevice;
using tt::TensorType;
using tt::tt_block_shape;
using tt::tt_buffer;
using tt::tt_buffer_metadata;
using tt::tt_buffer_queue;
using tt::tt_bufq_ptr;
using tt::tt_core_buf_id;
using tt::tt_core_coord;
using tt::tt_core_ptr_grid;
using tt::tt_dims;
using tt::tt_dram;
using tt::tt_dram_chan_desc;
using tt::tt_dram_io;
using tt::tt_dram_io_desc;
using tt::tt_dram_resident;
using tt::tt_epoch_inst;
using tt::tt_graph_spec;
using tt::tt_grid_shape;
using tt::tt_grid_slice;
using tt::tt_logical_core_coords;
using tt::tt_logical_physical_core_coords;
using tt::tt_op;
using tt::tt_param_buffer_metadata;
using tt::tt_shape;
using tt::tt_tensor;
using tt::tt_tensor_slice;
using tt::tt_tile;


struct tt_comparison_config {
    bool user_configured = false;
    double atol = tt::constants::DEFAULT_ATOL;
    double rtol = tt::constants::DEFAULT_RTOL;
    double check_pct = tt::constants::DEFAULT_PCT_MATCHED;
    double check_pcc = tt::constants::DEFAULT_PCC_THRESH;
    std::unordered_set<std::string> check_dims_only_names = {};
    std::unordered_set<std::string> check_dims_only_types = {};
};

void print_tensor_dims(const tt_tensor &tensor);
bool are_tensor_dims_equal(const tt_tensor &lhs, const tt_tensor &rhs);
bool operator==(const tt_tensor &lhs, const tt_tensor &rhs);
bool operator!=(const tt_tensor &lhs, const tt_tensor &rhs);
bool are_tensors_equal(const tt_tensor &lhs, const tt_tensor &rhs);
bool allclose(const tt_tensor &lhs, const tt_tensor &rhs);
bool allclose_hw_check(const tt_tensor &lhs, const tt_tensor &rhs, const tt_comparison_config &comp, bool print_diffs=false);

enum class tt_hex_type
{
    Nrisc, Brisc, Trisc0, Trisc1, Trisc2, Blob, RuntimeConfig, Misc, Erisc
};

class tt_hex : public tt_dram_resident
{
    public:
    vector<uint32_t> hex_vec;
    vector<uint32_t> rd_hex_vec;
    tt_hex_type type;
    tt_core *creating_core;
    string name;

    // stores the core x-y location on the noc/routing grid of cores. Sometimes referred to as the physical location
    // - Should be populated this for cores, like ethernet or router-only, that do not live on the logical grid
    // - We may wish to later also populate this also when creating a `tt_hex` object for a logical core (in that case
    //   we can populate both the `creating_core` and the `associated_routing_core`), but that usage currently isn't
    //   implemented
    tt_xy_pair associated_routing_core;
    uint32_t base_addr;

    tt_hex(vector<uint32_t> arr, tt_hex_type itype, const tt_xy_pair &routing_core, string hex_name = "");
    tt_hex(vector<uint32_t> arr, tt_hex_type itype, tt_core *core_ptr, string hex_name = "");
    tt_hex(const tt_hex& ihex);
    void add_hex_array(vector<uint32_t> arr);
    void print_content(string name);

    tt_hex();
    ~tt_hex();
};

// We need to pass the output dir to gstate so that it is available to various components of the compiler and env,
// so they have a location to dump output files to. That requires every test to be changed... In the meantime,
// we'll set a global variable and have gstate fall back to this if one isn't set explicitly
// This value will be set by get_output_dir in utils.hpp
extern std::string g_output_dir;

// Dump functions
void save_global_state_ops_to_csv(const std::string& file_name, const vector<vector<tt_op*>> op_list);

