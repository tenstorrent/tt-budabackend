// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "blob_graph_creator/blob_graph_creator.h"

//TODO: Remove the iostream include when create_impl gets really implemented.
#include <iostream>
#include <string>

#include "model/stream_graph/stream_graph.h" // In pipegen2.
#include "model/blob_graph.h"
#include "model/blob_graph_node.h"

namespace blobgen2
{

    BlobGraphCreator::BlobGraphCreator() = default;

    BlobGraphCreator::~BlobGraphCreator() = default;

    std::unique_ptr<BlobGraph> BlobGraphCreator::create(
        const std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs)
    {
        return this->create_impl(stream_graphs);
    }

    std::unique_ptr<BlobGraph> BlobGraphCreator::create_impl(
        const std::vector<std::unique_ptr<pipegen2::StreamGraph>>& stream_graphs)
    {
        //TODO: Implement real BlobGraph creation out of a vector of StreamGraphs.
        
        //TODO: Remove debug prints.
        std::cout << "BlobGraphCreator: create BlobGraph." << std::endl;

        //TODO: Get graph name from StreamGraphs.
        std::string blob_graph_name {"blobgen2_test_graph"};
        std::unique_ptr<BlobGraph> blob_graph = std::make_unique<BlobGraph>(blob_graph_name);

        // Example how to add nodes.
        std::unique_ptr<BlobGraphNode> blob_graph_node_1 = std::make_unique<BlobGraphNode>("1");
        std::unique_ptr<BlobGraphNode> blob_graph_node_2 = std::make_unique<BlobGraphNode>("2");
        std::unique_ptr<BlobGraphNode> blob_graph_node_3 = std::make_unique<BlobGraphNode>("3");

        blob_graph->add_node(std::move(blob_graph_node_1));
        blob_graph->add_node(std::move(blob_graph_node_2));
        blob_graph->add_node(std::move(blob_graph_node_3));

        return std::move(blob_graph);
    }

}