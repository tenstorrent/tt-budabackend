// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/stream_graph/stream_graph_creator.h"

#include <algorithm>
#include <numeric>

#include "eth_l1_address_map.h"
#include "l1_address_map.h"

#include "data_flow_calculator/data_flow_calculator.h"
#include "graph_creator/stream_graph/pipe_streams_creator_factory.h"
#include "graph_creator/stream_graph/stream_flow_control_optimizer.h"
#include "graph_creator/stream_graph/stream_resources_allocator.h"
#include "model/rational_graph/nodes/pcie_streaming_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "model/stream_graph/ncrisc_config.h"
#include "model/stream_graph/stream_graph_collection.h"
#include "model/stream_graph/stream_node.h"
#include "pipegen2_constants.h"
#include "utils/logger.hpp"

namespace pipegen2
{
    std::unique_ptr<StreamGraphCollection> StreamGraphCreator::create_stream_graphs(
        const std::vector<std::unique_ptr<RationalGraph>>& rational_graphs,
        ResourceManager* resource_manager,
        const int epoch_num)
    {
        std::vector<std::unique_ptr<StreamGraph>> stream_graphs;

        PipeStreamsCreatorFactory pipe_streams_creator_factory(resource_manager->get_soc_info()->get_device_arch());

        // Map from virtual node to stream node phases subset. It's shared among all pipe streams creators.
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig> virt_node_to_stream_node;

        for (const std::unique_ptr<RationalGraph>& rational_graph : rational_graphs)
        {
            std::unique_ptr<StreamGraph> stream_graph = std::make_unique<StreamGraph>();

            // CAUTION: This map contains all the streams that have been created for each pipe in rational graph, but
            // some of those streams might have been softly deleted during creation of successive pipes and not added to
            // the stream graph. Use with caution!
            std::unordered_map<const RGBasePipe*, std::vector<StreamNode*>> streams_per_pipe =
                create_streams_per_pipe(rational_graph.get(), &pipe_streams_creator_factory, resource_manager,
                                        &virt_node_to_stream_node, stream_graph.get());

            connect_pcie_streams(rational_graph.get(), streams_per_pipe, virt_node_to_stream_node);
            connect_streams_sharing_same_buffer(stream_graph.get());

            stream_graphs.push_back(std::move(stream_graph));
        }

        std::unique_ptr<StreamGraphCollection> stream_graph_collection = std::make_unique<StreamGraphCollection>();
        for (std::unique_ptr<StreamGraph>& stream_graph : stream_graphs)
        {
            std::vector<std::unique_ptr<StreamGraph>> connected_components =
                stream_graph->split_into_connected_components();
            for (std::unique_ptr<StreamGraph>& connected_component : connected_components)
            {
                stream_graph_collection->add_stream_graph(std::move(connected_component));
            }
        }

        const auto& max_unroll_factors = calculate_max_unroll_factor(stream_graph_collection.get());

        const std::vector<std::unique_ptr<StreamGraph>>& connected_stream_graphs =
            stream_graph_collection->get_stream_graphs();
        calculate_unrolled_phase_offset(connected_stream_graphs);

        for (std::size_t i = 0; i < connected_stream_graphs.size(); ++i)
        {
            const std::unique_ptr<StreamGraph>& stream_graph = connected_stream_graphs[i];

            optimize_stream_graph(stream_graph.get());

            unroll_stream_graph(stream_graph.get(), max_unroll_factors[i]);

            optimize_stream_graph_flow_control(stream_graph.get());
        }

        insert_dummy_phases(stream_graph_collection.get());

        StreamResourcesAllocator stream_resources_allocator(resource_manager);
        stream_resources_allocator.allocate_resources(stream_graph_collection.get());

        assign_pcie_streams_noc_addresses(resource_manager);

        // Needs to be done last (after the resource allocation) as otherwise, we won't allocate buffers properly,
        // as some fork or intermed streams may be missing from fork lists and thus won't get correct addresses.
        fix_fork_lists(stream_graph_collection.get());

        add_epoch_offset_to_stream_phases(stream_graph_collection.get(), epoch_num);

        return stream_graph_collection;
    }

    std::vector<PhaseConfig>::iterator StreamGraphCreator::find_phase_config_by_phase_id(
        std::vector<PhaseConfig>& phase_configs,
        const PhaseId phase_id)
    {
        // The below functionality is the same as content of binary_search as stated in the C++ standard.
        // The binary_search function sadly doesn't return the iterator to the found element.
        // Bellow code should be faster than std::find for larger vectors.
        auto equal_or_higher = std::lower_bound(phase_configs.begin(),
                                                phase_configs.end(),
                                                phase_id,
                                                [](const PhaseConfig& phase_config, PhaseId phase_id)
                                                {
                                                    return phase_config.phase_id < phase_id;
                                                });
        log_assert(equal_or_higher != phase_configs.end() && equal_or_higher->phase_id == phase_id,
                   "Expected to find at least one phase config with phase_id equal to {}", phase_id);

        return equal_or_higher;
    }

    std::unordered_map<const RGBasePipe*, std::vector<StreamNode*>>
    StreamGraphCreator::create_streams_per_pipe(
        const RationalGraph* rational_graph,
        PipeStreamsCreatorFactory* pipe_streams_creator_factory,
        ResourceManager* resource_manager,
        std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>* virt_node_to_stream_node,
        StreamGraph* stream_graph)
    {
        DataFlowCalculator data_flow_calculator(rational_graph, constants::general_max_num_tiles_per_phase);
        DataFlowInfo data_flow_info = data_flow_calculator.get_data_flow_info();

        virt_node_to_stream_node->clear();

        std::vector<std::unique_ptr<StreamNode>> created_streams;
        std::unordered_map<const RGBasePipe*, std::vector<StreamNode*>> streams_per_pipe;

        for (const RGBasePipe* pipe : rational_graph->get_pipes_in_topological_order())
        {
            PipeStreamsCreator* streams_creator = pipe_streams_creator_factory->get_pipe_streams_creator(
                pipe, resource_manager, virt_node_to_stream_node);

            std::vector<std::unique_ptr<StreamNode>> pipe_streams = streams_creator->create_streams(
                pipe, data_flow_info);

            add_created_streams_to_streams_per_pipe(pipe, pipe_streams, streams_per_pipe);

            // We can't add created streams to stream graph right away because they might be softly deleted during
            // creation of streams for another pipe after this.
            std::move(pipe_streams.begin(), pipe_streams.end(), std::back_inserter(created_streams));
        }

        move_created_streams_to_graph(created_streams, stream_graph);

        return streams_per_pipe;
    }

