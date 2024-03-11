// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "model/data_flow/data_flow_info.h"
#include "model/rational_graph/nodes/base_input_node.h"
#include "model/stream_graph/stream_config.h"
#include "model/stream_graph/stream_node.h"

namespace pipegen2
{
    class BaseIntermedNode;
    class DramInputNode;
    class NcriscConfig;
    class PackerInputNode;
    class RGBaseNode;
    class RGBasePipe;
    class SerialForkPipe;
    class SharedPackerIntermedNode;
    class UnpackerOutputNode;

    // Creates various stream configurations.
    class StreamCreator
    {
    public:
        // Virtual destructor, necessary for base class.
        virtual ~StreamCreator() = default;

        // Configures stream that receives data from dram input and relays it to other streams.
        void configure_dram_relay_stream(const RGBasePipe* rg_pipe,
                                         const DataFlowInfo& data_flow_info,
                                         const NcriscConfig& base_ncrisc_config,
                                         const bool is_dram_prefetch_post_tm,
                                         StreamNode* stream);

        // Configures stream that receives data over NOC to the unpacker.
        void configure_noc_to_unpacker_stream(const UnpackerOutputNode* unpacker_node, StreamNode* stream);

         // Configures unpacker stream that receives data from dram, if it is post TM prefetch.
        void configure_dram_to_unpacker_prefetch_post_tm_stream(const RGBasePipe* rg_pipe,
                                                                const DataFlowInfo& data_flow_info,
                                                                const NcriscConfig& base_ncrisc_config,
                                                                const UnpackerOutputNode* unpacker_node,
                                                                StreamNode* stream);

        // Returns true if it is possible to configure single stream to handle given transfer from dram to unpacker,
        // false otherwise.
        bool can_configure_dram_to_unpacker_stream(const RGBasePipe* rg_pipe,
                                                   const NcriscConfig& base_ncrisc_config,
                                                   const UnpackerOutputNode* unpacker_node,
                                                   unsigned int max_dram_input_buffer_size_tiles);

        // Configures unpacker stream that receives data from dram.
        void configure_dram_to_unpacker_stream(const RGBasePipe* rg_pipe,
                                               const NcriscConfig& base_ncrisc_config,
                                               const UnpackerOutputNode* unpacker_node,
                                               StreamNode* stream);

        // Configures stream that sends data from packer to another stream across noc.
        void configure_packer_to_noc_stream(StreamNode* stream,
                                            const RGBasePipe* rg_pipe,
                                            const PackerInputNode* packer_node);

        // Configures stream that sends data from packer to dram across noc.
        void update_stream_for_dram_write(StreamNode* stream,
                                          const bool is_remote_write,
                                          const bool is_untilized_output);

        // Configures stream's parameters related to untilization.
        void configure_packer_stream_with_untilization_params(const RGBasePipe* rg_pipe,
                                                              unsigned int packer_stride,
                                                              StreamNode* stream);

        // Configures relay stream, with default buffer size.
        void configure_relay_stream_with_default_buffer_size(StreamNode* relay_stream,
                                                             const unsigned int num_unique_input_nodes,
                                                             const unsigned int tile_size,
                                                             const bool is_ethernet_core = false);

        // Configures relay stream.
        void configure_relay_stream(StreamNode* relay_stream,
                                    const unsigned int buffer_size_tiles,
                                    const unsigned int tile_size);

        // Configures stream that is a relay stream between packer and gather streams.
        void configure_gather_relay_stream(StreamNode* relay_stream,
                                           const RGBasePipe* pipe);

        // Configures relay stream which is gathering from packer streams and writing to PCIe.
        void configure_gather_to_pcie_relay_stream(StreamNode* relay_stream,
                                                   const RGBasePipe* pipe);

        // Configures stream that gathers data from multiple streams.
        void configure_gather_stream(const RGBasePipe* pipe, StreamNode* gather_stream);

        // Configures stream that sends data from packer to multiple unpackers.
        void configure_multicast_stream(const RGBasePipe* rg_pipe, StreamNode* multicast_stream);

        // Converts stream to multicast stream.
        void convert_stream_to_multicast(const bool is_direct_packer_multicast, StreamNode* stream);

