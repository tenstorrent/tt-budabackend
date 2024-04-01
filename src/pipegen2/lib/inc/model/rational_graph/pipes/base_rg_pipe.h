// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "device/tt_xy_pair.h"

#include "model/data_flow/data_flow_info.h"
#include "model/rational_graph/nodes/virtual_node.h"
#include "model/typedefs.h"

namespace pipegen2
{
    class RGBaseNode;
    class UnpackerOutputNode;
    class DramInputNode;

    // Enumerates different types of rational graph pipes.
    // Warning: Don't use this type in your logic to check for certain pipe type if that could lead to your logic not
    // working for new pipes in the future extending that pipe. Use casts in such scenarios.
    enum class RGPipeType
    {
        DramUnicast = 0,
        DramTilizer,
        Unicast,
        UnicastToDram,
        NonSharedIntermed,
        DramOutputIntermed,
        SharedPackerIntermed,
        DramMulticast,
        Multicast,
        DramParallelFork,
        DramPrefetchPreTMParallelFork,
        DramPrefetchPostTMUnicast,
        ParallelFork,
        SerialFork,
        PaddingSerialFork,
        DramGather,
        DramEmbedding,
        Gather,
        GatherToDram,
        DramPrefetchPostTMGather,
        Relay,
        PCIeUnicast,
        UnicastToPCIe,
        PCIeMulticast,
        GatherToPCIe,
        EthernetRelay,
        EthernetFWRelay,
        DormantRelay,
        Union
    };

#ifdef TT_DEBUG
    // Returns appropriate name string for each pipe type, used for debug visualizer to visualize rational graph.
    std::string pipe_type_to_string(RGPipeType pipe_type);
#endif

    // Types of dataflow in a pipe.
    enum class DataFlowType
    {
        // Denotes a transfer where producer node sends data to multiple consumer nodes and each data path
        // simultaneously and the transfers are independent from each other (tiles could be sent in different order on
        // different data paths).
        Parallel = 0,

        // Denotes a data transfer between producer and (multiple) consumers simultaneously where the producer sends
        // same data to consumers in a single transaction.
        ParallelCopy,

        // Denotes a transfer where producer node sends data to multiple consumer nodes sequentially, i.e. in the
        // first timestamp producer sends data to one destination, then in the next timestamp it sends data to another
        // destination and so on.
        Serial
    };

    // Pipe input, represented with input node and offset in its data address.
    class PipeInput
    {
    public:
        explicit PipeInput(const RGBaseNode* node) :
            m_node(node),
            m_offset(0)
        {
        }

        PipeInput(const RGBaseNode* node, unsigned int offset) :
            m_node(node),
            m_offset(offset)
        {
        }

        const RGBaseNode* get_node() const { return m_node; }

        void set_node(const RGBaseNode* node) { m_node = node; }

        unsigned int get_offset() const { return m_offset; }

        // Returns input node if it is not virtual, otherwise returns its parent.
        const RGBaseNode* get_non_virtual_node() const
        {
            return VirtualNode::get_non_virtual_node(m_node);
        }

    private:
        // Input node.
        const RGBaseNode* m_node;

        // Offset in input node's data address, in number of tiles.
        unsigned int m_offset;
    };

    class RGPipeProperties
    {
    public:
        RGPipeProperties(NodeId pipe_id = -1,
                         unsigned int incoming_noc_id = -1,
                         unsigned int incoming_noc_vc = -1,
                         unsigned int outgoing_noc_id = -1,
                         unsigned int outgoing_noc_vc = -1,
                         unsigned int periodic_repeat = 1,
                         unsigned int consumer_repeat = 1,
                         int ethernet_channel = -1,
                         bool is_ethernet_pipe = false) :
            m_pipe_id(pipe_id),
            m_incoming_noc_id(incoming_noc_id),
            m_incoming_noc_vc(incoming_noc_vc),
            m_outgoing_noc_id(outgoing_noc_id),
            m_outgoing_noc_vc(outgoing_noc_vc),
            m_periodic_repeat(periodic_repeat),
            m_consumer_repeat(consumer_repeat),
            m_ethernet_channel(ethernet_channel),
            m_is_ethernet_pipe(is_ethernet_pipe)
        {
        }

