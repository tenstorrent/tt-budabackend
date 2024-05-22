// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

#include "device/resource_manager.h"
#include "model/fork_join_graph/fork_join_graph_collection.h"
#include "model/pipe_graph/pipe_graph.h"

namespace pipegen2
{

class ForkJoinGraphCreator
{
public:
    // Creates an ForkJoinGraphColleciton object from a given pipe graph and a stream graph collection.
    std::unique_ptr<ForkJoinGraphCollection> create_fork_join_graphs(
        const PipeGraph* pipe_graph,
        const StreamGraphCollection* stream_graph_collection,
        const ResourceManager* resource_manager);
};

}  // namespace pipegen2