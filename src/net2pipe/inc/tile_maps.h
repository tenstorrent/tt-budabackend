// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <assert.h>
#include <cstdint>
#include <climits>

#include "tile_maps_common.h"
#include "net2pipe_logger.h"

class tile_to_core_index_map {
public:
  int core_r;
  int core_c;
  int tile_index;
  bool is_padding_tile = false;
  tile_to_core_index_map(int r, int c, int index) : core_r(r), core_c(c), tile_index(index) {};
  tile_to_core_index_map() : core_r(-1), core_c(-1), tile_index(-1) {};
  bool tile_is_continuous(const tile_to_core_index_map& next) {
    return
      ((this->core_r == next.core_r) &&
      (this->core_c == next.core_c) &&
      ((this->tile_index+1) == next.tile_index)) or (this->is_padding_tile and next.is_padding_tile);
  }
  bool operator == (const tile_to_core_index_map& other) {
    return
      (this->core_r == other.core_r) &&
      (this->core_c == other.core_c) &&
      (this->tile_index == other.tile_index);     
  }
  bool operator != (const tile_to_core_index_map& other) {
    return
      (this->core_r != other.core_r) ||
      (this->core_c != other.core_c) ||
      (this->tile_index != other.tile_index);     
  }
};


class phase_pipe_tile_map {
public:
  std::vector<tile_to_core_index_map> tile_map;
  std::vector<std::pair<int, int>> dest_cores;
  std::vector<bool> padding_output_list;
  bool dest_mcast;
  bool check_tile_map_periodic(int consumer_tile_clear_granularity, int& period, int& periodic_repeat);
  bool validate_padding();
};


class consumer_to_producer_tile_map {
public:
  consumer_to_producer_tile_map() { }
  consumer_to_producer_tile_map(int producer_num_cores_r, int producer_num_cores_c,
                                int consumer_num_cores_r, int consumer_num_cores_c) {
    this->producer_num_cores_r = producer_num_cores_r;
    this->producer_num_cores_c = producer_num_cores_c; 
    this->consumer_num_cores_r = consumer_num_cores_r;
    this->consumer_num_cores_c = consumer_num_cores_c;
  }
  std::map<int, phase_pipe_tile_map> pipes;
  int scatter_granularity;
  bool producer_tiles_out_of_order;
  int producer_num_cores_r;
  int producer_num_cores_c;
  int consumer_num_cores_r;
  int consumer_num_cores_c;
  void check_producer_tiles_out_of_order(int producer_grid_r, int producer_grid_c, int producer_mblock_tiles);
  void apply_stack(int c_stack_factor, int r_stack_factor, bool row_major_stack, int c_stack_padding, int r_stack_padding);
  void print();
  int max_producer_core_fan_out();
  int max_producer_core_phases(int consumer_tile_clear_granularity = 1, bool producer_output_single_buffer = false);
  int max_dram_queue_input_indexes(int consumer_tile_clear_granularity = 1);
  int max_consumer_core_phases();
};


class three_d_array_tile_src_map {
  
protected:
  
  std::map <int, std::map <int, std::map<int, tile_to_core_index_map> > > tile_map;
  std::map <map_dims, int> dim_size_map;
  
  std::string producer_name;
  std::string consumer_name;
  int r_stack_factor;
  int c_stack_factor;
  int r_padding_tiles;
  int c_padding_tiles;
  int r_stack_padding;
  int c_stack_padding;
  bool row_major_stack;
  int r_slice_factor;
  int c_slice_factor;
  int r_bcast_factor;
  int c_bcast_factor;
  bool tm_has_transpose;
  int producer_output_buf_size_t;
  bool multi_t_reach_stack;
  int adjusted_slice_factor;

  std::vector<std::string> tm_sequence;
  std::vector<std::vector<int>> tm_args_sequence;
    
  tile_to_core_index_map get_tile_map(int t, int rt, int ct, int input_index = 0);

  inline std::string dim_to_str(int t, int rt, int ct) {
    std::stringstream result;
    result << "t = " + std::to_string(t) + ", rt = " + std::to_string(rt) + ", ct = " + std::to_string(ct);
    return result.str();
  }

  inline void tm_assert(bool condition, std::string error_msg) {
    if (!condition) {
      std::stringstream full_err_msg;
      full_err_msg << "TM ERROR (producer = " << this->producer_name << ", consumer = " << this->consumer_name << "): " << error_msg << std::endl;
      throw std::runtime_error(full_err_msg.str());                   
    }
  }

