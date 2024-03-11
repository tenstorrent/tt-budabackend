// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "graph_creator/stream_graph/stream_creators/stream_creator.h"
#include "model/data_flow/data_flow_info.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "model/stream_graph/stream_node.h"

namespace pipegen2
{
    class DramEmbeddingTableInputNode;
    class DramInputNode;
    class PCIeStreamingNode;
    class UnpackerOutputNode;

    class DramReadCommonStreamsCreator
    {
    public:
        DramReadCommonStreamsCreator(StreamCreator* stream_creator) :
            m_stream_creator(stream_creator)
        {
        }

        std::vector<std::unique_ptr<StreamNode>> create_dram_prefetch_post_tm_input_stream(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info,
            const unsigned int max_dram_input_buffer_size_tiles,
            std::vector<NcriscConfig>&& ncrisc_configs);

        // Creates streams necessary to handle transfer from DRAM to Unpacker.
        std::vector<std::unique_ptr<StreamNode>> create_dram_input_streams(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info,
            const unsigned int max_dram_input_buffer_size_tiles,
            std::vector<NcriscConfig>&& ncrisc_configs);

        // Creates streams necessary to handle transfer from DRAM embedding table to Unpacker.
        std::vector<std::unique_ptr<StreamNode>> create_dram_embedding_input_streams(
            const RGBasePipe* pipe,
            const DramEmbeddingTableInputNode* dram_embedding_table,
            const DataFlowInfo& data_flow_info,
            const unsigned int max_dram_input_buffer_size_tiles,
            std::vector<NcriscConfig>&& ncrisc_configs);

        // Creates streams necessary to handle transfer from PCIe to Unpacker.
        std::vector<std::unique_ptr<StreamNode>> create_pcie_to_unpacker_streams(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info,
            NcriscConfig&& ncrisc_config);

        // Creates stream which handles transfer from DRAM to multiple Unpackers.
        // Saves calculated value of max_num_tiles_per_phase to be used for other streams connected to it.
        std::unique_ptr<StreamNode> create_dram_multicast_stream(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info,
            const unsigned int max_dram_input_buffer_size_tiles,
            std::vector<NcriscConfig>&& ncrisc_configs,
            unsigned int& max_num_tiles_per_phase);

        // Creates stream which handles transfer from PCIe to multiple Unpackers.
        // Saves calculated value of max_num_tiles_per_phase to be used for other streams connected to it.
        std::unique_ptr<StreamNode> create_pcie_multicast_stream(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info,
            NcriscConfig&& ncrisc_config,
            unsigned int& max_num_tiles_per_phase);

        // Creates unpacker streams for DRAM or PCIe multicast.
        std::vector<std::unique_ptr<StreamNode>> create_multicast_unpacker_streams(
            StreamNode* multicast_stream,
            const RGBasePipe* pipe,
            const RGBaseNode* input_node,
            const DataFlowInfo& data_flow_info,
            const unsigned int max_num_tiles_per_phase);

        // Updates stream's phases by decomposing original phases to the initial size and then merging decomposed
        // phases such that the total number of messages sent in one phase does not exceed max tiles per phase.
        void update_stream_with_merged_decomposed_phases(
            StreamNode* stream_node,
            const RGBaseNode* input_node,
            const std::vector<PhaseInfo>& original_phases,
            const DataFlowInfo& data_flow_info,
            const unsigned int num_iterations_in_epoch,
            const unsigned int max_num_tiles_per_phase);

        // Returns unpacker node from the output of the pipe, or from the virtual node parent, if there is virtual
        // node at the output of the pipe.
        const UnpackerOutputNode* get_unpacker_output_node(const RGBasePipe* pipe, std::size_t output_index = 0);

        // Configures specific parameters of the embedding index stream.
        void configure_embedding_index_params(StreamNode* embedding_index_stream);

    private:
        // Creates streams necessary to handle transfer from DRAM or PCIe to Unpacker.
        std::vector<std::unique_ptr<StreamNode>> create_dram_or_pcie_to_unpacker_streams(
            const RGBasePipe* pipe,
            const NcriscConfig& base_ncrisc_config,
            const bool is_dram_prefetch,
            const unsigned int max_dram_input_buffer_size_tiles,
            const UnpackerOutputNode* unpacker_node,
            const DataFlowInfo& data_flow_info);

        // Creates stream which handles transfer from DRAM or PCIe to multiple Unpackers.
        std::unique_ptr<StreamNode> create_dram_or_pcie_multicast_stream(
            const RGBasePipe* pipe,
            const NcriscConfig& base_ncrisc_config,
            const bool is_dram_prefetch);

        // Creates single stream to handle transfer from DRAM to Unpacker.
        std::unique_ptr<StreamNode> create_single_dram_to_unpacker_stream(
            const RGBasePipe* pipe,
            const NcriscConfig& base_ncrisc_config,
            const bool is_dram_prefetch,
            const UnpackerOutputNode* unpacker_node);

        // Creates combination of dram relay + unpacker streams to handle transfer from DRAM to Unpacker.
        void create_dram_to_unpacker_streams_with_relay(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info,
            const NcriscConfig& base_ncrisc_config,
            const bool is_dram_prefetch,
            const UnpackerOutputNode* unpacker_node,
            std::vector<std::unique_ptr<StreamNode>>& created_streams);

