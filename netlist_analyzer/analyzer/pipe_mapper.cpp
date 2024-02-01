// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <cassert>

#include "pipe_mapper.hpp"

#include "op.hpp"

#define DRAM_READ_VC 6
#define MCAST_VC 4

namespace analyzer {

// Pipe routing / mapping
// increment statistics / link usage
// Noc0: Direction order East then South
void routeNoc0(Chip & chip, Pipe* p, std::shared_ptr<Node> src, std::shared_ptr<Node> dst, uint32_t data_size, uint32_t vc) {
    uint64_t pipe_id = p->pipe_id;
    int cur_x = src->soc_location.x;
    int cur_y = src->soc_location.y;
    const int end_x = dst->soc_location.x;
    const int end_y = dst->soc_location.y;

    // handle source point
    auto src_node = chip.getNode(cur_y, cur_x);
    src_node->routeFromNodeToNoc0(p, data_size);
    src_node->noc0_link_out->addPipeWithoutVC(pipe_id, data_size);

    // handle sink point
    auto end_node = chip.getNode(end_y, end_x);
    end_node->noc0_link_in->addPipeWithoutVC(pipe_id, data_size);
    end_node->routeToNodeFromNoc0(p, data_size);

    // Map east
    while(cur_x != end_x) {
        // map outgoing link
        auto cur_node = chip.getNode(cur_y, cur_x);
        if(cur_x == 0) {
            vc = (vc + 8) % 16;
        }
        cur_node->external_links.noc0_out_east->addPipe(pipe_id, data_size, vc); 
        // increment node
        cur_x = (cur_x + 1 ) % chip.getGridSize().x;
    }

    // Map south 
    while(cur_y != end_y) {
        // map outgoing link
        auto cur_node = chip.getNode(cur_y, cur_x);
        if(cur_y == 0) {
            vc = (vc + 8) % 16;
        }
        cur_node->external_links.noc0_out_south->addPipe(pipe_id, data_size, vc);
        // increment node
        cur_y = (cur_y + 1 ) % chip.getGridSize().y;
    }

    
}

void routeMcastRowNoc0(Chip & chip, Pipe* p, std::shared_ptr<Node> start, std::shared_ptr<Node> end, uint32_t data_size, uint32_t vc) {
    uint64_t pipe_id = p->pipe_id;
    int cur_x = start->soc_location.x;
    int cur_y = start->soc_location.y;
    const int end_x = end->soc_location.x;
    //const int end_y = end->soc_location.y;

    // handle source point
    auto src_node = chip.getNode(cur_y, cur_x);

    // Loopback  
    src_node->noc0_link_out->addPipeWithoutVC(pipe_id, data_size);
    //src_node->noc0_link_in->addPipeWithoutVC(pipe_id, data_size);

    // Mcast to external
    src_node->noc0_link_out->addPipeWithoutVC(pipe_id, data_size);

    // Map east
    while(cur_x != end_x) {
        // map outgoing link
        auto cur_node = chip.getNode(cur_y, cur_x);
        if(cur_x == 0) {
            vc = (vc + 8) % 16;
        }
        cur_node->external_links.noc0_out_east->addPipe(pipe_id, data_size, vc);

        if(cur_node->node_type == "core") {
            cur_node->noc0_link_in->addPipeWithoutVC(pipe_id, data_size);
        }

        // increment node
        cur_x = (cur_x + 1 ) % chip.getGridSize().x;
    }
    auto cur_node = chip.getNode(cur_y, cur_x);
    log_assert(cur_node->node_type == "core", "Incorrect node type");
    cur_node->noc0_link_in->addPipeWithoutVC(pipe_id, data_size); 
}

// Noc1: Direction order North then West
void routeNoc1(Chip & chip, Pipe* p, std::shared_ptr<Node> src, std::shared_ptr<Node> dst, uint32_t data_size, uint32_t vc) {
    uint64_t pipe_id = p->pipe_id;
    int cur_x = src->soc_location.x;
    int cur_y = src->soc_location.y;
    const int end_x = dst->soc_location.x;
    const int end_y = dst->soc_location.y;

    // handle source point
    auto src_node = chip.getNode(cur_y, cur_x);
    src_node->routeFromNodeToNoc1(p, data_size);
    src_node->noc1_link_out->addPipeWithoutVC(pipe_id, data_size);

    // handle sink point
    auto end_node = chip.getNode(end_y, end_x);
    end_node->routeToNodeFromNoc1(p, data_size);
    end_node->noc1_link_in->addPipeWithoutVC(pipe_id, data_size);
    
    // Map north
    while(cur_y != end_y) {
        // map outgoing link
        auto cur_node = chip.getNode(cur_y, cur_x);
        if(cur_y == (chip.getGridSize().y - 1)) {
            vc = (vc + 8) % 16;
        }
        cur_node->external_links.noc1_out_north->addPipe(pipe_id, data_size, vc);
        // increment node
        cur_y = (cur_y + (chip.getGridSize().y - 1)) % chip.getGridSize().y;
    }

    // Map west
    while(cur_x != end_x) {
        // map outgoing link
        auto cur_node = chip.getNode(cur_y, cur_x);
        if(cur_x ==(chip.getGridSize().x - 1)) {
            vc = (vc + 8) % 16;
        }
        cur_node->external_links.noc1_out_west->addPipe(pipe_id, data_size, vc);
        // increment node
        cur_x = (cur_x + (chip.getGridSize().x - 1)) % chip.getGridSize().x;
    }
}

// Noc1: Direction order North then West
void routeMcastColNoc1(Chip & chip, Pipe* p, std::shared_ptr<Node> src, std::shared_ptr<Node> dst, uint32_t data_size, uint32_t vc) {
    uint64_t pipe_id = p->pipe_id;
    int cur_x = src->soc_location.x;
    int cur_y = src->soc_location.y;
    //const int end_x = dst->soc_location.x;
    const int end_y = dst->soc_location.y;

    // handle source point
    auto src_node = chip.getNode(cur_y, cur_x);
    
    // Loopback
    src_node->noc1_link_out->addPipeWithoutVC(pipe_id, data_size);
    //src_node->noc1_link_in->addPipeWithoutVC(pipe_id, data_size);

    // Mcast to external
    src_node->noc1_link_out->addPipeWithoutVC(pipe_id, data_size);
    
    // Map north
    while(cur_y != end_y) {
        // map outgoing link
        auto cur_node = chip.getNode(cur_y, cur_x);
        if(cur_y == (chip.getGridSize().y - 1)) {
            vc = (vc + 8) % 16;
        }
        cur_node->external_links.noc1_out_north->addPipe(pipe_id, data_size, vc);

        if(cur_node->node_type == "core") {
            cur_node->noc1_link_in->addPipeWithoutVC(pipe_id, data_size);
        }

        // increment node
        cur_y = (cur_y + (chip.getGridSize().y -1)) % chip.getGridSize().y;
    }
    auto cur_node = chip.getNode(cur_y, cur_x);
    log_assert(cur_node->node_type == "core", "Incorrect node type");
    cur_node->noc1_link_in->addPipeWithoutVC(pipe_id, data_size);
}

// Pipe routing / mapping
// Deprecated
void mapPipe(Chip & chip, Pipe* p) {

    // Do direction order routing over links
    // Noc0: East then South
    // Noc1: North then West
    
    // XXX: For now only use noc0 and only unicast pipes without gather
    auto src_node = chip.getNode(p->inputs.at(0));
    auto dst_node = chip.getNode(p->outputs.at(0));

    routeNoc0(chip, p, src_node, dst_node, 1, 1);

}

// Pipe routing / mapping
void mapGenericPipe(Chip & chip, Pipe* p) {

    const bool mcast = p->outputs.size() > 1; // output scatter handled by creating multiple pipes
    //auto gather_dst_node = chip.getNode(mcast ? p->location : p->outputs.at(0));
    auto gather_dst_node = chip.getNode(p->location);

    // TODO: this may not be valid for host untilizer assert(p->unique_inputs_total_tiles.size() == 1); // no gathers on DRAM output pipes
    
    const int tile_size = p->tile_size;
    auto pipe_src_0 = p->inputs.at(0); 
    const bool is_dram_read = chip.getNode(pipe_src_0)->node_type == "dram";


    // Gather routing: calculate routes for aggregated data from multiple sources
    // Gather only if we have multiple inputs and source is from another location
    const bool is_gather = not std::all_of(p->inputs.cbegin(), p->inputs.cend(), [p](auto loc) { return loc == p->inputs.at(0); }) or p->inputs.at(0) != p->location;
    if(is_gather) {
        for(const auto & [in, num_tiles] : p->unique_inputs_total_tiles) {
            auto src_node = chip.getNode(in);
            uint32_t input_bw = p->post_tm_prolog ? 0 : num_tiles * tile_size;
            uint32_t vc = is_dram_read ? DRAM_READ_VC : p->incoming_vc;
            if(p->incoming_noc_id == 1) {
                routeNoc1(chip, p, src_node, gather_dst_node, input_bw, vc);
            }
            else { // if default (-1) or 0
                routeNoc0(chip, p, src_node, gather_dst_node, input_bw, vc);
            }
        }
    }

    uint32_t pipe_output_bw = p->post_tm_prolog ? 0 : p->total_tiles * tile_size; //  / (float) consumer_cycles;

    // if pipe location not the same as output location, do secondary routing
    if(not mcast and p->location != p->outputs.at(0)) {
        auto src_node = chip.getNode(p->location);
        auto dst_node = chip.getNode(p->outputs.at(0));
        
        const uint32_t vc = p->outgoing_vc;
        if(p->outgoing_noc_id == 1) {
            routeNoc1(chip, p, src_node, dst_node, pipe_output_bw, vc);
        }
        else { // if default (-1) or 0
            routeNoc0(chip, p, src_node, dst_node, pipe_output_bw, vc);
        }
    }

    // Do second step mcast if necessary
    if(mcast) {
        // determine shape and routing of mcast
        // only allow single row and single colum mcast
        const bool row_not_col_mcast = p->outputs.at(0).y == p->outputs.at(1).y;
        // use mcast vc
        const uint32_t vc = MCAST_VC;
        // If we are row mcast, must use noc0 and assert that the pipe->location is the left most bound of the mcast box
        if(row_not_col_mcast) {
            int x_min = chip.getGridSize().x;
            int x_max = -1;
            const int y = p->outputs.at(0).y;
            for (const auto &out : p->outputs) {
                log_assert(out.y == y, "y coordinate changing in row multicast");
                if(out.x < x_min){
                    x_min = out.x;
                }
                if(out.x > x_max){
                    x_max = out.x;
                }
            }
            // Assert pipe location is within the mcast box at the correct corner
            // Not true with new optimizations // assert(p->location.x == x_min and p->location.y == y);
            routeMcastRowNoc0(chip, p, gather_dst_node, chip.getNode(y, x_max), pipe_output_bw, vc);
        }
        else { // col mcast, must use noc1 and assert hat the pipe->location is the bottom most bound of the mcast box
            int y_min = chip.getGridSize().y;
            int y_max = -1;
            const int x = p->outputs.at(0).x;
            for (const auto &out : p->outputs) {
                log_assert(out.x == x, "x coordinate changing in column multicast");
                if(out.y < y_min){
                    y_min = out.y;
                }
                if(out.y > y_max){
                    y_max = out.y;
                }
            }
            // Assert pipe location is within the mcast box at the correct corner
            // Not true with new optimizations // assert(p->location.y == y_max and p->location.x == x);
            routeMcastColNoc1(chip, p, gather_dst_node, chip.getNode(y_min, x), pipe_output_bw, vc);
        }
    }
}

void mapEthernetPipe(Chip & chip, int chip_id, EthernetPipe * p) {
    const int data_size = p->num_tiles * p->tile_size;
    if(p->input_chip_id == chip_id) {
        auto eth_node = std::dynamic_pointer_cast<EthNode>(chip.getEthNode(p->input_eth_chan));
        eth_node->to_ethernet->addPipeWithoutVC(p->pipe_id, data_size);
    }

    if(p->output_chip_id == chip_id) {
        auto eth_node = std::dynamic_pointer_cast<EthNode>(chip.getEthNode(p->output_eth_chan));
        eth_node->from_ethernet->addPipeWithoutVC(p->pipe_id, data_size);
    }
}

void mapPipe(Chip & chip, std::shared_ptr<Pipe> p) {
    mapPipe(chip, p.get());
}

}
