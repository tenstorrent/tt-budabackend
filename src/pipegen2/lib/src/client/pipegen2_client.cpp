// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "client/pipegen2_client.h"

#include <filesystem>

namespace pipegen2
{

void Pipegen2Client::run_pipegen2()
{
    if (is_pipegen_run())
    {
        return;
    }

    m_stream_graphs = m_pipegen.create_stream_graphs(m_pipegen_yaml_path, m_epoch_num);

    m_pipegen.output_blob_yaml(m_stream_graphs.get(), m_blob_yaml_path, m_perf_dump_info);

    do_post_processing();
}

const std::unordered_map<tt_cxy_pair, std::vector<const pipegen2::L1Buffer*>>
Pipegen2Client::get_all_worker_l1_data_buffers()
{
    run_pipegen2();

    return m_pipegen.get_all_worker_l1_data_buffers();
}

void Pipegen2Client::do_post_processing()
{
    output_memory_allocations();

    do_input_buffer_usage_analysis();
}

void Pipegen2Client::output_memory_allocations()
{
    const char* log_memory_allocations_dir = std::getenv("TT_BACKEND_MEMORY_ALLOCATIONS_DIR");
    if (log_memory_allocations_dir)
    {
        m_pipegen.output_memory_allocations(log_memory_allocations_dir, m_epoch_num);
    }
}

void Pipegen2Client::do_input_buffer_usage_analysis()
{
    const char* input_buffer_usage_analysis_dir = std::getenv("PIPEGEN2_INPUT_BUFFER_USAGE_ANALYSIS_CSV_DIR");
    if (input_buffer_usage_analysis_dir)
    {
        if (!std::filesystem::exists(input_buffer_usage_analysis_dir))
        {
            std::filesystem::create_directories(input_buffer_usage_analysis_dir);
        }
        std::stringstream input_buffer_usage_analysis_csv_file;
        input_buffer_usage_analysis_csv_file << std::string(input_buffer_usage_analysis_dir)
                                             << "/input_buffer_usage_epoch_" << m_epoch_num << ".csv";
        Pipegen2::output_input_buffer_usage_analysis(
            m_epoch_num, m_stream_graphs.get(), input_buffer_usage_analysis_csv_file.str());
    }
}

}  // namespace pipegen2