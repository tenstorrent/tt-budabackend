// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2
{
    void StreamGraphCollection::add_stream_graph(std::unique_ptr<StreamGraph>&& stream_graph)
    {
        m_stream_graphs.push_back(std::move(stream_graph));
        for (const std::unique_ptr<StreamNode>& stream_node: m_stream_graphs.back()->get_streams())
        {
            m_streams.push_back(stream_node.get());
        }
    }

    std::vector<const StreamNode*> StreamGraphCollection::get_streams_on_core(const tt_cxy_pair& core_location) const
    {
        std::vector<const StreamNode*> streams_on_core;
        for (StreamNode* stream: m_streams)
        {
            if (stream->get_physical_location() == core_location)
            {
                streams_on_core.push_back(stream);
            }
        };

        return streams_on_core;
    }

    std::unordered_set<tt_cxy_pair> StreamGraphCollection::get_physical_locations() const
    {
        std::unordered_set<tt_cxy_pair> physical_locations;
        for (const StreamNode* stream_node : m_streams)
        {
            physical_locations.insert(stream_node->get_physical_location());
        }
        
        return physical_locations;
    }
}