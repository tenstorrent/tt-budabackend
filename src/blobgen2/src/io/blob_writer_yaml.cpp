// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "io/blob_writer_yaml.h"

//TODO: Remove the iostream include when write_impl gets really implemented.
#include <iostream>

#include "model/blob_graph.h"
#include "model/blob_graph_node.h"

namespace blobgen2
{

    BlobWriterYaml::BlobWriterYaml(const std::string& output_directory_path)
        : m_output_directory_path(output_directory_path)
    {
    }

    BlobWriterYaml::~BlobWriterYaml() = default;

    void BlobWriterYaml::write_impl(const BlobGraph *const blob_graph) const
    {
        //TODO: Implement real yaml writer.
        std::cout << "BlobWriterYaml: write BlobGraph into .yaml files under \""
                  << m_output_directory_path
                  << "\" directory."
                  << std::endl;

        std::cout << "  graph: " << blob_graph->get_name() << std::endl;

        for (auto& node : blob_graph->get_nodes())
        {
            std::cout << "      node: " << node->get_node_id() << std::endl;
        }
    }

}