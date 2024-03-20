// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "grid.hpp"

namespace analyzer {

struct QueueParams{
    std::string name;
    int grid_size_y;
    int grid_size_x;
    std::vector<int> dram_channels;
};


class Queue : public Grid {
    // TODO: more to Fill out
    public:
        Queue(QueueParams params) {
            this->name = params.name;
            this->dims[1] = params.grid_size_y;
            this->dims[0] = params.grid_size_x;
            this->dram_channels = params.dram_channels;
        };
        ~Queue() = default;

        std::vector<int> dram_channels;

        // TODO: I think this mapping is only needed for displaying queue names associated with a dram channel
        // And, this probably doesnt need to be used for any of the bandwidth calculations
        void map(Chip& chip) override {
            int channel = 0;
            for (int j = 0; j < this->dims[1]; j++) {
                for (int i = 0; i < this->dims[0]; i++) {
                    this->grid_to_node_mapping[j][i] = chip.getDramNode(dram_channels[channel++]);
                }
            }
            //chip.addMappedGrid
        };

    
};

}  // namespace analyzer

