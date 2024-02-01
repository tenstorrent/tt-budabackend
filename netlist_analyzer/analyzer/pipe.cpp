// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "pipe.hpp"

namespace analyzer {

int Pipe::pipe_id_counter = 0;

void Pipe::evaluateGather(int t_factor) {

    this->total_tiles = 0;
    for (uint i = 0; i < this->inputs.size(); i++) {
        const auto & in = this->inputs[i];
        unique_inputs_total_tiles[in] += inputs_num_tiles[i] * t_factor;
        this->total_tiles += inputs_num_tiles[i] * t_factor;
    }

    average_tiles_per_input = (float) this->total_tiles / (float) this->inputs.size();
}

}



