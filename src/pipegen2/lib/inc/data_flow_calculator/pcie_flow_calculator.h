// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <vector>

#include "model/pipe_graph/pg_buffer.h"
#include "model/pipe_graph/pg_pipe.h"

namespace pipegen2 {
class PCIeFlowCalculator {
   public:
    // Calculates necessary buffer size in tiles on the source side for PCIE transfer.
    static unsigned int calculate_source_size_tiles(const std::vector<PGPipe::Input>& mmio_pipe_inputs,
                                                    const std::vector<PGBuffer*>& mmio_pipe_outputs,
                                                    const unsigned int tile_size);

    // Calculates necessary buffer size in tiles on the destination side for PCIE transfer.
    static unsigned int calculate_destination_size_tiles(const std::vector<PGBuffer*>& mmio_pipe_outputs,
                                                         const unsigned int tile_size,
                                                         const unsigned int src_size_tiles);
};
}  // namespace pipegen2