// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "device/resource_manager.h"
#include "model/pipe_graph/pg_buffer.h"
#include "model/pipe_graph/pg_pipe.h"
#include "model/pipe_graph/pipe_graph.h"
#include "model/rational_graph/nodes/dram_input_node.h"
#include "model/rational_graph/rational_graph.h"

struct tt_cxy_pair;

namespace pipegen2
{
    class BaseInputNode;
    class BaseOutputNode;
    class BaseIntermedNode;
    class DramStreamingNode;
    class ForkPipe;
    class PGSubgraph;
    class RGBaseNode;
    class RGBasePipe;
    class VirtualNode;

    class RationalGraphCreator
    {
    public:
        // Creates rational graphs from the connected node groups in the pipe graph.
        std::vector<std::unique_ptr<RationalGraph>> create_rational_graphs(const PipeGraph& pipe_graph,
                                                                           ResourceManager* resource_manager);

    private:
        // Creates rational graph from pipe graph subgraph.
        std::unique_ptr<RationalGraph> create_rational_graph(const PGSubgraph* pg_subgraph,
                                                             ResourceManager* resource_manager);

        // Creates rational graph nodes from subgraph buffers.
        std::vector<std::unique_ptr<RGBaseNode>> create_rational_graph_nodes(
            const std::vector<const PGBuffer*>& subgraph_buffers,
            ResourceManager* resource_manager);

        // Creates rational graph node from subgraph buffer.
        static std::unique_ptr<RGBaseNode> create_rational_graph_node(const PGBuffer* subgraph_buffer,
                                                                      ResourceManager* resource_manager);

        // Creates rational node from input buffer.
        static std::unique_ptr<BaseInputNode> create_input_node(const PGBuffer* input_buffer,
                                                                ResourceManager* resource_manager);

        // Creates rational node from output buffer.
        static std::unique_ptr<BaseOutputNode> create_output_node(const PGBuffer* output_buffer,
                                                                  ResourceManager* resource_manager);

        // Creates rational node from intermediate buffer.
        static std::unique_ptr<BaseIntermedNode> create_intermed_node(const PGBuffer* intermed_buffer,
                                                                      ResourceManager* resource_manager);

        static std::unique_ptr<RGBaseNode> create_relay_node(const PGBuffer* relay_buffer,
                                                             const SoCInfo* soc_info);

        // Creates rational graph pipes and additional virtual nodes if necessary, from subgraph pipe.
        void create_rational_graph_pipes(const PGPipe* subgraph_pipe,
                                         std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
                                         std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
                                         const SoCInfo* soc_info);

        // Creates rational graph pipes and additional virtual nodes if necessary, from subgraph pipe inputs and
        // outputs at the given scatter index.
        void create_rational_graph_pipes(
            const PGPipe* subgraph_pipe,
            const unsigned int scatter_index,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
            std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe,
            const SoCInfo* soc_info);

