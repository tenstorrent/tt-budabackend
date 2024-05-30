// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "pipegen2.h"

namespace pipegen2
{

class Pipegen2Client
{
public:
    Pipegen2Client(
        const std::string& soc_descriptors_yaml_path,
        const std::string& pipegen_yaml_path,
        const std::string& blob_yaml_path,
        const int epoch_num,
        const int perf_dump_info) :
        m_pipegen(soc_descriptors_yaml_path),
        m_pipegen_yaml_path(pipegen_yaml_path),
        m_soc_descriptors_yaml_path(soc_descriptors_yaml_path),
        m_blob_yaml_path(blob_yaml_path),
        m_epoch_num(epoch_num),
        m_perf_dump_info(perf_dump_info)
    {
    }

    // Runs pipegen and creates stream graphs.
    std::unique_ptr<StreamGraphCollection> run_pipegen2();

    // Public methods below expose internal structures created by pipegen2. They are implemented in order
    // to keep functionalities in other parts of the system which expect these structures (e.g runtime).
    // This should be the only place from which we return pipegen2 structures. These function should serve as
    // API between pipegen2 and other parts of the system, API which is used to expose pipegen2 structures if needed.

    // Returns L1 memory allocations for all worker cores.
    const std::unordered_map<tt_cxy_pair, std::vector<L1BufferAllocationInfo>> get_all_worker_l1_data_buffers();

private:
    // Runs conditional post processing determined by environment variables.
    void do_post_processing(const StreamGraphCollection* stream_graphs);

    // Outputs memory allocations, if TT_BACKEND_MEMORY_ALLOCATIONS_DIR is set.
    void output_memory_allocations();

    // Outputs input buffer usage analysis, if PIPEGEN2_INPUT_BUFFER_USAGE_ANALYSIS_CSV_DIR is set.
    void do_input_buffer_usage_analysis(const StreamGraphCollection* stream_graphs);

    // Instance of pipegen2 class that does the actual work.
    Pipegen2 m_pipegen;

    // Path to pipegen yaml.
    std::string m_pipegen_yaml_path;

    // Path to SOC descriptors yaml.
    std::string m_soc_descriptors_yaml_path;

    // Path to which pipegen will output blob yaml.
    std::string m_blob_yaml_path;

    // Temporal epoch number for which we run pipegen.
    int m_epoch_num;

    // Perf dump info for PerfInfoManager inside pipegen.
    int m_perf_dump_info;

    // A flag guarding from multiple runs of pipegen2 client.
    bool m_pipegen_was_run = false;
};

}  // namespace pipegen2