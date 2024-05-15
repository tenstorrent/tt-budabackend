// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "tile_maps.h"
#include <iostream>
#include <cstdio>
#include <numeric>
#include <algorithm>
#include <cmath>

three_d_array_tile_src_map::three_d_array_tile_src_map(std::string producer_name,
                                                       std::string consumer_name,
                                                       int t_dim,
                                                       int ublock_tiles_r, int ublock_tiles_c,
                                                       int mblock_ublocks_m, int mblock_ublocks_n,
                                                       int num_cores_r, int num_cores_c,
                                                       int producer_output_buf_size_t,
                                                       bool producer_row_major_ublock_scan_order) {

  assert(t_dim > 0);
  assert(ublock_tiles_r > 0);
  assert(ublock_tiles_c > 0);
  assert(mblock_ublocks_m > 0);
  assert(mblock_ublocks_n > 0);
  assert(num_cores_r > 0);
  assert(num_cores_c > 0);

  this->producer_name = producer_name;
  this->consumer_name = consumer_name;
  this->r_stack_factor = 1;
  this->c_stack_factor = 1;
  this->r_slice_factor = 1;
  this->c_slice_factor = 1;
  this->r_stack_padding = 0;
  this->c_stack_padding = 0;
  this->r_padding_tiles = 0;
  this->c_padding_tiles = 0;
  this->r_bcast_factor = 1;
  this->c_bcast_factor = 1;
  this->tm_has_transpose = false;
  this->producer_output_buf_size_t = producer_output_buf_size_t;
  this->multi_t_reach_stack = false;
  this->adjusted_slice_factor = 1;

  this->tm_assert(divisible_either_direction(producer_output_buf_size_t, t_dim),
                  "producer op has output buffer size in mblocks that is not divisible either way with the output t dimension");

  this->producer_data_format = data_format(ublock_tiles_r, ublock_tiles_c,
                                           mblock_ublocks_m, mblock_ublocks_n,
                                           num_cores_r, num_cores_c, t_dim,
                                           producer_row_major_ublock_scan_order);
  
  int ublock_size_tiles = ublock_tiles_r * ublock_tiles_c;
  int mblock_size_tiles = mblock_ublocks_m * mblock_ublocks_n * ublock_size_tiles;

  int rt = 0;
  int ct = 0;

  this->dim_size_map[map_dims::t] = t_dim;
  this->dim_size_map[map_dims::ct] = ublock_tiles_c * mblock_ublocks_n * num_cores_c;
  this->dim_size_map[map_dims::rt] = ublock_tiles_r * mblock_ublocks_m * num_cores_r;
  
  for(int t = 0; t < t_dim; t++) {
    rt = 0;
    for(int core_r = 0; core_r < num_cores_r; core_r++) {      
      for(int ublock_r = 0; ublock_r < mblock_ublocks_m; ublock_r++) {        
        for(int tile_r = 0; tile_r < ublock_tiles_r; tile_r++) {
          ct = 0;
          for(int core_c = 0; core_c < num_cores_c; core_c++) {
            for(int ublock_c = 0; ublock_c < mblock_ublocks_n; ublock_c++) {
              for(int tile_c = 0; tile_c < ublock_tiles_c; tile_c++) {
                int tile_index;
                if (producer_row_major_ublock_scan_order) {
                  tile_index = t*mblock_size_tiles + ublock_r*(mblock_ublocks_n*ublock_size_tiles) + ublock_c*ublock_size_tiles + tile_r*ublock_tiles_c + tile_c;
                } else {
                  // with column-major scan order of mblocks, tiles are still scanned in row-major order within ublocks
                  tile_index = t*mblock_size_tiles + ublock_c*(mblock_ublocks_m*ublock_size_tiles) + ublock_r*ublock_size_tiles + tile_r*ublock_tiles_c + tile_c;
                }
                this->tile_map[t][rt][ct] = tile_to_core_index_map(core_r, core_c, tile_index);   // for input t, in RM, (rt,ct) is found at (core_r,core_c,tile_index)
                ct++;
              }
            }
          }
          rt++;
        }
      }
    }
  }
}


three_d_array_tile_src_map three_d_array_tile_src_map::vslice(int factor) {

  this->tm_assert((this->dim_size_map[map_dims::rt] % factor) == 0,
                  "vslice called with non-divisible factor = " + std::to_string(factor) + ", rt = " + std::to_string(this->dim_size_map[map_dims::rt]));

  three_d_array_tile_src_map result = this->copy_with_clear_tile_map();

  result.r_slice_factor *= factor;
  result.adjusted_slice_factor *= factor;
  result.dim_size_map[map_dims::t] *= factor;
  result.dim_size_map[map_dims::rt] /= factor;

  
  for (int out_t = 0; out_t < result.get_size(map_dims::t); out_t++) {
    for (int out_rt = 0; out_rt < result.get_size(map_dims::rt); out_rt++) {
      for (int out_ct = 0; out_ct < result.get_size(map_dims::ct); out_ct++) {
        int in_t = out_t / factor;
        int in_ct = out_ct;
        int in_rt = (out_t % factor) * result.get_size(map_dims::rt) + out_rt;
        result.tile_map[out_t][out_rt][out_ct] = this->get_tile_map(in_t, in_rt, in_ct);
      }
    }
  }

  return result;
}


three_d_array_tile_src_map three_d_array_tile_src_map::vstack(int factor) {
  this->tm_assert((this->dim_size_map[map_dims::t] % factor) == 0,
    "vstack called with non-divisible factor = " + std::to_string(factor) + ", t = " + std::to_string(this->dim_size_map[map_dims::t]));

  three_d_array_tile_src_map result = this->copy_with_clear_tile_map();

  if ((this->adjusted_slice_factor % factor) == 0) {
    result.adjusted_slice_factor /= factor;
  } else {
    result.adjusted_slice_factor = 1;
    result.multi_t_reach_stack = true;
  }

  result.row_major_stack = (this->c_stack_factor > 1);
  result.r_stack_factor *= factor;
  result.dim_size_map[map_dims::t] /= factor;
  result.dim_size_map[map_dims::rt] *= factor;

  for (int in_t = 0; in_t < this->get_size(map_dims::t); in_t++) {
    for (int in_rt = 0; in_rt < this->get_size(map_dims::rt); in_rt++) {
      for (int in_ct = 0; in_ct < this->get_size(map_dims::ct); in_ct++) {
        int out_t = in_t / factor;
        int out_ct = in_ct;
        int out_rt = (in_t % factor) * this->get_size(map_dims::rt) + in_rt;
        result.tile_map[out_t][out_rt][out_ct] = this->get_tile_map(in_t, in_rt, in_ct);
      }
    }
  }

  return result;
}


three_d_array_tile_src_map three_d_array_tile_src_map::hslice(int factor) {

  this->tm_assert((this->dim_size_map[map_dims::ct] % factor) == 0,
                  "hslice called with non-divisible factor = " + std::to_string(factor) + ", ct = " + std::to_string(this->dim_size_map[map_dims::ct]));

  three_d_array_tile_src_map result = this->copy_with_clear_tile_map();

  result.c_slice_factor *= factor;
  result.adjusted_slice_factor *= factor;
  result.dim_size_map[map_dims::t] *= factor;
  result.dim_size_map[map_dims::ct] /= factor;

  for (int out_t = 0; out_t < result.get_size(map_dims::t); out_t++) {
    for (int out_rt = 0; out_rt < result.get_size(map_dims::rt); out_rt++) {
      for (int out_ct = 0; out_ct < result.get_size(map_dims::ct); out_ct++) {
        int in_t = out_t / factor;
        int in_rt = out_rt;
        int in_ct = (out_t % factor) * result.get_size(map_dims::ct) + out_ct;
        result.tile_map[out_t][out_rt][out_ct] = this->get_tile_map(in_t, in_rt, in_ct);
      }
    }
  }

  return result;
}


three_d_array_tile_src_map three_d_array_tile_src_map::hstack(int factor) {
  this->tm_assert((this->dim_size_map[map_dims::t] % factor) == 0,
    "hstack called with non-divisible factor = " + std::to_string(factor) + ", t = " + std::to_string(this->dim_size_map[map_dims::t]));
  
  three_d_array_tile_src_map result = this->copy_with_clear_tile_map();

  if ((this->adjusted_slice_factor % factor) == 0) {
      result.adjusted_slice_factor /= factor;
  } else {
      result.adjusted_slice_factor = 1;
      result.multi_t_reach_stack = true;
  }

  result.c_stack_factor *= factor;
  result.row_major_stack = (this->r_stack_factor == 1);
  result.dim_size_map[map_dims::t] /= factor;
  result.dim_size_map[map_dims::ct] *= factor;

  for (int in_t = 0; in_t < this->get_size(map_dims::t); in_t++) {
    for (int in_rt = 0; in_rt < this->get_size(map_dims::rt); in_rt++) {
      for (int in_ct = 0; in_ct < this->get_size(map_dims::ct); in_ct++) {
        int out_t = in_t / factor;
        int out_rt = in_rt;
        int out_ct = (in_t % factor) * this->get_size(map_dims::ct) + in_ct;
        result.tile_map[out_t][out_rt][out_ct] = this->get_tile_map(in_t, in_rt, in_ct);
      }
    }
  }

  return result;
}



three_d_array_tile_src_map three_d_array_tile_src_map::broadcast(map_dims dimension, int factor) {

  three_d_array_tile_src_map result = this->copy_with_clear_tile_map();

  if(dimension == map_dims::t) {
    this->tm_assert((factor == 1) || (this->dim_size_map[map_dims::t] == 1),
                    "broadcast t-dim called with input that has t > 1");
    // with t=1, z-bcast is equivalent to r-bcast + vslice or c-bcast + hslicee
    result.r_bcast_factor *= factor;
    result.r_slice_factor *= factor;
    result.adjusted_slice_factor *= factor;
    result.dim_size_map[map_dims::t] *= factor;
  }
  if(dimension == map_dims::rt) {
    result.r_bcast_factor *= factor;
    result.dim_size_map[map_dims::rt] *= factor;
  }
  if(dimension == map_dims::ct) {
    result.c_bcast_factor *= factor;
    result.dim_size_map[map_dims::ct] *= factor;
  }
 
  for (int out_t = 0; out_t < result.get_size(map_dims::t); out_t++) {
    for (int out_rt = 0; out_rt < result.get_size(map_dims::rt); out_rt++) {
      for (int out_ct = 0; out_ct < result.get_size(map_dims::ct); out_ct++) {
        int in_t = (out_t % this->get_size(map_dims::t));
        int in_rt = (out_rt % this->get_size(map_dims::rt));
        int in_ct = (out_ct % this->get_size(map_dims::ct));
        result.tile_map[out_t][out_rt][out_ct] = this->get_tile_map(in_t, in_rt, in_ct);
      }
    }
  }

  return result;
}

three_d_array_tile_src_map three_d_array_tile_src_map::pad(int padding_rt, int padding_ct) {
  three_d_array_tile_src_map result = this->copy_with_clear_tile_map();
  result.dim_size_map[map_dims::rt] += padding_rt;
  result.dim_size_map[map_dims::ct] += padding_ct;

  auto padding_tile = tile_to_core_index_map();
  padding_tile.is_padding_tile = true;
  for (int out_t = 0; out_t < result.get_size(map_dims::t); out_t++) {
    for (int out_rt = 0; out_rt < result.get_size(map_dims::rt); out_rt++) {
      for (int out_ct = 0; out_ct < result.get_size(map_dims::ct); out_ct++) {;
        if(out_rt < dim_size_map[map_dims::rt] and out_ct < dim_size_map[map_dims::ct]) {
          result.tile_map[out_t][out_rt][out_ct] = this->get_tile_map(out_t, out_rt, out_ct);
        }
        else {
          result.tile_map[out_t][out_rt][out_ct] = padding_tile;
        }
      }
    }
  }

  result.r_padding_tiles = padding_rt;
  result.c_padding_tiles = padding_ct;

  return result;
}

three_d_array_tile_src_map three_d_array_tile_src_map::unpad(int padding_rt, int padding_ct) {
  three_d_array_tile_src_map result = this->copy_with_clear_tile_map();
  result.dim_size_map[map_dims::rt] -= padding_rt;
  result.dim_size_map[map_dims::ct] -= padding_ct;

  for (int out_t = 0; out_t < result.get_size(map_dims::t); out_t++) {
    for (int out_rt = 0; out_rt < result.get_size(map_dims::rt); out_rt++) {
      for (int out_ct = 0; out_ct < result.get_size(map_dims::ct); out_ct++) {;
        result.tile_map[out_t][out_rt][out_ct] = this->get_tile_map(out_t, out_rt, out_ct);
      }
    }
  }
  return result;
}

three_d_array_tile_src_map three_d_array_tile_src_map::tile_transpose() {

  three_d_array_tile_src_map result = this->copy_with_clear_tile_map();

  result.dim_size_map[map_dims::ct] = this->dim_size_map[map_dims::rt];
  result.dim_size_map[map_dims::rt] = this->dim_size_map[map_dims::ct];
  result.tm_has_transpose = true;

  for (int out_t = 0; out_t < result.get_size(map_dims::t); out_t++) {
    for (int out_rt = 0; out_rt < result.get_size(map_dims::rt); out_rt++) {
      for (int out_ct = 0; out_ct < result.get_size(map_dims::ct); out_ct++) {
        int in_t = out_t;
        int in_rt = out_ct;
        int in_ct = out_rt;
        result.tile_map[out_t][out_rt][out_ct] = this->get_tile_map(in_t, in_rt, in_ct);
      }
    }
  }

  return result;  
}


