// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// grid.hpp

#pragma once

#include <unordered_map>
#include <string>

namespace analyzer {
class Node;
class Chip;

class Grid {
    public:
        Grid() {};
        ~Grid() = default;
        
        bool equalGrid(Grid* g) {
           return this->dims == g->dims;
        }

        // TODO: these should probably not be public
        Shape dims; // y,x dims;
        
        
        std::string name;

        virtual void map(Chip& chip) = 0;
        virtual int cyclesPerTensor() { return -1; };

        bool is_mapped;
        std::unordered_map<int, std::unordered_map<int,std::shared_ptr<Node>>> grid_to_node_mapping;

        //std::vector<std::vector<std::shared_ptr<Node>>> nodes = {};
    protected:
        
};
}