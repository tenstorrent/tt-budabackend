// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "graph_creator/fork_join_graph/fork_join_graph_stream_info.h"
#include "model/fork_join_graph/fork_join_node.h"
#include "model/pipe_graph/pg_buffer.h"

namespace pipegen2
{

class ForkJoinPath
{
public:
    ForkJoinPath(
        const PGBuffer* start_buffer,
        std::vector<std::unique_ptr<ForkJoinNode>>& fork_join_nodes,
        const ResourceManager* resource_manager);

    // Adds the required information from the stream graph into the fork join nodes of the path.
    void populate_with_stream_info(const ForkJoinGraphStreamInfo& fork_join_stream_graph_info);

    const std::vector<std::unique_ptr<ForkJoinNode>>& get_fork_join_nodes() const { return m_fork_join_nodes; }

    const ForkJoinNode* get_starting_fork_join_node() const { return m_fork_join_nodes[0].get(); }

    const std::vector<const ForkJoinNode*> get_fork_join_nodes_except_starting_node() const;

    // Gets the number of tiles sent in one firmware iteration in the starting node of the fork join path.
    const unsigned int get_num_tiles_in_iteration_from_first_node() const;

private:
    // List of nodes in the ForkJoinPath.
    std::vector<std::unique_ptr<ForkJoinNode>> m_fork_join_nodes;
};

}  // namespace pipegen2