        // Configures stream which reads data from dram in prefetch phase in pre TM manner.
        void configure_prefetch_pre_tm_relay_stream(StreamNode* stream,
                                                    const DramInputNode* prefetch_input_node,
                                                    const RGBasePipe* rg_pipe,
                                                    std::vector<NcriscConfig>&& ncrisc_configs);

        // Configures stream that sends data from dram to multiple unpackers.
        void configure_dram_multicast_stream(const RGBasePipe* rg_pipe,
                                             const bool is_dram_prefetch,
                                             const NcriscConfig& base_ncrisc_config,
                                             StreamNode* multicast_stream);

        // Configures stream buffer address offsets for each phase based on input offsets of the node that stream is
        // reading from. This is necessary only for streams reading from scattered input.
        void configure_address_offsets(StreamNode* stream, const RGBasePipe* pipe, const BaseInputNode* input_node);

        // Decomposes each phase into multiple phases of the given size.
        std::vector<PhaseInfo> decompose_phases(const std::vector<PhaseInfo>& phase_info_vec,
                                                unsigned int decomposed_phase_max_size);

        // Merges phases in such a way that the total number of msgs per phase does not exceed the upper bound.
        std::vector<PhaseInfo> merge_phases(const std::vector<PhaseInfo>& phase_info_vec,
                                            unsigned int max_msgs_in_phase);

        // Configures stream configs source and destination fields based on given source and destination streams.
        void configure_stream_src_and_dest(StreamNode* stream, StreamNode* source_stream,
                                           StreamNode* destination_stream);

        // Configures stream configs source and destination fields based on given source and destination streams.
        void configure_stream_src_and_dest(StreamNode* stream, StreamNode* source_stream,
                                           const std::vector<std::unique_ptr<StreamNode>>& destination_streams);

        // Configures source and destination fields for multicast and its unpackers streams.
        void configure_multicast_streams_src_and_dest(StreamNode* multicast_stream,
                                                      const std::vector<std::unique_ptr<StreamNode>>& unpacker_streams);

        // For a given input stream and its static config, configures phases of the input stream based on the data flow
        // info and the receiving streams. Updates receiving streams' source fields in connected phases. Returns an
        // iterator to the first newly configured phase in the vector of phases of the input stream.
        void configure_phases_of_input_stream(
            const StreamPhasesCommonConfig& input_stream_static_config,
            const RGBaseNode* input_node,
            const RGBasePipe* rg_pipe,
            std::vector<StreamNode*> receiving_streams,
            const DataFlowInfo& data_flow_info);

        // Configures stream's input and output scatter loop count based on the values in RG pipe.
        void configure_scatter_loops(const RGBasePipe* rg_pipe, StreamNode* stream);

        // Finds phase among receiver phases that corresponds to the given input stream phase. Returns an iterator to
        // the found phase. Using this iterator, function chooses appropriate receiver stream to connect to the input
        // stream.
        std::vector<PhaseInfo>::const_iterator connect_input_stream_phase_to_receiving_stream(
            StreamNode* input_stream,
            const unsigned int input_stream_phase_offset,
            StreamConfig& input_stream_phase_config,
            const std::vector<PhaseInfo>& receiver_phases,
            std::vector<PhaseInfo>::const_iterator receiver_phase_start_iter,
            const std::vector<StreamNode*>& receiving_streams);

        // Updates stream's iterations_in_epoch parameter.
        void update_stream_iterations_in_epoch(StreamNode* stream,
                                               const RGBasePipe* pipe,
                                               const DataFlowInfo& data_flow_info);

        // Updates stream's resend parameter by setting false in the starting phase, and true in the rest of the phases.
        void update_resend_of_sending_stream(StreamNode* stream,
                                             std::vector<PhaseConfig>::iterator starting_phase_iter,
                                             int num_phases_to_update);

        // Returns an iterator to the phase with the given phase id in the given stream. Caller must make sure that
        // the phase exists in the stream.
        std::vector<PhaseConfig>::iterator find_phase_by_phase_id(StreamNode* stream, const PhaseId phase_id);

        // Set fields that are related to the pipe and are common to all phases.
        void set_common_stream_properties_from_pipe(StreamNode* stream,
                                                    const RGBasePipe* rg_pipe,
                                                    bool incoming_stream = true);

        // Sets intermed stream config default values.
        void configure_default_intermed_stream_properties(StreamNode* stream);

