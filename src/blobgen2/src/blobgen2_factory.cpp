// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "blobgen2_factory.h"

#include <stdexcept>
#include <string>

#include "blob_graph_creator/blob_graph_creator.h"
#include "io/blob_writer.h"
#include "io/blob_writer_hex.h"
#include "io/blob_writer_yaml.h"
#include "blobgen2.h"

namespace blobgen2
{

    Blobgen2Factory::Blobgen2Factory() = default;

    Blobgen2Factory::~Blobgen2Factory() = default;

    std::unique_ptr<Blobgen2Factory> Blobgen2Factory::get_instance()
    {
        return std::make_unique<Blobgen2Factory>();
    }

    std::unique_ptr<Blobgen2> Blobgen2Factory::create(
        const std::string& arch,
        const std::vector<Blobgen2OutputType>& out_types,
        const std::string& blob_out_dir)
    {
        std::unique_ptr<BlobGraphCreator> blob_graph_creator = std::make_unique<BlobGraphCreator>();

        std::vector<std::unique_ptr<BlobWriter>> blob_writers;
        for (const Blobgen2OutputType& type : out_types)
        {
            std::unique_ptr<BlobWriter> blob_writer;

            switch (type)
            {
            case Blobgen2OutputType::Hex:
                blob_writer = std::make_unique<BlobWriterHex>(blob_out_dir);
                break;
            case Blobgen2OutputType::Yaml:
                blob_writer = std::make_unique<BlobWriterYaml>(blob_out_dir);
                break;
            default:
                throw std::runtime_error("Unsupported output type: " + type);
            }

            blob_writers.push_back(std::move(blob_writer));
        }

        return std::make_unique<Blobgen2>(std::move(blob_graph_creator), std::move(blob_writers));
    }

}