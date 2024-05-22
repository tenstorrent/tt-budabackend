// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "model/rational_graph/rational_graph.h"

namespace pipegen2
{

// Wrapper class delegating checks for rational graph NCRISC constraints to particular checkers modelling those
// constraints.
class NcriscResourcesChecker
{
public:
    NcriscResourcesChecker() = default;

    ~NcriscResourcesChecker() = default;

    void check(const RationalGraph* rational_graph) const;

private:
    void check_ncrisc_readers_limits(const RationalGraph* rational_graph) const;

    void check_ncrisc_writers_limits(const RationalGraph* rational_graph) const;

    void check_ncrisc_input_node_fork_limits(const RationalGraph* rational_graph) const;
};

}  // namespace pipegen2
