// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "base_stream_destination.h"
#include "base_stream_source.h"
#include "dram_config.h"
#include "model/pipe_graph/base_node.h"
#include "model/stream_graph/phase.h"
#include <string>
//TODO: Move to a separate file of constants representing string literals in blob.yaml
namespace pipegen2
{
    const std::string blob_yaml_chip_key = "chip_";
    const std::string blob_yaml_tensix_y_key = "__y_";
    const std::string blob_yaml_tensix_x_key = "__x_";
    const std::string blob_yaml_stream_key = "__stream_id_";
}
namespace pipegen2
{
    // Base streams class, containing common stream parameters.
    class BaseStreamNode
    {
    public:
        using Ptr = std::shared_ptr<BaseStreamNode>;
        using UPtr = std::unique_ptr<BaseStreamNode>;

    private:
        // Contains common parameters for stream configuration.
        // Parameters are extracted into structure to make their copying easier and less error prone.

        struct StreamId
        {
            StreamId() = default;

            StreamId(
                unsigned int chip_id,
                unsigned int tensix_coord_y,
                unsigned int tensix_coord_x,
                unsigned int stream_id)
            {
                this->chip_id = chip_id;
                this->tensix_coord_y = tensix_coord_y;
                this->tensix_coord_x = tensix_coord_x;
                this->stream_id = stream_id;
            }

            unsigned int chip_id;

            unsigned int tensix_coord_y;
            unsigned int tensix_coord_x;

            unsigned int stream_id;
        };

        struct StreamConfig
        {
            StreamConfig() = default;

            StreamConfig(unsigned int msg_size, unsigned int num_iters_in_epoch, unsigned int num_unroll_iter, unsigned int reg_update_vc)
            {
                this->msg_size = msg_size;
                this->num_iters_in_epoch = num_iters_in_epoch;
                this->num_unroll_iter = num_unroll_iter;
                this->reg_update_vc = reg_update_vc;

                // Currently always set to true.
                this->data_auto_send = true;
                this->phase_auto_advance = true;
            }

            // Tile size.
            unsigned int msg_size;

            // Number of iterations in epoch.
            unsigned int num_iters_in_epoch;

            // Number of iterations after unrolling.
            unsigned int num_unroll_iter;

            // TODO: fill in
            bool data_auto_send;

            // TODO: fill in
            bool phase_auto_advance;

            // TODO: fill in
            unsigned int reg_update_vc;

            // ID of the buffer that this stream is handling.
            std::optional<NodeId> buf_id;

            // True if buffer is intermediate of a kernel.
            std::optional<bool> intermediate;

            // Address in the buffer to write to or read from.
            // TODO: check if this is correct
            std::optional<unsigned int> buf_addr;

            // TODO: fill in
            std::optional<unsigned int> msg_info_buf_addr;

            // TODO: fill in
            std::optional<uint64_t> dram_buf_noc_addr;

            // TODO: fill in
            std::optional<unsigned int> buf_size;

            // TODO: fill in
            std::optional<std::vector<unsigned int>> fork_stream_ids;

            // TODO: fill in
            std::optional<unsigned int> num_fork_streams;

            // TODO: fill in
            std::optional<unsigned int> group_priority;

            // True if stream is reading from or writing directly to DRAM.
            std::optional<bool> dram_io;

            // True if stream is streaming from/to DRAM.
            std::optional<bool> dram_streaming;

            // True if stream is reading/streaming from DRAM.
            std::optional<bool> dram_input;

            // TODO: fill in
            std::optional<bool> dram_input_no_push;

            // True if stream is writing/streaming to DRAM.
            std::optional<bool> dram_output;

            // TODO: fill in
            std::optional<bool> dram_output_no_push;

            // TODO: fill in
            std::optional<unsigned int> producer_epoch_id;

            // TODO: fill in
            std::optional<unsigned int> num_scatter_inner_loop;

            // TODO: fill in
            std::optional<uint64_t> pipe_id;

            // TODO: fill in
            std::optional<unsigned int> pipe_scatter_output_loop_count;

            // TODO: fill in
            std::optional<bool> resend;

            // TODO: fill in
            std::optional<unsigned int> buf_base_addr;