        RGPipeProperties(const RGPipeProperties& other) :
            m_pipe_id(other.m_pipe_id),
            m_incoming_noc_id(other.m_incoming_noc_id),
            m_incoming_noc_vc(other.m_incoming_noc_vc),
            m_outgoing_noc_id(other.m_outgoing_noc_id),
            m_outgoing_noc_vc(other.m_outgoing_noc_vc),
            m_periodic_repeat(other.m_periodic_repeat),
            m_consumer_repeat(other.m_consumer_repeat),
            m_ethernet_channel(other.m_ethernet_channel),
            m_is_ethernet_pipe(other.m_is_ethernet_pipe)
        {
        }

        void set_incoming_noc_id(unsigned int incoming_noc_id) { m_incoming_noc_id = incoming_noc_id; }

        void set_incoming_noc_vc(unsigned int incoming_noc_vc) { m_incoming_noc_vc = incoming_noc_vc; }

        void set_pipe_id(NodeId pipe_id) { m_pipe_id = pipe_id; }

        NodeId get_pipe_id() const { return m_pipe_id; }

        unsigned int get_incoming_noc_id() const { return m_incoming_noc_id; }

        unsigned int get_incoming_noc_vc() const { return m_incoming_noc_vc; }

        unsigned int get_outgoing_noc_id() const { return m_outgoing_noc_id; }

        unsigned int get_outgoing_noc_vc() const { return m_outgoing_noc_vc; }

        unsigned int get_periodic_repeat() const { return m_periodic_repeat; }

        unsigned int get_consumer_repeat() const { return m_consumer_repeat; }

        int get_ethernet_channel() const { return m_ethernet_channel; }

        bool is_ethernet_pipe() const { return m_is_ethernet_pipe; }

    private:
        // Pipe ID, used for debug purposes.
        // Currently set to ID of the PipeGraph pipe from which this pipe was created from.
        NodeId m_pipe_id;

        // ID of the NOC used to receive data.
        unsigned int m_incoming_noc_id;

        // Virtual channel of the NOC used to receive data.
        unsigned int m_incoming_noc_vc;

        // ID of the NOC used to send data.
        unsigned int m_outgoing_noc_id;

        // Virtual channel of the NOC used to send data.
        unsigned int m_outgoing_noc_vc;

        // How many times to transfer the same pipe inputs in a loop (same tiles).
        unsigned int m_periodic_repeat;

        // How many times to repeat the transfer (new tiles) towards the consumers.
        unsigned int m_consumer_repeat;

        // If pipe sends data over ethernet, this is the ethernet channel used for the transfer.
        int m_ethernet_channel;

        // Whether this pipe receives data over ethernet.
        bool m_is_ethernet_pipe;
    };

    // Rational graph base pipe class.
    class RGBasePipe
    {
    public:
        virtual ~RGBasePipe() = default;

        // Returns pipe type.
        // Warning: Don't use this type in your logic to check for certain pipe type if that could lead to your logic
        // not working for new pipes in the future extending that pipe. Use casts in such scenarios.
        RGPipeType get_pipe_type() const { return m_pipe_type; }

        DataFlowType get_dataflow_type() const { return m_dataflow_type; }

        const RGPipeProperties& get_pipe_properties() const { return m_pipe_properties; }

        NodeId get_id() const { return m_pipe_properties.get_pipe_id(); }

        unsigned int get_incoming_noc_id() const { return m_pipe_properties.get_incoming_noc_id(); }

        unsigned int get_incoming_noc_vc() const { return m_pipe_properties.get_incoming_noc_vc(); }

        unsigned int get_outgoing_noc_id() const { return m_pipe_properties.get_outgoing_noc_id(); }

        unsigned int get_outgoing_noc_vc() const { return m_pipe_properties.get_outgoing_noc_vc(); }

        unsigned int get_periodic_repeat() const { return m_pipe_properties.get_periodic_repeat(); }

