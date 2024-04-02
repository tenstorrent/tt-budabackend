// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "netlist/tt_backend_api_types.hpp"

#include "device/resource_manager.h"
#include "model/rational_graph/rational_graph.h"
#include "model/stream_graph/stream_graph.h"
#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2
{
    class PipeStreamsCreatorFactory;
    class RGBasePipe;
    class StreamNode;
    class VirtualNode;

    class StreamGraphCreator
    {
    public:
        // Creates stream graphs from the rational graphs.
        std::unique_ptr<StreamGraphCollection> create_stream_graphs(
            const std::vector<std::unique_ptr<RationalGraph>>& rational_graphs,
            ResourceManager* resource_manager,
            const int epoch_num);

        // Finds phase config with given phase id in the given vector of phase configs. Throws exception if
        // given phase_id was not found in the list of phase configs.
        static std::vector<PhaseConfig>::iterator find_phase_config_by_phase_id(
            std::vector<PhaseConfig>& phase_configs,
            const PhaseId phase_id);

    private:
        // Creates streams for each pipe in a rational graph.
        std::unordered_map<const RGBasePipe*, std::vector<StreamNode*>>
        create_streams_per_pipe(
            const RationalGraph* rational_graph,
            PipeStreamsCreatorFactory* pipe_streams_creator_factory,
            ResourceManager* resource_manager,
            std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node,
            StreamGraph* stream_graph);

        // Connects streams streaming data over same PCIe node.
        void connect_pcie_streams(
            const RationalGraph* rational_graph,
            const std::unordered_map<const RGBasePipe*, std::vector<StreamNode*>>& streams_per_pipe,
            const std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>& virt_node_to_stream_node);

        // Finds pipes in a rational graph doing transfer through PCIe.
        static void find_pipes_doing_pcie_transfer(const RationalGraph* rational_graph,
                                                   std::vector<const RGBasePipe*>& pipes_writing_to_pcie,
                                                   std::vector<const RGBasePipe*>& pipes_reading_from_pcie);

        // Finds stream writing to PCIe created for given pipe.
        static StreamNode* find_stream_writing_to_pcie(
            const RGBasePipe* pipe_writing_to_pcie,
            const std::vector<StreamNode*>& pipe_streams,
            const std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>& virt_node_to_stream_node);

        // Finds first stream in a vector of streams which has NCRISC config set.
        static StreamNode* find_first_stream_with_ncrisc_config(const std::vector<StreamNode*>& streams);

        // Connects streams that share the same buffer.
        static void connect_streams_sharing_same_buffer(const StreamGraph* stream_graph);

        // Helper function that returns mapping from buffer ID to stream node used to avoid double looping over all
        // streams. It's guaranteed that streams with buf_id set are unique.
        static std::unordered_map<NodeId, StreamNode*> create_node_id_to_stream_mapping(
            const StreamGraph* stream_graph);

        // Adds created pipe streams to map of streams per pipe.
        static void add_created_streams_to_streams_per_pipe(
            const RGBasePipe* pipe,
            const std::vector<std::unique_ptr<StreamNode>>& pipe_streams,
            std::unordered_map<const RGBasePipe*, std::vector<StreamNode*>>& streams_per_pipe);

        // Moves created streams to stream graph.
        static void move_created_streams_to_graph(std::vector<std::unique_ptr<StreamNode>>& created_streams,
                                                  StreamGraph* stream_graph);

        // Calculates the largest phase id in the created stream graphs and assigns m_unrolled_phase_offset.
        void calculate_unrolled_phase_offset(const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs);

        // Applies optimizations to the stream graph.
        void optimize_stream_graph(StreamGraph* stream_graph);

        // Optimizes unpacker streams fed by union stream such that the unpacker stream phases are merged until
        // unpacker's tile clear granularity.
        void optimize_unpacker_streams_fed_by_union(StreamGraph* stream_graph);
        
        // Calls flow control optimizer for this stream graph.
        void optimize_stream_graph_flow_control(StreamGraph* stream_graph);

        // Inserts indicators for dummy phases at appropriate places in the stream graph. Dummy phase may be used on
        // sender or receiver side of the stream. It is used to mitigate possible race condition.
        // Explanation: Streams exchange AreYouReady (sent by sender) and DestReady (sent by receiver) messages to make
        // sure that both sides are ready to start the transfer. In cases when flow control is OFF or when phase
        // consists of a single tile, stream will not flush all those messages. It is possible that sender starts a new
        // FW iteration and sees an old DestReady message from the previous iteration. This should be prevented. To work
        // around this issue, we insert dummy phases on both sides which will send flow control messages and thus flush
        // all the messages on the communication channel.
        static void insert_dummy_phases(StreamGraphCollection* stream_graph_collection);

        // Sets incoming NOC ids for all streams in the stream graph that do not have it set.
        static void set_incoming_noc_ids(StreamGraphCollection* stream_graph_collection);

        // Get complementary NOC route.
        static NOC_ROUTE get_complementary_noc_route(NOC_ROUTE route)
        {
            return route == NOC_ROUTE::NOC0 ? NOC_ROUTE::NOC1 : NOC_ROUTE::NOC0;
        }

        // Unrolls data transfer iterations for each stream in the stream graph as much as possible.
        void unroll_stream_graph(StreamGraph* stream_graph, unsigned int max_unroll_factor);

        // Returns the largest phase offset present in the stream nodes of the given stream graph.
        PhaseId get_max_phase_in_stream_graph(const StreamGraph* stream_graph);

        // Returns the largest unroll factor possible for every stream graph in the collection.
        std::vector<unsigned int> calculate_max_unroll_factor(const StreamGraphCollection* stream_graph_collection);

        // Copies stream node's phases of the initial iteration unroll factor times. The first unrolled iteration starts
        // right after the largest phase id on the chip in the first iteration, the second one starts at 2 times the
        // same offset etc.
        void unroll_phases(StreamNode* stream_node,
                           unsigned int num_iterations_in_epoch,
                           unsigned int unroll_factor);

        // Tries collapsing phases that emerged from unrolling into a single phase if possible.
        void try_merging_unrolled_phases(StreamNode* stream_node);

        // Adjusts phase properties that affect stream phase transitions between given stream node and its
        // destinations.
        void adjust_phase_transition_properties(StreamNode* stream_node);

        // Copies phase transition properties from the first destination stream to the others.
        void copy_phase_transition_properties(const std::vector<StreamNode*> destination_streams);

        // Computes whether stream can be autoconfigured in the next phase, for each phase.
        void compute_phase_auto_config_field(StreamNode* stream_node);

        // Computes whether stream buffer read/write pointers are aligned with start of the buffer at the end of the
        // last phase.
        void compute_ptrs_not_zero_field(StreamNode* stream_node);

        // Returns true if we should skip checking last phase for buffer read/write pointers alignment.
        // Last phase has effect on alignment only when there is more than one iteration (before unroll).
        bool should_skip_last_phase_for_ptrs_not_zero(const std::vector<PhaseConfig>& phase_configs,
                                                      const std::size_t last_unrolled_phase_idx,
                                                      const unsigned int num_iterations_in_epoch,
                                                      const unsigned int unroll_factor);

        // Optimizes phases on the connection between source and dest stream by setting flags next_phase_src|dest_change
        // to false on both sides. 'start_phase_idx' and 'end_phase_idx' represent phase indices in the source stream
        // configuration that correspond to the phases segment where dest stream is constant. Function finds subsegments
        // where source stream in dest stream's phases is constant and sets flags to false in those subsegments, while
        // sets flags to true on the boundaries of those subsegments.
        void optimize_edge_phases(StreamNode* source_stream,
                                  StreamNode* dest_stream,
                                  const int start_phase_idx,
                                  const int end_phase_idx);

        // Helper function used by optimize_edge_phases. Sets flags appropriately on the boundaries of the subsegment.
        void set_next_phase_src_dest_change(
            std::vector<PhaseConfig>::iterator source_phases_left_boundary,
            std::vector<PhaseConfig>::iterator source_phases_right_boundary,
            std::vector<PhaseConfig>::iterator dest_phases_left_boundary,
            std::vector<PhaseConfig>::iterator dest_phases_right_boundary);

        // Goes through all stream nodes and in case a stream is a fork, it fixes the fork streams list.
        void fix_fork_lists(const StreamGraphCollection* stream_graph_collection);

        // Goes through all phases of all stream nodes and adds epoch offset to the original phase offset.
        void add_epoch_offset_to_stream_phases(const StreamGraphCollection* stream_graph_collection,
                                               const int epoch_num);

        // Goes through fork streams and removes intermeds.
        void remove_intermeds_from_fork_list(StreamNode* stream_node);

        // Assings NOC addresses to PCIe streams. It has to be done after resources allocation because
        // PCIe NOC addresses depend on allocated stream buffer addresses.
        void assign_pcie_streams_noc_addresses(const ResourceManager* resource_manager);

        // Estimated size of a stream phase configuration in bytes.
        constexpr static unsigned int c_estimated_phase_physical_size_bytes = 64;

        // Estimate epoch_t FW structure at 12KB.
        constexpr static unsigned int c_estimated_epoch_t_size_bytes = 0x3000;

        // How many bits to the left to shift epoch id when adding epoch offset to stream phases. This number limits
        // the max number of phases per epoch - upper 32 bits of phase id are used to encode the epoch id, and the lower
        // 32 bits of phase id are used to encode phase id within an epoch.
        constexpr static unsigned int c_epoch_id_phase_shift = 32;

        // Whether we can optimize stream connections that transfer more than 2k tiles. If true, pipegen will generate
        // optimized phases for such connections. On the other hand, firmware will execute code that will clear buffers
        // after each 2k tiles.
        constexpr static bool c_use_2k_tile_header_buffer_reset = true;

        // The largest phase id present in the first iteration among all streams on the chip incremented by 1 to denote
        // starting phase id of the next unrolled iteration.
        PhaseId m_unrolled_phase_offset;

        // Contains all created streams writing to PCIe.
        std::vector<StreamNode*> m_pcie_writing_streams;
    };
}