three_d_array_tile_src_map
three_d_array_tile_src_map::reblock_with_t(int consumer_t, int consumer_rt, int consumer_ct) {

  int total_size_tiles = this->dim_size_map[map_dims::t] * this->dim_size_map[map_dims::rt] * this->dim_size_map[map_dims::ct];
  int consumer_total_size_tiles = consumer_t * consumer_rt * consumer_ct;

  this->tm_assert(total_size_tiles == consumer_total_size_tiles,
                  "reblocking with t change called with incompatible consumer dimensions (total tiles mismatch):  " +
                  dim_to_str(consumer_t, consumer_rt, consumer_ct) + 
                  ", applied to format with dimensions: " +
                  dim_to_str(this->dim_size_map[map_dims::t], this->dim_size_map[map_dims::rt], this->dim_size_map[map_dims::ct]));

  three_d_array_tile_src_map result = *this;

  if (consumer_rt < this->dim_size_map[map_dims::rt]) {
    this->tm_assert((this->dim_size_map[map_dims::rt] % consumer_rt) == 0,
                  "reblocking with t change called with incompatible consumer dimensions (rt not divisible):  " +
                  dim_to_str(consumer_t, consumer_rt, consumer_ct) + 
                  ", applied to format with dimensions: " +
                  dim_to_str(this->dim_size_map[map_dims::t], this->dim_size_map[map_dims::rt], this->dim_size_map[map_dims::ct]));
    int vslice_factor = this->dim_size_map[map_dims::rt] / consumer_rt;
    result = result.vslice(vslice_factor);
  }
  if (consumer_ct < this->dim_size_map[map_dims::ct]) {
    this->tm_assert((this->dim_size_map[map_dims::ct] % consumer_ct) == 0,
                  "reblocking with t change called with incompatible consumer dimensions (ct not divisible):  " +
                  dim_to_str(consumer_t, consumer_rt, consumer_ct) + 
                  ", applied to format with dimensions: " +
                  dim_to_str(this->dim_size_map[map_dims::t], this->dim_size_map[map_dims::rt], this->dim_size_map[map_dims::ct]));
    int hslice_factor = this->dim_size_map[map_dims::ct] / consumer_ct;
    result = result.hslice(hslice_factor);
  }
  if (consumer_ct > this->dim_size_map[map_dims::ct]) {
    this->tm_assert((consumer_ct % this->dim_size_map[map_dims::ct]) == 0,
                  "reblocking with t change called with incompatible consumer dimensions (ct not divisible):  " +
                  dim_to_str(consumer_t, consumer_rt, consumer_ct) + 
                  ", applied to format with dimensions: " +
                  dim_to_str(this->dim_size_map[map_dims::t], this->dim_size_map[map_dims::rt], this->dim_size_map[map_dims::ct]));
    int hstack_factor = consumer_ct / this->dim_size_map[map_dims::ct];
    result = result.hstack(hstack_factor);
  }
  if (consumer_rt > this->dim_size_map[map_dims::rt]) {
    this->tm_assert((consumer_rt % this->dim_size_map[map_dims::rt]) == 0,
                  "reblocking with t change called with incompatible consumer dimensions (rt not divisible):  " +
                  dim_to_str(consumer_t, consumer_rt, consumer_ct) + 
                  ", applied to format with dimensions: " +
                  dim_to_str(this->dim_size_map[map_dims::t], this->dim_size_map[map_dims::rt], this->dim_size_map[map_dims::ct]));
    int vstack_factor = consumer_rt / this->dim_size_map[map_dims::rt];
    result = result.vstack(vstack_factor);
  }

  return result;  
}



bool three_d_array_tile_src_map::need_phased_stack() {
  if (this->multi_t_reach_stack) {
    int total_stack_factor = this->r_stack_factor * this->c_stack_factor;
    bool can_stack_from_single_producer_output_buf = 
      (total_stack_factor == 1) ||
      ((this->producer_output_buf_size_t % total_stack_factor) == 0) ||
      ((this->producer_output_buf_size_t % this->producer_data_format.t) == 0);
    
    return !can_stack_from_single_producer_output_buf;
  } else {
    return false;
  }
}


void three_d_array_tile_src_map::check_phased_stack(int& effective_c_stack_factor, int& effective_r_stack_factor,
                                                    int consumer_ublock_tiles_r, int consumer_ublock_tiles_c,
                                                    int consumer_mblock_ublocks_r, int consumer_mblock_ublocks_c,
                                                    int consumer_num_cores_r, int consumer_num_cores_c,
                                                    bool consumer_row_major_ublock_scan_order) {

  this->tm_assert(this->need_phased_stack(), "this function shouldn't be called unless we need phased stack");
  
  effective_c_stack_factor = 1;
  effective_r_stack_factor = 1;

  bool prev_vstack = false;
  bool prev_hstack = false;
  bool prev_pad = false;
  bool stack_ok_for_single_t_buf = true;
  
  for (const std::string& tm_name : this->tm_sequence) {
    if (tm_name == "hstack") {
      stack_ok_for_single_t_buf &= (!prev_hstack && !prev_pad);
      prev_hstack = true;
    }
    else if (tm_name == "vstack") {
      stack_ok_for_single_t_buf &= (!prev_vstack && !prev_pad);
      prev_vstack = true;
    }
    else if (tm_name == "pad") {
      stack_ok_for_single_t_buf &= (!prev_pad);
      prev_pad = true;
    }
    else {
      stack_ok_for_single_t_buf &= (!(prev_vstack || prev_hstack || prev_pad));
    }
  }

  this->tm_assert(stack_ok_for_single_t_buf, 
                  "TM order does't satisfy constraints for stacking with phased pipes, the number of buffered output mblocks on the producer (not counting double-buffering) must be a multiple of the total stack factor or producer t");

  // Prior to the stacking, how many t's correspond to one t being stacked?
  int total_slice_factor = this->r_slice_factor * this->c_slice_factor;

  n2p::Log() <<
    "TM (producer = " << this->producer_name << ", consumer = " << this->consumer_name <<
    "): vstack_factor = " << this->r_stack_factor << ", hstack_factor = " << this->c_stack_factor <<
    ", vslice_factor = " << this->r_slice_factor << ", hslice_factor = " << this->c_slice_factor <<
    ", row major stack = " << this->row_major_stack << 
    "\n";

  // rt, ct dimensions pre-stacking, post-other TMs
  int producer_block_tiles_pre_stack_r = (this->dim_size_map[map_dims::rt] - this->r_padding_tiles) / this->r_stack_factor;
  int producer_block_tiles_pre_stack_c = (this->dim_size_map[map_dims::ct] - this->c_padding_tiles) / this->c_stack_factor;

  // 2D block of tiles corresponding to one original producer-side t, mapped in the stacking order:
  int  stacking_block_tiles_c;
  int  stacking_block_tiles_r;

  // Producer-side constraints: make sure the stacking factors are aligned with the number of t's stacked
  // that correspond to one original producer-side t. Otherwise pipes will not be periodic. 
  
  if (this->row_major_stack) {
    
    this->tm_assert(divisible_either_direction(total_slice_factor, this->c_stack_factor),
                    "row major stack with c stack factor = " + std::to_string(this->c_stack_factor) +
                    " not divisible in either direction with preceding total slice factor = " + std::to_string(total_slice_factor));
    
    if ((total_slice_factor % this->c_stack_factor) == 0) {
      
      stacking_block_tiles_c = producer_block_tiles_pre_stack_c * this->c_stack_factor;
      effective_c_stack_factor = 1;

      int total_slice_factor_r_stack_cover = total_slice_factor/this->c_stack_factor;
      
      this->tm_assert(divisible_either_direction(total_slice_factor_r_stack_cover, this->r_stack_factor),
                      "row major stack with c stack factor = " + std::to_string(this->c_stack_factor) +
                      " and total slice factor = " + std::to_string(this->c_stack_factor) + " x " + std::to_string(total_slice_factor_r_stack_cover) +
                      " -> the second factor is not divisible in either direction with r stack factor = " + std::to_string(this->r_stack_factor));

      if ((total_slice_factor_r_stack_cover % this->r_stack_factor) == 0) {
        stacking_block_tiles_r = producer_block_tiles_pre_stack_r * this->r_stack_factor;
        effective_r_stack_factor = 1;
      }
      else {
        stacking_block_tiles_r = producer_block_tiles_pre_stack_r * total_slice_factor_r_stack_cover;
        effective_r_stack_factor = this->r_stack_factor/total_slice_factor_r_stack_cover;
      }
      
    } else { // this->c_stack_factor % total_slice_factor == 0
      
      stacking_block_tiles_c = producer_block_tiles_pre_stack_c * total_slice_factor;
      effective_c_stack_factor = this->c_stack_factor / total_slice_factor;
      stacking_block_tiles_r = producer_block_tiles_pre_stack_r;
      effective_r_stack_factor = this->r_stack_factor;
    }

    this->tm_assert(divisible_either_direction(effective_c_stack_factor, this->producer_output_buf_size_t),
                                         "row-major stack: effective c stack factor not divisible either way with producer output buffer size mblocks");
    if (effective_c_stack_factor > this->producer_output_buf_size_t) {
      effective_c_stack_factor /= this->producer_output_buf_size_t;
    } else {
      effective_c_stack_factor = 1;
      this->tm_assert(divisible_either_direction(effective_r_stack_factor, this->producer_output_buf_size_t / effective_c_stack_factor),
                                           "row-major stack: effective stack factors not divisible with producer output buffer size mblocks");
      if (effective_r_stack_factor > (this->producer_output_buf_size_t / effective_c_stack_factor)) {
        effective_r_stack_factor /= (this->producer_output_buf_size_t / effective_c_stack_factor);
      } else {
        effective_r_stack_factor = 1;
      }
    }
                    
  }
  else { // col major stack
    
   this->tm_assert(divisible_either_direction(total_slice_factor, this->r_stack_factor),
                    "column major stack with r stack factor = " + std::to_string(this->r_stack_factor) +
                    " not divisible in either direction with preceding total slice factor = " + std::to_string(total_slice_factor));
   
    if ((total_slice_factor % this->r_stack_factor) == 0) {

      stacking_block_tiles_r = producer_block_tiles_pre_stack_r * this->r_stack_factor;
      effective_r_stack_factor = 1;

      int total_slice_factor_c_stack_cover = total_slice_factor/this->r_stack_factor;
      
      this->tm_assert(divisible_either_direction(total_slice_factor_c_stack_cover, this->c_stack_factor),
                      "column major stack with r stack factor = " + std::to_string(this->r_stack_factor) +
                      " and total slice factor = " + std::to_string(this->r_stack_factor) + " x " + std::to_string(total_slice_factor_c_stack_cover) +
                      " -> the second factor is not divisible in either direction with c stack factor = " + std::to_string(this->c_stack_factor));      

      if ((total_slice_factor_c_stack_cover % this->c_stack_factor) == 0) {
        stacking_block_tiles_c = producer_block_tiles_pre_stack_c * this->c_stack_factor;
        effective_c_stack_factor = 1;
      }
      else {
        stacking_block_tiles_c = producer_block_tiles_pre_stack_c * total_slice_factor_c_stack_cover;
        effective_c_stack_factor = this->c_stack_factor/total_slice_factor_c_stack_cover;
      }
      
    } else { // this->r_stack_factor % total_slice_factor == 0
      
      stacking_block_tiles_r = producer_block_tiles_pre_stack_r * total_slice_factor;
      effective_r_stack_factor = this->r_stack_factor / total_slice_factor;
      stacking_block_tiles_c = producer_block_tiles_pre_stack_c;
      effective_c_stack_factor = this->c_stack_factor;
    }

    this->tm_assert(divisible_either_direction(effective_r_stack_factor, this->producer_output_buf_size_t),
                    "row-major stack: effective r stack factor not divisible either way with producer output buffer size mblocks");
    if (effective_r_stack_factor > this->producer_output_buf_size_t) {
      effective_r_stack_factor /= this->producer_output_buf_size_t;
    } else {
      effective_r_stack_factor = 1;
      this->tm_assert(divisible_either_direction(effective_c_stack_factor, this->producer_output_buf_size_t / effective_r_stack_factor),
                                           "row-major stack: effective stack factors not divisible with producer output buffer size mblocks");
      if (effective_c_stack_factor > (this->producer_output_buf_size_t / effective_r_stack_factor)) {
        effective_c_stack_factor /= (this->producer_output_buf_size_t / effective_r_stack_factor);
      } else {
        effective_c_stack_factor = 1;
      }
    }

  }

  n2p::Log() << "  -> effective_c_stack_factor = " << effective_c_stack_factor << ", effective_r_stack_factor = " << effective_r_stack_factor << "\n";
  n2p::Log() << "  -> stacking_block_tiles_c = " << stacking_block_tiles_c << ", stacking_block_tiles_r = " << stacking_block_tiles_r << "\n";
  n2p::Log() << "  -> padding_ct = " << this->c_padding_tiles << ", padding_rt = " << this->r_padding_tiles << "\n";

  // Padding
  if (effective_r_stack_factor > 1) {
    this->tm_assert(this->r_padding_tiles % stacking_block_tiles_r == 0, "Padding rt (" + std::to_string(this->r_padding_tiles) + 
      ") not divisible by stacking block r (" + std::to_string(stacking_block_tiles_r) + ")");
    this->r_stack_padding = this->r_padding_tiles / stacking_block_tiles_r;
    effective_r_stack_factor += this->r_stack_padding;
  }
  else {
    stacking_block_tiles_r += this->r_padding_tiles;
  }

  if (effective_c_stack_factor > 1) {
    this->tm_assert(this->c_padding_tiles % stacking_block_tiles_c == 0, "Padding ct (" + std::to_string(this->c_padding_tiles) + 
      ") not divisible by stacking block c (" + std::to_string(stacking_block_tiles_c) + ")");
    this->c_stack_padding = this->c_padding_tiles / stacking_block_tiles_c;
    effective_c_stack_factor += this->c_stack_padding;
  }
  else {
    stacking_block_tiles_c += this->c_padding_tiles;
  }

  // Consumer-side constraints

  // Stack factors must be aligned with grid dimensions. 
  this->tm_assert(divisible_either_direction(consumer_num_cores_r, effective_r_stack_factor),
                  "effective r stack factor = " + std::to_string(effective_r_stack_factor) +
                  " not divisible in either direction with consumer grid_r = " + std::to_string(consumer_num_cores_r));
  
  this->tm_assert(divisible_either_direction(consumer_num_cores_c, effective_c_stack_factor),
                  "effective c stack factor = " + std::to_string(effective_c_stack_factor) +
                  " not divisible in either direction with consumer grid_c = " + std::to_string(consumer_num_cores_c));

  // Stacking blocks must align with consumer-side mblocks. 
  this->tm_assert(divisible_either_direction(stacking_block_tiles_r, consumer_mblock_ublocks_r*consumer_ublock_tiles_r),
                  "stacking block r size tiles = " + std::to_string(stacking_block_tiles_r) +
                  " not divisible in either direction with consumer mblock tiles r = " + std::to_string(consumer_mblock_ublocks_r*consumer_ublock_tiles_r));
  
  this->tm_assert(divisible_either_direction(stacking_block_tiles_c, consumer_mblock_ublocks_c*consumer_ublock_tiles_c),
                  "stacking block c size tiles = " + std::to_string(stacking_block_tiles_c) +
                  " not divisible in either direction with consumer mblock tiles c = " + std::to_string(consumer_mblock_ublocks_c*consumer_ublock_tiles_c));

  // Stacking blocks must align with consumer-side ublocks. 
  this->tm_assert(divisible_either_direction(stacking_block_tiles_r, consumer_ublock_tiles_r),
                  "stacking block r size tiles = " + std::to_string(stacking_block_tiles_r) +
                  " not divisible in either direction with consumer ublock tiles r = " + std::to_string(consumer_ublock_tiles_r));
  
  this->tm_assert(divisible_either_direction(stacking_block_tiles_c, consumer_ublock_tiles_c),
                  "stacking block c size tiles = " + std::to_string(stacking_block_tiles_c) +
                  " not divisible in either direction with consumer ublock tiles c = " + std::to_string(consumer_ublock_tiles_c));

  bool stacking_block_within_consumer_ublock =
    (stacking_block_tiles_c < consumer_ublock_tiles_c) || (stacking_block_tiles_r < consumer_ublock_tiles_r);

  if (stacking_block_within_consumer_ublock) {
    // Multiple stacking blocks cover one consumer-side ublock. Ublocks are internally scanned row-major.
    bool ublock_level_stack_ok;
    if (stacking_block_tiles_c < consumer_ublock_tiles_c) {
      ublock_level_stack_ok =
        row_major_stack &&
        (stacking_block_tiles_r == 1) &&
        ((consumer_mblock_ublocks_c == 1) || ((consumer_ublock_tiles_r == 1) && (consumer_row_major_ublock_scan_order || (consumer_mblock_ublocks_r == 1))));
    } else if (stacking_block_tiles_c == consumer_ublock_tiles_c) {
      ublock_level_stack_ok =
        !row_major_stack &&
        ((consumer_mblock_ublocks_c == 1) || !consumer_row_major_ublock_scan_order);
    } else { // stacking_block_tiles_c > consumer_ublock_tiles_c, implies stacking_block_tiles_r < consumer_ublock_tiles_r since stacking_block_within_consumer_ublock==true
      ublock_level_stack_ok = false;
    }
    this->tm_assert(ublock_level_stack_ok, "stacking within consumer ublock that does not follow consumer-side tile scan order");
  } else {
    // One stacking block covers one or more whole consumer-side ublocks -> need ublock scan order constraints:  
    if (consumer_row_major_ublock_scan_order) {
      if (stacking_block_tiles_c < (consumer_mblock_ublocks_c*consumer_ublock_tiles_c)) {
        this->tm_assert(row_major_stack || (effective_r_stack_factor == 1), "stacking within consumer mblock in column-major direction while ublock scan order is row-major");
        //std::cout << "stacking_block_tiles_r: " << stacking_block_tiles_r << std::endl;
        //std::cout << "consumer_ublock_tiles_r: " << consumer_ublock_tiles_r << std::endl;
        this->tm_assert(stacking_block_tiles_r == consumer_ublock_tiles_r,
                        "hstack within consumer mblock in row-major scan order -> input block r dimension must be one consumer ublock_r");
      }
    } else { // consumer has column major ublock scan order
      if (stacking_block_tiles_r < (consumer_mblock_ublocks_r*consumer_ublock_tiles_r)) {
        this->tm_assert(!row_major_stack || (effective_c_stack_factor == 1), "stacking within consumer mblock in row-major direction while ublock scan order is column-major");      
        this->tm_assert(stacking_block_tiles_c == consumer_ublock_tiles_c,
                        "vstack within consumer mblock in column-major scan order -> input block c dimension must be one consumer ublock_c");
      }   
    }
  }
    
}


