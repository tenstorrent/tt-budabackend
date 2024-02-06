// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <vector>

#include "common/analyzer_api_types.hpp"

#include "grid.hpp"

namespace analyzer {

struct OpParams{
    std::string name;
    std::string type;
    int grid_size_y;
    int grid_size_x;
    int grid_loc_y;
    int grid_loc_x;
    bool grid_transpose;
    int estimated_cycles;
};


class Op : public Grid {
    public:
        Op(OpParams p){
            this->name = p.name;
            this->core_grid_location = GridLoc(p.grid_loc_y, p.grid_loc_x);
            this->dims.y = p.grid_size_y;
            this->dims.x = p.grid_size_x;
            this->node_op = p.type;
            this->estimated_cycles = p.estimated_cycles;
            this->grid_transpose = p.grid_transpose;
        };
        ~Op() = default;

        void map(Chip& chip) override;
        int cyclesPerTensor() override;

    GridLoc core_grid_location;
    std::string node_op = "invalid";
    int estimated_cycles = -1;
    bool grid_transpose;
};

}  // namespace analyzer