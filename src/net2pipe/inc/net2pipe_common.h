// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "net2pipe_logger.h"
#include "netlist/netlist.hpp"
#include "net2pipe_constants.h"
#include "tile_maps_common.h"

int env_var(const char *env_var_name, int default_val);

#define ERROR(error_msg) \
do { \
  std::stringstream full_err_msg; \
  full_err_msg << "<NET2PIPE-ERROR> " << error_msg << std::endl; \
  throw std::runtime_error(full_err_msg.str()); \
} while(0)

namespace n2p {

  OpClass get_op_class(const tt_op_info &op_info);
  bool op_has_intermediate_buf(const tt_op_info &op_info);
  int  op_num_intermediate_buf(const tt_op_info &op_info);
  bool op_has_output_buf(const tt_op_info &op_info);
  bool op_is_fused(const tt_op_info &op_info);
  bool op_is_topk(const tt_op_info &op_info);
  bool op_output_buffer_shared_with_intermediate(const tt_op_info &op_info);

  int get_op_output_mblock_tiles(const tt_op_info &op_info);
  int get_op_num_input_bufs(const tt_op_info &op_info);

  int get_op_output_single_buf_size_mblocks(const tt_op_info &op_info); // doesn't include double-buffering

  int get_op_output_full_buf_size_mblocks(const tt_op_info &op_info); // full storage size with double-buffering

  int get_op_output_tiles_per_input(const tt_op_info& op_info);
  
  int get_tile_height(TileDim tile_dim);
  int get_tile_width(TileDim tile_dim);
  int get_tile_xy_size(TileDim tile_dim);
  int get_tile_exp_sec_size(TileDim tile_dim);
  int get_format_tile_size_bytes(tt::DataFormat format, bool has_header = true, TileDim tile_dim = TileDim::Dim32x32);

  bool is_bfp_format(DataFormat df);

  inline int pad_32B(int n) {
    int result = (n >> 5);
    if ((n % 32) != 0) {
      result++;
    }
    return (result << 5);
  }
};