        // Sets gradient intermed stream config default values.
        void configure_default_dram_output_intermed_stream_properties(StreamNode* stream);

        // Sets intermed stream config values contained in intermed node.
        void configure_intermed_stream_properties_from_node(StreamNode* stream, const BaseIntermedNode* intermed_node);

        // Change stream type to intermed, change physical location, and operand_id.
        void convert_packer_to_intermed_stream(StreamNode* stream_node, const SharedPackerIntermedNode* intermed_node);

        // Adds common configuration for streams which sends data from L1 buffer to NoC.
        void set_common_l1_to_noc_stream_properties(StreamNode* stream,
                                                    const RGBasePipe* rg_pipe,
                                                    const BaseInputNode* input_node);

        // Configures scatter order size and padding scatter order size fields of a given stream.
        void configure_scatter_order_sizes(const SerialForkPipe* serial_fork_pipe, StreamNode* stream_node);

        // Calculates dram pipe's reduce mem threshold - value indicating how much a dram receiving stream's buffer
        // can be sized in bytes.
        unsigned int calculate_pipe_reduce_mem_usage_threshold(unsigned int max_dram_input_buffer_size_tiles,
                                                               bool is_ethernet_core,
                                                               unsigned int tile_size);

        // Conditionally converts stream from unpacker to embedding index stream. This includes enabling ncrisc
        // involvement and disabling kernel unpacker involvement in the stream.
        void check_and_convert_unpacker_to_embedding_index_stream(StreamNode* stream_node,
                                                                  const UnpackerOutputNode* unpacker_node);

    protected:
        // Hiding base constructor.
        StreamCreator() = default;

        // Configures unpacker stream receiver parameters.
        virtual void configure_unpacker_stream_receiver_params(StreamConfig& base_stream_config) = 0;

    private:
        // Calculation for `buf_space_available_ack_thr` copied over from pipegen1.
        static unsigned int calculate_buf_space_available_ack_thr(unsigned int size_tiles,
                                                                  unsigned int scatter_gather_num_tiles);

        // Calls `calculate_buf_space_available_ack_thr(uint, uint)`.
        static unsigned int calculate_buf_space_available_ack_thr(const UnpackerOutputNode* unpacker_node);

        // Calls `calculate_buf_space_available_ack_thr(uint, uint)`.
        static unsigned int calculate_buf_space_available_ack_thr(const BaseIntermedNode* intermed_node);

        // Calculates buffer size necessary for single stream to handle transfer from dram to unpacker.
        static unsigned int calculate_dram_to_unpacker_buffer_size(const NcriscConfig& base_ncrisc_config,
                                                                   const UnpackerOutputNode* unpacker_node);

        // Calculates default buffer size in tiles for a relay stream. It uses a heuristic from pipegen1 to determine
        // the size of the buffer based on number of unique input nodes to the pipe for which relay is being created.
        static unsigned int calculate_default_relay_stream_buffer_size_tiles(const unsigned int num_unique_input_nodes,
                                                                             const bool is_ethernet_core = false);

        // Returns true if the packer is sending data to pipe's outgoing noc.
        bool is_packer_stream_sending_to_pipe_output_noc(const RGBasePipe* rg_pipe, const PackerInputNode* packer_node);

        // Gets the size of the buffer for a prefetch pipe.
        unsigned int get_prefetch_post_tm_dram_buffer_size_bytes( 
            const RGBasePipe* pipe,
            const DataFlowInfo& data_flow_info,
            const NcriscConfig& ncrisc_config) const;

        // Minimum number of tiles a gather relay streams receives per input on worker cores.
        static constexpr unsigned int c_worker_gather_streamed_read_input_buffer_num_tiles = 4;

        // Minimum number of tiles a gather relay streams receives per input on ethernet cores.
        static constexpr unsigned int c_eth_gather_streamed_read_input_buffer_num_tiles = 4;

        // Minimum number of tiles that all relay gather streams coming from one pipe should have on worker cores.
        static constexpr unsigned int c_sum_gather_input_min_size_tiles = 12;

        // Minimum number of tiles that all relay gather streams coming from one pipe should have on ethernet cores.
        static constexpr unsigned int c_eth_sum_gather_input_min_size_tiles = 8;
    };
}