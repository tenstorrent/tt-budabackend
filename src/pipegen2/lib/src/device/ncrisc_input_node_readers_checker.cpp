// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "device/ncrisc_input_node_readers_checker.h"

#include <string>

#include "device/core_resources_constants.h"
#include "model/rational_graph/nodes/base_rg_node.h"
#include "model/rational_graph/pipes/base_rg_pipe.h"
#include "model/rational_graph/pipes/fork/dram_parallel_fork_pipe.h"
#include "pipegen2_exceptions.h"

namespace pipegen2
{

void NcriscInputNodeForkLimitsChecker::check(const RationalGraph* rational_graph)
{
    for (const std::unique_ptr<RGBasePipe>& pipe : rational_graph->get_pipes())
    {
        // Currently, the only nodes that are read from using NCRISC are DRAM and PCIe inputs. DramParallelForkPipe
        // forks DramInputNode or PCIeStreamingNode to multiple readers.
        const DramParallelForkPipe* parallel_fork = dynamic_cast<const DramParallelForkPipe*>(pipe.get());
        if (parallel_fork != nullptr)
        {
            check_forking_factor(parallel_fork);
        }
    }
}

void NcriscInputNodeForkLimitsChecker::check_forking_factor(const DramParallelForkPipe* pipe)
{
    unsigned int forking_factor = pipe->get_output_nodes().size();

    if (forking_factor > core_resources_constants::max_ncrisc_input_node_readers)
    {
        const RGBaseNode* ncrisc_input_node = pipe->get_input_node();
        std::string node_type = node_type_to_string(ncrisc_input_node->get_node_type());

        throw InvalidPipeGraphSpecificationException(
            node_type + " node with ID " + std::to_string(ncrisc_input_node->get_id()) +
                " exceeds maximum number of streams reading from it (" + std::to_string(forking_factor) + " out of " +
                std::to_string(core_resources_constants::max_ncrisc_input_node_readers) + ").",
            std::nullopt,
            ncrisc_input_node->get_physical_location());
    }
}

}  // namespace pipegen2
