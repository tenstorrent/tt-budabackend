// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <map>
#include <vector>

#include "common/fwd_declarations.hpp"

#include "pipe.hpp"

namespace analyzer {

class Link {
    public:
        Link(std::string name, float max_link_bw);

        ~Link() = default;
        
        void setName(std::string name) { this->name = name; };
        std::string getName() const { return this->name; };
        int getNumOccupants() const;
        float getTotalCapacity() const;
        float getTotalData() const;
        float getTotalBandwidth(int num_cycles_per_input) const;
        float getPercentCapacity(int num_cycles_per_input) const;

        void addPipe(std::uint64_t p, float bw, uint32_t vc);
        void addPipeWithoutVC(std::uint64_t pipe_id, uint32_t data_size);

        std::unordered_map<uint64_t, float> getMappedPipes() const {
            return mapped_pipe_ids;
        };

    private:
        std::string name;
        float max_link_bw;
        std::unordered_map<uint64_t, float> mapped_pipe_ids;
        // VCs
        // Response VCs 6/7 and 14/15 -- Main modelling for DRAM READS

        // Write VCs 1/2 and 9/10 -- Main modelling for DRAM WRITES

        // MCast VCs 4/5 and 12/13 -- Main modelling for Mcast writes
        std::map<uint32_t, std::vector<uint64_t>> virtual_channels_to_pipe_ids;
    
};

};
