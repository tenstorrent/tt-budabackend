// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "device/tt_xy_pair.h"

#include "model/pipe_graph/pg_buffer.h"
#include "model/typedefs.h"

namespace pipegen2
{
    // Pipe node in pipe graph.
    class PGPipe
    {
    public:
        // Pipe input, represented with input buffer and offset in its data address.
        class Input
        {
        public:
            explicit Input(PGBuffer* buffer) :
                m_buffer(buffer),
                m_offset(0)
            {
            }

            Input(PGBuffer* buffer, unsigned int offset) :
                m_buffer(buffer),
                m_offset(offset)
            {
            }

            const PGBuffer* get_buffer() const { return m_buffer; }

            PGBuffer* get_buffer() { return m_buffer; }

            unsigned int get_offset() const { return m_offset; }

        private:
            // Input buffer.
            // TODO: non-const pointer to buffer is needed only to accommodate the need to change PipeGraph once it is
            // created. When net2pipe is fixed this can be set to const and accessors can be rewritten to use const.
            PGBuffer* m_buffer;

            // Offset in input buffer's data address, in number of tiles.
            unsigned int m_offset;
        };

        PGPipe();

        PGPipe(NodeId id);

        NodeId get_id() const { return m_id; }

        void set_id(NodeId id) { m_id = id; }

        bool is_periodic() const { return m_pipe_periodic_repeat > 1; }

        unsigned int get_pipe_periodic_repeat() const { return m_pipe_periodic_repeat; }

        void set_pipe_periodic_repeat(unsigned int pipe_periodic_repeat)
        {
            m_pipe_periodic_repeat = pipe_periodic_repeat;
        }

        unsigned int get_consumer_repeat() const { return m_consumer_repeat; }

        void set_consumer_repeat(unsigned int consumer_repeat) { m_consumer_repeat = consumer_repeat; }

        int get_ethernet_channel() const { return m_ethernet_channel; }

        void set_ethernet_channel(int ethernet_channel) { m_ethernet_channel = ethernet_channel; }

        int get_incoming_noc_id() const { return m_incoming_noc_id; }

        void set_incoming_noc_id(int incoming_noc_id) { m_incoming_noc_id = incoming_noc_id; }

        int get_incoming_noc_vc() const { return m_incoming_noc_vc; }

        void set_incoming_noc_vc(int incoming_noc_vc) { m_incoming_noc_vc = incoming_noc_vc; }

        int get_outgoing_noc_id() const { return m_outgoing_noc_id; }

        void set_outgoing_noc_id(int outgoing_noc_id) { m_outgoing_noc_id = outgoing_noc_id; }

        int get_outgoing_noc_vc() const { return m_outgoing_noc_vc; }

        void set_outgoing_noc_vc(int outgoing_noc_vc) { m_outgoing_noc_vc = outgoing_noc_vc; }

        bool is_mmio_pipe() const { return m_mmio_pipe; }

        void set_is_mmio_pipe(bool is_mmio_pipe) { m_mmio_pipe = is_mmio_pipe; }

        bool is_mmio_pipe_downstream() const { return m_mmio_pipe_downstream; }

        void set_is_mmio_pipe_downstream(bool is_mmio_pipe_downstream)
        {
            m_mmio_pipe_downstream = is_mmio_pipe_downstream;
        }

        bool is_ethernet_pipe() const { return m_ethernet_pipe; }

        void set_is_ethernet_pipe(bool is_ethernet_pipe) { m_ethernet_pipe = is_ethernet_pipe; }

        bool is_gather_optimization_disabled() const { return m_disable_gather_optimization; }

        void set_gather_optimization_disabled(bool disable_gather_optimization)
        {
            m_disable_gather_optimization = disable_gather_optimization;
        }

        bool is_packer_multicast_optimization_enabled() const { return m_packer_multicast_optimization_enabled; }

        void set_packer_multicast_optimization_enabled(bool packer_multicast_optimization_enabled)
        {
            m_packer_multicast_optimization_enabled = packer_multicast_optimization_enabled;
        }

        unsigned int get_op_input_dram_io_buf_size_tiles() const { return m_op_input_dram_io_buf_size_tiles; }

        void set_op_input_dram_io_buf_size_tiles(unsigned int op_input_dram_io_buf_size_tiles)
        {
            m_op_input_dram_io_buf_size_tiles = op_input_dram_io_buf_size_tiles;
        }

        const std::vector<tt_cxy_pair>& get_mcast_core_logical_locations() const
        {
            return m_mcast_core_logical_locations;
        }

        void add_mcast_core_logical_location(tt_cxy_pair&& location)
        {
            m_mcast_core_logical_locations.push_back(std::move(location));
        }