  inline bool tm_is_slice(std::string tm_name) {
    return (tm_name == "vslice") || (tm_name == "hslice");
  }

  three_d_array_tile_src_map copy_with_clear_tile_map() {
    three_d_array_tile_src_map result = *this;
    result.tile_map.clear();
    return result;
  }

  three_d_array_tile_src_map tile_transpose();
  three_d_array_tile_src_map vslice(int factor);
  three_d_array_tile_src_map hslice(int factor);
  three_d_array_tile_src_map vstack(int factor);
  three_d_array_tile_src_map hstack(int factor);
  three_d_array_tile_src_map broadcast(map_dims dimension, int factor);

  bool need_phased_stack();

  void check_phased_stack(int& effective_c_stack_factor, int& effective_r_stack_factor,
                          int consumer_ublock_tiles_r, int consumer_ublock_tiles_c,
                          int consumer_mblock_ublocks_r, int consumer_mblock_ublocks_c,
                          int consumer_num_cores_r, int consumer_num_cores_c,
                          bool consumer_row_major_ublock_scan_order);

public:

  class data_format {
  public:
    int ublock_tiles_r;
    int ublock_tiles_c;
    int mblock_ublocks_m;
    int mblock_ublocks_n;
    int num_cores_r;
    int num_cores_c;
    int t;
    bool row_major_ublock_scan_order;
    
    data_format(int ublock_tiles_r, int ublock_tiles_c,
                int mblock_ublocks_m, int mblock_ublocks_n,
                int num_cores_r, int num_cores_c,
                int t, bool row_major_ublock_scan_order) :
      ublock_tiles_r(ublock_tiles_r), ublock_tiles_c(ublock_tiles_c),
      mblock_ublocks_m(mblock_ublocks_m), mblock_ublocks_n(mblock_ublocks_n),
      num_cores_r(num_cores_r), num_cores_c(num_cores_c), t(t),
      row_major_ublock_scan_order(row_major_ublock_scan_order) {}
    
    data_format() :
      ublock_tiles_r(-1), ublock_tiles_c(-1),
      mblock_ublocks_m(-1), mblock_ublocks_n(-1),
      num_cores_r(-1), num_cores_c(-1), t(-1) {}
    
    int r_size_tiles() { return num_cores_r*mblock_ublocks_m*ublock_tiles_r; }
    int c_size_tiles() { return num_cores_c*mblock_ublocks_n*ublock_tiles_c; }
    int mblock_size_tiles() { return mblock_ublocks_n*ublock_tiles_c*mblock_ublocks_m*ublock_tiles_r; }
    int ublock_size_tiles() { return ublock_tiles_c*ublock_tiles_r; }
    int mblock_r_size_tiles() { return mblock_ublocks_m*ublock_tiles_r; }
    int mblock_c_size_tiles() { return mblock_ublocks_n*ublock_tiles_c; }
    int total_size_tiles() { return this->r_size_tiles() * this->c_size_tiles() * this->t; }

    std::string to_string();

    bool operator == (const data_format& other) {
      return
        (this->ublock_tiles_r == other.ublock_tiles_r) &&
        (this->ublock_tiles_c == other.ublock_tiles_c) &&
        (this->mblock_ublocks_m == other.mblock_ublocks_m) &&
        (this->mblock_ublocks_n == other.mblock_ublocks_n) &&
        (this->num_cores_r == other.num_cores_r) &&
        (this->num_cores_c == other.num_cores_c) &&
        (this->t == other.t) &&
        (this->row_major_ublock_scan_order == other.row_major_ublock_scan_order);      
    }
    
  };

  data_format producer_data_format;

  three_d_array_tile_src_map() {}

  three_d_array_tile_src_map(std::string producer_name,
                             std::string consumer_name,
                             int t_dim,
                             int ublock_tiles_r, int ublock_tiles_c,
                             int mblock_ublocks_m, int mblock_ublocks_n,
                             int num_cores_r, int num_cores_c,
                             int producer_output_buf_size_t,
                             bool producer_row_major_ublock_scan_order);

  three_d_array_tile_src_map reblock_with_t(int consumer_t, int consumer_rt, int consumer_ct);

  consumer_to_producer_tile_map get_embedding_table_input(int consumer_num_cores_r, int consumer_num_cores_c,
                                                          int indexes_per_input);

  consumer_to_producer_tile_map get_untilized_input(int consumer_num_cores_r, int consumer_num_cores_c,
                                                    int indexes_per_input);

