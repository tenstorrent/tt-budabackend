// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "device/tt_xy_pair.h"

#include "model/typedefs.h"

namespace pipegen2
{
    class PGPipe;

    // Buffer types from pipegen yaml.
    enum class BufferType
    {
        kDramProlog = 0,
        kDramEpilog,
        kGradientOp,
        kIntermediate,
        kPacker,
        kUnpacker,
        kDramIO,
        kRelay,
        kEthernetRelay,
        kPrologRelay,
        kUnknown
    };

    // Buffer node in pipe graph.
    class PGBuffer
    {
    public:
        PGBuffer();

        // Copy constructor, copies all fields except input and output pipes.
        PGBuffer(const PGBuffer& other);

#ifdef TT_DEBUG
        std::string type_to_string();

        std::string node_type_to_color();
#endif

        NodeId get_id() const { return m_id; }

        void set_id(NodeId id) { m_id = id; }

        std::string get_op_name() const { return m_op_name; }

        void set_op_name(const std::string& op_name) { m_op_name = op_name; }

        BufferType get_type() const { return m_type; }

        void set_type(BufferType type) { m_type = type; }

        const tt_cxy_pair& get_logical_location() const { return m_logical_location; }

        void set_chip_id(ChipId chip_id);

        void set_logical_core_x(std::size_t core_x) { m_logical_location.x = core_x; }

        void set_logical_core_y(std::size_t core_y) { m_logical_location.y = core_y; }

        unsigned int get_num_epoch_tiles() const { return m_num_epoch_tiles; }

        void set_num_epoch_tiles(unsigned int num_epoch_tiles) { m_num_epoch_tiles = num_epoch_tiles; }

        unsigned int get_size_tiles() const { return m_size_tiles; }

        void set_size_tiles(unsigned int size_tiles) { m_size_tiles = size_tiles; }

        unsigned int get_tile_size() const { return m_tile_size; }

        void set_tile_size(unsigned int tile_size) { m_tile_size = tile_size; }

        unsigned int get_num_tiles_per_input() const { return m_num_tiles_per_input; }

        void set_num_tiles_per_input(unsigned int num_tiles_per_input) { m_num_tiles_per_input = num_tiles_per_input; }

        unsigned int get_scatter_gather_num_tiles() const { return m_scatter_gather_num_tiles; }

        void set_scatter_gather_num_tiles(unsigned int scatter_gather_num_tiles)
        {
            m_scatter_gather_num_tiles = scatter_gather_num_tiles;
        }

        int get_operand_id() const { return m_operand_id; }

        void set_operand_id(int operand_id) { m_operand_id = operand_id; }

        unsigned int get_num_queue_slots() const { return m_num_queue_slots; }

        void set_num_queue_slots(unsigned int num_queue_slots) { m_num_queue_slots = num_queue_slots; }

        bool get_dram_io_flag() const { return m_dram_io_flag; }

        void set_dram_io_flag(bool dram_io_flag) { m_dram_io_flag = dram_io_flag; }

        bool get_dram_io_flag_is_remote() const { return m_dram_io_flag_is_remote; }

        void set_dram_io_flag_is_remote(bool dram_io_flag_is_remote)
        {
            m_dram_io_flag_is_remote = dram_io_flag_is_remote;
        }

        bool get_dram_buf_streaming() const { return m_dram_buf_streaming; }

        void set_dram_buf_streaming(bool dram_buf_streaming) { m_dram_buf_streaming = dram_buf_streaming; }

        bool get_dram_buf_flag() const { return m_dram_buf_flag; }

        void set_dram_buf_flag(bool dram_buf_flag) { m_dram_buf_flag = dram_buf_flag; }

        bool get_write_dram_buf_flag() const { return m_write_dram_buf_flag; }

        void set_write_dram_buf_flag(bool write_dram_buf_flag) { m_write_dram_buf_flag = write_dram_buf_flag; }

        bool get_dram_ram_flag() const { return m_dram_ram_flag; }

        void set_dram_ram_flag(bool dram_ram_flag) { m_dram_ram_flag = dram_ram_flag; }

        bool get_moves_raw_data() const { return m_moves_raw_data; }

        void set_moves_raw_data(bool moves_raw_data) { m_moves_raw_data = moves_raw_data; }

        unsigned int get_untilized_output_full_r_dim() const { return m_untilized_output_full_r_dim; }