consumer_to_producer_tile_map
three_d_array_tile_src_map::get_op_matmul_col_input(int kernel_bcast_tiles, bool kernel_bcast_tiles_per_t,
                                                    int consumer_t,
                                                    int consumer_ublock_tiles_k, int consumer_ublock_tiles_c,
                                                    int consumer_col_input_block_ublocks_k, int consumer_mblock_ublocks_n,
                                                    int consumer_num_cores_r, int consumer_num_cores_c, bool consumer_row_major_ublock_scan_order) {

  int consumer_ublock_tiles = consumer_ublock_tiles_c * consumer_ublock_tiles_k;
  int consumer_rt = consumer_ublock_tiles_k * consumer_col_input_block_ublocks_k;
  int consumer_ct = consumer_ublock_tiles_c * consumer_mblock_ublocks_n * consumer_num_cores_c;
  int consumer_col_input_block_tiles = consumer_rt * (consumer_ublock_tiles_c * consumer_mblock_ublocks_n);
  int consumer_total_tiles_per_col = consumer_t * consumer_col_input_block_tiles;
  if (kernel_bcast_tiles) {
    this->tm_assert((this->producer_output_buf_size_t == 1 && this->producer_data_format.t == 1) || kernel_bcast_tiles_per_t,
                    "with kernel broadcast that's not per-t, producer must have t = 1 and buf_size_mb = 1 or 2");
    // FIXME imatosevic - should we have this check? presently there is a test which brings in ublocks row by row with kernel bcast
    // this->tm_assert(kernel_bcast_tiles % consumer_ublock_tiles == 0,
    //                 "kernel bcast tiles read = " + std::to_string(kernel_bcast_tiles) +
    //                 " not divisible by consumer ublock tiles = " + std::to_string(consumer_ublock_tiles));
  }
  else if (this->producer_output_buf_size_t > this->producer_data_format.t) {
    // constructor has the assertion that these must be divisible
    consumer_total_tiles_per_col *= (this->producer_output_buf_size_t / this->producer_data_format.t);    
  }

  this->tm_assert((consumer_rt == this->dim_size_map[map_dims::rt]) &&
                  (consumer_ct == this->dim_size_map[map_dims::ct]) &&
                  (consumer_t == this->dim_size_map[map_dims::t]),
                  "incompatible dimensions:  consumer -> " +
                  dim_to_str(consumer_t, consumer_rt, consumer_ct) + 
                  ", post-TM producer -> " +
                  dim_to_str(this->dim_size_map[map_dims::t], this->dim_size_map[map_dims::rt], this->dim_size_map[map_dims::ct]));

  consumer_to_producer_tile_map result(this->producer_data_format.num_cores_r, this->producer_data_format.num_cores_c,
                                       consumer_num_cores_r, consumer_num_cores_c);

  // initialize to maximum granularity
  result.scatter_granularity = this->producer_output_buf_size_t * this->producer_data_format.mblock_size_tiles();
  
  tile_to_core_index_map prev_tile_map;

  int producer_core_output_buf_size_tiles = this->producer_data_format.mblock_size_tiles() * this->producer_output_buf_size_t;

  // We try to trace all input tiles for each consumer column  to the producer core output buffer.  
  // Once we're past the producer core output buffer size, we stop.
  // If this doesn't provide for a full column for the consumer cores, it means that phased pipes
  // are needed. 
  int pipe_index = -1;  
  for (int consumer_col = 0; consumer_col < consumer_num_cores_c; consumer_col++) {

    int curr_col_cont_tiles = 0;
    int consumer_start_ct = consumer_col * (consumer_ublock_tiles_c * consumer_mblock_ublocks_n);

    for (int consumer_col_tile_index = 0;
         consumer_col_tile_index < consumer_total_tiles_per_col;
         consumer_col_tile_index++) {
        
      int consumer_curr_input = (consumer_col_tile_index / consumer_col_input_block_tiles) / consumer_t;
      int consumer_curr_t = (consumer_col_tile_index / consumer_col_input_block_tiles) % consumer_t;
      int consumer_mblock_col_tile_index = consumer_col_tile_index % consumer_col_input_block_tiles;
        
      int consumer_ublock_index = consumer_mblock_col_tile_index / consumer_ublock_tiles;
      int consumer_ublock_tile_index = consumer_mblock_col_tile_index % consumer_ublock_tiles;
        
      int consumer_tile_r = consumer_ublock_tile_index / consumer_ublock_tiles_c;
      int consumer_tile_c = consumer_ublock_tile_index % consumer_ublock_tiles_c;
      int consumer_ublock_r = consumer_row_major_ublock_scan_order ? (consumer_ublock_index / consumer_mblock_ublocks_n) : (consumer_ublock_index % consumer_col_input_block_ublocks_k);
      int consumer_ublock_c = consumer_row_major_ublock_scan_order ? (consumer_ublock_index % consumer_mblock_ublocks_n) : (consumer_ublock_index / consumer_col_input_block_ublocks_k);

      int consumer_in_rt = consumer_ublock_r*consumer_ublock_tiles_k + consumer_tile_r;
      int consumer_in_ct = consumer_start_ct + consumer_ublock_c*consumer_ublock_tiles_c + consumer_tile_c;

      tile_to_core_index_map curr_tile_map = this->get_tile_map(consumer_curr_t, consumer_in_rt, consumer_in_ct, consumer_curr_input);
      if (curr_tile_map.tile_index >= producer_core_output_buf_size_tiles) {
        // We've reached past the end of producer core output buffer.
        // The remaining inputs to this column need to be provided by phased pipes.
        break;
      } else if (kernel_bcast_tiles) {
        if (kernel_bcast_tiles_per_t) {
          if (consumer_mblock_col_tile_index >= kernel_bcast_tiles) {
            this->tm_assert(curr_tile_map == result.pipes[pipe_index].tile_map.at((consumer_curr_t * kernel_bcast_tiles) + (consumer_col_tile_index % kernel_bcast_tiles)),
                            "input using kernel_broadcast_per_t but post-TM input canonical form is not periodic for t = " + std::to_string(consumer_curr_t));
            continue;
          }
        } else {
          if (consumer_col_tile_index >= kernel_bcast_tiles) {
            this->tm_assert(curr_tile_map == result.pipes[pipe_index].tile_map.at(consumer_col_tile_index % kernel_bcast_tiles),
                            "input using kernel_broadcast but post-TM input canonical form is not periodic");
            continue;
          }
        }
      }

      if (consumer_col_tile_index == 0) {
        pipe_index++;
        result.pipes[pipe_index].dest_cores.push_back(std::make_pair(-1, consumer_col));
        result.pipes[pipe_index].dest_mcast = true;
        result.scatter_granularity = std::gcd(result.scatter_granularity, curr_tile_map.tile_index);
        prev_tile_map = curr_tile_map;
        curr_col_cont_tiles = 0;
      }
      if ((curr_col_cont_tiles > 0) && !(prev_tile_map.tile_is_continuous(curr_tile_map))) {
        result.scatter_granularity = std::gcd(result.scatter_granularity, curr_col_cont_tiles);
        result.scatter_granularity = std::gcd(result.scatter_granularity, curr_tile_map.tile_index);
        curr_col_cont_tiles = 0;
      }

      result.pipes[pipe_index].tile_map.push_back(curr_tile_map);

      prev_tile_map = curr_tile_map;
      curr_col_cont_tiles++;
    }
    result.scatter_granularity = std::gcd(result.scatter_granularity, curr_col_cont_tiles);   
  }

  // now generate phased pipes if needed
  if (this->need_phased_stack()) {

    this->tm_assert(!kernel_bcast_tiles, "can't use stacking pipes with kernel broadcast");

    int effective_c_stack_factor;
    int effective_r_stack_factor;
    
    this->check_phased_stack(effective_c_stack_factor, effective_r_stack_factor,
                             consumer_ublock_tiles_k, consumer_ublock_tiles_c,
                             consumer_col_input_block_ublocks_k, consumer_mblock_ublocks_n,
                             1, consumer_num_cores_c,
                            consumer_row_major_ublock_scan_order); 
    
    if ((effective_c_stack_factor > 1) || (effective_r_stack_factor > 1)) {
      result.apply_stack(effective_c_stack_factor, effective_r_stack_factor, this->row_major_stack, this->c_stack_padding, this->r_stack_padding);
    }
      
  }
  result.check_producer_tiles_out_of_order(this->producer_data_format.num_cores_r,
                                           this->producer_data_format.num_cores_c,
                                           this->producer_data_format.mblock_size_tiles());
  return result;
}