    void StreamGraphCreator::connect_pcie_streams(
        const RationalGraph* rational_graph,
        const std::unordered_map<const RGBasePipe*, std::vector<StreamNode*>>& streams_per_pipe,
        const std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>& virt_node_to_stream_node)
    {
        if (!rational_graph->is_doing_pcie_transfer())
        {
            return;
        }

        // This logic assumes that pipes and streaming nodes doing one PCIe write/read are added in the same order to
        // the rational graph, since they are added during decomposition of one MMIO pipe.

        std::vector<const RGBasePipe*> pipes_writing_to_pcie;
        std::vector<const RGBasePipe*> pipes_reading_from_pcie;
        find_pipes_doing_pcie_transfer(rational_graph, pipes_writing_to_pcie, pipes_reading_from_pcie);

        log_assert(pipes_writing_to_pcie.size() == pipes_reading_from_pcie.size(),
                   "Expected to find the same number of pipes writing and reading from PCIe in one rational graph");

        for (std::size_t i = 0; i < pipes_writing_to_pcie.size(); ++i)
        {
            StreamNode* stream_writing_to_pcie =
                find_stream_writing_to_pcie(pipes_writing_to_pcie[i], streams_per_pipe.at(pipes_writing_to_pcie[i]),
                                            virt_node_to_stream_node);
            log_assert(stream_writing_to_pcie,
                       "Expected to find NCRISC config in one of the streams created for pipe writing to PCIe.");

            StreamNode* stream_reading_from_pcie =
                find_first_stream_with_ncrisc_config(streams_per_pipe.at(pipes_reading_from_pcie[i]));
            log_assert(stream_reading_from_pcie,
                       "Expected to find NCRISC config in one of the streams created for pipe reading from PCIe.");

            stream_writing_to_pcie->get_base_ncrisc_config().dram_streaming_dest = stream_reading_from_pcie;

            m_pcie_writing_streams.emplace_back(stream_writing_to_pcie);
        }
    }

    void StreamGraphCreator::find_pipes_doing_pcie_transfer(const RationalGraph* rational_graph,
                                                            std::vector<const RGBasePipe*>& pipes_writing_to_pcie,
                                                            std::vector<const RGBasePipe*>& pipes_reading_from_pcie)
    {
        for (const std::unique_ptr<RGBaseNode>& node : rational_graph->get_nodes())
        {
            const PCIeStreamingNode* pcie_streaming_node = dynamic_cast<const PCIeStreamingNode*>(node.get());
            if (pcie_streaming_node == nullptr)
            {
                continue;
            }

            if (pcie_streaming_node->get_output_pipes().empty())
            {
                log_assert(pcie_streaming_node->get_input_pipe(),
                           "PCIe streaming node has to have either input or output pipe.");
                pipes_writing_to_pcie.push_back(pcie_streaming_node->get_input_pipe());
            }
            else
            {
                pipes_reading_from_pcie.push_back(pcie_streaming_node->get_output_pipe());
            }
        }
    }

    StreamNode* StreamGraphCreator::find_stream_writing_to_pcie(
        const RGBasePipe* pipe_writing_to_pcie,
        const std::vector<StreamNode*>& pipe_streams,
        const std::unordered_map<const VirtualNode*, StreamPhasesCommonConfig>& virt_node_to_stream_node)
    {
        StreamNode* stream_writing_to_pcie = find_first_stream_with_ncrisc_config(pipe_streams);

        if (stream_writing_to_pcie)
        {
            return stream_writing_to_pcie;
        }

        // This means that stream was created for some of the previous pipes and is connected to the virtual
        // node at the pipe input.
        const VirtualNode* virtual_input_node =
            dynamic_cast<const VirtualNode*>(pipe_writing_to_pcie->get_inputs()[0].get_node());
        log_assert(virtual_input_node, "Expected to find virtual node at the input of the pipe");

        auto it = virt_node_to_stream_node.find(virtual_input_node);
        log_assert(it != virt_node_to_stream_node.end(), "Expected to find stream node assigned to virtual node");

        return it->second.get_stream_node();
    }

    StreamNode* StreamGraphCreator::find_first_stream_with_ncrisc_config(const std::vector<StreamNode*>& streams)
    {
        for (StreamNode* stream : streams)
        {
            if (!stream->get_ncrisc_configs().empty())
            {
                return stream;
            }
        }

        return nullptr;
    }

    void StreamGraphCreator::connect_streams_sharing_same_buffer(const StreamGraph* stream_graph)
    {
        std::unordered_map<NodeId, StreamNode*> buf_node_id_to_stream_node = create_node_id_to_stream_mapping(
            stream_graph);

        // Go through all streams and if a stream is configured to share a buffer with another stream, point them to
        // each other.
        for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
        {
            if (!stream_node->get_base_config().get_shared_space_buffer_node_id().has_value())
            {
                // Stream is not configured to share its buffer with another stream.
                continue;
            }
            if (stream_node->is_sharing_buffer())
            {
                // Already connected to its buffer sharing stream.
                continue;
            }
            if (stream_node->get_stream_type() == StreamType::Intermed)
            {
                // Skip intermed streams, even though they may share buffers with packer streams, they are
                // configured with `space_shared_with_operand` property.
                continue;
            }

            const NodeId shared_space_buffer_node_id =
                stream_node->get_base_config().get_shared_space_buffer_node_id().value();
            auto sharing_stream_it = buf_node_id_to_stream_node.find(shared_space_buffer_node_id);
            log_assert(sharing_stream_it != buf_node_id_to_stream_node.end(),
                           "Expected to find a stream node with buf_id equal to {} in the given rational graph",
                           shared_space_buffer_node_id);

            if (sharing_stream_it->second->get_stream_type() == StreamType::Intermed)
            {
                // Skip intermed streams, even though they may share buffers with packer streams, they are
                // configured with `space_shared_with_operand` property.
                continue;
            }

            sharing_stream_it->second->set_space_shared_with_stream(stream_node.get());
            stream_node->set_space_shared_with_stream(sharing_stream_it->second);
        }
    }

    std::unordered_map<NodeId, StreamNode*> StreamGraphCreator::create_node_id_to_stream_mapping(
        const StreamGraph* stream_graph)
    {
        std::unordered_map<NodeId, StreamNode*> buf_node_id_to_stream_node;

        for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
        {
            if (stream_node->get_base_config().get_buf_id().has_value())
            {
                buf_node_id_to_stream_node.emplace(stream_node->get_base_config().get_buf_id().value(),
                                                    stream_node.get());
            }
        }

        return buf_node_id_to_stream_node;
    }

