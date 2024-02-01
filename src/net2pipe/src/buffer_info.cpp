// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "buffer_info.h"
#include "model/tile.hpp"
#include "common/size_lib.hpp"

namespace tt {
bool buffer_info::operator==(buffer_info const&rhs) const {
  return this->_t == rhs._t &&
    this->_buffer_type == rhs._buffer_type &&
    this->_stream_id == rhs._stream_id &&
    this->_ublock_rt == rhs._ublock_rt &&
    this->_ublock_ct == rhs._ublock_ct &&
    this->_total_epoch_tiles == rhs._total_epoch_tiles &&
    this->_mblock_m == rhs._mblock_m &&
    this->_mblock_n == rhs._mblock_n &&
    this->_mblock_k == rhs._mblock_k &&
    this->_allocated_size_tiles == rhs._allocated_size_tiles &&
    this->_scatter_gather_num_tiles == rhs._scatter_gather_num_tiles &&
    this->_replication_factor == rhs._replication_factor && 
    this->_data_format == rhs._data_format &&
    this->_untilized == rhs._untilized &&
    this->_is_scatter == rhs._is_scatter &&
    this->_tile_dims == rhs._tile_dims;
}


buffer_info::buffer_info(
  RouterBufferType buffer_type,
  int stream_id,
  int t,
  int allocated_size_tiles,
  int ublock_rt, 
  int ublock_ct, 
  int input_count, 
  int mblock_m, 
  int mblock_n, 
  int scatter_gather_num_tiles,
  int num_epoch_tiles,
  int replication_factor,
  DataFormat data_format,
  tt_xy_pair const& tile_dims,
  bool untilized,
  bool is_scatter
) : 
  _buffer_type(buffer_type),
  _stream_id(stream_id),
  _t(t),
  _ublock_rt(ublock_rt),
  _ublock_ct(ublock_ct),
  _total_epoch_tiles(num_epoch_tiles),
  _mblock_m(mblock_m),
  _mblock_n(mblock_n),
  _mblock_k(1),
  _allocated_size_tiles(allocated_size_tiles),
  _scatter_gather_num_tiles(scatter_gather_num_tiles),
  _replication_factor(replication_factor),
  _data_format(data_format),
  _untilized(untilized),
  _is_scatter(is_scatter),
  _tile_dims(tile_dims)
{
  TT_ASSERT(data_format != DataFormat::Invalid);
  //TT_ASSERT(_total_epoch_tiles >= _scatter_gather_num_tiles);
}


buffer_info::buffer_info(
  RouterBufferType buffer_type, 
  int stream_id, 
  int t, 
  int allocated_size_tiles, 
  int scatter_gather_num_tiles,
  int total_epoch_tiles, 
  int replication_factor,
  DataFormat data_format, 
  tt_xy_pair const& tile_dims,
  bool untilized,
  bool is_scatter
) :
  _buffer_type(buffer_type),
  _stream_id(stream_id),
  _t(t),
  _ublock_rt(1),
  _ublock_ct(1),
  _total_epoch_tiles(total_epoch_tiles),
  _mblock_m(1),
  _mblock_n(1),
  _mblock_k(1),
  _allocated_size_tiles(allocated_size_tiles),
  _scatter_gather_num_tiles(scatter_gather_num_tiles),
  _replication_factor(replication_factor),
  _data_format(data_format),
  _tile_dims(tile_dims),
  _untilized(untilized),
  _is_scatter(is_scatter)
{
  TT_ASSERT(data_format != DataFormat::Invalid);
  //TT_ASSERT(_total_epoch_tiles >= _scatter_gather_num_tiles); 
}

buffer_info::buffer_info(RouterBufferType buffer_type, int stream_id, const buffer_info &info_to_copy) :
  _buffer_type(buffer_type),
  _stream_id(stream_id),
  _t(info_to_copy._t),
  _ublock_rt(info_to_copy._ublock_rt),
  _ublock_ct(info_to_copy._ublock_ct),
  _total_epoch_tiles(info_to_copy._total_epoch_tiles),
  _mblock_m(info_to_copy._mblock_m),
  _mblock_n(info_to_copy._mblock_n),
  _mblock_k(info_to_copy._mblock_k),
  _allocated_size_tiles(info_to_copy._allocated_size_tiles),
  _scatter_gather_num_tiles(info_to_copy._scatter_gather_num_tiles),
  _data_format(info_to_copy._data_format),
  _replication_factor(info_to_copy._replication_factor),
  _tile_dims(info_to_copy._tile_dims),
  _untilized(info_to_copy._untilized),
  _is_scatter(info_to_copy._is_scatter)
{}

int buffer_info::allocated_size_in_bytes() const {
  bool include_header_padding = !this->_untilized;
  return std::max(this->_replication_factor, 1) * tt::size::get_entry_size_in_bytes(
    this->_data_format,
    include_header_padding,
    this->_allocated_size_tiles,
    1,
    1,
    1,
    1,
    this->_tile_dims.y,
    this->_tile_dims.x
  );
}

int buffer_info::size_in_tiles() const {
  return std::max(this->_replication_factor, 1) * tt::size::get_entry_size_in_tiles(
    this->_ublock_ct,
    this->_ublock_rt,
    this->_mblock_m,
    this->_mblock_n,
    this->_t
  );
}

int buffer_info::size_in_bytes() const {
  bool include_header_padding = !this->_untilized;
  return std::max(this->_replication_factor, 1) * tt::size::get_entry_size_in_bytes(
    this->_data_format,
    include_header_padding,
    this->_ublock_ct,
    this->_ublock_rt,
    this->_mblock_m,
    this->_mblock_n,
    this->_t,
    this->_tile_dims.y,
    this->_tile_dims.x
  );
}

}; // namespace tt