consumer_to_producer_tile_map
three_d_array_tile_src_map::get_op_matmul_row_input(int kernel_bcast_tiles, bool kernel_bcast_tiles_per_t,
                                                    int consumer_t,
                                                    int consumer_ublock_tiles_r, int consumer_ublock_tiles_k,
                                                    int consumer_mblock_ublocks_m, int consumer_row_input_block_ublocks_k,
                                                    int consumer_num_cores_r, int consumer_num_cores_c, bool consumer_row_major_ublock_scan_order) {

  int consumer_ublock_tiles = consumer_ublock_tiles_r * consumer_ublock_tiles_k;
  int consumer_rt = consumer_ublock_tiles_r * consumer_mblock_ublocks_m * consumer_num_cores_r;
  int consumer_ct = consumer_ublock_tiles_k * consumer_row_input_block_ublocks_k;
  int consumer_row_input_block_tiles = consumer_ct * (consumer_ublock_tiles_r * consumer_mblock_ublocks_m);
  int consumer_total_tiles_per_row = consumer_t * consumer_row_input_block_tiles;
  if (kernel_bcast_tiles) {
    this->tm_assert((this->producer_output_buf_size_t == 1 && this->producer_data_format.t == 1) || kernel_bcast_tiles_per_t,
                    "with kernel broadcast that's not per-t, producer must have t = 1 and buf_size_mb = 1 or 2");
    // this->tm_assert(kernel_bcast_tiles % consumer_ublock_tiles == 0,
    //                 "kernel bcast tiles read = " + std::to_string(kernel_bcast_tiles) +
    //                 " not divisible by consumer ublock tiles = " + std::to_string(consumer_ublock_tiles));
  }
  else if (this->producer_output_buf_size_t > this->producer_data_format.t) {
    // constructor has the assertion that these must be divisible
    consumer_total_tiles_per_row *= (this->producer_output_buf_size_t / this->producer_data_format.t);    
  }
  
  this->tm_assert((consumer_rt == this->dim_size_map[map_dims::rt]) &&
                  (consumer_ct == this->dim_size_map[map_dims::ct]) &&
                  (consumer_t == this->dim_size_map[map_dims::t]),
                  "incompatible dimensions:  consumer -> " +
                  dim_to_str(consumer_t, consumer_rt, consumer_ct) + 
                  ", post-TM producer -> " +
                  dim_to_str(this->dim_size_map[map_dims::t], this->dim_size_map[map_dims::rt], this->dim_size_map[map_dims::ct]));

  
  consumer_to_producer_tile_map result(this->producer_data_format.num_cores_r, this->producer_data_format.num_cores_c,
                                       consumer_num_cores_r, consumer_num_cores_c);

  // initialize to maximum granularity
  result.scatter_granularity = this->producer_output_buf_size_t * this->producer_data_format.mblock_size_tiles();
  
  tile_to_core_index_map prev_tile_map;

  int producer_core_output_buf_size_tiles = this->producer_data_format.mblock_size_tiles() * this->producer_output_buf_size_t;

  // We try to trace all input tiles for each consumer row  to the producer core output buffer.  
  // Once we're past the producer core output buffer size, we stop.
  // If this doesn't provide for a full row for the consumer cores, it means that phased pipes
  // are needed. 
  int pipe_index = -1;  
  for (int consumer_row = 0; consumer_row < consumer_num_cores_r; consumer_row++) {

    int curr_row_cont_tiles = 0;
    int consumer_start_rt = consumer_row * (consumer_ublock_tiles_r * consumer_mblock_ublocks_m);

    for (int consumer_row_tile_index = 0;
         consumer_row_tile_index < consumer_total_tiles_per_row;
         consumer_row_tile_index++) {
        
      int consumer_curr_input = (consumer_row_tile_index / consumer_row_input_block_tiles) / consumer_t;
      int consumer_curr_t = (consumer_row_tile_index / consumer_row_input_block_tiles) % consumer_t;
      int consumer_row_block_tile_index = consumer_row_tile_index % consumer_row_input_block_tiles;
        
      int consumer_ublock_index = consumer_row_block_tile_index / consumer_ublock_tiles;
      int consumer_ublock_tile_index = consumer_row_block_tile_index % consumer_ublock_tiles;
        
      int consumer_tile_r = consumer_ublock_tile_index / consumer_ublock_tiles_k;
      int consumer_tile_c = consumer_ublock_tile_index % consumer_ublock_tiles_k;
      int consumer_ublock_r = consumer_row_major_ublock_scan_order ? (consumer_ublock_index / consumer_row_input_block_ublocks_k) : (consumer_ublock_index % consumer_mblock_ublocks_m);
      int consumer_ublock_c = consumer_row_major_ublock_scan_order ? (consumer_ublock_index % consumer_row_input_block_ublocks_k) : (consumer_ublock_index / consumer_mblock_ublocks_m);

      int consumer_in_rt = consumer_start_rt + consumer_ublock_r*consumer_ublock_tiles_r + consumer_tile_r;
      int consumer_in_ct = consumer_ublock_c*consumer_ublock_tiles_k + consumer_tile_c;

      tile_to_core_index_map curr_tile_map = this->get_tile_map(consumer_curr_t, consumer_in_rt, consumer_in_ct, consumer_curr_input);
      if (curr_tile_map.tile_index >= producer_core_output_buf_size_tiles) {
        // We've reached past the end of producer core output buffer.
        // The remaining inputs to this row need to be provided by phased pipes.
        break;
      } else if (kernel_bcast_tiles) {
        if (kernel_bcast_tiles_per_t) {
          if (consumer_row_block_tile_index >= kernel_bcast_tiles) {
            this->tm_assert(curr_tile_map == result.pipes[pipe_index].tile_map.at((consumer_curr_t * kernel_bcast_tiles) + (consumer_row_tile_index % kernel_bcast_tiles)),
                            "input using kernel_broadcast_per_t but post-TM input canonical form is not periodic for t = " + std::to_string(consumer_curr_t));
            continue;
          }
        } else {
          if (consumer_row_tile_index >= kernel_bcast_tiles) {
            this->tm_assert(curr_tile_map == result.pipes[pipe_index].tile_map.at(consumer_row_tile_index % kernel_bcast_tiles),
                            "input using kernel_broadcast but post-TM input canonical form is not periodic");
            continue;
          }
        }
      }

      if (consumer_row_tile_index == 0) {
        pipe_index++;
        result.pipes[pipe_index].dest_cores.push_back(std::make_pair(consumer_row, -1));
        result.pipes[pipe_index].dest_mcast = true;
        result.scatter_granularity = std::gcd(result.scatter_granularity, curr_tile_map.tile_index);
        prev_tile_map = curr_tile_map;
        curr_row_cont_tiles = 0;
      }
      if ((curr_row_cont_tiles > 0) && !(prev_tile_map.tile_is_continuous(curr_tile_map))) {
        result.scatter_granularity = std::gcd(result.scatter_granularity, curr_row_cont_tiles);
        result.scatter_granularity = std::gcd(result.scatter_granularity, curr_tile_map.tile_index);
        curr_row_cont_tiles = 0;
      }
      
      result.pipes[pipe_index].tile_map.push_back(curr_tile_map);

      prev_tile_map = curr_tile_map;
      curr_row_cont_tiles++;  
    }
    result.scatter_granularity = std::gcd(result.scatter_granularity, curr_row_cont_tiles);
  }

  // now generate phased pipes if needed
  if (this->need_phased_stack()) {

    this->tm_assert(!kernel_bcast_tiles, "can't use stacking pipes with kernel broadcast");

    int effective_c_stack_factor;
    int effective_r_stack_factor;
    
    this->check_phased_stack(effective_c_stack_factor, effective_r_stack_factor,
                             consumer_ublock_tiles_r, consumer_ublock_tiles_k,
                             consumer_mblock_ublocks_m, consumer_row_input_block_ublocks_k,
                             consumer_num_cores_r, 1,
                             consumer_row_major_ublock_scan_order);
    
    if ((effective_c_stack_factor > 1) || (effective_r_stack_factor > 1)) {
      result.apply_stack(effective_c_stack_factor, effective_r_stack_factor, this->row_major_stack, this->c_stack_padding, this->r_stack_padding);
    }
      
  }  
  result.check_producer_tiles_out_of_order(this->producer_data_format.num_cores_r,
                                           this->producer_data_format.num_cores_c,
                                           this->producer_data_format.mblock_size_tiles());
  return result;
}


consumer_to_producer_tile_map
three_d_array_tile_src_map::get_embedding_table_input(int consumer_num_cores_r, int consumer_num_cores_c,
                                                      int indexes_per_input) {

  consumer_to_producer_tile_map result(this->producer_data_format.num_cores_r, this->producer_data_format.num_cores_c,
                                       consumer_num_cores_r, consumer_num_cores_c);

  this->tm_assert(consumer_num_cores_r >= this->producer_data_format.num_cores_r &&
                  consumer_num_cores_c >= this->producer_data_format.num_cores_c &&
                  (consumer_num_cores_r % this->producer_data_format.num_cores_r) == 0 &&
                  (consumer_num_cores_c % this->producer_data_format.num_cores_c) == 0,
                  "consumer core dimensions must be >= producer core dimensions for embeddings");

  int core_r_div = consumer_num_cores_r / this->producer_data_format.num_cores_r;
  int core_c_div = consumer_num_cores_c / this->producer_data_format.num_cores_c;

  this->tm_assert(this->producer_data_format.mblock_ublocks_n >= core_c_div &&
                  (this->producer_data_format.mblock_ublocks_n % core_c_div) == 0,
                  "Tilized data can only be column distributed to cores by splitting on the mblock, not ublock for embeddings");

  result.scatter_granularity = 1;
  int pipe_index = 0;
  for (int r = 0; r < consumer_num_cores_r; r++) {
    for (int c = 0; c < consumer_num_cores_c; c++) {
      result.pipes[pipe_index].dest_cores.push_back(std::make_pair(r, c));
      result.pipes[pipe_index].dest_mcast = false;
      int pr = this->producer_data_format.num_cores_r*r/consumer_num_cores_r;
      int pc = this->producer_data_format.num_cores_c*c/consumer_num_cores_c;
      for (int k = 0; k < (this->producer_data_format.mblock_ublocks_n/core_c_div); k++) {
        for (int i = 0; i < indexes_per_input; i++) {
          tile_to_core_index_map curr_tile_map(pr, pc, i);
          result.pipes[pipe_index].tile_map.push_back(curr_tile_map);
        }
      }
      pipe_index++;
    }
  }
  result.producer_tiles_out_of_order = true;
  return result;
}


consumer_to_producer_tile_map
three_d_array_tile_src_map::get_untilized_input(int consumer_num_cores_r, int consumer_num_cores_c,
                                                int indexes_per_input) {

  consumer_to_producer_tile_map result(this->producer_data_format.num_cores_r, this->producer_data_format.num_cores_c,
                                       consumer_num_cores_r, consumer_num_cores_c);

  this->tm_assert(consumer_num_cores_r >= this->producer_data_format.num_cores_r &&
                  consumer_num_cores_c >= this->producer_data_format.num_cores_c &&
                  (consumer_num_cores_r % this->producer_data_format.num_cores_r) == 0 &&
                  (consumer_num_cores_c % this->producer_data_format.num_cores_c) == 0,
                  "consumer core dimensions must be >= producer core dimensions for tilizer");

  int core_r_div = consumer_num_cores_r / this->producer_data_format.num_cores_r;
  int core_c_div = consumer_num_cores_c / this->producer_data_format.num_cores_c;

  int num_rows = indexes_per_input/core_r_div;
  if (this->producer_data_format.row_major_ublock_scan_order) {
    this->tm_assert(this->producer_data_format.mblock_ublocks_m >= core_r_div &&
                    (this->producer_data_format.mblock_ublocks_m % core_r_div) == 0,
                    "Tilized data can only be row distributed to cores by splitting on the mblock, not ublock for tilizer");
    num_rows /= this->producer_data_format.mblock_ublocks_m/core_r_div;
  }

  this->tm_assert(this->producer_data_format.mblock_ublocks_n >= core_c_div &&
                  (this->producer_data_format.mblock_ublocks_n % core_c_div) == 0,
                  "Tilized data can only be column distributed to cores by splitting on the mblock, not ublock for tilizer");

  result.scatter_granularity = 1;
  int pipe_index = 0;
  for (int r = 0; r < consumer_num_cores_r; r++) {
    for (int c = 0; c < consumer_num_cores_c; c++) {
      result.pipes[pipe_index].dest_cores.push_back(std::make_pair(r, c));
      result.pipes[pipe_index].dest_mcast = false;
      int pr = this->producer_data_format.num_cores_r*r/consumer_num_cores_r;
      int pc = this->producer_data_format.num_cores_c*c/consumer_num_cores_c;
      for (int t_i = 0; t_i < this->producer_data_format.t; t_i++) {
        int mblock_ublocks_m_loops = 1;
        if (this->producer_data_format.row_major_ublock_scan_order) {
          mblock_ublocks_m_loops = this->producer_data_format.mblock_ublocks_m/core_r_div;
        }

        for (int j = 0; j < mblock_ublocks_m_loops; j++) {
          for (int k = 0; k < (this->producer_data_format.mblock_ublocks_n/core_c_div); k++) {
            for (int i = j*num_rows + 0; i < j*num_rows + num_rows; i++) {
              tile_to_core_index_map curr_tile_map(pr, pc, t_i*indexes_per_input + i);
              result.pipes[pipe_index].tile_map.push_back(curr_tile_map);
            }
          }
        }
      }
      pipe_index++;
    }
  }
  result.producer_tiles_out_of_order = true;
  return result;
}