    void StreamGraphCreator::add_created_streams_to_streams_per_pipe(
        const RGBasePipe* pipe,
        const std::vector<std::unique_ptr<StreamNode>>& pipe_streams,
        std::unordered_map<const RGBasePipe*, std::vector<StreamNode*>>& streams_per_pipe)
    {
        std::vector<StreamNode*> raw_pipe_streams;
        raw_pipe_streams.reserve(pipe_streams.size());
        std::transform(pipe_streams.begin(), pipe_streams.end(), std::back_inserter(raw_pipe_streams),
                       [](const std::unique_ptr<StreamNode>& stream_node){ return stream_node.get(); });

        streams_per_pipe.emplace(pipe, std::move(raw_pipe_streams));
    }

    void StreamGraphCreator::move_created_streams_to_graph(std::vector<std::unique_ptr<StreamNode>>& created_streams,
                                                           StreamGraph* stream_graph)
    {
        for (std::unique_ptr<StreamNode>& stream_node : created_streams)
        {
            if (stream_node->is_deleted())
            {
                continue;
            }

            stream_graph->add_stream(std::move(stream_node));
        }
    }

    void StreamGraphCreator::calculate_unrolled_phase_offset(
        const std::vector<std::unique_ptr<StreamGraph>>& stream_graphs)
    {
        m_unrolled_phase_offset = 0;
        for (const std::unique_ptr<StreamGraph>& stream_graph : stream_graphs)
        {
            m_unrolled_phase_offset = std::max(m_unrolled_phase_offset,
                                               get_max_phase_in_stream_graph(stream_graph.get()));
        }
        ++m_unrolled_phase_offset;
    }

    void StreamGraphCreator::optimize_stream_graph(StreamGraph* stream_graph)
    {
        optimize_unpacker_streams_fed_by_union(stream_graph);
    }

    void StreamGraphCreator::optimize_unpacker_streams_fed_by_union(StreamGraph* stream_graph)
    {
        for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
        {
            if (!stream_node->get_phase_config(0).get_source().has_value() ||
                stream_node->get_stream_type() != StreamType::Unpacker)
            {
                continue;
            }

            StreamNode* source_stream = stream_node->get_phase_config(0).get_source().value();
            if (!source_stream->get_base_config().get_is_union_stream().value_or(false))
            {
                continue;
            }

            const unsigned int unpacker_tcg = stream_node->get_base_config().get_tile_clear_granularity().value();
            std::vector<PhaseConfig>& phase_configs = stream_node->get_phase_configs();
            log_assert(!phase_configs.empty(), "At this point, stream node should have at least one phase config");

            // TODO: Unify this logic with StreamCreator::merge_phases.
            std::vector<PhaseConfig> merged_phase_configs {phase_configs[0]};
            for (std::size_t i = 1; i < phase_configs.size(); ++i)
            {
                unsigned int cumulative_num_msgs = merged_phase_configs.back().config.get_num_msgs().value();
                unsigned int current_num_msgs = phase_configs[i].config.get_num_msgs().value();

                if (cumulative_num_msgs + current_num_msgs <= unpacker_tcg)
                {
                    merged_phase_configs.back().config.set_num_msgs(cumulative_num_msgs + current_num_msgs);
                }
                else
                {
                    merged_phase_configs.emplace_back(std::move(phase_configs[i]));
                }
            }

            // Max tiles per phase shall remain unchanged.
            stream_node->update_phases(std::move(merged_phase_configs), stream_node->get_max_num_tiles_per_phase());
        }
    }

    void StreamGraphCreator::optimize_stream_graph_flow_control(StreamGraph* stream_graph)
    {
        StreamFlowControlOptimizer stream_flow_control_optimizer;
        stream_flow_control_optimizer.optimize_stream_graph_flow_control(stream_graph);
    }

    void StreamGraphCreator::insert_dummy_phases(StreamGraphCollection* stream_graph_collection)
    {
        // Insert dummy phases (indication for dummy phase, blobgen will insert the binary blob for the dummy phase) on
        // source streams when they are connected to a remote receiver stream and the source stream has only one phase.
        // Additionally, insert dummy phase on the destination streams of the source stream.
        //
        // TODO: When we have single phase but flow control is enabled we will insert dummy phases. However this looks
        // unnecessary. Try to remove this logic and see if it breaks anything. To do that, we need to make sure to have
        // race condition reproducible.
        for (const std::unique_ptr<StreamGraph>& stream_graph : stream_graph_collection->get_stream_graphs())
        {
            for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
            {
                if (!stream_node->get_base_config().get_remote_receiver().value_or(false))
                {
                    // Stream has not a remote receiver, so we don't need to insert dummy phase.
                    continue;
                }

                std::vector<PhaseConfig>& phase_configs = stream_node->get_phase_configs();
                if (phase_configs.size() > 1)
                {
                    // Stream has more than one phase, so we don't need to insert dummy phase.
                    continue;
                }

                StreamConfig& phase_config = phase_configs[0].config;
                phase_config.set_follow_by_sender_dummy_phase(true);

                log_assert(phase_config.get_dest().has_value(),
                           "Expected to find dest in phase config when remote receiver is set");

                // Set dummy phase on the destination streams.
                for (StreamNode* dest_stream : phase_config.get_dest().value())
                {
                    auto phase_iter = find_phase_config_by_phase_id(dest_stream->get_phase_configs(),
                                                                    phase_configs[0].phase_id);
                    phase_iter->config.set_follow_by_receiver_dummy_phase(true);

                    // Set the flag on the next phase to false, so that we don't insert more dummy phases.
                    // Set it only if the next phase exists.
                    // Set it only if next phase was not already set (by some previous sender).
                    ++phase_iter;
                    if (phase_iter != dest_stream->get_phase_configs().end() &&
                        !phase_iter->config.get_follow_by_receiver_dummy_phase().has_value())
                    {
                        phase_iter->config.set_follow_by_receiver_dummy_phase(false);
                    }
                }
            }
        }
    }
    
