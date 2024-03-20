// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "dram_parameters.h"
#include "model/rational_graph/base_rg_pipe.h"
#include "model/rational_graph/dram_input_node.h"

namespace pipegen2
{
    class DataFlowParameters
    {
    public:
        DataFlowParameters(unsigned int num_iterations_in_epoch,
                           unsigned int unroll_factor,
                           DramParameters::UPtr&& dram_params) :
            m_num_iterations_in_epoch(num_iterations_in_epoch),
            m_unroll_factor(unroll_factor),
            m_dram_params(std::move(dram_params))
        {
        }

        unsigned int get_num_iterations_in_epoch() const { return m_num_iterations_in_epoch; }

        unsigned int get_unroll_factor() const { return m_unroll_factor; }

        const DramParameters* get_dram_parameters() const { return m_dram_params.get(); }

    private:
        // Number of iterations required to transfer all data in a graph.
        unsigned int m_num_iterations_in_epoch;

        // Calculated unroll factor for phases in one iteration.
        unsigned int m_unroll_factor;

        // Data flow parameters related to dram inputs/outputs.
        DramParameters::UPtr m_dram_params;
    };
}