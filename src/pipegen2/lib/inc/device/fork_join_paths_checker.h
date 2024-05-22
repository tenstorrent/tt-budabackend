// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/fork_join_graph/fork_join_graph_collection.h"

namespace pipegen2
{

// Utility class which checks for fork-join hangs, given a fork-join graph collection.
class ForkJoinPathsChecker
{
public:
    // Checks for a fork-join hang, given a collection of fork-join graphs.
    void check_fork_join_hangs(const ForkJoinGraphCollection* fork_join_graph_collection) const;

private:
    // Checks for a fork-join hang, given a single fork join graph.
    void check_fork_join_hangs(const ForkJoinGraph* fork_join_graph) const;

    // Check for a fork-join hang between two paths in a fork-join graph.
    void check_fork_join_hang(const ForkJoinPath* first_path, const ForkJoinPath* second_path) const;
};

}  // namespace pipegen2