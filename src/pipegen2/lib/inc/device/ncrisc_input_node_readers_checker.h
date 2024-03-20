#pragma once

#include "model/rational_graph/rational_graph.h"

namespace pipegen2
{

class DramParallelForkPipe;

// Class modelling constraint that forking factor of a DRAM or PCIe buffer is limited to
// core_resources_constants::max_ncrisc_input_node_readers. Variable holding this info is 8bit and located in L0 NCRISC
// DATA RAM.
class NcriscInputNodeForkLimitsChecker
{
public:
    NcriscInputNodeForkLimitsChecker() = default;

    ~NcriscInputNodeForkLimitsChecker() = default;

    // Checks if forking factors of DRAM of PCIe input nodes in rational graph breach limits.
    void check(const RationalGraph* rational_graph);

private:
    // Checks if forking factor of DRAM of PCIe input node (forked through DramParallelForkPipe) breaches limits.
    void check_forking_factor(const DramParallelForkPipe* pipe);
};

} // namespace pipegen2
