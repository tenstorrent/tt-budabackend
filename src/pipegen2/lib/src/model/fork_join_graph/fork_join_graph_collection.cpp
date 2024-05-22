// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "model/fork_join_graph/fork_join_graph_collection.h"

#include <utility>

namespace pipegen2
{

ForkJoinGraphCollection::ForkJoinGraphCollection(std::vector<std::unique_ptr<ForkJoinGraph>>& fork_join_graphs) {}

}  // namespace pipegen2