    void StreamGraphCreator::unroll_stream_graph(StreamGraph* stream_graph, const unsigned int max_unroll_factor)
    {
        // TODO: split code below in smaller functions.
        // - one that calculates iterations_limit_wrt_max_tiles_per_phase_for_graph,
        // - one that calculates unroll_skip_num,
        // - one that calculates unroll_factor.
        unsigned int iterations_limit_wrt_max_tiles_per_phase_for_graph = std::numeric_limits<unsigned int>::max();
        unsigned int unroll_skip_num = 1;

        for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
        {
            if (stream_node->get_stream_type() == StreamType::Unpacker)
            {
                const unsigned int unpacker_tcg = stream_node->get_base_config().get_tile_clear_granularity().value();
                const unsigned int unpacker_num_tiles_in_iter = stream_node->get_num_tiles_in_iteration();
                unroll_skip_num = std::lcm(
                    unroll_skip_num,
                    unpacker_tcg > unpacker_num_tiles_in_iter ? unpacker_tcg / unpacker_num_tiles_in_iter : 1);
            }

            unsigned int iterations_limit_wrt_max_tiles_per_phase_for_node =
                constants::general_max_num_tiles_per_phase / stream_node->get_num_tiles_in_iteration();
            if (iterations_limit_wrt_max_tiles_per_phase_for_node <= stream_node->get_num_iterations_in_epoch())
            {
                iterations_limit_wrt_max_tiles_per_phase_for_graph =
                    std::min(iterations_limit_wrt_max_tiles_per_phase_for_graph,
                             iterations_limit_wrt_max_tiles_per_phase_for_node);
            }
        }

        for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
        {
            const unsigned int num_iterations_in_epoch = stream_node->get_num_iterations_in_epoch();

            bool try_full_unroll = true;
            unsigned int unroll_factor = unroll_skip_num;
            for (unsigned int cur_unroll_factor = unroll_skip_num;
                cur_unroll_factor <= max_unroll_factor && cur_unroll_factor <= num_iterations_in_epoch &&
                (iterations_limit_wrt_max_tiles_per_phase_for_graph == 0 ||
                cur_unroll_factor <= iterations_limit_wrt_max_tiles_per_phase_for_graph);
                cur_unroll_factor += unroll_skip_num)
            {
                if (num_iterations_in_epoch % cur_unroll_factor == 0)
                {
                    unroll_factor = cur_unroll_factor;
                    try_full_unroll = false;
                }
            }

            // Important note: this whole conditional block is copied from pipegen_v1, although this logic is not
            // completely clear about how does it ensure divisibility of unroll factor by unpacker's TCG?
            // Anyway, this is a hack for perf and we have to investigate whether it is useful or not.
            // TODO: inspect when we under which circumstances (through testing) we hit this condition.
            // TODO: run pipegen v1 on the OG dataset and try to catch this code running.
            if (try_full_unroll)
            {
                if (iterations_limit_wrt_max_tiles_per_phase_for_graph != 0 &&
                    iterations_limit_wrt_max_tiles_per_phase_for_graph <= max_unroll_factor &&
                    iterations_limit_wrt_max_tiles_per_phase_for_graph < num_iterations_in_epoch)
                {
                    if (num_iterations_in_epoch % iterations_limit_wrt_max_tiles_per_phase_for_graph == 0)
                    {
                        unroll_factor = iterations_limit_wrt_max_tiles_per_phase_for_graph;
                    }
                }
                else if (num_iterations_in_epoch <= max_unroll_factor)
                {
                    unroll_factor = num_iterations_in_epoch;
                }
            }

            unroll_phases(stream_node.get(), num_iterations_in_epoch, unroll_factor);
            if (unroll_factor > 1)
            {
                try_merging_unrolled_phases(stream_node.get());
            }
        }

        for (const std::unique_ptr<StreamNode>& stream_node: stream_graph->get_streams())
        {
            adjust_phase_transition_properties(stream_node.get());
        }
    }

    PhaseId StreamGraphCreator::get_max_phase_in_stream_graph(const StreamGraph* stream_graph)
    {
        PhaseId max_phase_in_stream_graph = 0;
        for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
        {
            log_assert(stream_node->get_num_phases() > 0, "Expected phases to be created for stream node");
            max_phase_in_stream_graph = std::max(max_phase_in_stream_graph, stream_node->get_last_phase_offset());
        }
        return max_phase_in_stream_graph;
    }

    std::vector<unsigned int> StreamGraphCreator::calculate_max_unroll_factor(
        const StreamGraphCollection* stream_graph_collection)
    {
        // One max unroll factor per stream graph.
        std::vector<unsigned int> max_unroll_factors;
        using StreamLocation = tt_cxy_pair;

        // Mapping from core location to the estimated number of bytes used by all streams on the core.
        std::unordered_map<StreamLocation, unsigned int> phases_footprint_per_core;

        for (const std::unique_ptr<StreamGraph>& stream_graph : stream_graph_collection->get_stream_graphs())
        {
            unsigned int max_unroll_factor = std::numeric_limits<unsigned int>::max();

            std::unordered_set<StreamLocation> stream_graph_core_locations;
            for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
            {
                stream_graph_core_locations.insert(stream_node->get_physical_location());
            }

            // Note: pipegen_v2 implementation is more precise as we account only for every created stream, whereas in
            // pipegen_v1 we account even for streams that won't be created (e.g. dram streams that are merged with
            // unpacker streams). This could affect the max_unroll_factor calculation, as we could potentially have
            // greater value computed than in pipegen_v1.
            for (const StreamLocation& core_location : stream_graph_core_locations)
            {
                if (phases_footprint_per_core.find(core_location) != phases_footprint_per_core.end())
                {
                    // Already computed footprint during processing of another stream graph.
                    continue;
                }
                for (const StreamNode* stream_node : stream_graph_collection->get_streams_on_core(core_location))
                {
                    const unsigned int mblock_buffering =
                        stream_node->get_base_config().get_num_mblock_buffering().value_or(1);
                    phases_footprint_per_core[stream_node->get_physical_location()] +=
                        stream_node->get_phase_configs().size() * c_estimated_phase_physical_size_bytes *
                        mblock_buffering;
                }
            }

            // TODO: set is_ethernet to true when stream graph spans across ethernet cores.
            const bool is_ethernet = false;
            for (const StreamLocation& core_location : stream_graph_core_locations)
            {
                log_assert(phases_footprint_per_core.find(core_location) != phases_footprint_per_core.end(),
                           "Core location not found in phases_footprint_per_core map");
                const unsigned int overlay_blob_size = is_ethernet ?
                    eth_l1_mem::address_map::OVERLAY_BLOB_SIZE : l1_mem::address_map::OVERLAY_BLOB_SIZE;

                // Compute possible unroll factor based on the estimated footprint of all phases on the core and allowed
                // space in L1 memory.
                const unsigned int possible_unroll_factor =
                    (overlay_blob_size - c_estimated_epoch_t_size_bytes) / phases_footprint_per_core[core_location];

                // Update max unroll factor as the smallest possible factor on each core across the stream graph.
                max_unroll_factor = std::min(max_unroll_factor, possible_unroll_factor);
            }

            max_unroll_factors.push_back(max_unroll_factor);
        }

        return max_unroll_factors;
    }

