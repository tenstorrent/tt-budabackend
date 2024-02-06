// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "pipegen2.h"

#include <stdexcept>

#include "utils/scoped_timer.hpp"

#include "data_flow_calculator/data_flow_calculator.h"
#include "device/perf_info_manager.h"
#include "device/resource_manager.h"
#include "device/soc_info.h"
#include "graph_creator/pipe_graph/pipe_graph_creator.h"
#include "graph_creator/rational_graph/rational_graph_creator.h"
#include "graph_creator/stream_graph/stream_graph_creator.h"
#include "io/blob_yaml_writer.h"
#include "model/pipe_graph/pipe_graph.h"
#include "model/rational_graph/rational_graph.h"
#include "model/stream_graph/stream_graph.h"

namespace pipegen2
{
    Pipegen2::Pipegen2(const std::string& soc_descriptors_yaml_path) :
        m_soc_descriptors_yaml_path(soc_descriptors_yaml_path)
    {
    }

    Pipegen2::~Pipegen2() = default;

    std::unique_ptr<StreamGraphCollection> Pipegen2::create_stream_graphs(const std::string& pipegen_yaml_path,
                                                                          int epoch_num)
    {
        PROFILE_SCOPE_MS();

        create_pipe_graph(pipegen_yaml_path);
        create_resource_manager();
        create_rational_graphs();
        return create_stream_graphs(epoch_num);
    }

    void Pipegen2::create_pipe_graph(const std::string& pipegen_yaml_path)
    {
        PROFILE_SCOPE_MS();

        PipeGraphCreator pipe_graph_creator;
        m_pipe_graph = pipe_graph_creator.create_pipe_graph(pipegen_yaml_path);
    }

    void Pipegen2::create_resource_manager()
    {
        PROFILE_SCOPE_MS();

        std::unique_ptr<SoCInfo> soc_info;
        try
        {
            soc_info = SoCInfo::parse_from_yaml(m_soc_descriptors_yaml_path, m_pipe_graph->get_all_chip_ids());
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error("Invalid SoC descriptors yaml: " + std::string(e.what()));
        }

        m_resource_manager = std::make_unique<ResourceManager>(std::move(soc_info));
    }

    void Pipegen2::create_rational_graphs()
    {
        PROFILE_SCOPE_MS();

        RationalGraphCreator rational_graph_creator;
        m_rational_graphs = rational_graph_creator.create_rational_graphs(*m_pipe_graph, m_resource_manager.get());
    }

    std::unique_ptr<StreamGraphCollection> Pipegen2::create_stream_graphs(const int epoch_num)
    {
        PROFILE_SCOPE_MS();

        StreamGraphCreator stream_graph_creator;
        return stream_graph_creator.create_stream_graphs(m_rational_graphs,
                                                         m_resource_manager.get(),
                                                         epoch_num);
    }

    void Pipegen2::output_blob_yaml(const StreamGraphCollection* stream_graph_collection,
                                    const std::string& blob_yaml_path,
                                    int perf_dump_info)
    {
        PROFILE_SCOPE_MS();

        PerfInfoManager perf_info_manager(perf_dump_info, m_resource_manager->get_soc_info());
        perf_info_manager.calculate_dram_perf_info();

        BlobYamlWriter blob_yaml_writer(blob_yaml_path);
        blob_yaml_writer.write_blob_yaml(stream_graph_collection, perf_info_manager);
    }
}