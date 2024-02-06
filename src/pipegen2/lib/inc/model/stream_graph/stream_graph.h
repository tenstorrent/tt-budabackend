// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "stream_node.h"

namespace pipegen2
{
    class StreamGraph
    {
    public:
        void add_stream(std::unique_ptr<StreamNode> stream) { m_streams.push_back(std::move(stream)); }

        const std::vector<std::unique_ptr<StreamNode>>& get_streams() const { return m_streams; }

        // Returns a vector of connected components where each component is a new StreamGraph. After calling this
        // function, this StreamGraph will be empty.
        std::vector<std::unique_ptr<StreamGraph>> split_into_connected_components();

#ifdef TT_DEBUG
        // Forms JSON string used for debug visualizer to visualize stream graph.
        std::string to_json() const;
#endif

    private:
        // Finds streams connected to the stream on a given index.
        void find_connected_streams(const std::size_t stream_node_index,
                                    const std::unordered_map<const StreamNode*, std::size_t>& stream_node_to_index,
                                    std::unordered_set<std::size_t>& visited_indices,
                                    std::vector<std::size_t>& component_indices);

        std::vector<std::unique_ptr<StreamNode>> m_streams;
    };
}