        void set_untilized_output_full_r_dim(unsigned int untilized_output_full_r_dim)
        {
            m_untilized_output_full_r_dim = untilized_output_full_r_dim;
        }

        unsigned int get_untilized_output_full_c_dim() const { return m_untilized_output_full_c_dim; }

        void set_untilized_output_full_c_dim(unsigned int untilized_output_full_c_dim)
        {
            m_untilized_output_full_c_dim = untilized_output_full_c_dim;
        }

        unsigned int get_untilized_output_r_dim() const { return m_untilized_output_r_dim; }

        void set_untilized_output_r_dim(unsigned int untilized_output_r_dim)
        {
            m_untilized_output_r_dim = untilized_output_r_dim;
        }

        unsigned int get_untilized_output_c_dim() const { return m_untilized_output_c_dim; }

        void set_untilized_output_c_dim(unsigned int untilized_output_c_dim)
        {
            m_untilized_output_c_dim = untilized_output_c_dim;
        }

        unsigned int get_untilized_output_z_dim() const { return m_untilized_output_z_dim; }

        void set_untilized_output_z_dim(unsigned int untilized_output_z_dim)
        {
            m_untilized_output_z_dim = untilized_output_z_dim;
        }

        unsigned int get_untilized_output_type_0_zdim() const { return m_untilized_output_type_0_zdim; }

        void set_untilized_output_type_0_zdim(unsigned int untilized_output_type_0_zdim)
        {
            m_untilized_output_type_0_zdim = untilized_output_type_0_zdim;
        }

        unsigned int get_untilized_output_type_1_zdim() const { return m_untilized_output_type_1_zdim; }

        void set_untilized_output_type_1_zdim(unsigned int untilized_output_type_1_zdim)
        {
            m_untilized_output_type_1_zdim = untilized_output_type_1_zdim;
        }

        unsigned int get_untilized_output_tile_dim_r() const { return m_untilized_output_tile_dim_r; }

        void set_untilized_output_tile_dim_r(unsigned int untilized_output_tile_dim_r)
        {
            m_untilized_output_tile_dim_r = untilized_output_tile_dim_r;
        }

        unsigned int get_untilized_output_tile_dim_c() const { return m_untilized_output_tile_dim_c; }

        void set_untilized_output_tile_dim_c(unsigned int untilized_output_tile_dim_c)
        {
            m_untilized_output_tile_dim_c = untilized_output_tile_dim_c;
        }

        unsigned int get_ublock_rt() const { return m_ublock_rt; }

        void set_ublock_rt(unsigned int ublock_rt) { m_ublock_rt = ublock_rt; }

        unsigned int get_ublock_ct() const { return m_ublock_ct; }

        void set_ublock_ct(unsigned int ublock_ct) { m_ublock_ct = ublock_ct; }

        unsigned int get_mblock_m() const { return m_mblock_m; }

        void set_mblock_m(unsigned int mblock_m) { m_mblock_m = mblock_m; }

        unsigned int get_mblock_n() const { return m_mblock_n; }

        void set_mblock_n(unsigned int mblock_n) { m_mblock_n = mblock_n; }

        unsigned int get_mblock_k() const { return m_mblock_k; }

        void set_mblock_k(unsigned int mblock_k) { m_mblock_k = mblock_k; }

        unsigned int get_tile_clear_granularity() const { return m_tile_clear_granularity; }

        void set_tile_clear_granularity(unsigned int tile_clear_granularity)
        {
            m_tile_clear_granularity = tile_clear_granularity;
        }

        NodeId get_shared_space_buffer_id() const { return m_shared_space_buffer_id; }

        void set_shared_space_buffer_id(NodeId id) { m_shared_space_buffer_id = id; }

        int get_producer_epoch_id() const { return m_producer_epoch_id; }

        void set_producer_epoch_id(int producer_epoch_id) { m_producer_epoch_id = producer_epoch_id; }

        bool is_scatter() const { return m_is_scatter; }

        void set_is_scatter(bool is_scatter) { m_is_scatter = is_scatter; }

        unsigned int get_replicate_count() const { return m_replicate_count; }

        void set_replicate_count(unsigned int replicate_count) { m_replicate_count = replicate_count; }

        int get_ethernet_channel() const { return m_ethernet_channel; }

        void set_ethernet_channel(int ethernet_channel) { m_ethernet_channel = ethernet_channel; }

        unsigned int get_dram_channel() const { return m_dram_channel; }