    void StreamGraphCreator::unroll_phases(StreamNode* stream_node,
                                           const unsigned int num_iterations_in_epoch,
                                           const unsigned int unroll_factor)
    {
        stream_node->get_phase_config(0).set_num_unroll_iter(unroll_factor);
        stream_node->get_phase_config(0).set_num_iters_in_epoch(num_iterations_in_epoch / unroll_factor);
        stream_node->get_phase_config(0).set_unroll_iter(0);
        const unsigned int initial_phase_count = stream_node->get_phase_configs().size();
        for (unsigned int unroll_iter = 1; unroll_iter < unroll_factor; ++unroll_iter)
        {
            for (unsigned int i = 0; i < initial_phase_count; ++i)
            {
                // Ignore phases that were added during unrolling.
                const PhaseConfig& phase_config = stream_node->get_phase_configs()[i];

                StreamConfig new_phase_config = phase_config.config;
                new_phase_config.set_unroll_iter(unroll_iter);
                stream_node->add_phase_config(m_unrolled_phase_offset * unroll_iter + phase_config.phase_id,
                                              std::move(new_phase_config));
            }
        }
    }

    void StreamGraphCreator::try_merging_unrolled_phases(StreamNode* stream_node)
    {
        if (!stream_node->get_phase_config(0).get_source().has_value() ||
            (stream_node->get_stream_type() != StreamType::Unpacker &&
             stream_node->get_stream_type() != StreamType::Multicast &&
             stream_node->get_stream_type() != StreamType::Relay))
        {
            return;
        }

        if (stream_node->changes_source())
        {
            return;
        }

        StreamNode* source_stream = stream_node->get_phase_config(0).get_source().value();

        // Unrolled phases on multicast stream can only be merged if the source is a relay stream.
        if (stream_node->get_stream_type() == StreamType::Multicast &&
            source_stream->get_stream_type() != StreamType::Relay)
        {
            return;
        }

        if (source_stream->get_base_config().get_is_union_stream().value_or(false))
        {
            return;
        }

        if (source_stream->changes_destinations())
        {
            return;
        }

        std::vector<PhaseConfig>& phase_configs = stream_node->get_phase_configs();
        if (phase_configs.size() <= 1)
        {
            // Nothing to merge.
            return;
        }

        unsigned int total_num_msgs = stream_node->get_num_tiles_in_iteration();
        if (total_num_msgs > constants::general_max_num_tiles_per_phase)
        {
            return;
        }

        phase_configs.erase(phase_configs.begin() + 1, phase_configs.end());
        StreamConfig update;
        update.set_num_msgs(total_num_msgs);
        update.set_num_unroll_iter(1);
        update.set_unroll_iter(0);
        phase_configs[0].config += update;

        NcriscConfig* dram_writer_config = stream_node->get_ncrisc_writer_config();
        if (dram_writer_config)
        {
            dram_writer_config->num_msgs = total_num_msgs;
        }
    }