        // Creates rational graph direct pipe from subgraph pipe.
        void create_direct_pipe(
            const PGPipe* subgraph_pipe,
            const unsigned int scatter_index,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            const tt_cxy_pair& pipe_physical_location,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Creates direct relay pipe. If pipe connects buffers on different chips, it will create ethernet relay pipe,
        // otherwise it will create regular relay pipe.
        std::unique_ptr<RGBasePipe> create_relay_pipe(RGPipeProperties&& rg_pipe_properties,
                                                      const tt_cxy_pair& pipe_physical_location,
                                                      const PGBuffer* input_buffer,
                                                      const PGBuffer* relay_buffer);

        // Creates ethernet relay pipe of appropriate type (HW or FW).
        std::unique_ptr<RGBasePipe> create_ethernet_relay_pipe(RGPipeProperties&& rg_pipe_properties,
                                                               const tt_cxy_pair& pipe_physical_location,
                                                               const bool is_fw_relay_pipe);

        // Creates rational graph multicast pipe from subgraph pipe.
        void create_multicast_pipe(
            const PGPipe* subgraph_pipe,
            const unsigned int scatter_index,
            const tt_cxy_pair& pipe_physical_location,
            const SoCInfo* soc_info,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes);

        // Creates rational graph join pipe from subgraph pipe that has multiple inputs and one output. Function is
        // called only for pipes that read from or write to dram.
        void create_join_pipe(
            const PGPipe* subgraph_pipe,
            const unsigned int scatter_index,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            const tt_cxy_pair& pipe_physical_location,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Creates a RG subgraph connecting input nodes and appropriate RG pipe
        void create_receiving_subgraph(
            const PGPipe* subgraph_pipe,
            const unsigned int scatter_index,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            const std::vector<PGPipe::Input>& pg_pipe_inputs,
            const tt_cxy_pair& pipe_physical_location,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Creates a RG subgraph which transmits gathered data toward destinations given by pipe outputs. Returns single
        // input pipe of the created subgraph.
        RGBasePipe* get_sending_subgraph(
            const PGPipe* subgraph_pipe,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            const std::vector<PGPipe::Input>& pg_pipe_inputs,
            const std::vector<PGBuffer*>& pg_pipe_outputs,
            const tt_cxy_pair& pipe_physical_location,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
            std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe);

        // Creates a RG subgraph which transmits gathered data toward destinations given by pipe outputs.
        void create_sending_subgraph(
            const PGPipe* subgraph_pipe,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            const std::vector<PGPipe::Input>& pg_pipe_inputs,
            const std::vector<PGBuffer*>& pg_pipe_outputs,
            const tt_cxy_pair& pipe_physical_location,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Creates a RG pipe which transmits data toward destinations given by pipe outputs.
        void create_pipe_sending_to_destinations(
            const PGPipe* subgraph_pipe,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            const std::vector<PGPipe::Input>& pg_pipe_inputs,
            const std::vector<PGBuffer*>& pg_pipe_outputs,
            const tt_cxy_pair& pipe_physical_location,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Creates a union pipe for a given RG node which will be set as output node for the created union pipe.
        void create_union_pipe_for_rg_node(
            RGBaseNode* union_pipe_output_node,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes);

        // Returns VirtualNode that wraps the RG node that is associated with the given PG buffer.
        std::unique_ptr<VirtualNode> wrap_into_virtual_node(const PGBuffer* pg_buffer);

        // Decomposes pipe which connects buffers on tensix cores to combination of rational graph pipes, plus virtual
        // nodes in between.
        void decompose_inter_tensix_pipe(
            const PGPipe* subgraph_pipe,
            const unsigned int scatter_index,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            const tt_cxy_pair& pipe_physical_location,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
            std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe);

        // Decomposes MMIO pipe to a combination of two pipes, one pipe sending to PCIe on one chip and another pipe
        // reading from PCIe on another chip.
        void decompose_mmio_pipe(const PGPipe* mmio_pipe,
                                 const unsigned int scatter_index,
                                 const tt_cxy_pair& pipe_logical_location,
                                 std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
                                 std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
                                 std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
                                 const SoCInfo* soc_info,
                                 std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe);

        // Creates MMIO pipe sending to PCIe on one chip.
        std::unique_ptr<RGBasePipe> create_pipe_sending_to_pcie(
            const PGPipe* mmio_pipe,
            const unsigned int scatter_index,
            const SoCInfo* soc_info);

        // Creates PCIe streaming node as the output of the pipe sending to PCIe and connects pipe to it. In case of
        // duplicated pipe outputs, creates union pipe between PCIe sending pipe and streaming node. If there is already
        // streaming node and union pipe created for the first duplicate of current destination outputs, then just
        // connects pipe to that union pipe. Returns true if new PCIe streaming node is created, false otherwise.
        bool connect_pipe_sending_to_pcie_to_streaming_node(
            RGBasePipe* pipe_sending_to_pcie,
            const unsigned int tile_size,
            const unsigned int epoch_tiles,
            const unsigned int src_size_tiles,
            const unsigned int dest_size_tiles,
            const bool is_streaming_downstream,
            const ChipId chip_id,
            const PGPipe* mmio_pipe,
            const std::vector<PGBuffer*>& mmio_pipe_outputs,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe);

        // Connects pipe sending to pcie to a union pipe which feeds the pcie streaming node, by creating a virtual
        // node in between the two pipes. Returns true if new union pipe PCIe streaming node is created, false
        // otherwise if union pipe already exists for given destination outputs.
        bool connect_pipe_sending_to_pcie_to_union_pipe(
            RGBasePipe* pipe_sending_to_pcie,
            const unsigned int tile_size,
            const unsigned int epoch_tiles,
            const unsigned int src_size_tiles,
            const unsigned int dest_size_tiles,
            const bool is_streaming_downstream,
            const ChipId chip_id,
            const PGPipe* mmio_pipe,
            const std::vector<PGBuffer*>& mmio_pipe_outputs,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe);

        // Returns a union pipe for a corresponding pcie streaming node if it is already created, otherwise it creates
        // the new pipe and connects it to the pcie streaming node.
        RGBasePipe* get_union_pipe_sending_to_pcie_streaming_node(
            const unsigned int tile_size,
            const unsigned int epoch_tiles,
            const unsigned int src_size_tiles,
            const unsigned int dest_size_tiles,
            const bool is_streaming_downstream,
            const ChipId chip_id,
            const PGPipe* mmio_pipe,
            const std::vector<PGBuffer*>& mmio_pipe_outputs,
            std::unordered_set<RGBasePipe*>* created_rg_pipes_from_subgraph_pipe,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::map<std::vector<PGBuffer*>, RGBasePipe*>& pg_pipe_dest_list_to_rg_pipe,
            bool& new_pcie_streaming_node_created);

        // Creates MMIO pipe reading from PCIe on another chip.
        std::unique_ptr<RGBasePipe> create_pipe_reading_from_pcie(
            const PGPipe* mmio_pipe,
            const unsigned int scatter_index,
            const std::vector<PGBuffer*>& mmio_pipe_outputs,
            const tt_cxy_pair& mmio_pipe_logical_location,
            const SoCInfo* soc_info);

        // Chooses a location to assign to MMIO pipe sending to PCIe, based on scatter index.
        tt_cxy_pair choose_location_of_pipe_sending_to_pcie(const std::vector<PGPipe::Input>& mmio_pipe_inputs,
                                                            const unsigned int scatter_index,
                                                            const SoCInfo* soc_info);

        // Creates PCIe streaming node as the input to the pipe reading from PCIe.
        void connect_pipe_reading_from_pcie_to_streaming_node(
            RGBasePipe* pipe_reading_from_pcie,
            const unsigned int tile_size,
            const unsigned int epoch_tiles,
            const unsigned int src_size_tiles,
            const unsigned int dest_size_tiles,
            const bool is_streaming_downstream,
            const ChipId chip_id,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Connects rational graph pipe with its input nodes.
        void connect_rg_pipe_with_input_nodes(RGBasePipe* rg_pipe,
                                              const std::vector<PGPipe::Input>& pg_pipe_inputs,
                                              const unsigned int scatter_index);

        // Connects rational graph pipe with its input node.
        void connect_rg_pipe_with_input_node(RGBasePipe* rg_pipe,
                                             const PGPipe::Input& pg_pipe_input,
                                             const unsigned int scatter_index);

        // Connects rational graph pipe with its input node.
        void connect_rg_pipe_with_input_node(RGBasePipe* rg_pipe, RGBaseNode* rg_node);

        // Connects rational graph pipe with its output nodes.
        void connect_rg_pipe_with_output_nodes(RGBasePipe* rg_pipe,
                                               const std::vector<PGBuffer*>& pg_pipe_outputs);

        // Connects rational graph pipe with its output node.
        void connect_rg_pipe_with_output_node(RGBasePipe* rg_pipe, const PGBuffer* pg_pipe_output);

        // Connects rational graph pipe with its output node.
        void connect_rg_pipe_with_output_node(RGBasePipe* rg_pipe, RGBaseNode* rg_node);

        // Creates fork pipe and virtual buffers for each buffer going into multiple pipes.
        // In some future hardware versions we might not want to do this, in that case we will have
        // factory which returns rational graph creator for specific hardware architecture,
        // and this function will be virtual and overriden in specific creator.
        void handle_forked_buffers(const std::vector<const PGBuffer*>& subgraph_buffers,
                                   std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
                                   std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
                                   const SoCInfo* soc_info);

        // Finds subgraph buffers which are forked.
        static std::vector<const PGBuffer*> find_forked_buffers(const std::vector<const PGBuffer*>& subgraph_buffers);

        // Creates ParallelFork pipe for the node and creates virtual nodes as the outputs of the fork,
        // which will further be connected to the pipes that were previously having that node as an input.
        void create_parallel_fork_for_node(const PGBuffer* pg_buffer,
                                           const SoCInfo* soc_info,
                                           RGBaseNode* rg_node,
                                           std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
                                           std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Creates appropriate fork pipe for dram input node based on the prefetch type.
        static std::unique_ptr<ForkPipe> create_dram_parallel_fork_pipe_for_node(const PGBuffer* pg_buffer,
                                                                                 const tt_cxy_pair& physical_location);

        // Creates a serial fork node and pipe for every input to the given subgraph pipe - for both regular input
        // buffers and for scatter padding buffers.
        void create_serial_fork_nodes_for_pipe_inputs(
            const PGPipe* subgraph_pipe,
            const std::unordered_set<RGBasePipe*>& created_rg_pipes_from_subgraph_pipe,
            const SoCInfo* soc_info,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Creates SerialFork pipe for the node and creates virtual nodes as the outputs of the fork,
        // which will further be connected to the pipes that were previously having that node as an input.
        void create_serial_fork_for_node(
            const PGPipe* subgraph_pipe,
            const std::unordered_set<RGBasePipe*>& created_rg_pipes_from_subgraph_pipe,
            RGBaseNode* rg_node,
            const tt_cxy_pair& physical_location,
            const unsigned int scatter_fanout,
            const unsigned int scatter_padding_fanout,
            const bool is_output_padding,
            std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
            std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Creates appropriate serial fork pipe for a given RG node based on the node's type.
        std::unique_ptr<ForkPipe> create_serial_fork_pipe_for_node(
            const RGBaseNode* rg_node,
            const PGPipe* subgraph_pipe,
            const tt_cxy_pair& physical_location,
            const unsigned int scatter_fanout,
            const unsigned int scatter_padding_fanout,
            const bool is_output_padding);

        // Connects node with the fork pipe and creates virtual nodes as the outputs of the fork, which will further be
        // connected to the pipes that were previously having that node as an input. Output pipes of the node will be
        // reconnected to fork only if they are present in rg_pipes_behind_fork, otherwise they will be connected to the
        // node directly.
        void reconnect_forked_node(RGBaseNode* rg_node,
                                   std::unique_ptr<ForkPipe> fork_pipe,
                                   const std::unordered_set<RGBasePipe*>& rg_pipes_behind_fork,
                                   std::vector<std::unique_ptr<RGBasePipe>>& rational_graph_pipes,
                                   std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes,
                                   const bool is_serial_fork);

        // Creates a fork node which will be implaced betweent the input node and the output pipe.
        std::unique_ptr<VirtualNode> create_fork_node_for_rg_edge(const RGBaseNode* input_node,
                                                                  const RGBasePipe* output_pipe,
                                                                  const bool is_serial_fork);

        // Returns true if buffer is forking and in one of the fork paths there is optimized packer multicast.
        static bool is_buffer_forking_to_optimized_packer_multicast(const PGBuffer* buffer, const SoCInfo* soc_info);

        // Ensures that fork path containing optimized packer multicast pipe is on the first fork index.
        static void ensure_optimized_packer_multicast_is_first_fork(RGBasePipe* fork_pipe);

        // Returns true if fork path starting from the given forked node contains optimized packer multicast.
        static bool is_fork_path_containing_optimized_packer_multicast(const RGBaseNode* forked_node);

        // Checks if packer multicast optimization is enabled for given PG pipe.
        static bool is_packer_multicast_optimization_enabled(const PGPipe* subgraph_pipe, const SoCInfo* soc_info);

        // Returns if this PG pipe can be broken down into a simple RG pipe which connects L1 source to L1 dest, without
        // the need to decompose the original pipe to multiple RG pipes and virtual node(s).
        static bool can_directly_connect_l1_source_to_l1_dest(const PGPipe* subgraph_pipe, const SoCInfo* soc_info);

        // Creates default RG pipe properties based on values in PG pipe.
        static RGPipeProperties create_rg_pipe_properties_from_pg_pipe(const PGPipe* subgraph_pipe);

        // Returns if gather optimization should be disabled for a given PG pipe.
        static bool should_disable_gather_optimization_for_pipe(const PGPipe* subgraph_pipe);

        // Returns the type of input dram buffer based on the fields set in pipegen yaml.
        // Buffer type can be one of: dram io, prefetch post-tm, prefetch pre-tm.
        static DramInputNodeType get_input_dram_buffer_type(const PGBuffer* input_dram_buffer);

        // Returns buffer's physical location based on its logical location.
        static tt_cxy_pair get_physical_location(const PGBuffer* pg_buffer, const SoCInfo* soc_info);

        // Returns pipe's physical location based on its logical location.
        static tt_cxy_pair get_physical_location(const PGPipe* pg_pipe,
                                                 const tt_cxy_pair& pipe_logical_location,
                                                 const SoCInfo* soc_info);

        // Returns true if PG subgraph is doing transfer through PCIe.
        static bool is_subgraph_doing_pcie_transfer(const PGSubgraph* pg_subgraph);

        // Creates relay nodes for all relay buffers in the subgraph.
        void create_relay_nodes(const std::vector<const PGBuffer*>& subgraph_buffers,
                                std::vector<std::unique_ptr<RGBaseNode>>& rational_graph_nodes);

        // Counts number of gather/multicast streams each pipe in given rational graphs will allocate, and in case when
        // too many are allocated on some core replaces excess Gather pipes with DormantRelay pipes.
        void manage_gather_optimizations(const ResourceManager* resource_manager,
                                         std::vector<std::unique_ptr<RationalGraph>>& rational_graphs);

        // Allocates input index for all RG nodes which are kernel input operands, ensuring the ordering of allocation -
        // first allocate input index for unpackers and then for intermediates (pipegen needs to ensure this ordering
        // due to implementation details of overlay decouple perf infra:
        // tenstorrent/budabackend#2074).
        void allocate_kernel_input_index(ResourceManager* resource_manager,
                                         std::vector<std::unique_ptr<RationalGraph>>& rational_graphs);

        // Allocates output index for all RG nodes which are kernel output operands, ensuring the ordering of allocation
        // - first allocate output index for packers and then for intermediates (pipegen needs to ensure this ordering
        // due to implementation details of overlay decouple perf infra:
        // tenstorrent/budabackend#2074).
        void allocate_kernel_output_index(ResourceManager* resource_manager,
                                          std::vector<std::unique_ptr<RationalGraph>>& rational_graphs);

        // Returns true if the given intermed node is a dummy node located on left in (buffer -> pipe -> buffer)
        // connection.
        bool is_dummy_intermediate_node(const BaseIntermedNode* intermed_node);

        // Mapping rational graph node to the pipe graph node it was created from.
        std::unordered_map<const PGBuffer*, RGBaseNode*> m_node_from_buffer;

        // Mapping between rational graph edge (node -> pipe) and a correponding scatter index of that edge.
        std::unordered_map<const RGBaseNode*, std::unordered_map<const RGBasePipe*, unsigned int>>
            m_rg_edge_to_scatter_index;
    };
}