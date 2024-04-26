// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/blob_graph_node.h"

namespace blobgen2
{

    BlobGraphNode::BlobGraphNode(const std::string& node_id)
        : m_node_id(node_id)
    {
    }

    BlobGraphNode::~BlobGraphNode() = default;

    const std::string& BlobGraphNode::get_node_id() const
    {
        return m_node_id;
    }

}