        unsigned int get_consumer_repeat() const { return m_pipe_properties.get_consumer_repeat(); }

        int get_ethernet_channel() const { return m_pipe_properties.get_ethernet_channel(); }

        bool is_ethernet_pipe() const { return m_pipe_properties.is_ethernet_pipe(); }

        bool is_part_of_scattered_pipe() const { return m_is_part_of_scattered_pipe; }

        void set_is_part_of_scattered_pipe(bool is_part_of_scattered_pipe)
        {
            m_is_part_of_scattered_pipe = is_part_of_scattered_pipe;
        }

        const tt_cxy_pair& get_physical_location() const { return m_physical_location; }

        virtual const std::vector<PipeInput>& get_inputs() const { return m_inputs; }

        const RGBaseNode* get_first_input_node() const;

        // Expects that pipe has a single input node and returns it, asserts otherwise.
        const RGBaseNode* get_input_node() const;

        const std::vector<const RGBaseNode*>& get_output_nodes() const { return m_output_nodes; }

        // Expects that pipe has a single output node and returns it, asserts otherwise.
        const RGBaseNode* get_output_node() const;

        void add_input(const PipeInput& input) { m_inputs.push_back(input); }

        void add_input_node(const RGBaseNode* node) { m_inputs.emplace_back(PipeInput(node)); }

        void add_input_node(const RGBaseNode* node, unsigned int offset)
        {
            m_inputs.emplace_back(PipeInput(node, offset));
        }

        void add_output_node(const RGBaseNode* node) { m_output_nodes.push_back(node); }

        // Replaces all occurrences of the old input node with the new input node.
        void replace_input_node(const RGBaseNode* old_node, const RGBaseNode* new_node);

        // Replaces the old output node with the new output node in the output nodes vector.
        void replace_output_node(const RGBaseNode* old_node, const RGBaseNode* new_node);

        // Swaps two output nodes with given indexes.
        void swap_output_nodes(const std::size_t first_output_node_index, const std::size_t second_output_node_index);

        // Gets all unique input nodes, in the order they appear in pipe inputs.
        // Expensive, creates vector for each call.
        std::vector<const RGBaseNode*> get_unique_input_nodes() const;

        // Returns all offsets with which a given node appears in pipe inputs.
        // Expensive, creates vector for each call.
        std::vector<unsigned int> get_input_node_offsets(const RGBaseNode* input_node) const;

        // Returns the least possible number of tiles to transfer in one chunk of input data.
        virtual unsigned int get_min_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const;

        // Returns total number of tiles to transfer that is accumulated from all inputs.
        virtual unsigned int get_num_tiles_to_transfer(const DataFlowInfo& data_flow_info) const;

        // Gets the first dram input node of a RG pipe.
        const DramInputNode* get_first_dram_input_node() const;

    protected:
        RGBasePipe(RGPipeType pipe_type,
                   DataFlowType dataflow_type,
                   RGPipeProperties&& rg_pipe_properties,
                   const tt_cxy_pair& physical_location) :
            m_pipe_type(pipe_type),
            m_dataflow_type(dataflow_type),
            m_pipe_properties(std::move(rg_pipe_properties)),
            m_physical_location(physical_location),
            m_is_part_of_scattered_pipe(false)
        {
        }

        // Pipe type.
        RGPipeType m_pipe_type;

        // Dataflow type.
        DataFlowType m_dataflow_type;

        // Pipe properties such as pipe_id, incoming_noc_id, etc.
        RGPipeProperties m_pipe_properties;

        // Whether this pipe was created from a scattered PipeGraph pipe.
        bool m_is_part_of_scattered_pipe;

        // Physical location of the core which does the transfer for this pipe.
        tt_cxy_pair m_physical_location;

        // Inputs to this pipe.
        std::vector<PipeInput> m_inputs;

        // Output nodes from this pipe.
        std::vector<const RGBaseNode*> m_output_nodes;

        // TODO: After we implement streams creation for all pipes, check if all the fields above are needed in all
        // pipes, and if they can default to certain values in some pipes.
    };
}