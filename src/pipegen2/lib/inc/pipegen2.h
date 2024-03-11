// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "model/stream_graph/stream_graph_collection.h"
#include "device/l1_memory_allocation.h"
namespace pipegen2
{
    class PipeGraph;
    class RationalGraph;
    class ResourceManager;
    class StreamGraph;

    class Pipegen2
    {
    public:
        // Constructs Pipegen2 based on SOC descriptors yaml.
        // In case when device has no harvested chips, SOC descriptors yaml should contain
        // the SOC descriptor definition that each of the device chips will use.
        // Otherwise, when device has harvested chips, SOC descriptors yaml should contain
        // just paths to multiple SOC descriptor yamls, one for each of device chips.
        // TODO: This is a bit convoluted, that's how backend currently calls pipegen, we should change it in the
        // future.
        explicit Pipegen2(const std::string& soc_descriptors_yaml_path);

        // Destructor, necessary for forward declarations of classes in smart pointer members.
        ~Pipegen2();

        // Creates stream graphs for all pipes in the pipegen yaml. Stream phases are configured for the given epoch.
        // Pipegen releases ownership of the created stream graphs.
        std::unique_ptr<StreamGraphCollection> create_stream_graphs(const std::string& pipegen_yaml_path,
                                                                    int epoch_num);

        // Writes stream and firmware configurations into blob yaml for all stream graphs in the collection.
        void output_blob_yaml(const StreamGraphCollection* stream_graph_collection,
                              const std::string& blob_yaml_path,
                              int perf_dump_info);

        // Outputs input buffer usage analysis for all inputs that have under 100% usage during FW iteration.
        static void output_input_buffer_usage_analysis(const int epoch_num,
                                                       const StreamGraphCollection* stream_graph_collection,
                                                       const std::string& input_buffer_usage_analysis_path);

        // Outputs analysis of L1 memory allocations by stream buffers.
        void output_memory_allocations(const std::string& log_path, const int temporal_epoch);

        // Outputs all L1 memory allocations per worker core
        std::unordered_map<tt_cxy_pair, std::vector<const L1MemoryAllocation*>> get_all_worker_l1_allocations() const;

    private:
        // Creates pipe graph from the input net2pipe pipegen yaml.
        void create_pipe_graph(const std::string& pipegen_yaml_path);

        // Creates resource manager for the device.
        void create_resource_manager();

        // Creates rational graphs from the connected pipe graph node groups.
        void create_rational_graphs();

        // Creates stream graph from the rational graphs.
        std::unique_ptr<StreamGraphCollection> create_stream_graphs(const int epoch_num);

        // Device resources manager.
        std::unique_ptr<ResourceManager> m_resource_manager;

        // Created pipe graph.
        std::unique_ptr<PipeGraph> m_pipe_graph;

        // Created rational graphs for each pipe graph subgraph.
        std::vector<std::unique_ptr<RationalGraph>> m_rational_graphs;

        // Created stream graphs for each rational graph.
        std::unique_ptr<StreamGraphCollection> m_stream_graphs;

        // Path to SOC descriptors yaml.
        std::string m_soc_descriptors_yaml_path;
    };
}