consumer_to_producer_tile_map
three_d_array_tile_src_map::get_op_eltwise_input(int kernel_bcast_tiles, bool kernel_bcast_tiles_per_t,
                                                 int consumer_t,
                                                 int consumer_ublock_tiles_r, int consumer_ublock_tiles_c,
                                                 int consumer_mblock_ublocks_m, int consumer_mblock_ublocks_n,
                                                 int consumer_num_cores_r, int consumer_num_cores_c,
                                                 bool consumer_row_major_ublock_scan_order) {
    

  data_format consumer_data_format = data_format(consumer_ublock_tiles_r, consumer_ublock_tiles_c,
                                                 consumer_mblock_ublocks_m, consumer_mblock_ublocks_n,
                                                 consumer_num_cores_r, consumer_num_cores_c,
                                                 consumer_t, consumer_row_major_ublock_scan_order);

  this->tm_assert((consumer_data_format.r_size_tiles() == this->dim_size_map[map_dims::rt]) &&
                  (consumer_data_format.c_size_tiles() == this->dim_size_map[map_dims::ct]) &&
                  (consumer_t == this->dim_size_map[map_dims::t]),
                  "incompatible dimensions:  consumer -> " +
                  dim_to_str(consumer_t, consumer_data_format.r_size_tiles(), consumer_data_format.c_size_tiles()) + 
                  ", post-TM producer -> " +
                  dim_to_str(this->dim_size_map[map_dims::t],
                             this->dim_size_map[map_dims::rt],
                             this->dim_size_map[map_dims::ct]));

  
  consumer_to_producer_tile_map result(this->producer_data_format.num_cores_r, this->producer_data_format.num_cores_c,
                                       consumer_num_cores_r, consumer_num_cores_c);

  // initialize to maximum granularity
  result.scatter_granularity = this->producer_output_buf_size_t * this->producer_data_format.mblock_size_tiles();
  //std::cout << "result.scatter_granularity A: " << result.scatter_granularity << std::endl;
  tile_to_core_index_map prev_tile_map;

  int producer_core_output_buf_size_tiles = this->producer_data_format.mblock_size_tiles() * this->producer_output_buf_size_t;

  // We try to trace all input tiles for each consumer core  to the producer core output buffer.  
  // Once we're past the producer core output buffer size, we stop.
  // If this doesn't provide for a full mblock for each consumer core, it means that phased pipes
  // are needed. 
  int consumer_total_tiles_per_core = consumer_data_format.mblock_size_tiles() * consumer_t;
  if (kernel_bcast_tiles) {
    int consumer_ublock_tiles = consumer_ublock_tiles_r * consumer_ublock_tiles_c;
    // this->tm_assert(kernel_bcast_tiles % consumer_ublock_tiles == 0,
    //                 "kernel bcast tiles read = " + std::to_string(kernel_bcast_tiles) +
    //                 " not divisible by consumer ublock tiles = " + std::to_string(consumer_ublock_tiles));
    this->tm_assert((this->producer_output_buf_size_t == 1 && this->producer_data_format.t == 1) || kernel_bcast_tiles_per_t,
                    "with kernel broadcast that's not per-t, producer must have t = 1 and buf_size_mb = 1 or 2");
  }
  else if (this->producer_output_buf_size_t > this->producer_data_format.t) {
    // constructor has the assertion that these must be divisible
    consumer_total_tiles_per_core *= (this->producer_output_buf_size_t / this->producer_data_format.t);    
  }
  int pipe_index = -1;
  for (int consumer_core_r = 0; consumer_core_r < consumer_num_cores_r; consumer_core_r++) {
    for (int consumer_core_c = 0; consumer_core_c < consumer_num_cores_c; consumer_core_c++) {
      
      int curr_core_cont_tiles = 0;

      int consumer_start_rt = consumer_core_r * consumer_data_format.mblock_r_size_tiles();
      int consumer_start_ct = consumer_core_c * consumer_data_format.mblock_c_size_tiles();

      for (int consumer_core_tile_index = 0;
           consumer_core_tile_index < consumer_total_tiles_per_core;
           consumer_core_tile_index++) {
        
        int consumer_curr_input = (consumer_core_tile_index / consumer_data_format.mblock_size_tiles()) / consumer_t;
        int consumer_curr_t = (consumer_core_tile_index / consumer_data_format.mblock_size_tiles()) % consumer_t; 
        int consumer_mblock_tile_index = consumer_core_tile_index % consumer_data_format.mblock_size_tiles();
        
        int consumer_ublock_index = consumer_mblock_tile_index / consumer_data_format.ublock_size_tiles();
        int consumer_ublock_tile_index = consumer_mblock_tile_index % consumer_data_format.ublock_size_tiles();
        
        int consumer_tile_r = consumer_ublock_tile_index / consumer_ublock_tiles_c;
        int consumer_tile_c = consumer_ublock_tile_index % consumer_ublock_tiles_c;
        int consumer_ublock_r = consumer_row_major_ublock_scan_order ? (consumer_ublock_index / consumer_mblock_ublocks_n) : (consumer_ublock_index % consumer_mblock_ublocks_m);
        int consumer_ublock_c = consumer_row_major_ublock_scan_order ? (consumer_ublock_index % consumer_mblock_ublocks_n) : (consumer_ublock_index / consumer_mblock_ublocks_m);

        int consumer_in_rt = consumer_start_rt + consumer_ublock_r*consumer_ublock_tiles_r + consumer_tile_r;
        int consumer_in_ct = consumer_start_ct + consumer_ublock_c*consumer_ublock_tiles_c + consumer_tile_c;

        tile_to_core_index_map curr_tile_map = this->get_tile_map(consumer_curr_t, consumer_in_rt, consumer_in_ct, consumer_curr_input);        
        if (curr_tile_map.tile_index >= producer_core_output_buf_size_tiles) {
          // We've reached past the end of producer core output buffer.
          // The remaining inputs to this core need to be provided by phased pipes.
          break;
      } else if (kernel_bcast_tiles) {
        if (kernel_bcast_tiles_per_t) {
          if (consumer_mblock_tile_index >= kernel_bcast_tiles) {
            this->tm_assert(curr_tile_map == result.pipes[pipe_index].tile_map.at((consumer_curr_t * kernel_bcast_tiles) + (consumer_core_tile_index % kernel_bcast_tiles)),
                            "input using kernel_broadcast_per_t but post-TM input canonical form is not periodic for t = " + std::to_string(consumer_curr_t));
            continue;
          }
        } else {
          if (consumer_core_tile_index >= kernel_bcast_tiles) {
            this->tm_assert(curr_tile_map == result.pipes[pipe_index].tile_map.at(consumer_core_tile_index % kernel_bcast_tiles),
                            "input using kernel_broadcast but post-TM input canonical form is not periodic");
            continue;
          }
        }
      }

        if (consumer_core_tile_index == 0) {
          pipe_index++;
          result.pipes[pipe_index].dest_cores.push_back(std::make_pair(consumer_core_r, consumer_core_c));
          result.pipes[pipe_index].dest_mcast = false;
          result.scatter_granularity = std::gcd(result.scatter_granularity, curr_tile_map.tile_index);
          prev_tile_map = curr_tile_map;
          curr_core_cont_tiles = 0;
        }
        if ((curr_core_cont_tiles > 0) && !(prev_tile_map.tile_is_continuous(curr_tile_map))) {
          result.scatter_granularity = std::gcd(result.scatter_granularity, curr_core_cont_tiles);
          result.scatter_granularity = std::gcd(result.scatter_granularity, curr_tile_map.tile_index);
          curr_core_cont_tiles = 0;
        }

        result.pipes[pipe_index].tile_map.push_back(curr_tile_map);

        prev_tile_map = curr_tile_map;
        curr_core_cont_tiles++;
      }
      result.scatter_granularity = std::gcd(result.scatter_granularity, curr_core_cont_tiles);
    }
  }

  // now generate phased pipes if needed
  if (this->need_phased_stack()) {

    this->tm_assert(!kernel_bcast_tiles, "can't use stacking pipes with kernel broadcast");

    int effective_c_stack_factor;
    int effective_r_stack_factor;
    
    this->check_phased_stack(effective_c_stack_factor, effective_r_stack_factor,
                             consumer_ublock_tiles_r, consumer_ublock_tiles_c,
                             consumer_mblock_ublocks_m, consumer_mblock_ublocks_n,
                             consumer_num_cores_r, consumer_num_cores_c,
                             consumer_row_major_ublock_scan_order); 
    //std::cout << "elt input: effective_c_stack_factor: " << effective_c_stack_factor << std::endl;
    //std::cout << "elt input: effective_r_stack_factor: " << effective_r_stack_factor << std::endl;
    if ((effective_c_stack_factor > 1) || (effective_r_stack_factor > 1)) {
      result.apply_stack(effective_c_stack_factor, effective_r_stack_factor, this->row_major_stack, this->c_stack_padding, this->r_stack_padding);
    }
  }
  result.check_producer_tiles_out_of_order(this->producer_data_format.num_cores_r,
                                           this->producer_data_format.num_cores_c,
                                           this->producer_data_format.mblock_size_tiles());
  return result;
}

std::tuple<consumer_to_producer_tile_map, consumer_to_producer_tile_map> three_d_array_tile_src_map::get_prolog_matmul_col_input(
  int kernel_bcast_tiles, bool kernel_bcast_tiles_per_t,                                                                                                                                                                                   
  int consumer_t,
  int consumer_ublock_tiles_k, int consumer_mblock_ublocks_k,
  int consumer_ublock_tiles_c,
  int consumer_mblock_ublocks_n,
  int consumer_num_cores_r, int consumer_num_cores_c
  ) {
  
  // Total number of tiles for one column of cores
  const int consumer_col_input_block_tiles = (consumer_ublock_tiles_k * consumer_mblock_ublocks_k) * (consumer_ublock_tiles_c * consumer_mblock_ublocks_n);

  int consumer_total_tiles_per_col = consumer_t * consumer_col_input_block_tiles;
  int consumer_total_tiles_per_col_post_kernel_bcast = consumer_t * consumer_col_input_block_tiles;
  if (kernel_bcast_tiles) {
    consumer_total_tiles_per_col = kernel_bcast_tiles;
    // int consumer_ublock_tiles = consumer_ublock_tiles_k * consumer_ublock_tiles_c;
    // this->tm_assert(kernel_bcast_tiles % consumer_ublock_tiles == 0,
    //                 "kernel bcast tiles read = " + std::to_string(kernel_bcast_tiles) +
    //                 " not divisible by consumer ublock tiles = " + std::to_string(consumer_ublock_tiles));
  }

  // Currently only supports one chunk per core and will ensure that each chunk has the same number of tiles
  int consumer_column_num_chunks = consumer_num_cores_r;
  while (consumer_column_num_chunks > 0 and consumer_total_tiles_per_col % consumer_column_num_chunks != 0) {
      consumer_column_num_chunks--;
  }
  assert(consumer_column_num_chunks > 0);

  const int num_tiles_per_chunk = consumer_total_tiles_per_col / consumer_column_num_chunks;
  
  // Create mapping which is a vector of pipes w/ metadata
  // This is the initial transfer map
  consumer_to_producer_tile_map result(this->producer_data_format.num_cores_r, this->producer_data_format.num_cores_c, consumer_column_num_chunks, consumer_num_cores_c);
  result.scatter_granularity = this->producer_output_buf_size_t * this->producer_data_format.mblock_size_tiles(); // initialize to maximum granularity

  // Create mapping which is a vector of pipes w/ metadata
  // This is the chunk gather/multicast map
  consumer_to_producer_tile_map result2(consumer_column_num_chunks, consumer_num_cores_c, consumer_num_cores_r, consumer_num_cores_c);
  result2.scatter_granularity = num_tiles_per_chunk; // Will always be contiguous tiles

  tile_to_core_index_map prev_tile_map;

  const int producer_core_output_buf_size_tiles = this->producer_data_format.mblock_size_tiles() * this->producer_output_buf_size_t;

  // We try to trace all input tiles for each consumer column  to the producer core output buffer.  
  // Once we're past the producer core output buffer size, we stop.
  // If this doesn't provide for a full column for the consumer cores, it means that phased pipes
  // are needed. 
  int pipe_index = -1;
  int chunk_index = 0;
  // For each column of cores
  for (int consumer_col = 0; consumer_col < consumer_num_cores_c; consumer_col++) {

    int curr_col_cont_tiles = 0;
    int consumer_ct_col_offset = consumer_col * (consumer_ublock_tiles_c * consumer_mblock_ublocks_n);
    int consumer_chunk = -1;

    std::vector<tile_to_core_index_map> kernel_bcast_tile_maps; // for kernel bcast periodicity checks

    // Go through the chunks in buffer order and create pipes to gather tiles in that order
    for (int consumer_col_tile_index = 0; consumer_col_tile_index < consumer_total_tiles_per_col_post_kernel_bcast; consumer_col_tile_index++) {
      
      // Get tile indexs: (t, r, c, input)
      const int consumer_curr_input = 0;
      const int consumer_curr_t = consumer_col_tile_index / consumer_col_input_block_tiles;
      const int curr_t_tile_index = consumer_col_tile_index % consumer_col_input_block_tiles;

      const int ub_size = consumer_ublock_tiles_k * consumer_ublock_tiles_c;
      const int col_width_in_ub = consumer_mblock_ublocks_n;

      const int ub_index = curr_t_tile_index / ub_size;
      const int tile_index = curr_t_tile_index % ub_size;

      const int ub_r = ub_index / col_width_in_ub;
      const int ub_c = ub_index % col_width_in_ub;

      const int tile_r = tile_index / consumer_ublock_tiles_c;
      const int tile_c = tile_index % consumer_ublock_tiles_c;

      const int consumer_in_rt = ub_r * consumer_ublock_tiles_k + tile_r;
      const int consumer_in_ct = consumer_ct_col_offset + ub_c * consumer_ublock_tiles_c + tile_c;

      n2p::Log() << "col: " << consumer_col << " tile idx: " << consumer_col_tile_index << " --> " << consumer_in_rt << ", " << consumer_in_ct << std::endl;
      tile_to_core_index_map curr_tile_map = this->get_tile_map(consumer_curr_t, consumer_in_rt, consumer_in_ct, consumer_curr_input);
      if (curr_tile_map.tile_index >= producer_core_output_buf_size_tiles) {
        // We've reached past the end of producer core output buffer.
        // The remaining inputs to this column need to be provided by phased pipes.
        assert(false);
        break;
      } else if (kernel_bcast_tiles) {
        if (kernel_bcast_tiles_per_t) {
          if (curr_t_tile_index >= kernel_bcast_tiles) {
            this->tm_assert(curr_tile_map == result.pipes[pipe_index].tile_map.at((consumer_curr_t * kernel_bcast_tiles) + (curr_t_tile_index % kernel_bcast_tiles)),
                            "input using kernel_broadcast_per_t but post-TM input canonical form is not periodic for t = " + std::to_string(consumer_curr_t));
            continue;
          }
        } else {
          if (consumer_col_tile_index >= kernel_bcast_tiles) {
            this->tm_assert(curr_tile_map == result.pipes[pipe_index].tile_map.at(consumer_col_tile_index % kernel_bcast_tiles),
                            "input using kernel_broadcast but post-TM input canonical form is not periodic");
            continue;
          }
        }
      }

      // Create new buffer and pipe for each new chunk
      if (consumer_col_tile_index % num_tiles_per_chunk == 0) {
        pipe_index++;
        consumer_chunk++;

        result.pipes[pipe_index].dest_cores.push_back(std::make_pair(consumer_chunk, consumer_col));
        result.pipes[pipe_index].dest_mcast = false;

        chunk_index = 0;
      }

      result.pipes[pipe_index].tile_map.push_back(curr_tile_map);

      if ((curr_col_cont_tiles > 0) && !(prev_tile_map.tile_is_continuous(curr_tile_map))) {
        if (curr_col_cont_tiles < result.scatter_granularity) {
          result.scatter_granularity = std::gcd(result.scatter_granularity, curr_col_cont_tiles);
        }
        curr_col_cont_tiles = 0;
      }
      prev_tile_map = curr_tile_map;
      curr_col_cont_tiles++;

      result2.pipes[consumer_col].tile_map.push_back(tile_to_core_index_map(consumer_chunk, consumer_col, chunk_index++));  
    }

    if (curr_col_cont_tiles < result.scatter_granularity) {
      result.scatter_granularity = std::gcd(result.scatter_granularity, curr_col_cont_tiles);
    }

    // Create dynamic runtime gather/mcast pipes
    result2.pipes[consumer_col].dest_cores.push_back(std::make_pair(-1, consumer_col));
    result2.pipes[consumer_col].dest_mcast = true;
  }

  // now generate phased pipes if needed
  if (this->need_phased_stack()) {
    
    this->tm_assert(false, "can't use stacking pipes with prologue");

    this->tm_assert(!kernel_bcast_tiles, "can't use stacking pipes with kernel broadcast");

    int effective_c_stack_factor;
    int effective_r_stack_factor;
    
    this->check_phased_stack(effective_c_stack_factor, effective_r_stack_factor,
                             consumer_ublock_tiles_k, consumer_ublock_tiles_c,
                             consumer_mblock_ublocks_k, consumer_mblock_ublocks_n,
                             1, consumer_num_cores_c,
                             true); // consumer_row_major_ublock_scan_order

    if ((effective_c_stack_factor > 1) || (effective_r_stack_factor > 1)) {
      result.apply_stack(effective_c_stack_factor, effective_r_stack_factor, this->row_major_stack, this->c_stack_padding, this->r_stack_padding);
    }
      
  }
  result.check_producer_tiles_out_of_order(this->producer_data_format.num_cores_r,
                                           this->producer_data_format.num_cores_c,
                                           this->producer_data_format.mblock_size_tiles());

  

  return {result, result2};
} 