  consumer_to_producer_tile_map get_op_eltwise_input(int kernel_bcast_tiles, bool kernel_bcast_tiles_per_t,
                                                     int consumer_t,
                                                     int consumer_ublock_tiles_r, int consumer_ublock_tiles_c,
                                                     int consumer_mblock_ublocks_m, int consumer_mblock_ublocks_n,
                                                     int consumer_num_cores_r, int consumer_num_cores_c,
                                                     bool consumer_row_major_ublock_scan_order);

  consumer_to_producer_tile_map get_op_matmul_row_input(int kernel_bcast_tiles, bool kernel_bcast_tiles_per_t,
                                                        int consumer_t,
                                                        int consumer_ublock_tiles_r, int consumer_ublock_tiles_k,
                                                        int consumer_mblock_ublocks_m, int consumer_row_input_block_ublocks_k,
                                                        int consumer_num_cores_r, int consumer_num_cores_c,                                                        
                                                        bool consumer_row_major_ublock_scan_order = false);

  consumer_to_producer_tile_map get_op_matmul_col_input(int kernel_bcast_tiles, bool kernel_bcast_tiles_per_t,
                                                        int consumer_t,
                                                        int consumer_ublock_tiles_k, int consumer_ublock_tiles_c,
                                                        int consumer_col_input_block_ublocks_k, int consumer_mblock_ublocks_n,
                                                        int consumer_num_cores_r, int consumer_num_cores_c,
                                                        bool consumer_row_major_ublock_scan_order = true);
  
  std::tuple<consumer_to_producer_tile_map, consumer_to_producer_tile_map>
  get_prolog_matmul_col_input(int kernel_bcast_tiles, bool kernel_bcast_tiles_per_t,
                              int consumer_t,
                              int consumer_ublock_tiles_k, int consumer_mblock_ublocks_k,
                              int consumer_ublock_tiles_c,
                              int consumer_mblock_ublocks_n,
                              int consumer_num_cores_r, int consumer_num_cores_c);

  int get_size(map_dims dim) {
    return this->dim_size_map[dim];
  }
    
  void get_val(int t, int rt, int ct, int& core_r, int& core_c, int& tile_index) {
    assert(t < get_size(map_dims::t) && rt < get_size(map_dims::rt) && ct < get_size(map_dims::ct));
    tile_to_core_index_map tm = this->tile_map[t][rt][ct];
    core_r = tm.core_r;
    core_c = tm.core_c;
    tile_index = tm.tile_index;
  }

  three_d_array_tile_src_map apply_tm(const std::string tm_name, const std::vector<int>& tm_args);

  three_d_array_tile_src_map pad(int rt, int ct);
  three_d_array_tile_src_map unpad(int rt, int ct);
  
  void print();

  static inline bool divisible_either_direction(int a, int b) {
    return ((a % b) == 0) || ((b % a) == 0);
  }

  static inline int int_div_with_ceil(int a, int b) {
    return (a / b) + ((a % b) ? 1 : 0);
  }

  void estimate_resource_usage(OpClass consumer_op_class, int input_id,
                               int consumer_t,
                               // consumer grid & output block dimensions (for eltwise, same as input dimensions)
                               int consumer_grid_size_r, int consumer_grid_size_c,
                               int consumer_mblock_ublocks_m, int consumer_mblock_ublocks_n, 
                               int consumer_ublock_tiles_r, int consumer_ublock_tiles_c,
                               // k = inner dimension for matmul input blocks, don't-care for eltwise (includes full row/column)                        
                               int consumer_mblock_ublocks_k, int consumer_ublock_tiles_k,
                               bool consumer_row_major_ublock_scan_order,
                               // function return values:
                               int& producer_max_fork_streams_used_per_core,
                               int& producer_max_scatter_stream_phases_used_per_core,
                               int& consumer_max_gather_stream_phases_used_per_core);

  void estimate_dram_input_perf_resource_usage(OpClass consumer_op_class, int input_id,
                                               int consumer_t,
                                               // consumer grid & output block dimensions (for eltwise, same as input dimensions)
                                               int consumer_grid_size_r, int consumer_grid_size_c,
                                               int consumer_mblock_ublocks_m, int consumer_mblock_ublocks_n, 
                                               int consumer_ublock_tiles_r, int consumer_ublock_tiles_c,
                                               // k = inner dimension for matmul input blocks, don't-care for eltwise (includes full row/column)                        
                                               int consumer_mblock_ublocks_k, int consumer_ublock_tiles_k,
                                               bool consumer_row_major_ublock_scan_order,
                                               // function return values:
                                               int& producer_contiguous_tiles_read,
                                               int& consumer_core_pipe_inputs);

};
