// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "client/pipegen2_client.h"

#include <filesystem>

namespace pipegen2
{

std::unique_ptr<StreamGraphCollection> Pipegen2Client::run_pipegen2()
{
    log_assert(!m_pipegen_was_run, "Pipegen2 was already run");

    std::unique_ptr<StreamGraphCollection> stream_graphs =
        m_pipegen.create_stream_graphs(m_pipegen_yaml_path, m_epoch_num);

    m_pipegen.output_blob_yaml(stream_graphs.get(), m_blob_yaml_path, m_perf_dump_info);

    do_post_processing(stream_graphs.get());

    m_pipegen_was_run = true;

    return std::move(stream_graphs);
}

const std::unordered_map<tt_cxy_pair, std::vector<L1BufferAllocationInfo>>
Pipegen2Client::get_all_worker_l1_data_buffers()
{
    log_assert(m_pipegen_was_run, "Pipegen2 was not run before calling get_all_worker_l1_data_buffers");

    return m_pipegen.get_all_worker_l1_data_buffers_info();
}

void Pipegen2Client::do_post_processing(const StreamGraphCollection* stream_graphs)
{
    output_memory_allocations();

    do_input_buffer_usage_analysis(stream_graphs);
}

void Pipegen2Client::output_memory_allocations()
{
    const char* log_memory_allocations_dir = std::getenv("TT_BACKEND_MEMORY_ALLOCATIONS_DIR");
    if (log_memory_allocations_dir)
    {
        m_pipegen.output_memory_allocations(log_memory_allocations_dir, m_epoch_num);
    }
}

void Pipegen2Client::do_input_buffer_usage_analysis(const StreamGraphCollection* stream_graphs)
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
            m_epoch_num, stream_graphs, input_buffer_usage_analysis_csv_file.str());
    }
}

}  // namespace pipegen2