void consumer_to_producer_tile_map::check_producer_tiles_out_of_order(int producer_grid_r, int producer_grid_c, int producer_mblock_tiles) {
  this->producer_tiles_out_of_order = false;
  std::map<int, std::map<int, int>> producer_core_next_tile_index;
  std::map<int, std::map<int, int>> producer_core_dest_pipe_id;
  for (int r = 0; r < producer_grid_r; r++) {
    for (int c = 0; c < producer_grid_c; c++) {
      producer_core_next_tile_index[r][c] = 0;
      producer_core_dest_pipe_id[r][c] = -1;
    }
  }
  for (auto it : this->pipes) {
    int curr_pipe_id = it.first;
    phase_pipe_tile_map curr_pipe = it.second;
    if (curr_pipe.dest_cores.size() > 1) {
      this->producer_tiles_out_of_order = true;
      return;
    }
    for (tile_to_core_index_map tile_core_index : curr_pipe.tile_map) {
      if (producer_core_dest_pipe_id[tile_core_index.core_r][tile_core_index.core_c] == -1) {
        producer_core_dest_pipe_id[tile_core_index.core_r][tile_core_index.core_c] = curr_pipe_id;
      }
      else if (producer_core_dest_pipe_id[tile_core_index.core_r][tile_core_index.core_c] != curr_pipe_id) {
        this->producer_tiles_out_of_order = true;
        return;        
      }
      if (producer_core_next_tile_index[tile_core_index.core_r][tile_core_index.core_c] != tile_core_index.tile_index) {
        this->producer_tiles_out_of_order = true;
        return;        
      }
      producer_core_next_tile_index[tile_core_index.core_r][tile_core_index.core_c]++;
    }
  }
  for (int r = 0; r < producer_grid_r; r++) {
    for (int c = 0; c < producer_grid_c; c++) {
      if ((producer_core_next_tile_index[r][c] % producer_mblock_tiles) != 0) {
        this->producer_tiles_out_of_order = true;
        return;
      }
    }
  }
}

                                                
void consumer_to_producer_tile_map::apply_stack(int c_stack_factor, int r_stack_factor, bool row_major_stack, int c_stack_padding, int r_stack_padding) {

  
  int total_stack_factor = c_stack_factor * r_stack_factor;
  std::map<int, std::pair<int, int>> pipe_first_stack_iter_dest;
  std::map<int, std::vector<std::pair<int, int>>> pipe_full_dests;

  int num_pipes = this->pipes.size();
  bool pipes_r_mcast = false;
  bool pipes_c_mcast = false;
  for (int p = 0; p < num_pipes; p++) {    
    pipe_first_stack_iter_dest[p] = this->pipes[p].dest_cores[0];
    // check and assert that pipes have consistent multicast direction (if any)
    bool curr_pipe_r_mcast = false;
    bool curr_pipe_c_mcast = false;
    if (this->pipes[p].dest_mcast) {
      if (pipe_first_stack_iter_dest[p].first == -1) {
        curr_pipe_r_mcast = true;
        assert(pipe_first_stack_iter_dest[p].second != -1);
      } else {
        assert(pipe_first_stack_iter_dest[p].second == -1);
        curr_pipe_c_mcast = true;
      }
    }
    if (p == 0) {
      pipes_r_mcast = curr_pipe_r_mcast;
      pipes_c_mcast = curr_pipe_c_mcast;
    } else {
      assert (curr_pipe_r_mcast == pipes_r_mcast);
      assert (curr_pipe_c_mcast == pipes_c_mcast);
    }
  }

  int effective_consumer_num_cores_c = pipes_c_mcast ? 1 : this->consumer_num_cores_c;
  int effective_consumer_num_cores_r = pipes_r_mcast ? 1 : this->consumer_num_cores_r;

  // Matches pattern where each destination always repeats the same number of times, e.g.:
  // [ [A] [A] [A] [B] [B] [B] [C] [C] [C] ]
  // In this case, net2pipe will label the pipe for subsequent pipegen optimization of
  // firmware data structures. In this function we compute this value only to assert that
  // our assumptions on stacking rules work. 
  int single_dest_repeat;
  
  // Matches pattern where all destinations repeat periodically, e.g.:
  // [ [A] [B] [C] [A] [B] [C] [A] [B] [C] ]
  // In this case, we can reduce the number of output phases by truncating after the first period.
  int dest_period = -1;
  
  if ((row_major_stack && (c_stack_factor > 1)) || (!row_major_stack && (r_stack_factor == 1))) {
    single_dest_repeat = (c_stack_factor > effective_consumer_num_cores_c) ? (c_stack_factor / effective_consumer_num_cores_c) : 1;
    if ((r_stack_factor > 1) && (effective_consumer_num_cores_r == 1) && (effective_consumer_num_cores_c > 1) and (r_stack_padding == 0)) {
      dest_period = c_stack_factor;
    }
  } else {
    single_dest_repeat = (r_stack_factor > effective_consumer_num_cores_r) ? (r_stack_factor / effective_consumer_num_cores_r) : 1;
    if ((c_stack_factor > 1) && (effective_consumer_num_cores_c == 1) && (effective_consumer_num_cores_r > 1) and (c_stack_padding == 0)) {
      dest_period = r_stack_factor;
    }
  }

  bool multi_core_stack = ((r_stack_factor > 1) && (effective_consumer_num_cores_r > 1)) || ((c_stack_factor > 1) && (effective_consumer_num_cores_c > 1));
  if  (!multi_core_stack and (r_stack_padding == 0) and (c_stack_padding == 0)) {
    return;
  }

  for (int p = 0; p < num_pipes; p++) {
    this->pipes[p].dest_cores.clear();
  }
  for (int s = 0; s < total_stack_factor; s++) {
    int rs;
    int cs;
    if (row_major_stack) {
      cs = (s % c_stack_factor);
      rs = (s / c_stack_factor);
    } else {
      rs = (s % r_stack_factor);
      cs = (s / r_stack_factor);
    }

    int num_pipes = this->pipes.size();
    for (int p = 0; p < num_pipes; p++) {
      int curr_stack_iter_dest_r = pipe_first_stack_iter_dest[p].first;
      if (curr_stack_iter_dest_r != -1) {
        int offset_core_r = (rs * this->consumer_num_cores_r) / r_stack_factor;
        curr_stack_iter_dest_r += offset_core_r;
      }
      int curr_stack_iter_dest_c = pipe_first_stack_iter_dest[p].second;
      if (curr_stack_iter_dest_c != -1) {
        int offset_core_c = (cs * this->consumer_num_cores_c) / c_stack_factor;
        curr_stack_iter_dest_c += offset_core_c;
      }
      std::pair<int, int> curr_stack_iter_dest = std::make_pair(curr_stack_iter_dest_r,
                                                                curr_stack_iter_dest_c);

      // We loop through full list of pipe destinations so we can assert that the above inference
      // about repetitions and periods is correct. 
      // These assertions shouldn't fail unless previous assertions are missing some divisibility
      // constraints, or there is a bug in the above code for calculating repetitions and periods
      // for destinations. 
      pipe_full_dests[p].push_back(curr_stack_iter_dest);

      bool include_dest = true;

      if ((single_dest_repeat > 1) && ((s % single_dest_repeat) != 0)) {
        assert(curr_stack_iter_dest == pipe_full_dests[p].at(s-1));
      }
      if ((dest_period != -1) && (s >= dest_period)) {
        assert(curr_stack_iter_dest == pipe_full_dests[p].at(s-dest_period));
        include_dest = false;
      }

      if (include_dest) {
        this->pipes[p].dest_cores.push_back(curr_stack_iter_dest);
        bool padded_output = rs >= (r_stack_factor - r_stack_padding) or cs >= (c_stack_factor - c_stack_padding);
        this->pipes[p].padding_output_list.push_back(padded_output);
      }
    }
  }
}


three_d_array_tile_src_map
three_d_array_tile_src_map::apply_tm(const std::string tm_name, const std::vector<int>& tm_args) {

  this->tm_sequence.push_back(tm_name);
  this->tm_args_sequence.push_back(tm_args);
  
  if (tm_name == "transpose") {
    return this->tile_transpose();
  }
  else if (tm_name == "c_broadcast") {
    return this->broadcast(map_dims::ct, tm_args[0]);
  }
  else if (tm_name == "r_broadcast") {
    return this->broadcast(map_dims::rt, tm_args[0]);
  }
  else if (tm_name == "z_broadcast") {
    return this->broadcast(map_dims::t, tm_args[0]);
  }
  else if (tm_name == "hslice") {
    return this->hslice(tm_args[0]);
  }
  else if (tm_name == "vslice") {
    return this->vslice(tm_args[0]);
  }
  else if (tm_name == "hstack") {
    return this->hstack(tm_args[0]);
  }
  else if (tm_name == "vstack") {
    return this->vstack(tm_args[0]);
  }
  else if (tm_name == "pad") {
    return this->pad(tm_args.at(0), tm_args.at(1));
  }
  else {
    this->tm_assert(false, "Unknown TM: " + tm_name);
    return *this;
  }
}
  


void three_d_array_tile_src_map::print() {

  std::string producer_name;
  std::string consumer_name;
  
  for (int t = 0; t < this->get_size(map_dims::t); t++) {
    printf("\nt = %d:\n\n", t);
    printf("               ");
    for (int ct = 0; ct < this->get_size(map_dims::ct); ct++) {
      printf("ct = %4d        ", ct);
      if (((ct+1) % this->producer_data_format.mblock_c_size_tiles()) == 0) {
        printf(" |  ");
      }
    }
    printf("\n");
    printf("            ");
    for (int ct = 0; ct < this->get_size(map_dims::ct); ct++) {
      printf("-----------------");
      if (((ct+1) % this->producer_data_format.mblock_c_size_tiles()) == 0) {
        printf("----");
      }
    }
    printf("\n");
    for (int rt = 0; rt < this->get_size(map_dims::rt); rt++) {
      printf("rt = %4d:  |  ", rt);
      for (int ct = 0; ct < this->get_size(map_dims::ct); ct++) {
        printf("[%2d, %2d -> %4d] ", this->get_tile_map(t, rt, ct).core_r, this->get_tile_map(t, rt, ct).core_c, this->get_tile_map(t, rt, ct).tile_index);
        if (((ct+1) % this->producer_data_format.mblock_c_size_tiles()) == 0) {
          printf(" |  ");
        }
      }
      printf("\n");
      if (((rt+1) % this->producer_data_format.mblock_r_size_tiles()) == 0) {
        printf("            ");
        for (int ct = 0; ct < this->get_size(map_dims::ct); ct++) {
          printf("-----------------");
          if (((ct+1) % this->producer_data_format.mblock_c_size_tiles()) == 0) {
            printf("----");
          }
        }
        printf("\n");
      }
    }
  }

}


void consumer_to_producer_tile_map::print() {
  printf("Scatter granularity: %d\n", this->scatter_granularity);
  printf("Max pipe fanout per core: %d\n", this->max_producer_core_fan_out());
  printf("Max producer core phases: %d\n", this->max_producer_core_phases());
  printf("Max consumer core phases: %d\n", this->max_consumer_core_phases());
  for (auto it : this->pipes) {
    int n = it.first;
    phase_pipe_tile_map pipe = it.second;
    printf("Pipe %d: [", n);
    for (auto it_dest_core : pipe.dest_cores) {
      printf(" (%d,%d)", it_dest_core.first, it_dest_core.second);
    }    
    printf(" ] <- [");
    for (tile_to_core_index_map tile_map_it : pipe.tile_map) {
      printf (" (%d,%d)-%d", tile_map_it.core_r, tile_map_it.core_c, tile_map_it.tile_index);
    }
    printf(" ]\n");
  }
}


int consumer_to_producer_tile_map::max_producer_core_fan_out() {

  std::map<int, std::map<int, std::map<int, std::map<int, int>>>> producer_consumer_core_adj_map;
  std::map<int, std::map<int, int>> producer_core_fanout;
  for (int pr = 0; pr < this->producer_num_cores_r; pr++) {
    for (int pc = 0; pc < this->producer_num_cores_c; pc++) {
      producer_core_fanout[pr][pc] = 0;
      for (int cr = 0; cr < this->consumer_num_cores_r; cr++) {
        for (int cc = 0; cc < this->consumer_num_cores_c; cc++) {
          producer_consumer_core_adj_map[pr][pc][cr][cc] = 0;
        }
      }
    }
  }
  
  for (auto it : this->pipes) {
    // int pipe_n = it.first;
    phase_pipe_tile_map pipe = it.second;
    for (std::pair<int, int>  dest_core : pipe.dest_cores) {
      int cr = (dest_core.first == -1) ? 0 : dest_core.first;
      int cc = (dest_core.second == -1) ? 0 : dest_core.second;
      for (tile_to_core_index_map tile_map_it : pipe.tile_map) {
        producer_consumer_core_adj_map[tile_map_it.core_r][tile_map_it.core_c][cr][cc] = 1;
      }
    }
  }

  int max_producer_fan_out = 0;
  for (int pr = 0; pr < this->producer_num_cores_r; pr++) {
    for (int pc = 0; pc < this->producer_num_cores_c; pc++) {
      for (int cr = 0; cr < this->consumer_num_cores_r; cr++) {
        for (int cc = 0; cc < this->consumer_num_cores_c; cc++) {
          // printf("  --- p %d,%d (%d/%d) -> c %d,%d (%d/%d) -> %d\n", pr, pc, this->producer_num_cores_r, this->producer_num_cores_c, cr, cc, this->consumer_num_cores_r, this->consumer_num_cores_c, producer_consumer_core_adj_map[pr][pc][cr][cc]);
          if (producer_consumer_core_adj_map[pr][pc][cr][cc]) {
            producer_core_fanout[pr][pc]++;
            if (producer_core_fanout[pr][pc] > max_producer_fan_out) {
              max_producer_fan_out = producer_core_fanout[pr][pc];
            }
          }
        }
      }
    }
  }

  return max_producer_fan_out;

}