    void StreamGraphCreator::adjust_phase_transition_properties(StreamNode* stream_node)
    {
        // This function sets stream phase properties that affect transitions between phases.
        // - phase_auto_config: if set, stream will auto load the next phase.
        // - next_phase_src|dest_change: this is a stream connection property, that enables streams on both sides of the
        // connection to skip handshaking. If this optimization is enabled, streams basically behave as if they work in
        // a single phase even though they are configured with several phases.

        compute_ptrs_not_zero_field(stream_node);
        compute_phase_auto_config_field(stream_node);

        StreamConfig& base_config = stream_node->get_base_config();
        base_config.set_phase_auto_advance(true);

        // Disable optimization by default. It may be enabled for certain subset of phases.
        base_config.set_next_phase_dest_change(true);

        if (stream_node->get_stream_type() == StreamType::GatherRelay)
        {
            // We do not optimize connections between gather relays and gather streams.
            return;
        }

        const unsigned int max_num_tiles_per_phase = stream_node->get_max_num_tiles_per_phase();

        log_assert(stream_node->get_phase_configs().size() > 0, "At this point, stream must have at least one phase");
        StreamConfig& first_phase_config = stream_node->get_phase_config(0);
        if (!first_phase_config.get_source().value_or(nullptr))
        {
            const bool is_dram_read = base_config.get_dram_io().value_or(false) ||
                                      base_config.get_dram_streaming().value_or(false);
            const bool rd_wr_pointers_aligned_with_zero = !base_config.get_ptrs_not_zero().value_or(true);
            const bool can_force_next_phase_src_change_opt =
                is_dram_read &&
                rd_wr_pointers_aligned_with_zero &&
                (stream_node->get_num_tiles_in_iteration() <= constants::general_max_num_tiles_per_phase ||
                 base_config.get_dram_input_no_push().value_or(false));

            if (can_force_next_phase_src_change_opt)
            {
                first_phase_config.set_next_phase_src_change(false);
            }
            else
            {
                first_phase_config.set_next_phase_src_change(true);
            }
        }

        if (!first_phase_config.get_dest().has_value())
        {
            // Streams without destination don't need this optimization.
            return;
        }

        log_assert(first_phase_config.get_dest().value().size() > 0, "Expected at least one destination stream");
        StreamNode* dest_stream_node = first_phase_config.get_dest().value().at(0);
        dest_stream_node->get_base_config().set_next_phase_src_change(true);

        auto is_dest_unpacker_with_2k_header_buffer_reset = [](StreamNode* dest_stream)
        {
            log_assert(dest_stream->get_base_config().get_buffer_size().has_value(), "Expected buffer size to be set");
            int buffer_size_tiles = dest_stream->get_base_config().get_buffer_size().value() /
                                    dest_stream->get_base_config().get_msg_size().value();
            return c_use_2k_tile_header_buffer_reset &&
                   dest_stream->get_stream_type() == StreamType::Unpacker &&
                   buffer_size_tiles < constants::general_max_num_tiles_per_phase / 2;
        };

        unsigned int msg_count = 0;
        unsigned int segment_start_phase_idx = 0;
        int cur_unroll_iter = 0;
        std::vector<PhaseConfig>& phase_configs = stream_node->get_phase_configs();

        const bool is_source_scatter_send_from_l1_buffer =
            base_config.get_is_l1_producer().value_or(false) &&
            base_config.get_is_sending_tiles_out_of_order().value_or(false);

        for (unsigned int i = 0; i < phase_configs.size(); ++i)
        {
            const StreamConfig& phase_config = phase_configs[i].config;
            const PhaseId phase_id = phase_configs[i].phase_id;
            StreamNode* cur_dest = phase_config.get_dest().value().at(0);
            int num_msgs_in_cur_phase = phase_config.get_num_msgs().value();

            // TODO 1.: stop optimization if packer sends data to gather block, unless it is done in consecutive phases.
            // Generally, pipegen1 never optimizes packer to gather relay connection, except for scatter packer when it
            // gather may merge some phases together as they are coming from the same packer. That case is covered by
            // is_consecutive_phase_on_sender check. We should investigate if we can enable this optimization for packer
            // to gather relay connection in other cases.
            // TODO 2.: we stop optimization if we have packer with new unroll iter. This is because pipegen1 does that
            // and we want to be consistent with it for now. However, we should investigate if we can enable
            // optimization across unrolled iterations when message count is less than max_num_tiles_per_phase.
            // Concern is if we could mess up divisibility by unpacker's TCG if we optimize edge between scatter packer
            // and a relay stream.

            // Stop optimization when source of gather skips phases. This means gather block changes sources between
            // phases.
            const bool is_dest_gather_relay = cur_dest->get_stream_type() == StreamType::GatherRelay ||
                                              cur_dest->get_stream_type() == StreamType::Gather;
            const bool is_consecutive_phase_on_sender = i == 0 || phase_id == phase_configs[i - 1].phase_id + 1;
            const bool stop_opt_for_non_consecutive_phase = is_dest_gather_relay && !is_consecutive_phase_on_sender;

            // Stop optimization when sending scattered data between unrolled iterations. Exception is when the
            // destination is an unpacker with 2k header buffer reset or a regular stream with a single phase.
            const bool is_different_unroll_iter = phase_config.get_unroll_iter().value_or(0) != cur_unroll_iter;
            const bool is_l1_scatter_buffer_new_unroll_iter = is_source_scatter_send_from_l1_buffer &&
                                                              is_different_unroll_iter;
            const bool stop_opt_for_scatter_send = is_l1_scatter_buffer_new_unroll_iter &&
                                                   dest_stream_node->get_num_phases() > 1 &&
                                                   !is_dest_unpacker_with_2k_header_buffer_reset(dest_stream_node);

            // Stop optimization when we cross max_num_tiles_per_phase threshold. Exception is when the destination is
            // an unpacker with 2k header buffer reset.
            const bool crossing_max_tiles_per_phase = msg_count + num_msgs_in_cur_phase > max_num_tiles_per_phase;
            const bool stop_opt_for_max_tiles_exceeded =
                crossing_max_tiles_per_phase &&
                !is_dest_unpacker_with_2k_header_buffer_reset(dest_stream_node);

            const bool should_stop_optimization = cur_dest != dest_stream_node ||
                                                  stop_opt_for_non_consecutive_phase ||
                                                  stop_opt_for_max_tiles_exceeded ||
                                                  stop_opt_for_scatter_send;

            // Stop optimization means we stop further optimization and try optimizing edge between current stream and
            // current destination stream for the phases we have seen so far.
            if (should_stop_optimization)
            {
                optimize_edge_phases(stream_node, dest_stream_node, segment_start_phase_idx, i - 1);
                segment_start_phase_idx = i;
                msg_count = 0;
                dest_stream_node = cur_dest;
            }
            msg_count += num_msgs_in_cur_phase;
            cur_unroll_iter = phase_config.get_unroll_iter().value_or(0);
        }
        optimize_edge_phases(stream_node, dest_stream_node, segment_start_phase_idx, phase_configs.size() - 1);

        // Copy phase transition properties to other destination streams in case of multicast.
        copy_phase_transition_properties(first_phase_config.get_dest().value());
    }

    void StreamGraphCreator::copy_phase_transition_properties(const std::vector<StreamNode*> destination_streams)
    {
        StreamNode* first_dest_stream = destination_streams[0];
        std::vector<PhaseConfig>& first_dest_stream_configs = first_dest_stream->get_phase_configs();

        for (std::size_t dest_stream_idx = 1; dest_stream_idx < destination_streams.size(); ++dest_stream_idx)
        {
            StreamNode* dest_stream = destination_streams[dest_stream_idx];
            std::vector<PhaseConfig>& dest_stream_configs = dest_stream->get_phase_configs();

            log_assert(dest_stream_configs.size() == first_dest_stream_configs.size(),
                       "Expecting that all multicast output streams have the same number of phases");

            dest_stream->get_base_config().opt_set_next_phase_src_change(
                first_dest_stream->get_base_config().get_next_phase_src_change());
            dest_stream->get_base_config().opt_set_next_phase_dest_change(
                first_dest_stream->get_base_config().get_next_phase_dest_change());

            for (unsigned int i = 0; i < dest_stream_configs.size(); ++i)
            {
                dest_stream_configs[i].config.opt_set_next_phase_src_change(
                    first_dest_stream_configs[i].config.get_next_phase_src_change());
                dest_stream_configs[i].config.opt_set_next_phase_dest_change(
                    first_dest_stream_configs[i].config.get_next_phase_dest_change());
            }
        }
    }

