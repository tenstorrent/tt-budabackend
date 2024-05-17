// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

namespace pipegen2
{
class DataFlowNode;
class SubgraphLeafGroups;

class SubgraphLeafGroupsFinder
{
public:
    // Finds leaf groups in the data flow graph and returns SubgraphLeafGroups object that contains leaf groups.
    std::unique_ptr<SubgraphLeafGroups> find_leaf_groups(const std::vector<DataFlowNode*>& root_nodes);
};
}  // namespace pipegen2
