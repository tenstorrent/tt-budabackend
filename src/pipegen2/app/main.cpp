// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "client/pipegen2_client.h"

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
}  // namespace

int main(int argc, char* argv[])
{
    tt::assert::register_segfault_handler();
    try
    {
        if (argc < 5)
        {
            print_error(
                "Usage: pipegen2 <input pipegen yaml> <soc desc yaml> <output blob yaml> <epoch number> "
                "<perf dump info>");
            return 1;
        }

        std::string pipegen_yaml_path = argv[1];
        std::string soc_descriptors_yaml_path = argv[2];
        std::string blob_yaml_path = argv[3];
        int epoch_num = std::stoul(argv[4], 0, 0);
        int perf_dump_info = std::stoul(argv[5], 0, 0);

        Pipegen2Client pipegen2_client(
            soc_descriptors_yaml_path, pipegen_yaml_path, blob_yaml_path, epoch_num, perf_dump_info);
        std::unique_ptr<StreamGraphCollection> _ = pipegen2_client.run_pipegen2();
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