        // Creates relay stream to receive data from DRAM.
        std::unique_ptr<StreamNode> create_dram_relay_stream(
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info,
            const NcriscConfig& ncrisc_config,
            const bool is_dram_prefetch);

        // Creates unpacker stream to receive data from relay stream.
        std::unique_ptr<StreamNode> create_relay_to_unpacker_stream(
            const RGBasePipe* pipe,
            const UnpackerOutputNode* unpacker_node);

        // Returns maximum number of tiles per phase to use.
        unsigned int get_max_num_tiles_per_phase(
            const RGBasePipe* pipe,
            bool can_use_single_unpacker_stream,
            const DataFlowInfo& data_flow_info,
            const unsigned int tiles_per_input);

        // Configures stream receiving data from DRAM.
        void configure_dram_receiving_stream(
            StreamNode* dram_receiving_stream,
            std::vector<NcriscConfig>&& ncrisc_configs,
            const RGBasePipe* pipe,
            const DramInputNode* dram_input_node,
            const DataFlowInfo& data_flow_info,
            const bool should_scale_up_stream,
            const unsigned int max_dram_input_buffer_size_tiles,
            unsigned int& max_num_tiles_per_phase);

        // Configures stream receiving data from PCIe.
        void configure_pcie_receiving_stream(
            StreamNode* pcie_receiving_stream,
            NcriscConfig&& ncrisc_config,
            const RGBasePipe* pipe,
            const PCIeStreamingNode* pcie_streaming_node,
            const DataFlowInfo& data_flow_info,
            const bool should_scale_up_stream,
            unsigned int& max_num_tiles_per_phase);

        // Configures stream receiving data from DRAM or PCIe.
        void configure_dram_or_pcie_receiving_stream(
            StreamNode* dram_or_pcie_receiving_stream,
            std::vector<NcriscConfig>&& ncrisc_configs,
            const RGBasePipe* pipe,
            const RGBaseNode* input_node,
            const DataFlowInfo& data_flow_info,
            unsigned int& max_num_tiles_per_phase);

        // Scales up buffer size of a dram receiving stream if possible. It is guaranteed that new buffer size is a
        // divisor of max_num_tiles_per_phase_bytes (max_num_tiles_per_phase * dram_input_node_tile_size_bytes) and that
        // it is less than or equal to max_dram_input_buffer_size_tiles_tiles * dram_input_node_tile_size_bytes (if
        // max_dram_input_buffer_size_tiles_tiles is specified). This makes sure we don't run into divisibility issues
        // between unpacker's and root buffers.
        void scale_up_dram_receiving_stream(StreamNode* stream,
                                            const RGBasePipe* pipe,
                                            unsigned int max_num_tiles_per_phase,
                                            unsigned int dram_input_node_tile_size_bytes,
                                            unsigned int max_dram_input_buffer_size_tiles);

        // Scales up buffer size of a PCIe receiving stream.
        void scale_up_pcie_receiving_stream(StreamNode* stream, const PCIeStreamingNode* pcie_streaming_node);

        // Recalculates max tiles per phase in case when we can configure single stream to handle transfer from dram to
        // unpacker. Max tiles per phase must be divisible by the size of the stream's buffer.
        unsigned int recalculate_max_num_tiles_per_phase(unsigned int tiles_per_input,
                                                         unsigned int buffer_size_tiles,
                                                         unsigned int num_iters_in_epoch);

        // Flattens phases from all the source buffers into a single vector in ascending order by phase offset.
        std::vector<PhaseInfo> get_flattened_input_phases(const RGBasePipe* pipe, const DataFlowInfo& data_flow_info);

        // Configures unpacker stream receiving data from dram relay stream.
        void configure_relay_to_unpacker_stream(
            StreamNode* relay_to_unpacker_stream,
            const RGBasePipe* pipe,
            const RGBaseNode* input_node,
            const UnpackerOutputNode* unpacker_node,
            const DataFlowInfo& data_flow_info,
            const unsigned int max_num_tiles_per_phase);

        // Connects dram relay with unpacker stream.
        void connect_dram_or_pcie_relay_to_unpacker_streams(
            const std::vector<std::unique_ptr<StreamNode>>& created_streams);

        // Creates unpacker stream for DRAM or PCIe multicast.
        std::unique_ptr<StreamNode> create_multicast_unpacker_stream(
            const RGBasePipe* pipe,
            const RGBaseNode* input_node,
            const UnpackerOutputNode* unpacker_node,
            const DataFlowInfo& data_flow_info,
            const unsigned int max_num_tiles_per_phase);

        // Configures unpacker stream receiving data from dram, in case of prefetch post TM.
        std::unique_ptr<StreamNode> create_dram_prefetch_post_tm_to_unpacker_stream(
            const RGBasePipe* pipe,
            const UnpackerOutputNode* unpacker_node,
            const DataFlowInfo& data_flow_info,
            std::vector<NcriscConfig>&& ncrisc_configs);

        // Stream creator to use for creating streams.
        StreamCreator* m_stream_creator;
    };
}