            // TODO: fill in
            std::optional<unsigned int> buf_full_size_bytes;

            // TODO: fill in
            std::optional<unsigned int> num_mblock_buffering;

            // TODO: fill in
            std::optional<unsigned int> scatter_idx;

            // TODO: fill in
            std::optional<unsigned int> scatter_order_size;

            // TODO: fill in
            std::optional<unsigned int> num_msgs_in_block;

            // TODO: fill in
            std::optional<bool> is_scatter_pack;
        };

    public:
        virtual ~BaseStreamNode() = default;

        BaseStreamNode* clone() const;

        const BaseStreamSource* get_stream_source() const { return m_source.get(); }

        const BaseStreamDestination* get_stream_destination() const { return m_destination.get(); }

        const unsigned int get_chip_id() const { return m_stream_id.chip_id; }

        void set_chip_id(const unsigned int chip_id) { m_stream_id.chip_id = chip_id; }

        const unsigned int get_tensix_coord_y() const { return m_stream_id.tensix_coord_y; }

        void set_tensix_coord_y(const unsigned int coord_y) { m_stream_id.tensix_coord_y = coord_y; }

        const unsigned int get_tensix_coord_x() const { return m_stream_id.tensix_coord_x; }

        void set_tensix_coord_x(const unsigned int coord_x) { m_stream_id.tensix_coord_x = coord_x; }

        const unsigned int get_stream_id() const { return m_stream_id.stream_id; }

        void set_stream_id(const unsigned int stream_id) { m_stream_id.stream_id = stream_id; }

        const std::optional<NodeId>& get_buffer_id() const { return m_stream_config.buf_id; }

        void set_buffer_id(std::optional<NodeId> id) { m_stream_config.buf_id = id; }
        void set_buffer_id(NodeId id) { m_stream_config.buf_id = id; }

        const std::optional<bool>& is_buffer_intermediate() const { return m_stream_config.intermediate; }

        void set_buffer_intermediate(std::optional<bool> value) { m_stream_config.intermediate = value; }
        void set_buffer_intermediate(bool value) { m_stream_config.intermediate = value; }

        const std::optional<unsigned int>& get_buffer_address() const { return m_stream_config.buf_addr; }

        void set_buffer_address(std::optional<unsigned int> value) { m_stream_config.buf_addr = value; }
        void set_buffer_address(unsigned int value) { m_stream_config.buf_addr = value; }

        const std::optional<unsigned int>& get_msg_info_buf_addr() const { return m_stream_config.msg_info_buf_addr; }

        void set_msg_info_buf_addr(std::optional<unsigned int> value) { m_stream_config.msg_info_buf_addr = value; }
        void set_msg_info_buf_addr(unsigned int value) { m_stream_config.msg_info_buf_addr = value; }

        const std::optional<uint64_t>& get_dram_buf_noc_addr() const { return m_stream_config.dram_buf_noc_addr; }

        void set_dram_buf_noc_addr(std::optional<uint64_t> value) { m_stream_config.dram_buf_noc_addr = value; }
        void set_dram_buf_noc_addr(uint64_t value) { m_stream_config.dram_buf_noc_addr = value; }

        const std::optional<unsigned int>& get_buffer_size() const { return m_stream_config.buf_size; }

        void set_buffer_size(std::optional<unsigned int> value) { m_stream_config.buf_size = value; }
        void set_buffer_size(unsigned int value) { m_stream_config.buf_size = value; }

        unsigned int get_tile_size() const { return m_stream_config.msg_size; }

        void set_tile_size(unsigned int value) { m_stream_config.msg_size = value; }

        unsigned int get_num_iterations_in_epoch() const { return m_stream_config.num_iters_in_epoch; }

        void set_num_iterations_in_epoch(unsigned int value) { m_stream_config.num_iters_in_epoch = value; }

        unsigned int get_num_unrolled_iterations() const { return m_stream_config.num_unroll_iter; }

        void set_num_unrolled_iterations(unsigned int value) { m_stream_config.num_unroll_iter = value; }

        bool is_data_auto_send() const { return m_stream_config.data_auto_send; }

        void set_data_auto_send(bool value) { m_stream_config.data_auto_send = value; }

