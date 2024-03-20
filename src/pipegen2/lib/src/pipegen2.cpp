// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "pipegen2.h"

#include <memory>
#include <stdexcept>

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
#include "model/stream_graph/stream_node.h"
#include "pipegen2_exceptions.h"

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
        create_pipe_graph(pipegen_yaml_path);
        create_resource_manager();
        create_rational_graphs();
        return create_stream_graphs(epoch_num);
    }

    void Pipegen2::create_pipe_graph(const std::string& pipegen_yaml_path)
    {
        PipeGraphCreator pipe_graph_creator;
        m_pipe_graph = pipe_graph_creator.create_pipe_graph(pipegen_yaml_path);
    }

    void Pipegen2::create_resource_manager()
    {
        std::unique_ptr<SoCInfo> soc_info;
        try
        {
            soc_info = SoCInfo::parse_from_yaml(m_soc_descriptors_yaml_path, m_pipe_graph->get_all_chip_ids());
        }
        catch (const std::exception& e)
        {
            throw InvalidSoCInfoYamlException("Invalid SoC descriptors yaml: " + std::string(e.what()));
        }

        m_resource_manager = std::make_unique<ResourceManager>(std::move(soc_info));
    }

    void Pipegen2::create_rational_graphs()
    {
        RationalGraphCreator rational_graph_creator;
        m_rational_graphs = rational_graph_creator.create_rational_graphs(*m_pipe_graph, m_resource_manager.get());
    }

    std::unique_ptr<StreamGraphCollection> Pipegen2::create_stream_graphs(const int epoch_num)
    {
        StreamGraphCreator stream_graph_creator;
        return stream_graph_creator.create_stream_graphs(m_rational_graphs,
                                                         m_resource_manager.get(),
                                                         epoch_num);
    }

    void Pipegen2::output_blob_yaml(const StreamGraphCollection* stream_graph_collection,
                                    const std::string& blob_yaml_path,
                                    int perf_dump_info)
    {
        PerfInfoManager perf_info_manager(perf_dump_info, m_resource_manager->get_soc_info());
        perf_info_manager.calculate_dram_perf_info();

        BlobYamlWriter blob_yaml_writer(blob_yaml_path);
        blob_yaml_writer.write_blob_yaml(stream_graph_collection, perf_info_manager);
    }

    void Pipegen2::output_input_buffer_usage_analysis(const int epoch_num,
                                                      const StreamGraphCollection* stream_graph_collection,
                                                      const std::string& input_buffer_usage_analysis_csv_path)
    {
        std::ofstream input_buffer_usage_analysis_file(input_buffer_usage_analysis_csv_path);
        if (!input_buffer_usage_analysis_file.is_open())
        {
            throw std::runtime_error("Failed to open input buffer usage analysis file");
        }

        input_buffer_usage_analysis_file << "-------------- epoch " << epoch_num << " --------------" << std::endl;
        input_buffer_usage_analysis_file << "op_name,operand_id,buffer_size,buffer_usage_in_fw_iteration,usage_pct"
                                         << std::endl;

        std::set<std::pair<std::string, unsigned int>> op_name_operand_id_pairs;

        for (const std::unique_ptr<StreamGraph>& stream_graph : stream_graph_collection->get_stream_graphs())
        {
            for (const std::unique_ptr<StreamNode>& stream_node : stream_graph->get_streams())
            {
                if (stream_node->get_stream_type() != StreamType::Unpacker)
                {
                    continue;
                }

                unsigned int operand_id = stream_node->get_operand_id();
                if (op_name_operand_id_pairs.find({stream_node->get_op_name(), operand_id}) !=
                    op_name_operand_id_pairs.end())
                {
                    continue;
                }

                unsigned int buffer_usage_in_fw_iteration =
                    stream_node->get_num_tiles_in_iteration() * stream_node->get_base_config().get_msg_size().value();

                const unsigned int buffer_size = stream_node->get_base_config().get_buffer_size().value();

                if (buffer_usage_in_fw_iteration >= buffer_size)
                {
                    continue;
                }

                // Usage is below 100% of buffer size.
                const double usage = static_cast<double>(buffer_usage_in_fw_iteration) / buffer_size * 100;

                input_buffer_usage_analysis_file << stream_node->get_op_name() << ","
                                                 << stream_node->get_operand_id() << ","
                                                 << buffer_size << ","
                                                 << buffer_usage_in_fw_iteration << ","
                                                 << usage << "%"
                                                 << std::endl;
                op_name_operand_id_pairs.insert({stream_node->get_op_name(), operand_id});
            }
        }
        input_buffer_usage_analysis_file << "-------------------------------------" << std::endl << std::endl;
    }
}