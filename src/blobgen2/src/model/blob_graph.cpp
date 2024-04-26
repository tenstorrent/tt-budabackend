// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/blob_graph.h"

#include "model/blob_graph_node.h"

namespace blobgen2
{

    BlobGraph::BlobGraph(const std::string& name)
        : m_name(name)
    {
    }

    BlobGraph::~BlobGraph() = default;

    const std::string& BlobGraph::get_name() const
    {
        return m_name;
    }

    void BlobGraph::add_node(std::unique_ptr<BlobGraphNode> node)
    {
        m_nodes.push_back(std::move(node));
    }

    const std::vector<std::unique_ptr<BlobGraphNode>>& BlobGraph::get_nodes() const
    {
        return m_nodes;
    }

}