bool phase_pipe_tile_map::check_tile_map_periodic(int consumer_tile_clear_granularity,
                                                  int& period, int& periodic_repeat) {

  int num_inputs = this->tile_map.size();
  bool result = false;

  for (int p = 1; p <= (num_inputs/2); p++) {
    // It's OK to use (p % consumer_tile_clear_granularity) since this type of pipe object
    // has single-tile inputs. 
    if (((num_inputs % p) == 0) && ((p % consumer_tile_clear_granularity) == 0)) {
      bool p_is_period = true;
      for (int r = 0; r < p; r++) {
        for (int q = p; q < num_inputs; q += p) {
          if (this->tile_map[r] != this->tile_map[q + r]) {
            p_is_period = false;
            break;
          }
        }
        if (!p_is_period) {
          break;
        }
      }
      if (p_is_period) {
        result = true;
        period = p;
        break;
      }
    }
  }

  if (result) {
    periodic_repeat = num_inputs / period;
  }

  return result;
}

bool phase_pipe_tile_map::validate_padding() {
  return not std::all_of(this->tile_map.begin(),this->tile_map.end(), [](auto& index_map) { return index_map.is_padding_tile;});
}


int consumer_to_producer_tile_map::max_producer_core_phases(int consumer_tile_clear_granularity, 
                                                            bool producer_output_single_buffer) {

  std::map<int, std::map<int, int>> producer_core_phases;
  for (int pr = 0; pr < this->producer_num_cores_r; pr++) {
    for (int pc = 0; pc < this->producer_num_cores_c; pc++) {
      producer_core_phases[pr][pc] = 0;
    }
  }

  for (auto it : this->pipes) {
    // int pipe_n = it.first;
    phase_pipe_tile_map pipe = it.second;
    std::map<int, std::map<int, int>> pipe_producer_core_tiles;
    int prev_pr = -1;
    int prev_pc = -1;
    int prev_tile_index = -1;
    int pipe_scatter_granularity = -1;
    int curr_core_cont_tiles = 0;
    tile_to_core_index_map prev_tile_map_it;
    for (tile_to_core_index_map tile_map_it : pipe.tile_map) {
      if ((curr_core_cont_tiles > 0) && !(prev_tile_map_it.tile_is_continuous(tile_map_it))) {        
        if (pipe_scatter_granularity == -1) { 
          pipe_scatter_granularity = curr_core_cont_tiles;
        } else {
          pipe_scatter_granularity = std::gcd(pipe_scatter_granularity, curr_core_cont_tiles);
        }
        curr_core_cont_tiles = 0;
      }
      curr_core_cont_tiles++;
      pipe_producer_core_tiles[tile_map_it.core_r][tile_map_it.core_c]++;
      prev_tile_map_it = tile_map_it;
    }
    if (pipe_scatter_granularity == -1) { 
      pipe_scatter_granularity = curr_core_cont_tiles;
    } else {
      pipe_scatter_granularity = std::gcd(pipe_scatter_granularity, curr_core_cont_tiles);
    }
    int period;
    int periodic_repeat;
    bool pipe_periodic = pipe.check_tile_map_periodic(consumer_tile_clear_granularity, period, periodic_repeat);
    for (int pr = 0; pr < this->producer_num_cores_r; pr++) {
      for (int pc = 0; pc < this->producer_num_cores_c; pc++) {
        int curr_pipe_producer_core_phases = three_d_array_tile_src_map::int_div_with_ceil(pipe_producer_core_tiles[pr][pc], pipe_scatter_granularity);
        if (!pipe_periodic) {
          producer_core_phases[pr][pc] += curr_pipe_producer_core_phases;        
        } else {
          producer_core_phases[pr][pc] += three_d_array_tile_src_map::int_div_with_ceil(curr_pipe_producer_core_phases, periodic_repeat);
        }
      }
    }
  }

  int max_producer_phases = 0;
  for (int pr = 0; pr < this->producer_num_cores_r; pr++) {
    for (int pc = 0; pc < this->producer_num_cores_c; pc++) {
      if (producer_core_phases[pr][pc] > max_producer_phases) {
        max_producer_phases = producer_core_phases[pr][pc];
      }
    }
  }

  if (!producer_output_single_buffer) {
    max_producer_phases *= 2;
  }

  return max_producer_phases;
}



int consumer_to_producer_tile_map::max_dram_queue_input_indexes(int consumer_tile_clear_granularity) {

  std::map<int, std::map<int, int>> consumer_core_indexes;
  std::map<int, std::map<int, int>> pipe_consumer_core_tiles;
  for (int cr = 0; cr < this->consumer_num_cores_r; cr++) {
    for (int cc = 0; cc < this->consumer_num_cores_c; cc++) {
      consumer_core_indexes[cr][cc] = 0;
    }
  }

  for (auto it : this->pipes) {
    // int pipe_n = it.first;
    phase_pipe_tile_map pipe = it.second;
    assert (pipe.dest_cores.size() == 1); // DRAM->op input pipe can't have scatter pipes
    std::pair<int, int>  dest_core = pipe.dest_cores[0];
    int cr = (dest_core.first == -1) ? 0 : dest_core.first;
    int cc = (dest_core.second == -1) ? 0 : dest_core.second;
    int pipe_scatter_granularity = -1;
    int curr_core_cont_tiles = 0;
    tile_to_core_index_map prev_tile_map_it;
    for (tile_to_core_index_map tile_map_it : pipe.tile_map) {
      if ((curr_core_cont_tiles > 0) && !(prev_tile_map_it.tile_is_continuous(tile_map_it))) {        
        if (pipe_scatter_granularity == -1) { 
          pipe_scatter_granularity = curr_core_cont_tiles;
        } else {
          pipe_scatter_granularity = std::gcd(pipe_scatter_granularity, curr_core_cont_tiles);
        }
        curr_core_cont_tiles = 0;
      }
      curr_core_cont_tiles++;
      pipe_consumer_core_tiles[cr][cc]++;
      prev_tile_map_it = tile_map_it;
    }
    if (pipe_scatter_granularity == -1) { 
      pipe_scatter_granularity = curr_core_cont_tiles;
    } else {
      pipe_scatter_granularity = std::gcd(pipe_scatter_granularity, curr_core_cont_tiles);
    }
    int period;
    int periodic_repeat;
    bool pipe_periodic = pipe.check_tile_map_periodic(consumer_tile_clear_granularity, period, periodic_repeat);
    int curr_pipe_consumer_core_indexes = three_d_array_tile_src_map::int_div_with_ceil(pipe_consumer_core_tiles[cr][cc], pipe_scatter_granularity);
    if (!pipe_periodic) {
      consumer_core_indexes[cr][cc] += curr_pipe_consumer_core_indexes;        
    } else {
      consumer_core_indexes[cr][cc] += three_d_array_tile_src_map::int_div_with_ceil(curr_pipe_consumer_core_indexes, periodic_repeat);
    }
  }

  int max_consumer_indexes = 0;
  for (int cr = 0; cr < this->consumer_num_cores_r; cr++) {
    for (int cc = 0; cc < this->consumer_num_cores_c; cc++) {
      if (consumer_core_indexes[cr][cc] > max_consumer_indexes) {
        max_consumer_indexes = consumer_core_indexes[cr][cc];
      }
    }
  }

  return max_consumer_indexes;
}


int consumer_to_producer_tile_map::max_consumer_core_phases() {

  std::map<int, std::map<int, int>> consumer_core_phases;
  for (int cr = 0; cr < this->consumer_num_cores_r; cr++) {
    for (int cc = 0; cc < this->consumer_num_cores_c; cc++) {
      consumer_core_phases[cr][cc] = 0;
    }
  }

  int max_consumer_phases = 0;
  for (auto it : this->pipes) {
    // int pipe_n = it.first;
    phase_pipe_tile_map pipe = it.second;
    for (std::pair<int, int>  dest_core : pipe.dest_cores) {
      int cr = (dest_core.first == -1) ? 0 : dest_core.first;
      int cc = (dest_core.second == -1) ? 0 : dest_core.second;
      int prev_pr = -1;
      int prev_pc = -1;
      for (tile_to_core_index_map tile_map_it : pipe.tile_map) {
        if ((tile_map_it.core_r != prev_pr) || (tile_map_it.core_c != prev_pc)) {
          consumer_core_phases[cr][cc]++;
          if (consumer_core_phases[cr][cc] > max_consumer_phases) {
            max_consumer_phases = consumer_core_phases[cr][cc];
          }
        }
        prev_pr = tile_map_it.core_r;
        prev_pc = tile_map_it.core_c;
      }        
    }
  }

  // Need to multiply with 2 in case we're using 2-stage gather for performance. 
  return 2 * max_consumer_phases;
  
}


void get_input_reblock_gather(OpClass consumer_op_class, int input_id,
                              bool& reblock_r, bool& reblock_c,
                              bool& gather_r, bool& gather_c) {
    
  // FIXME imatosevic - what about other op classes?
  if (consumer_op_class == OpClass::MatMul || consumer_op_class == OpClass::Depthwise) {
    if (input_id == 0) { // matmul input 0 -> outer dimension = r
      // need to reblock across rt dimension, whole ct is broadcast to each row
      reblock_r = true;
      reblock_c = false;
      gather_r = false;
      gather_c = true;
    }
    else { // matmul input 1 -> outer dimension = c
      // need to reblock across ct dimension, whole rt is broadcast to each column
      reblock_r = false;
      reblock_c = true;
      gather_r = true;
      gather_c = false;
    }
  }
  else { // eltwise input (doesn't matter if unary or binary)
    // need to reblock across both rt and ct
    reblock_r = true;
    reblock_c = true;
    gather_r = false;
    gather_c = false;
  }

}

