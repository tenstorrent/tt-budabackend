// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>
#include <string>

namespace blobgen2
{

    class BlobGraphNode;

    class BlobGraph
    {
    public:
        // Constructor.
        BlobGraph(const std::string& name);

        // Destructor.
        ~BlobGraph();

        // Returns BlobGraph name.
        const std::string& get_name() const;

        // Add a node to the vector of BlobGraphNodes.
        void add_node(std::unique_ptr<BlobGraphNode> node);

        // Return vector of BlobGraphNodes.
        const std::vector<std::unique_ptr<BlobGraphNode>>& get_nodes() const;

    private:
        // BlobGraph name.
        std::string m_name;

        // BlobGraphNodes (a node per Tensix core).
        std::vector<std::unique_ptr<BlobGraphNode>> m_nodes;

    };

}