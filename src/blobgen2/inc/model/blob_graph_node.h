// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace blobgen2
{

    class BlobGraphNode
    {
    public:
        // Constructor.
        BlobGraphNode(const std::string& node_id);

        // Desctructor.
        ~BlobGraphNode();

        // Return the node ID.
        const std::string& get_node_id() const;

    private:
        // The node ID.
        std::string m_node_id;

    };

}