        bool is_phase_auto_advance() const { return m_stream_config.phase_auto_advance; }

        void set_phase_auto_advance(bool value) { m_stream_config.phase_auto_advance = value; }

        unsigned int get_reg_update_vc() const { return m_stream_config.reg_update_vc; }

        void set_reg_update_vc(unsigned int value) { m_stream_config.reg_update_vc = value; }

        const std::optional<std::vector<unsigned int>>& get_fork_stream_ids() const { return m_stream_config.fork_stream_ids; }

        void set_fork_stream_ids(const std::optional<std::vector<unsigned int>>& ids) { m_stream_config.fork_stream_ids = ids; }
        void set_fork_stream_ids(const std::vector<unsigned int>& ids) { m_stream_config.fork_stream_ids = ids; }

        const std::optional<unsigned int>& get_num_fork_streams() const { return m_stream_config.num_fork_streams; }

        void set_num_fork_streams(std::optional<unsigned int> value) { m_stream_config.num_fork_streams = value; }
        void set_num_fork_streams(unsigned int value) { m_stream_config.num_fork_streams = value; }

        const std::optional<unsigned int>& get_group_priority() const { return m_stream_config.group_priority; }

        void set_group_priority(std::optional<unsigned int> value) { m_stream_config.group_priority = value; }
        void set_group_priority(unsigned int value) { m_stream_config.group_priority = value; }

        const std::optional<bool>& is_dram_io() const { return m_stream_config.dram_io; }

        void set_dram_io(std::optional<bool> value) { m_stream_config.dram_io = value; }
        void set_dram_io(bool value) { m_stream_config.dram_io = value; }

        const std::optional<bool>& is_dram_streaming() const { return m_stream_config.dram_streaming; }

        void set_dram_streaming(std::optional<bool> value) { m_stream_config.dram_streaming = value; }
        void set_dram_streaming(bool value) { m_stream_config.dram_streaming = value; }

        const std::optional<bool>& is_dram_input() const { return m_stream_config.dram_input; }

        void set_dram_input(std::optional<bool> value) { m_stream_config.dram_input = value; }
        void set_dram_input(bool value) { m_stream_config.dram_input = value; }

        const std::optional<bool>& is_dram_input_no_push() const { return m_stream_config.dram_input_no_push; }

        void set_dram_input_no_push(std::optional<bool> value) { m_stream_config.dram_input_no_push = value; }
        void set_dram_input_no_push(bool value) { m_stream_config.dram_input_no_push = value; }

        const std::optional<bool>& is_dram_output() const { return m_stream_config.dram_output; }

        void set_dram_output(std::optional<bool> value) { m_stream_config.dram_output = value; }
        void set_dram_output(bool value) { m_stream_config.dram_output = value; }

        const std::optional<bool>& is_dram_output_no_push() const { return m_stream_config.dram_output_no_push; }

        void set_dram_output_no_push(std::optional<bool> value) { m_stream_config.dram_output_no_push = value; }
        void set_dram_output_no_push(bool value) { m_stream_config.dram_output_no_push = value; }

        const std::optional<unsigned int>& get_producer_epoch_id() const { return m_stream_config.producer_epoch_id; }

        void set_producer_epoch_id(std::optional<unsigned int> value) { m_stream_config.producer_epoch_id = value; }
        void set_producer_epoch_id(unsigned int value) { m_stream_config.producer_epoch_id = value; }

        const std::optional<unsigned int>& get_num_scatter_inner_loop() const { return m_stream_config.num_scatter_inner_loop; }

        void set_num_scatter_inner_loop(std::optional<unsigned int> value) { m_stream_config.num_scatter_inner_loop = value; }
        void set_num_scatter_inner_loop(unsigned int value) { m_stream_config.num_scatter_inner_loop = value; }

        const std::optional<uint64_t>& get_pipe_id() const { return m_stream_config.pipe_id; }

        void set_pipe_id(std::optional<uint64_t> value) { m_stream_config.pipe_id = value; }
        void set_pipe_id(uint64_t value) { m_stream_config.pipe_id = value; }

        const std::optional<unsigned int>& get_pipe_scatter_output_loop_count() const { return m_stream_config.pipe_scatter_output_loop_count; }

