// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "graph_creator/fork_join_graph/fork_join_graph_stream_info.h"
#include "model/fork_join_graph/fork_join_path.h"

namespace pipegen2
{

class ForkJoinGraph
{
public:
    ForkJoinGraph(std::vector<std::unique_ptr<ForkJoinPath>>& fork_join_paths);

    // Adds the required information from the stream graph into the fork join paths in the graph.
    void populate_with_stream_info(const ForkJoinGraphStreamInfo& fork_join_stream_graph_info);

private:
    // List of paths that this graph consists of.
    std::vector<std::unique_ptr<ForkJoinPath>> m_fork_join_paths;
};

} // namespace pipegen2