    void StreamGraphCreator::compute_ptrs_not_zero_field(StreamNode* stream_node)
    {
        StreamConfig& base_config = stream_node->get_base_config();

        // Only certain streams need to have this parameter configured, and for those we have already set it to
        // default value.
        if (!base_config.get_ptrs_not_zero().has_value())
        {
            return;
        }

        // The following logic is a simplified version of the logic from the original pipegen for DRAM pipes.
        // TODO: Revise this logic in case we need to compute this for non-DRAM pipes' streams.

        const unsigned int num_tiles_in_iteration = stream_node->get_num_tiles_in_iteration();
        const unsigned int num_iterations_in_epoch = stream_node->get_num_iterations_in_epoch();
        const unsigned int unroll_factor = stream_node->get_phase_config(0).get_num_unroll_iter().value();
        const unsigned int num_tiles_in_unrolled_iteration = num_tiles_in_iteration / unroll_factor;
        const unsigned int buffer_size = base_config.get_buffer_size().value();
        const unsigned int tile_size = base_config.get_msg_size().value();

        bool ptrs_not_zero = false;

        if (num_tiles_in_iteration <= constants::general_max_num_tiles_per_phase)
        {
            ptrs_not_zero = num_tiles_in_iteration * tile_size % buffer_size != 0;
        }
        else if (num_tiles_in_unrolled_iteration <= constants::general_max_num_tiles_per_phase)
        {
            ptrs_not_zero = num_tiles_in_unrolled_iteration * tile_size % buffer_size != 0;
        }
        else
        {
            // For DRAM transfer we need to make sure that in each phase we transfer amount divisible by buffer size.
            const std::vector<PhaseConfig>& phase_configs = stream_node->get_phase_configs();
            std::size_t last_phase_to_check_idx = (phase_configs.size() / unroll_factor) - 1;
            if (should_skip_last_phase_for_ptrs_not_zero(phase_configs, last_phase_to_check_idx,
                                                         num_iterations_in_epoch, unroll_factor))
            {
                --last_phase_to_check_idx;
            }
            for (std::size_t phase_idx = 0; phase_idx <= last_phase_to_check_idx; ++phase_idx)
            {
                const unsigned int num_tiles_in_phase = phase_configs[phase_idx].config.get_num_msgs().value();
                if (num_tiles_in_phase * tile_size % buffer_size != 0)
                {
                    ptrs_not_zero = true;
                    break;
                }
            }
        }

        base_config.set_ptrs_not_zero(ptrs_not_zero);
    }

    bool StreamGraphCreator::should_skip_last_phase_for_ptrs_not_zero(const std::vector<PhaseConfig>& phase_configs,
                                                                      const std::size_t last_unrolled_phase_idx,
                                                                      const unsigned int num_iterations_in_epoch,
                                                                      const unsigned int unroll_factor)
    {
        // Pipegen1 always checks last phase for buffer read/write pointers alignment when it happens to be the phase
        // where accumulated number of tiles goes over the maximum number of tiles per phase.
        // That seems like an accidental consiquence of the way pipegen1 code is written, since last phase should have
        // effect on alignment only when there is more than one iteration (before unroll).
        // TODO: After testing on silicon we should decide if it is necessary to keep this code or it can be deleted.
        unsigned int accumulated_num_tiles = 0;
        for (std::size_t phase_idx = 0; phase_idx <= last_unrolled_phase_idx; ++phase_idx)
        {
            const unsigned int num_tiles_in_phase = phase_configs[phase_idx].config.get_num_msgs().value();
            accumulated_num_tiles += num_tiles_in_phase;

            if (accumulated_num_tiles > constants::general_max_num_tiles_per_phase)
            {
                if (phase_idx == last_unrolled_phase_idx)
                {
                    return false;
                }
                accumulated_num_tiles = num_tiles_in_phase;
            }
        }

        // In case of a single unrolled iteration we don't need to check the last phase.
        return num_iterations_in_epoch == 1 && unroll_factor == 1;
    }

    void StreamGraphCreator::compute_phase_auto_config_field(StreamNode* stream_node)
    {
        std::vector<PhaseConfig>& phase_configs = stream_node->get_phase_configs();

        // The last phase must wait for FW to reset its config, while all the others can be autoconfigured by default.
        stream_node->get_base_config().set_phase_auto_config(true);
        phase_configs.back().config.set_phase_auto_config(false);

        // In case of scattered pack, we have to autoconfigure after each unrolled iteration.
        if (!stream_node->get_base_config().get_is_scatter_pack().value_or(false))
        {
            return;
        }
        unsigned int last_unroll_iter = stream_node->get_phase_config(0).get_unroll_iter().value_or(0);
        unsigned int last_scatter_idx = stream_node->get_phase_config(0).get_scatter_idx().value_or(0);
        for (std::size_t i = 1; i < phase_configs.size(); ++i)
        {
            unsigned int current_unroll_iter = phase_configs[i].config.get_unroll_iter().value_or(last_unroll_iter);
            const bool different_unrolled_iterations = current_unroll_iter != last_unroll_iter;

            unsigned int current_scatter_idx = phase_configs[i].config.get_scatter_idx().value_or(0);
            const bool different_scatter_idx = current_scatter_idx != last_scatter_idx;

            if (different_unrolled_iterations || different_scatter_idx)
            {
                phase_configs[i - 1].config.set_phase_auto_config(false);
                phase_configs[i].config.set_phase_auto_config(true);
            }

            last_unroll_iter = current_unroll_iter;
            last_scatter_idx = current_scatter_idx;
        }
        phase_configs.back().config.set_phase_auto_config(false);
    }

    void StreamGraphCreator::optimize_edge_phases(StreamNode* source_stream,
                                                  StreamNode* dest_stream,
                                                  const int segment_start_phase_idx,
                                                  const int segment_end_phase_idx)
    {
        if (segment_start_phase_idx >= segment_end_phase_idx)
        {
            return;
        }

        std::vector<PhaseConfig>& source_phase_configs = source_stream->get_phase_configs();

        const PhaseId start_phase_id = source_phase_configs[segment_start_phase_idx].phase_id;
        const PhaseId end_phase_id = source_phase_configs[segment_end_phase_idx].phase_id;
        std::vector<PhaseConfig>& dest_phase_configs = dest_stream->get_phase_configs();

        // Locate the start_phase_id in the phases of the destination stream.
        auto dest_phase_iter = std::upper_bound(
            dest_phase_configs.begin(), dest_phase_configs.end(), start_phase_id,
            [](const PhaseId phase_id, const PhaseConfig& phase_config)
            {
                return phase_config.phase_id > phase_id;
            });

        log_assert(dest_phase_iter != dest_phase_configs.begin(),
                   "Expected at least one phase in the destination stream less than or equal to the start phase id");
        --dest_phase_iter;

        // Now that phase is located in the destination stream, we must find subsegments of phases in the destination
        // stream where the source stream is the same and equal to the source stream of the start phase. Those segments
        // will be optimized using next_phase_src|dest_change=false.
        auto source_stream_subsegment_start_it = source_phase_configs.begin() + segment_start_phase_idx;
        auto dest_stream_subsegment_start_it = dest_phase_iter;

        bool started_new_subsegment = false;
        for (; dest_phase_iter != dest_phase_configs.end() && dest_phase_iter->phase_id <= end_phase_id;
             ++dest_phase_iter)
        {
            const StreamNode* dest_phase_source_stream = dest_phase_iter->config.get_source().value();
            log_assert(dest_phase_source_stream, "Expected to find a source stream for the phase");
            if (dest_phase_source_stream == source_stream)
            {
                if (!started_new_subsegment)
                {
                    dest_stream_subsegment_start_it = dest_phase_iter;
                }
                started_new_subsegment = true;
                continue;
            }

            if (started_new_subsegment)
            {
                // Do the reverse search, and find phase in the source stream that corresponds to the current phase in
                // the destination stream.
                auto source_stream_subsegment_end_it = std::upper_bound(
                    source_phase_configs.begin(), source_phase_configs.end(), dest_phase_iter->phase_id,
                    [](const PhaseId phase_id, const PhaseConfig& phase_config)
                    {
                        return phase_config.phase_id > phase_id;
                    });
                --source_stream_subsegment_end_it;
                set_next_phase_src_dest_change(
                    source_stream_subsegment_start_it,
                    source_stream_subsegment_end_it,
                    dest_stream_subsegment_start_it,
                    dest_phase_iter - 1);

                source_stream_subsegment_start_it = source_stream_subsegment_end_it + 1;
            }

            started_new_subsegment = false;
        }

        if (started_new_subsegment)
        {
            // Here we don't need to employ search, it's guaranteed that the end of the subsegment is the end of the
            // segment.
            set_next_phase_src_dest_change(
                source_stream_subsegment_start_it,
                source_phase_configs.begin() + segment_end_phase_idx,
                dest_stream_subsegment_start_it,
                dest_phase_iter - 1);
        }
    }