        void set_pipe_scatter_output_loop_count(std::optional<unsigned int> value) { m_stream_config.pipe_scatter_output_loop_count = value; }
        void set_pipe_scatter_output_loop_count(unsigned int value) { m_stream_config.pipe_scatter_output_loop_count = value; }

        const std::optional<bool>& is_resend() const { return m_stream_config.resend; }

        void set_resend(std::optional<bool> value) { m_stream_config.resend = value; }
        void set_resend(bool value) { m_stream_config.resend = value; }

        const std::optional<unsigned int>& get_buf_base_addr() const { return m_stream_config.buf_base_addr; }

        void set_buf_base_addr(std::optional<unsigned int> value) { m_stream_config.buf_base_addr = value; }
        void set_buf_base_addr(unsigned int value) { m_stream_config.buf_base_addr = value; }

        const std::optional<unsigned int>& get_buf_full_size_bytes() const { return m_stream_config.buf_full_size_bytes; }

        void set_buf_full_size_bytes(std::optional<unsigned int> value) { m_stream_config.buf_full_size_bytes = value; }
        void set_buf_full_size_bytes(unsigned int value) { m_stream_config.buf_full_size_bytes = value; }

        const std::optional<unsigned int>& get_num_mblock_buffering() const { return m_stream_config.num_mblock_buffering; }

        void set_num_mblock_buffering(std::optional<unsigned int> value) { m_stream_config.num_mblock_buffering = value; }
        void set_num_mblock_buffering(unsigned int value) { m_stream_config.num_mblock_buffering = value; }

        const std::optional<unsigned int>& get_scatter_idx() const { return m_stream_config.scatter_idx; }

        void set_scatter_idx(std::optional<unsigned int> value) { m_stream_config.scatter_idx = value; }
        void set_scatter_idx(unsigned int value) { m_stream_config.scatter_idx = value; }

        const std::optional<unsigned int>& get_scatter_order_size() const { return m_stream_config.scatter_order_size; }

        void set_scatter_order_size(std::optional<unsigned int> value) { m_stream_config.scatter_order_size = value; }
        void set_scatter_order_size(unsigned int value) { m_stream_config.scatter_order_size = value; }

        const std::optional<unsigned int>& get_num_msgs_in_block() const { return m_stream_config.num_msgs_in_block; }

        void set_num_msgs_in_block(std::optional<unsigned int> value) { m_stream_config.num_msgs_in_block = value; }
        void set_num_msgs_in_block(unsigned int value) { m_stream_config.num_msgs_in_block = value; }

        const std::optional<bool>& is_scatter_pack() const { return m_stream_config.is_scatter_pack; }

        void set_scatter_pack(std::optional<bool> value) { m_stream_config.is_scatter_pack = value; }
        void set_scatter_pack(bool value) { m_stream_config.is_scatter_pack = value; }

        const std::vector<Phase>& get_phases() const { return m_phases; }

        void add_phase(const Phase& phase) { m_phases.push_back(phase); }

        const std::vector<DramConfig>& get_dram_configs() const { return m_dram_configs; }

        void add_dram_config(const DramConfig& dram_config) { m_dram_configs.emplace_back(dram_config); }

    protected:
        BaseStreamNode() = default;

        BaseStreamNode(unsigned int msg_size, unsigned int num_iters_in_epoch, unsigned int num_unroll_iter, unsigned int reg_update_vc,
            std::unique_ptr<BaseStreamSource> source, std::unique_ptr<BaseStreamDestination> destination);

        // Each derived stream should implement this to clone its additional config.
        virtual BaseStreamNode* clone_internal() const = 0;

        // Stream identificator within a multi-chip system.
        StreamId m_stream_id;

        // Common stream config.
        StreamConfig m_stream_config;

        // Source config for this stream.
        std::unique_ptr<BaseStreamSource> m_source;

        // Destination config for this stream.
        std::unique_ptr<BaseStreamDestination> m_destination;

        // Phases for this stream.
        std::vector<Phase> m_phases;

        // List of dram configs of this stream. One config per unique dram buffer that stream reads from or writes to.
        std::vector<DramConfig> m_dram_configs;
    };
}