        void set_dram_channel(unsigned int dram_channel) { m_dram_channel = dram_channel; }

        unsigned int get_dram_sub_channel() const { return m_dram_sub_channel; }

        void set_dram_sub_channel(unsigned int dram_sub_channel) { m_dram_sub_channel = dram_sub_channel; }

        bool is_padding() const { return m_is_padding; }

        void set_is_padding(bool is_padding) { m_is_padding = is_padding; }

        std::uint64_t get_dram_address() const { return m_dram_address; }

        void set_dram_address(std::uint64_t dram_address) { m_dram_address = dram_address; }

        int get_dram_prefetch_incoming_noc_id() const { return m_dram_prefetch_incoming_noc_id; }

        void set_dram_prefetch_incoming_noc_id(int dram_prefetch_incoming_noc_id)
        {
            m_dram_prefetch_incoming_noc_id = dram_prefetch_incoming_noc_id;
        }

        PrefetchType get_prefetch_type() const { return m_prefetch_type; }

        void set_prefetch_type(PrefetchType prefetch_type) { m_prefetch_type = prefetch_type; }

        bool is_embedding_table() const { return m_embedding_table; }

        void set_embedding_table(int embedding_table) { m_embedding_table = embedding_table; }

        int get_embedding_table_core_c_div() const { return m_embedding_table_core_c_div; }

        void set_embedding_table_core_c_div(int embedding_table_core_c_div)
        {
            m_embedding_table_core_c_div = embedding_table_core_c_div;
        }

        int get_embedding_table_row_size_per_core() const { return m_embedding_table_row_size_per_core; }

        void set_embedding_table_row_size_per_core(int embedding_table_row_size_per_core)
        {
            m_embedding_table_row_size_per_core = embedding_table_row_size_per_core;
        }

        bool is_embedding_index() const { return m_embedding_index; }

        void set_embedding_index(int embedding_index) { m_embedding_index = embedding_index; }

        unsigned int get_embedding_indices_per_tile() const { return m_embedding_indices_per_tile; }

        void set_embedding_indices_per_tile(unsigned int embedding_indices_per_tile)
        {
            m_embedding_indices_per_tile = embedding_indices_per_tile;
        }

        unsigned int get_embedding_indices_per_input() const { return m_embedding_indices_per_input; }

        void set_embedding_indices_per_input(unsigned int embedding_indices_per_input)
        {
            m_embedding_indices_per_input = embedding_indices_per_input;
        }

        bool is_hw_tilize() const { return m_hw_tilize; }

        void set_hw_tilize(bool hw_tilize) { m_hw_tilize = hw_tilize; }

        unsigned int get_tilize_mblock_n_loop_num_rows() const { return m_tilize_mblock_n_loop_num_rows; }

        void set_tilize_mblock_n_loop_num_rows(unsigned int tilize_mblock_n_loop_num_rows)
        {
            m_tilize_mblock_n_loop_num_rows = tilize_mblock_n_loop_num_rows;
        }

        unsigned int get_tilize_row_col_offset() const { return m_tilize_row_col_offset; }

        void set_tilize_row_col_offset(unsigned int tilize_row_col_offset)
        {
            m_tilize_row_col_offset = tilize_row_col_offset;
        }

        bool get_use_ethernet_fw_stream() const { return m_use_ethernet_fw_stream; }

        void set_use_ethernet_fw_stream(bool use_ethernet_fw_stream)
        {
            m_use_ethernet_fw_stream = use_ethernet_fw_stream;
        }

        unsigned int get_overlay_blob_size() const { return m_overlay_blob_size; }

        void set_overlay_blob_size(unsigned int overlay_blob_size) { m_overlay_blob_size = overlay_blob_size; }

        // Returns true if buffer is Post-TM relay buffer;
        bool is_post_tm_relay_buf() const { return m_is_post_tm_relay_buf; }

        void set_is_post_tm_relay_buf(bool is_post_tm_relay_buf)
        {
            m_is_post_tm_relay_buf = is_post_tm_relay_buf;
        }

        const PGPipe* get_input_pipe() const { return m_input_pipe; }

        PGPipe* get_input_pipe() { return m_input_pipe; }

        const std::unordered_set<PGPipe*>& get_output_pipes() const { return m_output_pipes; }

        std::unordered_set<PGPipe*>& get_output_pipes() { return m_output_pipes; }

        // Returns the only output pipe buffer has. Asserts if buffer has more than one output pipe.
        const PGPipe* get_single_output_pipe() const;