        const std::vector<int>& get_dram_pipe_total_readers() const { return m_dram_pipe_total_readers; }

        void set_dram_pipe_total_readers(const std::vector<int>& dram_pipe_total_readers)
        {
            m_dram_pipe_total_readers = dram_pipe_total_readers;
        }

        const std::vector<int>& get_dram_pipe_reader_index() const { return m_dram_pipe_reader_index; }

        void set_dram_pipe_reader_index(const std::vector<int>& dram_pipe_reader_index)
        {
            m_dram_pipe_reader_index = dram_pipe_reader_index;
        }

        const std::vector<NodeId>& get_input_buffers_ids() const { return m_input_buffers_ids; }

        void set_input_buffers_ids(const std::vector<NodeId>& input_buffers_ids)
        {
            m_input_buffers_ids = input_buffers_ids;
        }

        const std::vector<std::vector<NodeId>>& get_output_buffers_ids() const { return m_output_buffers_ids; }

        void set_output_buffers_ids(const std::vector<std::vector<NodeId>>& output_buffers_ids)
        {
            m_output_buffers_ids = output_buffers_ids;
        }

        const std::vector<NodeId>& get_output_padding_buffers_ids() const { return m_output_padding_buffers_ids; }

        void set_output_padding_buffers_ids(const std::vector<NodeId>& output_padding_buffers_ids)
        {
            m_output_padding_buffers_ids = output_padding_buffers_ids;
        }

        void add_output_buffer_id(const NodeId id)
        {
            m_output_buffers_ids.push_back({id});
        }

        void add_input_buffer_id(const NodeId id)
        {
            m_input_buffers_ids.push_back(id);
        }

        // Returns a list of inputs for a given scatter index - we have to get inputs depending on the scatter index
        // because when pipe has output padding then for that scatter index it reads only those padding buffers from
        // the output padding list, otherwise it reads "normal" input buffers.
        const std::vector<Input>& get_inputs(const std::size_t scatter_index) const;

        std::vector<Input>& get_inputs(const std::size_t scatter_index);

        // Returns a list of "normal" pipe inputs.
        const std::vector<Input>& get_inputs() const { return get_inputs(0); }

        std::vector<Input>& get_inputs() { return get_inputs(0); }

        // Returns a set of all unique input buffers - these are "normal" pipe input buffers (excluding implicit output
        // padding input buffers). Needs to return a vector in order to keep the order of the elements that are 
        // processed and printed out to blob.yaml.
        std::vector<const PGBuffer*> get_unique_input_buffers() const;

        // Returns a set of all unique output padding buffers which are used as implicit pipe inputs in scatter indexes
        // in which pipe reads from padding buffers.
        std::vector<const PGBuffer*> get_unique_output_padding_buffers() const;

        // Returns a set of all unique input buffer which includes the output padding buffers.
        std::vector<const PGBuffer*> get_unique_input_buffers_including_output_padding() const;

        const std::vector<std::vector<PGBuffer*>>& get_output_buffers() const { return m_output_buffers; }

        std::vector<std::vector<PGBuffer*>>& get_output_buffers() { return m_output_buffers; }

        // Returns the only output buffer it has. Asserts if pipe has more than one output buffer.
        const PGBuffer* get_single_output_buffer() const;

        PGBuffer* get_single_output_buffer();

        void add_input(Input& input) { m_inputs.push_back(input); }

        void add_input_buffer(PGBuffer* buffer) { m_inputs.emplace_back(Input(buffer)); }

        void add_input_buffer(PGBuffer* buffer, unsigned int offset)
        {
            m_inputs.emplace_back(Input(buffer, offset));
        }

        void remove_all_inputs();

        void add_output_buffer(PGBuffer* buffer, std::size_t scatter_index);

        // Adds an output padding buffer for a given scatter index. An output padding buffer is replicated such that
        // the replication number * scatter gather num tiles of this buffer is the same as the number of tiles pipes
        // transfers for every scatter index.
        void add_output_padding_buffer(PGBuffer* output_padding_buffer, std::size_t scatter_index);

        // Replaces the old output buffer with the new output buffer, for each of its scatter outputs.
        void replace_output_buffer(PGBuffer* old_buffer, PGBuffer* new_buffer);

        // Returns the number of tiles pipe transfers per scatter index.
        unsigned int get_num_msgs_per_scatter_index() const;

        // Returns the number of scatter outputs.
        std::size_t get_scatter_fanout() const { return m_output_buffers.size(); }

