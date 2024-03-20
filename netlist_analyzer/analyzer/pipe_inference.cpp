// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cassert>

#include "pipe_inference.hpp"

#include "grid.hpp"
#include "node.hpp"
#include "utils/logger.hpp"

namespace analyzer {

// Pipe inference
std::vector<Pipe*> routeData(Grid* src, Grid* dst) {
    
    std::vector<Pipe*> result_pipes;
    // TODO: For now, only allow same sized grids
    log_assert(src->equalGrid(dst), "Grid size mismatch");

    // depending on dst type gather data differently,
    // right now, only do eltwise
    // create pipes that will move data
    for (int j = 0; j < dst->dims[1]; j++) {
        for (int i = 0; i < dst->dims[0]; i++) {
            const auto src_node = src->grid_to_node_mapping[j][i];
            const auto dst_node = dst->grid_to_node_mapping[j][i];
            result_pipes.push_back(new Pipe({src_node->soc_location}, {dst_node->soc_location}));
        }
    }
    return result_pipes;
}

}