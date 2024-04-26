// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <ostream>
#include <vector>

#include "util_types.h"

namespace analyzer {

class Pipe {
    public:
        Pipe(const std::vector<GridLoc>& inputs, const std::vector<GridLoc>& outputs) {
            this->pipe_id = pipe_id_counter++;
            this->inputs  = inputs;
            this->outputs = outputs;
            this->incoming_noc_id = -1;
            this->outgoing_noc_id = -1;
        };
        
        Pipe(std::uint64_t pipe_id, 
            const std::vector<GridLoc>& inputs,
            const std::vector<int>& inputs_num_tiles,
            int t_factor,
            const std::vector<GridLoc>& outputs,
            int tile_size,
            GridLoc& pipe_location,
            int chip_location,
            int incoming_noc_id,
            int outgoing_noc_id,
            int incoming_vc,
            int outgoing_vc,
            bool post_tm_prolog,
            int dram_bank
        ) {
            this->pipe_id = pipe_id;
            this->inputs  = inputs;
            this->inputs_num_tiles = inputs_num_tiles;
            this->outputs = outputs;
            this->tile_size = tile_size;
            this->location = pipe_location;
            this->chip_location = chip_location;
            this->incoming_noc_id = incoming_noc_id;
            this->outgoing_noc_id = outgoing_noc_id;
            this->incoming_vc = incoming_vc;
            this->outgoing_vc = outgoing_vc;
            this->post_tm_prolog = post_tm_prolog;
            this->dram_bank = dram_bank;
            evaluateGather(t_factor);
        };

        ~Pipe() = default;
        
        std::uint64_t pipe_id;
        GridLoc location;
        int chip_location;

        std::vector<GridLoc> inputs;
        std::vector<int> inputs_num_tiles;
        // std::vector<int> inputs_modelled_bw_factor; //unused
        //float inputs_modelled_max_bw;
        float average_tiles_per_input;
        int total_tiles;
        int tile_size;

        int incoming_noc_id;
        int outgoing_noc_id;

        int incoming_vc;
        int outgoing_vc;

        bool post_tm_prolog;
        
        int dram_bank = 0;

        void evaluateGather(int t_factor);
        std::unordered_map<GridLoc, int> unique_inputs_total_tiles;
        
        std::vector<GridLoc> outputs;

    private:
        static int pipe_id_counter;
};

inline std::ostream& operator<<(std::ostream& os, const Pipe& pipe) {
    os << "Pipe{";
    os << ".id = " << pipe.pipe_id << ", ";
    os << ".location = " << "(" << pipe.location.y << ", "  << pipe.location.x << ")";
    os << "\n.inputs = ";
    for(auto loc: pipe.inputs) {
       os << "(" << loc.y << ", "  << loc.x << ")";
    }
    os << "\n.outputs = ";
    for(auto loc: pipe.outputs) {
       os << "(" << loc.y << ", "  << loc.x << ")";
    }
    os << "\n";
    return os;
}

struct EthernetPipe {
    uint64_t pipe_id;
    int num_tiles;
    int tile_size;
    int input_chip_id;
    int input_eth_chan;
    int output_chip_id;
    int output_eth_chan;
};

}
