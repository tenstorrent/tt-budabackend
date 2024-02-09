// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "link.hpp"

namespace analyzer {

Link::Link(std::string name, float max_link_bw) {
    this->name = name;
    this->max_link_bw = max_link_bw;
}

/*
bool Link::operator<(const Link& l) {
    return getPercentCapacity() < l.getPercentCapacity();
}; 
*/

float Link::getTotalCapacity() const {
    return this->max_link_bw;
}

float Link::getPercentCapacity(int num_cycles_per_input) const {
    return getTotalBandwidth(num_cycles_per_input) / this->max_link_bw;
}

int Link::getNumOccupants() const {
    return mapped_pipe_ids.size();
}

float Link::getTotalData() const {
    float result = 0;
    for (const auto& it : mapped_pipe_ids) {
        result += it.second;
    }
    return result;
}

float Link::getTotalBandwidth(int num_cycles_per_input) const {
    float result = 0;
    for (const auto& it : mapped_pipe_ids) {
        result += it.second;
    }
    result = result / (float) num_cycles_per_input;
    return result;
}

void Link::addPipe(std::uint64_t pipe_id, float bw, uint32_t vc) {
    //std::cout << "mapping pipe: " << pipe_id << " bw: " << bw << std::endl;
    mapped_pipe_ids[pipe_id] += bw;
    virtual_channels_to_pipe_ids[vc].push_back(pipe_id);
}

void Link::addPipeWithoutVC(std::uint64_t pipe_id, uint32_t bw) {
    //std::cout << "mapping pipe: " << pipe_id << " bw: " << bw << std::endl;
    mapped_pipe_ids[pipe_id] += (float) bw;
}

}