        // Returns the number of scatter indexes where the pipe uses output padding instead of regular pipe inputs.
        std::size_t get_padding_scatter_fanout() const { return m_scatter_index_to_output_padding_inputs.size(); }

        bool has_single_input() const { return m_inputs.size() == 1; }

        bool has_single_output() const { return m_output_buffers.size() == 1 && m_output_buffers[0].size() == 1; }

        bool is_pipe_scattering() const { return m_output_buffers.size() > 1; }

        // Returns true if pipe has consumer duplicates - either implicitly by having consumer_repeat > 1 or explicity
        // by having actual duplicates in the output list.
        bool has_consumer_duplicates() const;

        // Returns true if pipe has duplicates in the output list.
        bool has_output_list_duplicates() const;

        // Returns if, for a given scatter index, pipe reads from output padding buffer.
        bool is_output_padding(std::size_t scatter_index) const;

        // Returns true if the pipe's source and dest buffers are all located in L1.
        bool is_connecting_l1_buffers() const;

        // Returns true if pipe has dram input which is not of type prefetch Pre-TM.
        bool has_non_prefetch_pre_tm_dram_input() const;

        // Returns true if pipe inputs all point to the same buffer.
        bool has_single_buffer_input() const;

        // Returns true if pipe is connecting a single input and single output intermediate operands.
        bool is_direct_intermediate_pipe() const;

        // Returns true if pipe is connecting two input and one output intermediate operands.
        bool is_join_intermediate_pipe() const;

        // Returns true if pipe only has scatter DRAM prefetch post TM inputs.
        bool is_scatter_prefetch_post_tm() const;

    private:
        // Pipe id.
        NodeId m_id;

        // How many times to repeat inputs in inner loop (same tiles).
        unsigned int m_pipe_periodic_repeat;

        // How many times outputs are repeated.
        unsigned int m_consumer_repeat;

        // Ethernet channel pipe is using for transfer.
        int m_ethernet_channel;

        // ID of the NOC used to receive data.
        int m_incoming_noc_id;

        // Virtual channel of the NOC used to receive data.
        int m_incoming_noc_vc;

        // ID of the NOC used to send data.
        int m_outgoing_noc_id;

        // Virtual channel of the NOC used to send data.
        int m_outgoing_noc_vc;

        // Denotes whether pipe is used to transfer data through PCIE.
        // It will be set up to point to/from buffer denoted as dram_buf_streaming.
        bool m_mmio_pipe;

        // Denotes the direction of transfer across the PCIE ring
        // (downwards i.e. downstream, or upwards i.e. upstream).
        bool m_mmio_pipe_downstream;

        // Denotes whether the pipe goes through ethernet, connects two ethernet cores.
        bool m_ethernet_pipe;

        // Denotes we should disable allocating local relay streams for gather optimization when creating streams for
        // this pipe.
        bool m_disable_gather_optimization;

        // True if pipe has packer multicast optimization enabled where we can avoid creating both packer and
        // multicast streams and just use multicast stream directly from the packer core.
        bool m_packer_multicast_optimization_enabled;

        // Max amount of L1 space pipegen is allowed to use for scaling the input dram buffers of this pipe.
        unsigned int m_op_input_dram_io_buf_size_tiles;

        // Logical locations of the cores doing multicast for this pipe.
        std::vector<tt_cxy_pair> m_mcast_core_logical_locations;

        // Vector containing total amount of pipes that read from every input in this pipe's list of inputs.
        std::vector<int> m_dram_pipe_total_readers;

        // Vector containing reader index of this pipe for every input in the pipe. Used when multiple pipes read from
        // the same (forked) input.
        std::vector<int> m_dram_pipe_reader_index;

        // IDs of input buffers to this pipe.
        std::vector<NodeId> m_input_buffers_ids;

        // IDs of output buffers of this pipe, for each of its scatter outputs.
        std::vector<std::vector<NodeId>> m_output_buffers_ids;

        // IDs of output padding buffers - c_no_scatter_padding_buffer_id if no padding is used for that scatter index,
        // otherwise padding buffer id.
        std::vector<NodeId> m_output_padding_buffers_ids;

        // Inputs to this pipe.
        std::vector<Input> m_inputs;

        // Output buffers from this pipe, for each of its scatter outputs.
        // TODO: non-const pointers to buffers are needed only to accommodate the need to change PipeGraph once it is
        // created. When net2pipe is fixed this can be set to const and accessors can be rewritten to use const.
        std::vector<std::vector<PGBuffer*>> m_output_buffers;

        // List of all the buffers which are used as inputs to output padding grouped by the scatter index.
        std::unordered_map<std::size_t, std::vector<Input>> m_scatter_index_to_output_padding_inputs;
    };
}