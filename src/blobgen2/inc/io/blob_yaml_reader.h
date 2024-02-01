// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

#include "model/stream_graph/stream_graph.h"

namespace pipegen2
{
    class BaseStreamNode;
}

namespace blobgen2
{

    class BlobYamlReader
    {
    public:
        // Constructor.
        BlobYamlReader(const std::string& blob_yaml_path);

        // Destructor.
        ~BlobYamlReader();

        // Returns an instance of the BlobYamlReader.
        static std::unique_ptr<BlobYamlReader> get_instance(
            const std::string& blob_yaml_path);

        // Reads a vector of StreamGraphs from the blob yaml file.
        std::vector<std::unique_ptr<pipegen2::StreamGraph>> read_blob_yaml();

    private:
        // Input blob yaml file path.
        std::string m_blob_yaml_path;

        // Map of <stream_id, base_stream_node_ptr> pairs
        // for quickly getting previously created the base stream node objects.
        std::unordered_map<std::string, pipegen2::BaseStreamNode*> m_streams_cache;

    };
}