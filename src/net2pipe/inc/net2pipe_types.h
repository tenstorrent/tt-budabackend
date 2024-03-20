// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

struct QueueSettings {
  std::string name = {};
  bool prolog = false;
  bool epilog = false;
};

namespace n2p {
  struct prolog_buffer{
    std::string comment;
    std::string md_op_name;
    std::string operand_name;

    std::uint64_t uniqid;
    int id;
    
    int epoch_tiles;
    int size_tiles;
    int scatter_gather_num_tiles;
    int tile_clear_granularity;
    int tile_size;

    int chip_id;
    int core_r;
    int core_c;
  };

  struct prolog_layout {
    int num_cores_r;
    int num_cores_c;
    int num_chunks_to_gather;
    int num_cores_to_mcast;
    int chunk_size_tiles;
  };

  prolog_layout get_prolog_layout(
    int consumer_t,
    int consumer_ublock_tiles_k, int consumer_mblock_ublocks_k,
    int consumer_ublock_tiles_c,
    int consumer_mblock_ublocks_n,
    int consumer_num_cores_r, int consumer_num_cores_c
  );

struct padding_buffer {
  enum padding_buffer_type {
    DRAM_IO,
    PRE_TM,
    POST_TM
  };

  padding_buffer_type type;

  std::string comment;
  std::string md_op_name;
  std::string operand_name;

  std::uint64_t uniqid;
  int id;
  
  int epoch_tiles;
  int size_tiles;
  int q_slots;
  int tiles_per_input;
  int scatter_gather_num_tiles;
  int replicate;
  int tile_clear_granularity = 0;
  int tile_size;

  int chip_id;
  int core_r;
  int core_c;

  int dram_io_flag;
  int dram_buf_flag;
  uint32_t dram_chan;
  uint32_t dram_sub_chan;
  uint32_t dram_addr;
  int prefetch_type;
};

struct padding_db_key {
  std::string op_name;
  int input_index;
  int chip;
  int r;
  int c;

  bool operator==(const padding_db_key& o) const {
    return (chip == o.chip) and
           (r == o.r) and
           (c == o.c) and
           (op_name == o.op_name) and
           (input_index == o.input_index);
  }
};

};

template<> struct std::hash<n2p::padding_db_key> {
  std::size_t operator()(const n2p::padding_db_key& k) const { //noexcept
    // Compute individual hash values for first, second and third
    // http://stackoverflow.com/a/1646913/126995
    size_t res = 17;
    res = res * 31 + hash<int>()(k.chip);
    res = res * 31 + hash<int>()(k.r);
    res = res * 31 + hash<int>()(k.c);
    res = res * 31 + hash<string>()(k.op_name);
    res = res * 31 + hash<int>()(k.input_index);
    return res;
  }
};
