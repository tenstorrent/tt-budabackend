// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>
#include <unordered_map>

#include "stream_graph.h"

namespace pipegen2
{
    class StreamGraphCollection
    {
    public:
        StreamGraphCollection() = default;

        // Adds stream graph to the collection. The collection takes ownership of the stream graph.
        void add_stream_graph(std::unique_ptr<StreamGraph>&& stream_graph);

        // Returns all stream graphs in the collection.
        const std::vector<std::unique_ptr<StreamGraph>>& get_stream_graphs() const { return m_stream_graphs; }

        // Returns all streams in all the stream graphs in the collection.
        const std::vector<StreamNode*>& get_streams() const { return m_streams; }

        // Returns all streams that are on the same core as the provided core location.
        std::vector<const StreamNode*> get_streams_on_core(const tt_cxy_pair& core_location) const;

    private:
        // All stream graphs in the collection.
        std::vector<std::unique_ptr<StreamGraph>> m_stream_graphs;

        // All streams in all the stream graphs in the collection.
        std::vector<StreamNode*> m_streams;
    };
}