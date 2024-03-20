// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "op.hpp"

#include "chip.hpp"

namespace analyzer {
void Op::map(Chip& chip) {
    int outer, inner;
    if(this->grid_transpose) {
        outer = this->dims.x;
        inner = this->dims.y;
    }
    else {
        outer = this->dims.y;
        inner = this->dims.x;
    }
    for (int j = 0; j < outer; j++) {
        for (int i = 0; i < inner; i++) {
            this->grid_to_node_mapping[j][i] = chip.getCoreNode(this->core_grid_location.y + j, this->core_grid_location.x + i);
            this->grid_to_node_mapping[j][i]->mapped_op = this;
        }
    }
}

int Op::cyclesPerTensor() {
    return this->estimated_cycles;
}

}