void three_d_array_tile_src_map::
estimate_resource_usage(OpClass consumer_op_class, int input_id,
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
                        int& consumer_max_gather_stream_phases_used_per_core)  {

  bool reblock_r;
  bool reblock_c;
  bool gather_r;
  bool gather_c;

  get_input_reblock_gather(consumer_op_class, input_id, reblock_r, reblock_c, gather_r, gather_c);
    
  int consumer_input_block_size_tiles_r = (gather_r ? (consumer_mblock_ublocks_k * consumer_ublock_tiles_k) : (consumer_mblock_ublocks_m * consumer_ublock_tiles_r));
  int consumer_input_block_size_tiles_c = (gather_c ? (consumer_mblock_ublocks_k * consumer_ublock_tiles_k) : (consumer_mblock_ublocks_n * consumer_ublock_tiles_c));    
  int consumer_input_block_size_tiles = consumer_input_block_size_tiles_r * consumer_input_block_size_tiles_c;

  int consumer_input_ublock_tiles_r = gather_r ? consumer_ublock_tiles_k : consumer_ublock_tiles_r;
  int consumer_input_ublock_tiles_c = gather_c ? consumer_ublock_tiles_k : consumer_ublock_tiles_c;

  // Special case: no TMs and reblocking
  bool no_reblocking =
    (!reblock_r || (this->producer_data_format.num_cores_r == consumer_grid_size_r)) &&
    (!reblock_c || (this->producer_data_format.num_cores_c == consumer_grid_size_c)) &&
    (this->producer_data_format.mblock_ublocks_m == (consumer_input_block_size_tiles_r/consumer_input_ublock_tiles_r)) &&
    (this->producer_data_format.mblock_ublocks_n == (consumer_input_block_size_tiles_c/consumer_input_ublock_tiles_c)) &&
    (this->producer_data_format.ublock_tiles_r == consumer_input_ublock_tiles_r) &&
    (this->producer_data_format.ublock_tiles_c == consumer_input_ublock_tiles_c) && 
    (this->producer_data_format.row_major_ublock_scan_order == consumer_row_major_ublock_scan_order);    

  if (this->tm_sequence.size() == 0 && no_reblocking) {
    producer_max_fork_streams_used_per_core = 1;
    producer_max_scatter_stream_phases_used_per_core = 1;
    if (gather_r) {
      consumer_max_gather_stream_phases_used_per_core = this->producer_data_format.num_cores_r;
    } else if (gather_c) {
      consumer_max_gather_stream_phases_used_per_core = this->producer_data_format.num_cores_c;
    } else {
      consumer_max_gather_stream_phases_used_per_core = 1;
    }
    return;
  }
  else {

    // Block of tiles from a single producer core post-TM, before reblocking onto the consumer grid.
    // Pessimistically take only slice TMs into account. (We could make this more complex and less
    // pessimistic by considering broadcasts and stacks.)
    int effective_producer_mblock_tiles_r =
      (this->tm_has_transpose ? this->producer_data_format.mblock_c_size_tiles() : this->producer_data_format.mblock_r_size_tiles()) / this->r_slice_factor;
    int effective_producer_mblock_tiles_c =
      (this->tm_has_transpose ? this->producer_data_format.mblock_r_size_tiles() : this->producer_data_format.mblock_c_size_tiles()) / this->c_slice_factor;

    bool ublock_size_preserved =
      (this->producer_data_format.ublock_tiles_r == consumer_input_ublock_tiles_r) &&
      (this->producer_data_format.ublock_tiles_c == consumer_input_ublock_tiles_c);
    
    // Variables for estimating pipe implementation efficiency
    // (initialized to the most pessimistic value):
    int consumer_min_tiles_from_same_producer_core = 1; // How many back-to-back tiles at consumer are received from the same producer core?
    int producer_core_min_contiguous_tiles_sent = 1; // How many tiles from each producer core are sent out from contiguous locations?

    // Ublocks are scanned in row-major order, so if input block is divisible by consumer ublock in c
    // dimension, we're guaranteed a minimum granularity for both the above variables.
    if ((effective_producer_mblock_tiles_c % consumer_input_ublock_tiles_c) == 0) {
      consumer_min_tiles_from_same_producer_core = consumer_input_ublock_tiles_c;
      producer_core_min_contiguous_tiles_sent = std::gcd(consumer_input_ublock_tiles_c, this->producer_data_format.ublock_tiles_c);      
      if ((effective_producer_mblock_tiles_r % consumer_input_ublock_tiles_r) == 0) {
        // If the other dimension is also divisible, we get whole consumer ublocks from a single producer core
        consumer_min_tiles_from_same_producer_core *= consumer_input_ublock_tiles_r;
        // To get contiguous producer-side tiles, we need c dimension to be the same
        if (consumer_input_ublock_tiles_c == this->producer_data_format.ublock_tiles_c) {
          producer_core_min_contiguous_tiles_sent *= std::gcd(consumer_input_ublock_tiles_r, this->producer_data_format.ublock_tiles_r);
        }
        // If ublock scan order is the same, we can get contiguous tiles from multiple ublocks
        int cont_consumer_ublocks_r = std::gcd(effective_producer_mblock_tiles_r, consumer_input_block_size_tiles_r) / consumer_input_ublock_tiles_r;
        int cont_consumer_ublocks_c = std::gcd(effective_producer_mblock_tiles_c, consumer_input_block_size_tiles_c) / consumer_input_ublock_tiles_c;
        if (this->producer_data_format.row_major_ublock_scan_order && consumer_row_major_ublock_scan_order) {
          consumer_min_tiles_from_same_producer_core *= cont_consumer_ublocks_c;
          if (ublock_size_preserved) {
            producer_core_min_contiguous_tiles_sent *= cont_consumer_ublocks_c;
          }
        }
        else if (!this->producer_data_format.row_major_ublock_scan_order && !consumer_row_major_ublock_scan_order) {
          consumer_min_tiles_from_same_producer_core *= cont_consumer_ublocks_r;
          if (ublock_size_preserved) {
            producer_core_min_contiguous_tiles_sent *= cont_consumer_ublocks_r;
          }
        }
      }
    }

    // Pessimistically, multiply all TM factors along each dimension, even though
    // in some cases they may cancel out.
    int tm_c_factor = this->c_bcast_factor * this->c_slice_factor * this->c_stack_factor;
    int tm_r_factor = this->r_bcast_factor * this->r_slice_factor * this->r_stack_factor;

    int effective_consumer_grid_size_r = consumer_grid_size_r * tm_r_factor;
    int effective_consumer_grid_size_c = consumer_grid_size_c * tm_c_factor;

    int effective_consumer_grid_size_r_reblock = tm_has_transpose ? effective_consumer_grid_size_c : effective_consumer_grid_size_r;
    int effective_consumer_grid_size_c_reblock = tm_has_transpose ? effective_consumer_grid_size_r : effective_consumer_grid_size_c;

    int reblock_tm_fork_r_factor_int;
    int reblock_tm_fork_c_factor_int;

    if ((this->producer_data_format.num_cores_r % effective_consumer_grid_size_r_reblock) == 0) {
      reblock_tm_fork_r_factor_int = 1;
    } else if ((effective_consumer_grid_size_r_reblock % this->producer_data_format.num_cores_r) == 0) {
      reblock_tm_fork_r_factor_int = effective_consumer_grid_size_r_reblock / this->producer_data_format.num_cores_r;
    } else {
      // We must account for cases such as reblocking from dimension 7 -> 6, where
      // producer cores may have a fanout of 2 or 3 because of non-divisibility.
      reblock_tm_fork_r_factor_int = int_div_with_ceil(effective_consumer_grid_size_r_reblock, this->producer_data_format.num_cores_r) + 1;
    }

    if ((this->producer_data_format.num_cores_c % effective_consumer_grid_size_c_reblock) == 0) {
      reblock_tm_fork_c_factor_int = 1;
    } else if ((effective_consumer_grid_size_c_reblock % this->producer_data_format.num_cores_c) == 0) {
      reblock_tm_fork_c_factor_int = effective_consumer_grid_size_c_reblock / this->producer_data_format.num_cores_c;
    } else {
      reblock_tm_fork_c_factor_int = int_div_with_ceil(effective_consumer_grid_size_c_reblock, this->producer_data_format.num_cores_c) + 1;
    }
    
    // First return value:  max streams used by a single core for forking.
    producer_max_fork_streams_used_per_core = 1;
    if (reblock_r) {
      producer_max_fork_streams_used_per_core *= reblock_tm_fork_r_factor_int;
    }
    if (reblock_c) {
      producer_max_fork_streams_used_per_core *= reblock_tm_fork_c_factor_int;
    }
    
    // The final result can't be higher than the total number of consumer cores,
    // (for matmul only along the single reblocking dimension):
    int consumer_effective_grid_size =
      (reblock_r ? consumer_grid_size_r : 1) *
      (reblock_c ? consumer_grid_size_c : 1);
    
    if (producer_max_fork_streams_used_per_core > consumer_effective_grid_size) {
      producer_max_fork_streams_used_per_core = consumer_effective_grid_size;
    }

    // Second return value: max stream phases that need to be encoded in the blob on
    // a single producer core.
    //
    // Each access to the next tile that's non-contiguous implies a stream phase transition
    // that requires a new blob entry. 
    // 
    // Worst case: we access tiles at random indexes across the output buffer stream.
    //
    // Broadcast may be implemented efficiently, but stack requires multiple phases to
    // send out multiple copies of the same tile.
 
    int bcast_factor = this->c_bcast_factor * this->r_bcast_factor;
    if (bcast_factor > 1) {
      // For now, assume we can do efficient broadcast implementation only if the producer
      // data format is a single tile. Later we can revise this if it's too restrictive
      // (either by adding other useful cases that will be efficiently implemented by the
      // present pipegen, or by enhancing pipegen to handle more complex looping efficiently). 
      bool efficient_bcast = (this->producer_data_format.total_size_tiles() == 1);
      if (efficient_bcast) {
        bcast_factor = 1;
      }
    }

    producer_max_scatter_stream_phases_used_per_core =
      this->producer_data_format.mblock_size_tiles() *
      (2*this->producer_output_buf_size_t) *
      this->c_stack_factor *
      this->r_stack_factor *
      bcast_factor;

    // Divide worst-case with the minimum contiguous tiles sent from producer as
    // calculated above. 
    producer_max_scatter_stream_phases_used_per_core =
      int_div_with_ceil(producer_max_scatter_stream_phases_used_per_core,
                        producer_core_min_contiguous_tiles_sent);

    
    // Third return value: max stream phases that need to be encoded in the blob on a
    // single consumer core.
    //
    // Each transition between producer cores when gathering tiles implies a stream phase
    // transition that requires a new blob entry.

    // If we have slice, multiple consumer t's correspond to a single producer t, after which pipes are periodic
    int total_slice_factor = this->c_slice_factor * this->r_slice_factor;
    int gather_pipe_period_tiles = this->producer_output_buf_size_t * total_slice_factor * consumer_input_block_size_tiles;

    // Need to multiply with 2 since we're using 2-stage gather for performance. 
    consumer_max_gather_stream_phases_used_per_core = 2 * int_div_with_ceil(gather_pipe_period_tiles,
                                                                            consumer_min_tiles_from_same_producer_core);
    return;
  }
}


void three_d_array_tile_src_map::
estimate_dram_input_perf_resource_usage(OpClass consumer_op_class, int input_id,
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
                                        int& consumer_core_pipe_inputs) {
  
  
  bool reblock_r;
  bool reblock_c;
  bool gather_r;
  bool gather_c;

  get_input_reblock_gather(consumer_op_class, input_id, reblock_r, reblock_c, gather_r, gather_c);

  int consumer_input_block_size_tiles_r = (gather_r ? (consumer_mblock_ublocks_k * consumer_ublock_tiles_k) : (consumer_mblock_ublocks_m * consumer_ublock_tiles_r));
  int consumer_input_block_size_tiles_c = (gather_c ? (consumer_mblock_ublocks_k * consumer_ublock_tiles_k) : (consumer_mblock_ublocks_n * consumer_ublock_tiles_c));    

  int consumer_input_ublock_tiles_r = gather_r ? consumer_ublock_tiles_k : consumer_ublock_tiles_r;
  int consumer_input_ublock_tiles_c = gather_c ? consumer_ublock_tiles_k : consumer_ublock_tiles_c;

  // Special case: no TMs and reblocking
  bool no_reblocking =
    (!reblock_r || (this->producer_data_format.num_cores_r == consumer_grid_size_r)) &&
    (!reblock_c || (this->producer_data_format.num_cores_c == consumer_grid_size_c)) &&
    (this->producer_data_format.mblock_ublocks_m == (consumer_input_block_size_tiles_r/consumer_input_ublock_tiles_r)) &&
    (this->producer_data_format.mblock_ublocks_n == (consumer_input_block_size_tiles_c/consumer_input_ublock_tiles_c)) &&
    (this->producer_data_format.ublock_tiles_r == consumer_input_ublock_tiles_r) &&
    (this->producer_data_format.ublock_tiles_c == consumer_input_ublock_tiles_c) && 
    (this->producer_data_format.row_major_ublock_scan_order == consumer_row_major_ublock_scan_order);    

  if (this->tm_sequence.size() == 0 && no_reblocking) {
    producer_contiguous_tiles_read = this->producer_data_format.total_size_tiles();
    consumer_core_pipe_inputs = 1;
    return;
  }
  else {

    // Block of tiles from a single producer core post-TM, before reblocking onto the consumer grid.
    // Pessimistically take only slice TMs into account. (We could make this more complex and less
    // pessimistic by considering broadcasts and stacks.)
    int effective_producer_mblock_tiles_r =
      (this->tm_has_transpose ? this->producer_data_format.mblock_c_size_tiles() : this->producer_data_format.mblock_r_size_tiles()) / this->r_slice_factor;
    int effective_producer_mblock_tiles_c =
      (this->tm_has_transpose ? this->producer_data_format.mblock_r_size_tiles() : this->producer_data_format.mblock_c_size_tiles()) / this->c_slice_factor;

    bool ublock_size_preserved =
      (this->producer_data_format.ublock_tiles_r == consumer_input_ublock_tiles_r) &&
      (this->producer_data_format.ublock_tiles_c == consumer_input_ublock_tiles_c);

    // How many tiles from each producer buffer are read out from contiguous locations?
    // Start with the most pessimistic estimate and try to get a better one based on block sizes. 
    producer_contiguous_tiles_read = 1;      

    // Ublocks are scanned in row-major order, so if input block is divisible by consumer ublock in c
    // dimension, we're guaranteed a minimum granularity for both the above variables.
    if ((effective_producer_mblock_tiles_c % consumer_input_ublock_tiles_c) == 0) {
      producer_contiguous_tiles_read = std::gcd(consumer_input_ublock_tiles_c, this->producer_data_format.ublock_tiles_c);      
      if ((effective_producer_mblock_tiles_r % consumer_input_ublock_tiles_r) == 0) {
        // If the other dimension is also divisible, we get whole consumer ublocks from a single producer buffer.
        // To get contiguous producer-side tiles, we need c dimension to be the same
        if (consumer_input_ublock_tiles_c == this->producer_data_format.ublock_tiles_c) {
          producer_contiguous_tiles_read *= std::gcd(consumer_input_ublock_tiles_r, this->producer_data_format.ublock_tiles_r);
        }
        // If ublock scan order is the same, we can get contiguous tiles from multiple ublocks
        int cont_consumer_ublocks_r = std::gcd(effective_producer_mblock_tiles_r, consumer_input_block_size_tiles_r) / consumer_input_ublock_tiles_r;
        int cont_consumer_ublocks_c = std::gcd(effective_producer_mblock_tiles_c, consumer_input_block_size_tiles_c) / consumer_input_ublock_tiles_c;
        if (this->producer_data_format.row_major_ublock_scan_order && consumer_row_major_ublock_scan_order) {
          if (ublock_size_preserved) {
            producer_contiguous_tiles_read *= cont_consumer_ublocks_c;
          }
        }
        else if (!this->producer_data_format.row_major_ublock_scan_order && !consumer_row_major_ublock_scan_order) {
          if (ublock_size_preserved) {
            producer_contiguous_tiles_read *= cont_consumer_ublocks_r;
          }
        }
      }
    }
    
    // Total tiles per input per consumer op core (not for matmul inner dimension we need to use 1):
    int consumer_core_input_tiles = this->producer_data_format.total_size_tiles();
    if (reblock_r) {
      consumer_core_input_tiles = consumer_core_input_tiles / consumer_grid_size_r;
    }
    if (reblock_c) {
      consumer_core_input_tiles = consumer_core_input_tiles / consumer_grid_size_c;
    }

    consumer_core_pipe_inputs = consumer_core_input_tiles / producer_contiguous_tiles_read;
  }
}



tile_to_core_index_map three_d_array_tile_src_map::get_tile_map(int t, int rt, int ct, int input_index) {
  assert(t < get_size(map_dims::t) && rt < get_size(map_dims::rt) && ct < get_size(map_dims::ct));
  tile_to_core_index_map result = this->tile_map[t][rt][ct];
  if (input_index > 0) {
    assert(input_index < (this->producer_output_buf_size_t/this->producer_data_format.t));
    result.tile_index += (input_index * this->producer_data_format.t * this->producer_data_format.mblock_size_tiles());
  }
  return result;
}


std::string three_d_array_tile_src_map::data_format::to_string() {

  std::stringstream result;
  
  result << "ublock_tiles_r = " << ublock_tiles_r << ", ";
  result << "ublock_tiles_c = " << ublock_tiles_c << ", ";
  result << "mblock_ublocks_m = " << mblock_ublocks_m << ", ";
  result << "mblock_ublocks_n = " << mblock_ublocks_n << ", ";
  result << "num_cores_r = " << num_cores_r << ", ";
  result << "num_cores_c = " << num_cores_c << ", ";
  result << "t = " << t << ", ";
  result << "row_major_ublock_scan_order = " << row_major_ublock_scan_order;

  return result.str();
    
}

