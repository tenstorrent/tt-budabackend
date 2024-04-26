// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <map>
#include <unordered_map>
#include <set>

#include "buffer.hpp"
#include "device/tt_xy_pair.h"

// Utils
using std::uint32_t;

// Datastructures
using std::array;
using std::map;
using std::string;
using std::vector;
using std::set;
using std::deque;

class tt_hex;
namespace tt
{



//dram IO is a vector of buffer queues
//the thinking being that
class tt_dram_io
{
    private:
    // if set then it's an epoch-to-epoch connection, one epoch writes to this queue, another reads from it
    bool epoch_to_epoch_connection = false;
    bool untilized_output = false;
    // TensorType of the tensor this queue was initialized from
    tt_tensor* tensor = nullptr;

    set<int> prologue_read = {};
    set<int> epilogue_write = {};
    set<int> pipe_read = {};
    set<int> pipe_write = {};
    
    // List of epochs in which dram_io is treated as input
    vector<int> consumer_epochs;
    // epoch which produces the dram_io as its output
    vector<int> producer_epochs;

    public:

    vector<tt_buffer_queue *> bufqs;
    deque<vector<std::uint32_t>> untilized_payloads;
    tt_grid_shape grid_shape;

    int q_slots;
    DataFormat data_format;
    tt_buffer_shape buffer_shape;

    int untilized_output_per_core_r;
    int untilized_output_per_core_c;

    string name = "";

    bool can_loopback = false; // TODO: Remove any loopback queue logic
    bool loopback_queue_force_overwrite = false;
    set<int> use_shadow_rd_ptr_for_epochs;  // Set of all epochs in which this dram io uses shadow rd ptr to read
    uint32_t last_shadow_read_epoch = 0; // Used to specify the last epoch a shadow read dram io is used
    bool is_dram_bounce_queue = false;

    std::vector<std::pair<int, tt_buffer_metadata>> buffer_models;

    public:
    tt_dram_io();
    tt_dram_io(const string &name);
    tt_dram_io(const string &name, bool epoch_to_epoch_connection);
    int size_bytes();
    vector<tt_buffer_queue *> *get_bufqs_ptr();
    int occupancy();
    bool all_empty();
    bool all_not_empty();
    // creating an ioq without associating queues with specific cores (mapping is not 1-to-1)
    void add_buffer_queue(int slots, const tt_buffer_shape& buffer_shape, DataFormat data_formati, bool allocated_to_dram);
    void init_ioq(int slots, const tt_buffer_shape& buffer_shape, DataFormat data_formati, tt_grid_shape grid_shape, bool untilized_output=false);
    void init_ioq(int slots, const tt_buffer_shape& buffer_shape, DataFormat data_formati, tt_grid_shape grid_shape, vector <vector <tt_core *> > &core_grid, bool untilized_output=false);
    bool is_initialized();
    void set_epoch_to_epoch_connection(bool _epoch_to_epoch_connection);
    bool is_epoch_to_epoch_connection() const;
    void set_untilized_output(bool _untilized_output);
    bool is_having_untilized_output();
    void set_untilized_output_per_core_rc(int rdim, int cdim);
    int get_untilized_output_per_core_r();
    int get_untilized_output_per_core_c();
    void set_tensor(tt_tensor*);
    tt_tensor* get_tensor() const;
    TensorType get_tensor_type();
    void set_prologue_read(int epoch);
    bool is_prologue_read(int epoch);
    bool is_prologue_read_in_any_epoch();
    void set_epilogue_write(int epoch);
    bool is_epilogue_write(int epoch);
    bool is_epilogue_write_in_any_epoch();
    void set_pipe_read(int epoch);
    bool is_pipe_read(int epoch);
    void set_pipe_write(int epoch);
    bool is_pipe_write(int epoch);
    void clear_consumed_flag();
    void popall();
    void push(int index, tt_buffer *ptr);
    tt_tensor get(int q_offset, int grid_r, int grid_c, int ublock_rt, int ublock_ct, int ublock_id, int mblock_m, int mblock_n, int t);
    void push(tt_tensor *in_tensor, bool vertical_mblock_scan, int grid_r, int grid_c, int ublock_rt, int ublock_ct, int ublock_id, int mblock_m, int mblock_n, int t);
    tt_buffer *get_buf_ptr(int index);
    tt_buffer *get_buf_ptr(int index, int offset);
    tt_buffer_queue *get_q_ptr(int index);
    void reset_rd_ptrs(bool use_shadow_rd_ptr = false);
    void reset_rd_offset_ptrs();
    void pop_dram_io_slot(int num_pops);
    void set_producer_epoch(int epoch);
    void add_consumer_epoch(int epoch);
    bool is_consumer_epoch(int epoch);
    bool is_producer_epoch(int epoch);
    vector<int> &get_all_consumer_epochs();
    bool all_consumer_epochs_are_prologue_read();
    int get_last_consuming_epoch();
    bool has_consuming_epochs();
    bool has_producer_epoch();
    void populate_dram_io_desc(tt_dram_io_desc& io_desc);
    bool use_shadow_rd_ptr(int epoch);
    ~tt_dram_io();

