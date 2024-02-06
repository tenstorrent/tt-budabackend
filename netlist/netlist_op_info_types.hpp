// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <ostream>
#include <string>

#include "common/base.hpp"
#include "model/tensor.hpp"
#include "netlist_basic_info_types.hpp"
#include "netlist_fused_op_info_types.hpp"

using namespace tt;

struct tt_splice_info {
  int index = -1;
  int length = -1;
  int stride = -1;
};
ostream& operator<<(ostream& out, const tt_splice_info& t);
void verify(const tt_splice_info& t);

struct ethernet_datacopy_attributes {
  std::vector<int> ingress_channels;
  std::vector<int> egress_channels;
  int dest_device;
};

struct tt_input_pad_info {
  int rt = -1;
  int ct = -1;
  float pad_value = 0.0;
  bool tm_padding = false;
  bool default_padding = false;
};

// ostream& operator<<(ostream& out, const tt_input_pad_info& t);
// void verify(const tt_input_pad_info& t);

enum class TopKSort {
    None,
    Max,
    ArgMax
};

struct tt_op_attributes {
    // General attribute
    string op_type = "";
    //vector<bool> single_tile = {}; // tenstorrent/budabackend#863
    vector<std::pair<int, bool>> kernel_broadcast = {}; // tenstorrent/budabackend#863, also 2182
    // Normal Matmul Attributes
    int m_k = 0;
    int u_kt = 0;
    bool accumulate = false;
    bool bias = false;
    bool l1_acc = false;
    SfpuOp sfpu_op = SfpuOp::Invalid;
    std::vector<bool> min_input_buffer = {false, false};
    bool requant = false; //int32 matmul 
    bool dequant = false; //int32 matmul 
    float zero_point = 0.0f;
    // SFPU attributes
    Dim vector_mode = Dim::Invalid; // Default is invalid, is set to proper value later in the pipeline.
    bool approximate_mode = false;
    SfpuExecutionThread sfpu_execution_thread = SfpuExecutionThread::Default;

    // Relu attributes
    bool relu_en = false;
    float relu_threshold = 0.0f;
    ReluMode relu_mode = ReluMode::None;
    // Dropout related attributes
    float probability = 0.0f;
    float scale = 0.0f;
    int seed = 0;
    // Power related attributes
    int exponent = 0;
    // LRelu related attributes
    float slope = 0.01; // Default for pytorch
    // Reduce related attributes
    Dim reduce_dim = Dim::Invalid;
    int z = 0;  // Reduce z-dim
    ReduceFunc reduce_type = ReduceFunc::Invalid;
    // Nary Related Attributes
    std::vector<tt_splice_info> splice_infos = {};
    SpliceMode splice_mode = SpliceMode::Ublock;
    // Embedding Related Attributes
    int num_indices = 0;
    // Matmul Identity related attributes
    bool identity = false;
    int num_index_tiles = 0;
    int num_sparse_tiles = 0;
    int num_columns = 0;
    int sparse_tile_ptr_bits = 0;
    int sparse_ublock_idx_bits = -1;
    int indices_len = 0;
    int fracture_factor = 1;
    int starting_row = 0;
    int act_t = 0;
    vector <int> num_nz_strips = {0}; // also used by buffer matmul ident op
    // Fused Op related attributes
    string fused_op_id = "";
    string fused_op_output = "";   
    std::set <string> fused_op_inputs_to_pop = {};
    std::set <string> fused_op_inputs_to_pop_last = {};

    // Transpose YZ attributes
    int z_per_slice = 32;
    int y_last_slice = 32;
    
    // Ethernet Datacopy
    ethernet_datacopy_attributes ethernet_datacopy_attr = {};

    //Stochastic rounding
    StochRndMode stoch_rnd_mode = StochRndMode::None;

    // TopK attributes
    int k = 0;
    TopKSort sort = TopKSort::None;
};
ostream& operator<<(ostream& out, const tt_op_attributes& t);
void verify(const tt_op_attributes& t);

struct tt_op_info {
    string name = "";
    string type = "";
    std::vector<int> grid_loc = {};
    tt::tt_grid_shape grid_size = {};
    std::vector<string> input_names = {};
    std::vector<bool> is_input_queue = {};
    std::vector<TileDim> input_tile_dims = {};
    std::unordered_map<string, string> forked_dram_input_names = {};
    std::vector<DataFormat> input_data_formats = {};
    DataFormat intermed_data_format = DataFormat::Invalid;
    DataFormat dest_accumulate_data_format = DataFormat::Invalid;
    DataFormat output_data_format = DataFormat::Invalid;
    MathFidelity math_fidelity = MathFidelity::Invalid;

