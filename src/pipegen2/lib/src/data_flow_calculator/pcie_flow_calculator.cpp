// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "data_flow_calculator/pcie_flow_calculator.h"

#include <numeric>

#include "pipegen2_constants.h"

namespace pipegen2 {
unsigned int PCIeFlowCalculator::calculate_source_size_tiles(const std::vector<PGPipe::Input>& mmio_pipe_inputs,
                                                             const std::vector<PGBuffer*>& mmio_pipe_outputs,
                                                             const unsigned int tile_size) {
    // TODO: This is magic from pipegen1, we should eventually understand it and document it here.

    // Grouping adjacent buffers on the same cores.
    std::vector<unsigned int> size_tiles_per_core;
    size_tiles_per_core.push_back(mmio_pipe_inputs[0].get_buffer()->get_scatter_gather_num_tiles());
    for (std::size_t i = 1; i < mmio_pipe_inputs.size(); ++i) {
        const tt_cxy_pair& previous_buffer_location = mmio_pipe_inputs[i - 1].get_buffer()->get_logical_location();
        const tt_cxy_pair& current_buffer_location = mmio_pipe_inputs[i].get_buffer()->get_logical_location();
        if (previous_buffer_location == current_buffer_location) {
            size_tiles_per_core.back() += mmio_pipe_inputs[i].get_buffer()->get_scatter_gather_num_tiles();
        } else {
            size_tiles_per_core.push_back(mmio_pipe_inputs[i].get_buffer()->get_scatter_gather_num_tiles());
        }
    }

    unsigned int src_size_tiles = size_tiles_per_core[0];
    for (std::size_t i = 1; i < size_tiles_per_core.size(); ++i) {
        src_size_tiles = std::gcd(src_size_tiles, size_tiles_per_core[i]);
    }

    if (mmio_pipe_outputs.size() == 1) {
        src_size_tiles = std::gcd(src_size_tiles, mmio_pipe_outputs[0]->get_size_tiles());
    }

    while (src_size_tiles % 2 == 0 &&
           src_size_tiles * tile_size > constants::dram_output_stream_max_write_issue_bytes) {
        src_size_tiles /= 2;
    }

    return src_size_tiles;
}

unsigned int PCIeFlowCalculator::calculate_destination_size_tiles(const std::vector<PGBuffer*>& mmio_pipe_outputs,
                                                                  const unsigned int tile_size,
                                                                  const unsigned int src_size_tiles) {
    // TODO: This is magic from pipegen1, we should eventually understand it and document it here.

    if (mmio_pipe_outputs.size() > 1) {
        unsigned int scale_factor = 1;
        if (2 * src_size_tiles * tile_size <= constants::pipe_reduce_mem_usage_threshold_bytes) {
            scale_factor = 2;
        }

        return scale_factor * src_size_tiles;
    } else {
        return mmio_pipe_outputs[0]->get_size_tiles();
    }
}
}  // namespace pipegen2