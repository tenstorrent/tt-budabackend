// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/blob_yaml_reader.h"

//TODO: Remove the iostream include when read_blob_yaml gets really implemented.
#include <iostream>

#include "yaml-cpp/yaml.h"

#include "io/blob_yaml_reader_internal.h"

namespace blobgen2
{

    BlobYamlReader::BlobYamlReader(const std::string& blob_yaml_path)
        : m_blob_yaml_path(blob_yaml_path)
    {
    }

    BlobYamlReader::~BlobYamlReader() = default;

    std::unique_ptr<BlobYamlReader> BlobYamlReader::get_instance(
        const std::string& blob_yaml_path)
    {
        return std::make_unique<BlobYamlReader>(blob_yaml_path);
    }

    std::vector<std::unique_ptr<pipegen2::StreamGraph>> BlobYamlReader::read_blob_yaml()
    {
        // TODO: Remove debug print.
        std::cout << "BlobYamlReader: parse '" << m_blob_yaml_path << "' file." << std::endl;

        const YAML::Node& blob_yaml = YAML::LoadFile(m_blob_yaml_path);

        std::unique_ptr<pipegen2::StreamGraph> stream_graph_uptr = std::make_unique<pipegen2::StreamGraph>();
        pipegen2::StreamGraph *const stream_graph = stream_graph_uptr.get();

        blobgen2::internal::read_phases(blob_yaml, m_streams_cache, stream_graph);
        blobgen2::internal::read_dram_blob(blob_yaml, m_streams_cache, stream_graph);
        blobgen2::internal::read_overlay_blob_extra_size(blob_yaml, stream_graph);

        std::vector<std::unique_ptr<pipegen2::StreamGraph>> stream_graphs {};
        stream_graphs.push_back(std::move(stream_graph_uptr));

        return std::move(stream_graphs);
    }

}