        PGPipe* get_single_output_pipe();

        void set_input_pipe(PGPipe* pipe) { m_input_pipe = pipe; }

        void add_output_pipe(PGPipe* pipe) { m_output_pipes.insert(pipe); }

        void replace_output_pipe(PGPipe* old_pipe, PGPipe* new_pipe);

        // Returns true if buffer has no input pipe.
        bool has_no_input() const { return m_input_pipe == nullptr; }

        // Returns true if buffer has no output pipes.
        bool has_no_outputs() const { return m_output_pipes.empty(); }

        // Returns true if buffer has only one output pipe.
        bool has_single_output() const { return m_output_pipes.size() == 1; }

        // Returns whether this buffer shares buffer space with another buffer given by its id.
        bool shares_l1_space() const { return m_shared_space_buffer_id != -1; }

        // Returns true if buffer is forked to multiple output pipes.
        bool is_forked() const { return m_output_pipes.size() > 1; }

        // Returns true if buffer is on DRAM.
        bool is_dram() const;

        // Returns true if buffer is a DRAM prefetch buffer.
        bool is_dram_prefetch() const;

        // Returns true if buffer is a DRAM prefetch Post-TM buffer.
        bool is_dram_prefetch_post_tm() const;

        // Returns true if buffer is a DRAM prefetch Pre-TM buffer.
        bool is_dram_prefetch_pre_tm() const;

        // Returns true if buffer is input from DRAM.
        bool is_dram_input() const;

        // Returns true if buffer is input from DRAM but not prefetch Pre-TM buffer.
        bool is_non_prefetch_pre_tm_dram_input() const;

        // Returns true if buffer is output from DRAM.
        bool is_dram_output() const;

        // Returns true if buffer is input operand to kernel.
        bool is_input_operand() const;

        // Returns true if buffer is output operand from kernel.
        bool is_output_operand() const;

        // Returns true if buffer is intermediate operand from kernel.
        // TODO underlying confusion with overlaping operand ids could be avoided if m_type is used to determine this.
        bool is_intermediate_operand() const;

        // Returns true if buffer is relay based on operand_id.
        bool is_relay() const;

        // Returns true if buffer is packer.
        bool is_packer() const;

        // Returns true if buffer is unpacker.
        bool is_unpacker() const;

        // Returns true if buffer is located in L1.
        bool is_located_in_l1() const;

        // Returns true if buffer is on DRAM and is both input and output to DRAM pipes.
        bool is_end_to_end_queue() const;

    private:
        // Buffer id.
        NodeId m_id;

        // Netlist op name.
        std::string m_op_name;

        // Buffer type.
        // TODO can we rely on this attr to classify buffers further on?
        BufferType m_type;

        // Buffer logical location on the chip (counting only worker cores).
        tt_cxy_pair m_logical_location;

        // Total number of tiles that will move through this buffer in one epoch.
        unsigned int m_num_epoch_tiles;

        // Size of buffer in tiles.
        unsigned int m_size_tiles;

        // Tile size in bytes.
        unsigned int m_tile_size;

        // TODO: fill in
        unsigned int m_num_tiles_per_input;

        // Scatter transfer granularity in tiles.
        unsigned int m_scatter_gather_num_tiles;

        // Operand id, if buffer is kernel operand.
        int m_operand_id;

        // The number of queue slots in the buffer.
        unsigned int m_num_queue_slots;

        // Denotes that the buffer is a DRAM IO buffer.
        bool m_dram_io_flag;

        // Denotes that the buffer is located on another chip and that we should go through PCIE.
        bool m_dram_io_flag_is_remote;

        // Denotes a DRAM streaming buffer to transfer data on another chip through PCIE.
        bool m_dram_buf_streaming;

        // Denotes a DRAM prefetch buffer.
        bool m_dram_buf_flag;

        // Denotes an intermediate buffer that will write to DRAM at the end of the epoch.
        bool m_write_dram_buf_flag;

        // Indicates that DRAM buffer type is RAM (not Queue), for them NCRISC just gets the
        // read/write pointers and threats them like ordinary memory not like queues.
        bool m_dram_ram_flag;

        // Does the buffer represent untilized output data.
        bool m_moves_raw_data;

        // Full R dimension of a single core for untilized output.
        unsigned int m_untilized_output_full_r_dim;

