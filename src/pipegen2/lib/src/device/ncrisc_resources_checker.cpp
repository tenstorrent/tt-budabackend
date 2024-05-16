// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0

#include "device/ncrisc_resources_checker.h"

#include "device/ncrisc_input_node_readers_checker.h"
#include "device/ncrisc_readers_checker.h"
#include "device/ncrisc_writers_checker.h"

namespace pipegen2
{

void NcriscResourcesChecker::check(const RationalGraph* rational_graph) const
{
    check_ncrisc_input_node_fork_limits(rational_graph);
    check_ncrisc_readers_limits(rational_graph);
    check_ncrisc_writers_limits(rational_graph);
}

void NcriscResourcesChecker::check_ncrisc_readers_limits(const RationalGraph* rational_graph) const
{
    NcriscReadersChecker ncrisc_readers_checker;
    ncrisc_readers_checker.check(rational_graph);
}

void NcriscResourcesChecker::check_ncrisc_writers_limits(const RationalGraph* rational_graph) const
{
    NcriscWritersChecker ncrisc_writers_checker;
    ncrisc_writers_checker.check(rational_graph);
}

void NcriscResourcesChecker::check_ncrisc_input_node_fork_limits(const RationalGraph* rational_graph) const
{
    NcriscInputNodeForkLimitsChecker ncrisc_input_node_fork_limits_checker;
    ncrisc_input_node_fork_limits_checker.check(rational_graph);
}

} // namespace pipegen2
