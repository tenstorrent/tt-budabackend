// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>


#include "pipegen2.h"

using namespace pipegen2;

namespace
{
    void print_error(std::string error_message)
    {
        const std::string error_message_prefix = "<PIPEGEN-ERROR> ";

        std::string full_error_message = error_message_prefix + error_message;
        std::cout << full_error_message << std::endl;
        std::cerr << full_error_message << std::endl;
    }
}

int main(int argc, char* argv[])
{
    tt::assert::register_segfault_handler();
    try
    {
        if (argc < 5)
        {
            print_error("Usage: pipegen2 <input pipegen yaml> <soc desc yaml> <output blob yaml> <epoch number> "
                        "<perf dump info>");
            return 1;
        }

        std::string pipegen_yaml_path = argv[1];
        std::string soc_descriptors_yaml_path = argv[2];
        std::string blob_yaml_path = argv[3];
        int epoch_num = std::stoul(argv[4], 0, 0);
        int perf_dump_info = std::stoul(argv[5], 0, 0);

        Pipegen2 pipegen(soc_descriptors_yaml_path);

        std::unique_ptr<StreamGraphCollection> stream_graphs =
            pipegen.create_stream_graphs(pipegen_yaml_path, epoch_num);
        pipegen.output_blob_yaml(stream_graphs.get(), blob_yaml_path, perf_dump_info);

        const char* input_buffer_usage_analysis_dir = std::getenv("PIPEGEN2_INPUT_BUFFER_USAGE_ANALYSIS_CSV_DIR");
        if (input_buffer_usage_analysis_dir)
        {
            if (!std::filesystem::exists(input_buffer_usage_analysis_dir))
            {
                std::filesystem::create_directories(input_buffer_usage_analysis_dir);
            }
            std::stringstream input_buffer_usage_analysis_csv_file;
            input_buffer_usage_analysis_csv_file << std::string(input_buffer_usage_analysis_dir)
                                                 << "/input_buffer_usage_epoch_"
                                                 << epoch_num
                                                 << ".csv";
            Pipegen2::output_input_buffer_usage_analysis(
                epoch_num, stream_graphs.get(), input_buffer_usage_analysis_csv_file.str());
        }
        const char* log_memory_allocations_dir = std::getenv("TT_BACKEND_MEMORY_ALLOCATIONS_DIR");
        if (log_memory_allocations_dir)
        {
            pipegen.output_memory_allocations(log_memory_allocations_dir, epoch_num);
        }
    }
    catch (const std::exception& e)
    {
        print_error(e.what());
        return 1;
    }
    catch (...)
    {
        print_error("Unknown runtime error");
        return 1;
    }

    return 0;
}