    tt_dim_info output_dim;
    TileDim output_tile_dim;
    int buf_size_mb = -1;
    int overlay_size = -1;
    std::vector<int> input_buf_min_size_tiles = {};
    std::vector<int> input_dram_io_buf_size_tiles = {};

    bool has_queue_inputs = false;
    bool untilize_output = false;
    bool gradient_op = false;
    bool transpose = false;
    bool grid_transpose = false; // Feature requested in issue 736

    tt_op_attributes attributes;
    // input key: vector of tm with it's paired vector of args.
    tt_tm_info input_tm_ops = {};
 
    //*** This is not meant to be parsed at the parser level.  A container for when we derive the subsequent ops in the fused op
    tt_fused_op_info fused_op_info = {};
    //*** This is meant to house data about the inputs which is derived and not parsed directly during netlist_parser
    std::vector<tt_dim_info> input_dims = {}; 
    std::vector<tt::tt_grid_shape> input_core_grids = {};

    // Grid size along the op r/c directions.
    int input_grid_size_logical_r(const int input_index) const { return input_core_grids.at(input_index).at(0); }
    int input_grid_size_logical_c(const int input_index) const { return input_core_grids.at(input_index).at(1); }

    std::shared_ptr<tt_op> my_op;
    std::shared_ptr<tt_tensor> op_output_tensor_ptr;

    //*** This is derived from device info and not set directly from netlist_parser
    tt::ARCH arch_name = tt::ARCH::Invalid;

    // FIXME imatosevic - functions below need to be changed if ops ever wrap around the ring

    // Starting grid location of the op. It doesn't depend on grid_transpose.
    int grid_loc_x() const { return grid_loc.at(1); }
    int grid_loc_y() const { return grid_loc.at(0); }

    // Grid size along the NOC0 y/x dimensions. Aligned with the op r/c directions
    // if grid_transpose=0, otherwise aligned with c/r. 
    int grid_size_x() const { return grid_transpose ? grid_size.at(0) : grid_size.at(1); }
    int grid_size_y() const { return grid_transpose ? grid_size.at(1) : grid_size.at(0); }
  
    // Original grid size along the NOC0 y/x dimensions - without applying grid_transpose attribute.
    int original_grid_size_x() const { return grid_size.at(1); }
    int original_grid_size_y() const { return grid_size.at(0); }

    // Grid end coordinates (opposite corner of the starting grid coordinates),
    // in terms of NOC0 logical coordinates (i.e., with Tensix grid starting at (0,0)).
    int grid_end_x() const { return grid_loc_x() + grid_size_x() - 1; }
    int grid_end_y() const { return grid_loc_y() + grid_size_y() - 1; }

    // Grid size along the op r/c directions.
    int grid_size_logical_r() const { return grid_size.at(0); }
    int grid_size_logical_c() const { return grid_size.at(1); }

    // For core at (r,c) in the op grid, returns the location in terms of NOC0
    // logical coordinates (i.e., with Tensix grid starting at (0,0)).
    void get_core_yx_coord (int r_logical, int c_logical, int& y_coord, int& x_coord) const {
      y_coord = grid_loc.at(0) + (grid_transpose ? c_logical : r_logical);
      x_coord = grid_loc.at(1) + (grid_transpose ? r_logical : c_logical);
    }

    std::tuple<int, int> get_core_yx_coord (int r_logical, int c_logical) const {
      int y_coord;
      int x_coord;
      get_core_yx_coord(r_logical, c_logical, y_coord, x_coord);
      return {y_coord, x_coord};
    }
    
    // Input padding info
    std::vector<tt_input_pad_info> input_padding;
    std::vector<tt_input_pad_info> input_unpadding;

    int input_kernel_broadcasted(int index) const {
      return (this->attributes.kernel_broadcast.size() == 0) ? 0 : this->attributes.kernel_broadcast.at(index).first; 
    }
    bool input_kernel_broadcasted_per_t(int index) const {
      return (this->attributes.kernel_broadcast.size() == 0) ? 0 : this->attributes.kernel_broadcast.at(index).second; 
    }
};
ostream& operator<<(ostream& out, const tt_op_info& t);
void verify(const tt_op_info& t);
void verify_gradient_op(const tt_op_info& t);
int get_ublock_size_tiles(const tt_op_info& queue_info);
int get_mblock_size_tiles(const tt_op_info& queue_info);