    void StreamGraphCreator::set_next_phase_src_dest_change(
        std::vector<PhaseConfig>::iterator source_phases_left_boundary,
        std::vector<PhaseConfig>::iterator source_phases_right_boundary,
        std::vector<PhaseConfig>::iterator dest_phases_left_boundary,
        std::vector<PhaseConfig>::iterator dest_phases_right_boundary)
    {
        source_phases_left_boundary->config.set_next_phase_dest_change(false);
        source_phases_right_boundary->config.set_next_phase_dest_change(true);

        dest_phases_left_boundary->config.set_next_phase_src_change(false);
        dest_phases_right_boundary->config.set_next_phase_src_change(true);
    }

    void StreamGraphCreator::fix_fork_lists(const StreamGraphCollection* stream_graph_collection)
    {
        std::unordered_set<const StreamNode*> processed_fork_streams;
        for (StreamNode* stream_node : stream_graph_collection->get_streams())
        {
            StreamConfig& base_config = stream_node->get_base_config();
            if (!base_config.get_fork_streams().has_value())
            {
                continue;
            }
            if (processed_fork_streams.find(stream_node) != processed_fork_streams.end())
            {
                continue;
            }

            remove_intermeds_from_fork_list(stream_node);

            std::vector<StreamNode*> fork_streams = base_config.get_fork_streams().value();
            if (fork_streams[0] != stream_node)
            {
                continue;
            }

            std::vector<StreamNode*> fork_streams_without_first(fork_streams.begin() + 1, fork_streams.end());

            const bool is_scatter_pack = base_config.get_is_scatter_pack().value_or(false);

            for (std::size_t i = 0; i < fork_streams.size(); ++i)
            {
                StreamNode* fork_stream = fork_streams[i];
                processed_fork_streams.insert(fork_stream);

                // Only the first scatter pack stream should have itself in the fork list.
                if (!is_scatter_pack || i > 0)
                {
                    fork_stream->get_base_config().set_fork_streams(fork_streams_without_first);
                }

                int num_fork_streams = fork_stream->get_base_config().get_fork_streams().value().size();
                fork_stream->get_base_config().set_num_fork_streams(
                    is_scatter_pack ? num_fork_streams - 1 : num_fork_streams);
            }
        }
    }

    void StreamGraphCreator::remove_intermeds_from_fork_list(StreamNode* stream_node)
    {
        // Remove intermeds from fork list. Intermeds were attached only temporarily in order to have a necessary
        // packer->intermed node connection for buffers that share L1. Now we don't need them anymore.
        std::vector<StreamNode*> fork_streams_without_intermeds;
        std::vector<StreamNode*> fork_streams = stream_node->get_base_config().get_fork_streams().value();
        for (std::size_t i = 0; i < fork_streams.size(); ++i)
        {
            if (fork_streams[i]->get_stream_type() == StreamType::Intermed)
            {
                continue;
            }

            fork_streams_without_intermeds.push_back(fork_streams[i]);
        }
        stream_node->get_base_config().set_fork_streams(fork_streams_without_intermeds);
    }

    void StreamGraphCreator::add_epoch_offset_to_stream_phases(const StreamGraphCollection* stream_graph_collection,
                                                               const int epoch_num)
    {
        const PhaseId epoch_phase_offset = ((PhaseId) epoch_num) << c_epoch_id_phase_shift;

        for (StreamNode* stream_node : stream_graph_collection->get_streams())
        {
            for (PhaseConfig& phase_config : stream_node->get_phase_configs())
            {
                phase_config.phase_id += epoch_phase_offset;
            }
        }
    }

    void StreamGraphCreator::assign_pcie_streams_noc_addresses(const ResourceManager* resource_manager)
    {
        for (StreamNode* stream_writing_to_pcie : m_pcie_writing_streams)
        {
            NcriscConfig& write_ncrisc_config = stream_writing_to_pcie->get_base_ncrisc_config();
            StreamNode* stream_reading_from_pcie = write_ncrisc_config.dram_streaming_dest.value();
            NcriscConfig& read_ncrisc_config = stream_reading_from_pcie->get_base_ncrisc_config();

            std::uint64_t local_pcie_buffer_address = resource_manager->get_soc_info()->get_local_pcie_buffer_address(
                stream_reading_from_pcie->get_base_config().get_buf_addr().value(),
                stream_reading_from_pcie->get_physical_location(),
                read_ncrisc_config.dram_streaming_downstream.value());

            std::uint64_t dram_buf_noc_addr = resource_manager->get_soc_info()->get_buffer_noc_address_through_pcie(
                local_pcie_buffer_address,
                stream_reading_from_pcie->get_physical_location().chip);

            write_ncrisc_config.dram_buf_noc_addr = dram_buf_noc_addr;
            stream_writing_to_pcie->get_base_config().set_dram_buf_noc_addr(dram_buf_noc_addr);

            read_ncrisc_config.dram_buf_noc_addr = dram_buf_noc_addr;
            stream_reading_from_pcie->get_base_config().set_dram_buf_noc_addr(dram_buf_noc_addr);
        }
    }
}