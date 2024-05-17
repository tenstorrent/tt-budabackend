// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

namespace pipegen2
{

class DataFlowNode;
class LeafGroup;
class SubgraphLeafGroups;

class TransfersCalculator
{
public:
    // Calculates phases for all data flow nodes. Phases are calculated by visiting leaf nodes, in the order
    // provided by the subgraph leaf groups, and going backwards toward the root nodes.
    void calculate_phases(const SubgraphLeafGroups* subgraph_leaf_groups);

private:
    // Copies phases calculated for the first node in the leaf group to other nodes in the leaf group. Nodes in the
    // same leaf group will always have the exact same phases, so there is no need to compute them separately.
    void copy_calculated_phases_per_leaf_group(const SubgraphLeafGroups* subgraph_leaf_groups);

    // Copies phases calculated for the first node in the leaf group to other nodes in the leaf group. Every node in
    // the leaf group gets a copy of the first leaf node's receiving phases for all input groups, and all source
    // nodes of the given leaf group are updated to have sending phases towards all leaf nodes in the group.
    void copy_calculated_phases_per_leaf_group(const LeafGroup& leaf_group);
};

}  // namespace pipegen2
