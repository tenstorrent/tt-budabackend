//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "model/fork_join_graph/fork_join_graph.h"
#include "model/stream_graph/stream_graph_collection.h"

namespace pipegen2
{

class ForkJoinGraphCollection
{
public:
    ForkJoinGraphCollection(std::vector<std::unique_ptr<ForkJoinGraph>>& fork_join_graphs);

    // Adds the required information from the stream graph into the fork join nodes of the path.
    void populate_with_stream_info(const StreamGraphCollection* stream_graph_collection);

private:
    // Vector of all op fork-join cycles in the epoch.
    std::vector<std::unique_ptr<ForkJoinGraph>> m_fork_join_graphs;
};

}  // namespace pipegen2