    void set_local_rd_ptr(int rd_ptr);
    void set_global_rd_ptr(int rd_ptr);
};

// Collection of buffers in DRAM
// managed via large static buffer table array
// each epoch has a core/buffer pair list

// Should keep core/epoch mappings here?
// Tensor mappings?
class tt_dram
{
    // DRAM buffers holding parameters
    vector <vector<tt_buffer *> > buffers;

    // Subset of DRAM buffers that are written by each core as part of epilog
    vector < map<tt_core*, tt_buffer*> > epilog_written_buffers;

    // vector<tt_buffer *> per_core_bufs[128][128][2];//[1024][4];

    public:
    // list of epochs
    // each epoch a list of cores
    // each core a list of buffers
    tt_dram();
    
    // get_buffers();

    void incr_epoch();
    void reset_epochs();

    vector<tt_buffer*> &get_buf_vec_ptr(int epoch);
    map<tt_core*, tt_buffer*> &get_epilog_written_map_ptr(int epoch);

    void params_to_cores(int epoch);
    ~tt_dram();
    void add_buffer(tt_buffer *buf_ptr, int epoch);
    void clear_buffers(int epoch);
    void add_epilog_written_buffer(tt_buffer *buf_ptr, int epoch);
    tt_buffer* get_epilog_written_buffer(tt_core* core, int epoch);
    bool epilog_written_buffer_array_empty(int epoch);
};

class tt_dram_chan_desc
{
    public:
	int chan_num;
    int subchannel;
	int up_logical_core_coord[2];
    int dn_logical_core_coord[2];
	int up_routing_core_coord[2];
    int dn_routing_core_coord[2];
	uint64_t base_addr;
    uint64_t chan_capacity;

    tt_dram_chan_desc();
    bool is_mapped_to_logical_core(int logical_r, int logical_c) const;
    bool is_mapped_to_routing_core(int routing_r, int routing_c) const;
    int get_chan_capacity() const;
};

class tt_chan_alloc_struct
{
    public:
    uint64_t top_of_reserved;
    uint64_t top_of_epoch0_start_table;
    uint64_t top_of_kernel_cache;
    uint64_t top_of_binaries;
    uint64_t top_of_chan;
    uint64_t top_of_q_update_blobs;
    uint64_t epoch_q_start;
    uint64_t epoch_q_slot_size;
    uint64_t current_bin_ptr;
    uint64_t current_kernel_cache_ptr;
    uint64_t current_buf_ptr;
    uint64_t current_queue_ptr;
    uint64_t current_q_update_blob_ptr;

    tt_chan_alloc_struct(uint64_t tr, uint64_t tstart_table, uint64_t ttb, uint64_t tb, uint64_t tchan, uint64_t epoch_q_slot_size, uint64_t top_of_q_update_blobs);
    uint64_t alloc_buf(uint64_t size_bytes);
    uint64_t alloc_q(uint32_t channel_id, uint64_t size_bytes);
    uint64_t alloc_bin(uint64_t size_bytes);
    uint64_t alloc_kernel_cache_bin(uint64_t size_bytes);
    uint64_t alloc_q_update_blob(uint64_t size_bytes);
    uint64_t alloc_epoch0_start_table(uint64_t size_bytes);
    uint64_t alloc_epoch_address_q(uint64_t size_bytes, tt_xy_pair core);
    uint64_t current_buf_addr();
    uint64_t current_q_addr();
    void set_binary_address_to_q_slot_start(int epoch_q_slot);
    void set_q_update_blob_address_to_q_slot_start(int epoch_q_slot, int worker_idx_within_channel);
    uint64_t alloc_kernel_cache_bin_at_slot_idx(uint64_t size_bytes, int slot_idx);
};

} // end namespace tt
