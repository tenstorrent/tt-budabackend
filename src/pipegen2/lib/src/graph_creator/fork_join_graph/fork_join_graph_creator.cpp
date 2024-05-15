// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "graph_creator/fork_join_graph/fork_join_graph_creator.h"

#include <memory>

namespace pipegen2
{

std::unique_ptr<ForkJoinGraphCollection> ForkJoinGraphCreator::create_fork_join_graphs(
    const PipeGraph* pipe_graph, 
    const StreamGraphCollection* stream_graph_collection,
    const ResourceManager* resource_manager)
{
    // TODO: This is a dummy empty vector of fork-join graph, create a real one properly later.
    std::vector<std::unique_ptr<ForkJoinGraph>> fork_join_graphs_dummy;
    std::unique_ptr<ForkJoinGraphCollection> fork_join_graph_collection = 
        std::make_unique<ForkJoinGraphCollection>(fork_join_graphs_dummy);
    return fork_join_graph_collection;
}

} // namespace pipegen2