        // Full C dimension of a single core for untilized output.
        unsigned int m_untilized_output_full_c_dim;

        // Untilized output R dim.
        unsigned int m_untilized_output_r_dim;

        // Untilized output C dim.
        unsigned int m_untilized_output_c_dim;

        // Untilized output Z dim.
        unsigned int m_untilized_output_z_dim;

        // TODO: fill in
        unsigned int m_untilized_output_type_0_zdim;

        // TODO: fill in
        unsigned int m_untilized_output_type_1_zdim;

        // Untilized output tile R dim.
        unsigned int m_untilized_output_tile_dim_r;

        // Untilized output tile C dim.
        unsigned int m_untilized_output_tile_dim_c;

        // Maps directly to netlist op ublock rt dimension.
        unsigned int m_ublock_rt;

        // Maps directly to netlist op ublock ct dimension.
        unsigned int m_ublock_ct;

        // Maps directly to netlist op mblock m dimension.
        unsigned int m_mblock_m;

        // Maps directly to netlist op mblock n dimension.
        unsigned int m_mblock_n;

        // Maps directly to netlist op mblock k dimension.
        unsigned int m_mblock_k;

        // How many tiles of this buffer unpacker clears per input.
        unsigned int m_tile_clear_granularity;

        // Denotes ID of another buffer this buffer shares L1 space with.
        NodeId m_shared_space_buffer_id;

        // TODO: fill in
        int m_producer_epoch_id;

        // Whether this buffer is being scattered to its output pipes.
        bool m_is_scatter;

        // Number of replicated scatter buffers created from this buffer.
        unsigned int m_replicate_count;

        // Ethernet channel through which to access this buffer, i.e. ethernet core.
        int m_ethernet_channel;

        // DRAM channel of buffer, converted by pipegen to coordinates using the device descriptor.
        unsigned int m_dram_channel;

        // Wormhole uses different DRAM memory than Grayskull (GDDR6 instead of DDR4),
        // so in order to utilize that speed we need multiple noc routers accessing same DRAM channel,
        // and those partitions of the same DRAM channel that different routers access are called subchannels.
        unsigned int m_dram_sub_channel;

        // Does this buffer hold padding tiles.
        bool m_is_padding;

        // DRAM Address of buffer.
        std::uint64_t m_dram_address;

        // Denotes which noc is used for reading from dram.
        int m_dram_prefetch_incoming_noc_id;

        // Prologue buffer type.
        PrefetchType m_prefetch_type;

        // Whether this buffer is an embedding table stored in DRAM.
        bool m_embedding_table;

        // How many cores of consumer OP on x grid are fed by one embedding table queue
        // (consumer_op_info.grid_size_logical_c() / queue_info.grid_size.c).
        int m_embedding_table_core_c_div;

        // How many tiles embedding row consists of per core of consumer op.
        int m_embedding_table_row_size_per_core;

        // Whether this buffer represents an index to embedding table.
        bool m_embedding_index;

        // How many indices are stored in a single tile.
        unsigned int m_embedding_indices_per_tile;

        // How many indices are read from a single input.
        unsigned int m_embedding_indices_per_input;

        // Is data in this buffer untilized and should be tilized on device.
        bool m_hw_tilize;

        // TODO: Math on this in net2pipe is pretty unclear, figure out what this represents.
        unsigned int m_tilize_mblock_n_loop_num_rows;

        // TODO: Math on this in net2pipe is pretty unclear, figure out what this represents.
        unsigned int m_tilize_row_col_offset;

        // When the buffer is an ethernet relay, this flag indicates that we should use FW to send the data.
        bool m_use_ethernet_fw_stream;

        // Maximum allowed size of overlay blob in bytes on core where this buffer is located. This number is ignored if
        // it's smaller than the minimum required size.
        unsigned int m_overlay_blob_size;

        // Is this a post TM relay buffer.
        bool m_is_post_tm_relay_buf;

        // Input pipe to this node.
        // TODO: non-const pointer to input pipe is needed only to accommodate the need to change PipeGraph once it is
        // created. When net2pipe is fixed this can be set to const and accessors can be rewritten to use const.
        PGPipe* m_input_pipe;

        // Unique output pipes from this node.
        // TODO: non-const pointers to output pipes are needed only to accommodate the need to change PipeGraph once it
        // is created. When net2pipe is fixed this can be set to const and accessors can be rewritten to use const.
        std::unordered_set<PGPipe*> m_output_pipes;
    };
}