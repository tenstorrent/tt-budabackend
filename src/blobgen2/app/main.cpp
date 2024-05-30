// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include "blobgen2.h"
#include "io/blob_yaml_reader.h"
#include "io/blob_yaml_writer.h"

using namespace blobgen2;

namespace
{
void print_error(std::string error_message)
{
    const std::string error_message_prefix = "<BLOBGEN-ERROR> ";

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
        if (argc < 6)
        {
            print_error("Usage: blobgen2 <input blob yaml> <epoch number> <output_dir> <soc_descriptor> <debug>");
            return 1;
        }

        std::string blob_yaml_path = argv[1];
        int epoch_num = std::stoul(argv[2], 0, 0);
        std::string output_dir = argv[3];
        std::string soc_descriptors_yaml_path = argv[4];
        bool debug = (std::stoul(argv[5], 0, 0)) == 1;

        std::pair<std::unique_ptr<StreamGraphCollection>, std::map<tt_cxy_pair, dram_perf_info_t>> stream_graphs =
            BlobYamlReader::read_blob_yaml(blob_yaml_path);

        Blobgen2::create_and_output_blobs(
            std::move(stream_graphs.first),
            soc_descriptors_yaml_path,
            stream_graphs.second,
            epoch_num,
            output_dir,
            debug);
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
