// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <stdexcept>
#include <memory>
#include <vector>
#include <string>

#include "yaml-cpp/yaml.h"

#include "blobgen2.h"
#include "blobgen2_factory.h"
#include "io/blob_yaml_reader.h"
#include "model/stream_graph/stream_graph.h" // In pipegen2.

#include "io/blob_yaml_writer.h" //TODO: Remove include related to temporary test code.
#include <regex> //TODO: Remove include related to temporary test code.

using namespace blobgen2;

int main(int argc, char* argv[])
{
    std::string err_msg_prefix = "<BLOBGEN-ERROR> ";

    try
    {
        //TODO: Parse and check program arguments properly.
        // Check program arguments.
        const unsigned num_args = 12;
        if (argc < 2*num_args)
        {
            std::string error_msg
            {
                std::string("Usage:\n") +
                std::string("  blobgen2\n") +
                std::string("    --blob_out_dir <blobgen2_output_dir>\n") + //blobgen1 and blobgen2 argument
                std::string("    --graph_yaml <0 | 1>\n") + //blobgen1 argument to be removed (if possible)
                std::string("    --graph_input_file <blob_yaml_path>\n") + //blobgen1 and blobgen2 argument
                std::string("    --graph_name <graph_name>\n") + //blobgen1 argument to be removed (get graph name from StreamGraph)
                std::string("    --noc_x_size <int>\n") + //blobgen1 and blobgen2 argument
                std::string("    --noc_y_size <int>\n") + //blobgen1 and blobgen2 argument
                std::string("    --chip <grayskull | wormhole | wormhole_b0>\n") + //blobgen1 and blobgen2 argument
                std::string("    --noc_version <int>\n") + //blobgen1 argument to be removed (get noc version from chip description file)
                std::string("    --tensix_mem_size <l1_size_in_bytes>\n") + //blobgen1 argument to be removed (get size from chip description file)
                std::string("    --output_hex <0 | 1>\n") + //blobgen2 specific argument
                std::string("    --output_yaml <0 | 1>\n") + //blobgen2 specific argument
                std::string("    --blob_yaml_version <1 | 2>") //blobgen2 specific argument: 1 - pipegen1 generated, 2 - pipegen2 generated
            };

            throw std::runtime_error(error_msg + "\n");
        }

        // Print program arguments.
        std::cout << argv[0] << std::endl;
        for (auto i=0 ; i<2*num_args; i+=2)
        {
            std::cout << "  " << argv[i+1] << " " << argv[i+2] << std::endl;
        }
        std::cout << std::endl;

        // Parse StreamGraphs from the input blob.yaml file.
        std::string blob_yaml_path {argv[6]};
        std::unique_ptr<BlobYamlReader> blob_yaml_reader = BlobYamlReader::get_instance(blob_yaml_path);
        std::vector<std::unique_ptr<pipegen2::StreamGraph>> stream_graphs = blob_yaml_reader->read_blob_yaml();

        // Set name of stream graphs.
        const std::string graph_name {argv[8]};
        for (const auto& stream_graph : stream_graphs)
        {
            stream_graph->set_graph_name(graph_name);
        }

        //TODO: Remove temporary test code of writing back the parsed stream graph to out_blob.yaml.
        std::string out_blob_yaml_path = std::regex_replace(blob_yaml_path, std::regex("blob.yaml"), "out_blob.yaml");
        pipegen2::BlobYamlWriter blob_yaml_writer(out_blob_yaml_path);
        blob_yaml_writer.write_blob_yaml(stream_graphs);

        // Create blobgen2 (through a blobgen2_factory).
        std::unique_ptr<Blobgen2Factory> blobgen_factory = Blobgen2Factory::get_instance();
        std::string blob_out_dir {argv[2]};
        std::string chip_arch {argv[14]};
        std::vector<Blobgen2OutputType> output_types;

        std::string output_hex {argv[20]};
        if (std::stoi(output_hex) == 1)
        {
            output_types.push_back(Blobgen2OutputType::Hex);
        }

        std::string output_yaml {argv[22]};
        if (std::stoi(output_yaml) == 1)
        {
            output_types.push_back(Blobgen2OutputType::Yaml);
        }

        std::unique_ptr<Blobgen2> blobgen = blobgen_factory->create(chip_arch, output_types, blob_out_dir);

        // Generate blob.
        blobgen->generate_blob(stream_graphs);

    }
    catch(const std::exception& e)
    {
        std::string err_msg = err_msg_prefix + e.what();
        std::cout << err_msg << std::endl;
        return 1;
    }
    catch (...)
    {
        std::string err_msg = err_msg_prefix + "Unknown runtime error.";
        std::cout << err_msg << std::endl;
        return 1;
    }

    return 0;
}