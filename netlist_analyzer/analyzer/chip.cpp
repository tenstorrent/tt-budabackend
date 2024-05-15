// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// chip.hpp
#include <memory>
#include <cassert>

#include <iostream>
#include <fstream>

#include <yaml-cpp/yaml.h>

#include "node.hpp"

#include "chip.hpp"
#include "op.hpp"
#include "utils/logger.hpp"
using namespace std;

namespace analyzer {


Chip::Chip(std::string arch) {
    this->arch_name = arch;

    if(arch == "grayskull") {
        createDefaultGS();
    }
    else if (arch == "wormhole_b0") {
        createWHB0();
    }
    else {
       log_fatal("Unknown arch: {}, Valid Archnames: [grayskull, wormhole_b0]", arch);
    }
}
        
std::shared_ptr<Node> Chip::getNode(int y, int x) const {
    log_assert (x < this->grid_size.x, "Invalid x coord");
    log_assert (x >= 0, "Invalid x coord");
    log_assert (y < this->grid_size.y, "Invalid y coord");
    log_assert (y >= 0, "Invalid y coord");

    return _nodes.at(y).at(x);
}

std::shared_ptr<Node> Chip::getCoreNode(int y, int x) const {
    log_assert (x < this->core_grid_size.x, "Invalid x coord");
    log_assert (x >= 0, "Invalid x coord");
    log_assert (y < this->core_grid_size.y, "Invalid y coord");
    log_assert (y >= 0, "Invalid y coord");

    const auto [ty, tx] = _core_to_chip_translation.at(y).at(x);
    return _nodes.at(ty).at(tx);
}

std::shared_ptr<Node> Chip::getEthNode(int channel) const {
    // TODO: assert channel <  num channels
    return _eth_nodes.at(channel);
}

std::shared_ptr<Node> Chip::getPcieNode() const {
    return _pcie_node;
}

std::shared_ptr<Node> Chip::getDramNode(int channel, int sub_channel) const {
    // TODO: assert channel <  num channels
    // N.B. This channel override is only for GS
    if(channel == 255) {
        return this->getPcieNode();
    }
    return _dram_nodes.at(channel).at(sub_channel);
}

void Chip::createWHB0() {
    this->grid_size.y = 12;
    this->grid_size.x = 10;

    this->core_grid_size.y = 10;
    this->core_grid_size.x = 8;

    // Create grid translation
    for (int x = 0; x < 12; x++) {
        for (int y = 0; y < 10; y++) {
            int tx = x + 1;
            int ty = y + 1;
            if (tx > 4) {
                tx++;
            }
            if (ty > 5) {
                ty++;
            }
            _core_to_chip_translation[y][x] = {ty, tx};
        }
    }
        
    // Create all noc links
    std::shared_ptr<Link> outgoing_links[this->grid_size.y][this->grid_size.x][4];

    for (int j = 0; j < this->grid_size.y; j++) {
        for (int i = 0; i < this->grid_size.x; i++) {
            for (int k = 0; k < 4; k++) {
                outgoing_links[j][i][k] = std::make_shared<Link>("", NOC_BYTES_PER_CYCLE);
                registerLinks(outgoing_links[j][i][k]);
            }
        }
    }
    
    // Create all nodes
    int eth_counter = 0;
    int eth_index[] = {1, 3, 5, 7, 6, 4, 2, 0, 9, 11, 13, 15, 14, 12, 10, 8};

    const int GSX = this->grid_size.x;
    const int GSY = this->grid_size.y;
    const int GSX_1 = GSX - 1;
    const int GSY_1 = GSY - 1;
    for (int j = 0; j < this->grid_size.y; j++) {
        for (int i = 0; i < this->grid_size.x; i++) {
            Node::NodeLinks temp_links;
            temp_links.noc1_out_north = outgoing_links[j][i][0];
            temp_links.noc0_out_east  = outgoing_links[j][i][1];
            temp_links.noc0_out_south = outgoing_links[j][i][2] ;
            temp_links.noc1_out_west  = outgoing_links[j][i][3];

            temp_links.noc0_in_north  = outgoing_links[(j + GSY_1) % GSY][i][2]; // south from above
            temp_links.noc1_in_east   = outgoing_links[j][(i + 1) % GSX][3]; // west from the left
            temp_links.noc1_in_south  = outgoing_links[(j + 1) % GSY][i][0]; // north from below
            temp_links.noc0_in_west   = outgoing_links[j][(i + GSX_1) % GSX][1]; // east from the right

            temp_links.noc1_out_north->setName("noc1_north_" + to_string(j) + "_to_" + to_string((j + GSY_1) % GSY) + "_y_" + to_string(i) + "_x");
            temp_links.noc0_out_east->setName("noc0_east_" + to_string(j) + "_y_" + to_string(i) + "_to_" + to_string((i + 1) % GSX) + "_x");
            temp_links.noc0_out_south->setName("noc0_south_" + to_string(j) + "_to_" + to_string((j + 1) % GSY) + "_y_" + to_string(i) + "_x");
            temp_links.noc1_out_west->setName("noc1_west_" + to_string(j) + "_y_" + to_string(i) + "_to_" + to_string((i + GSX_1) % GSX) + "_x");

            // Router Nodes (+ ARC)
            if ((i == 0 and j == 2) or 
                (i == 0 and j == 4) or 
                (i == 0 and j == 8) or 
                (i == 0 and j == 9) or 
                (i == 0 and j == 10)
            ) {
                _nodes[j][i] = std::shared_ptr<RouterNode>(new RouterNode({j, i}, temp_links));
                continue;
            }
            
            // DRAM Nodes
            if ((i == 0 and j == 0) or 
                (i == 0 and j == 1) or 
                (i == 0 and j == 11) or 
                (i == 0 and j == 5) or 
                (i == 0 and j == 6) or 
                (i == 0 and j == 7) or 
                (i == 5 and j == 0) or 
                (i == 5 and j == 1) or 
                (i == 5 and j == 11) or 
                (i == 5 and j == 2) or 
                (i == 5 and j == 9) or 
                (i == 5 and j == 10) or 
                (i == 5 and j == 3) or 
                (i == 5 and j == 4) or 
                (i == 5 and j == 8) or 
                (i == 5 and j == 5) or 
                (i == 5 and j == 6) or
                (i == 5 and j == 7)
            ) {
                _nodes[j][i] = std::shared_ptr<DramNode>(new DramNode({j,i}, temp_links));
                continue;
            }

            // Eth Nodes
            if ((j == 0 or j == 6) and 
                (i == 1 or
                 i == 2 or
                 i == 3 or
                 i == 4 or
                 i == 6 or
                 i == 7 or
                 i == 8 or
                 i == 9
                )
            ) {
                _nodes[j][i] = std::shared_ptr<EthNode>(new EthNode({j,i}, temp_links));
                _eth_nodes[eth_index[eth_counter]] = _nodes[j][i];
                eth_counter++;
                continue;
            }

            // PCIe Node
            if (i == 0 and j == 3){
                // TODO: SHould this be a DramNode? New special type?
                _nodes[j][i] = std::shared_ptr<PcieNode>(new PcieNode({j,i}, temp_links));
                _pcie_node = _nodes[j][i]; // pcie node
                continue;
            }

            // Core Nodes
            _nodes[j][i] = std::shared_ptr<CoreNode>(new CoreNode({j,i}, temp_links));
        }
    }

    // Dram channel and subchannel mapping
    _dram_nodes[0][0] = _nodes[0][0];
    _dram_nodes[0][1] = _nodes[1][0];
    _dram_nodes[0][2] = _nodes[11][0];
    _dram_nodes[1][0] = _nodes[5][0];
    _dram_nodes[1][1] = _nodes[6][0];
    _dram_nodes[1][2] = _nodes[7][0];
    _dram_nodes[2][0] = _nodes[0][5];
    _dram_nodes[2][1] = _nodes[1][5];
    _dram_nodes[2][2] = _nodes[11][5];
    _dram_nodes[3][0] = _nodes[2][5];
    _dram_nodes[3][1] = _nodes[9][5];
    _dram_nodes[3][2] = _nodes[10][5];
    _dram_nodes[4][0] = _nodes[3][5];
    _dram_nodes[4][1] = _nodes[4][5];
    _dram_nodes[4][2] = _nodes[8][5];
    _dram_nodes[5][0] = _nodes[5][5];
    _dram_nodes[5][1] = _nodes[6][5];
    _dram_nodes[5][2] = _nodes[7][5];

    int channel_id = 0;
    for(int i = 0; i < 6; i++) {
        dram_internals.push_back(make_shared<wh_dram_channel>(channel_id++));
        registerLinks(dram_internals.at(i)->getInternalLinks());
    }

    for(auto &[chan, subs]: _dram_nodes) {
        for(auto &[sub, node]: subs) {
            auto dram_node = std::static_pointer_cast<DramNode>(node);
            dram_node->dram_internal = dram_internals.at(chan);
            dram_node->channel = chan;
            dram_node->subchannel = sub;
        }
    }

    for (int j = 0; j < this->grid_size.y; j++) {
        for (int i = 0; i < this->grid_size.x; i++) {
            registerLinks(_nodes[j][i]->getInternalLinks());
        }
    }
}

void Chip::createDefaultGS() {
    this->grid_size.y = 12;
    this->grid_size.x = 13;

    this->core_grid_size.y = 10;
    this->core_grid_size.x = 12;

    // Create grid translation
    for (int x = 0; x < 12; x++) {
        for (int y = 0; y < 10; y++) {
            int tx = x + 1;
            int ty = y + 1;
            if (ty > 5) {
                ty++;
            }
            _core_to_chip_translation[y][x] = {ty, tx};
        }
    }
        
    // Create all links
    std::shared_ptr<Link> outgoing_links[12][13][4];

    for (int j = 0; j < 12; j++) {
        for (int i = 0; i < 13; i++) {
            for (int k = 0; k < 4; k++) {
                outgoing_links[j][i][k] = std::make_shared<Link>("", NOC_BYTES_PER_CYCLE);
                registerLinks(outgoing_links[j][i][k]);
            }
        }
    }
    
    // DRAM Channels
    int dram_counter = 0;
    int dram_index[] = {0, 2, 4, 6, 1, 3, 5, 7};
    int channel_id = 0;
    for(int i = 0; i < 8; i++) {
        dram_internals.push_back(make_shared<gs_dram_channel>(channel_id++));
        registerLinks(dram_internals.at(i)->getInternalLinks());
    }

    // Create all nodes
    for (int j = 0; j < this->grid_size.y; j++) {
        for (int i = 0; i < this->grid_size.x; i++) {
            Node::NodeLinks temp_links;
            temp_links.noc1_out_north = outgoing_links[j][i][0];
            temp_links.noc0_out_east  = outgoing_links[j][i][1];
            temp_links.noc0_out_south = outgoing_links[j][i][2] ;
            temp_links.noc1_out_west  = outgoing_links[j][i][3];

            temp_links.noc0_in_north  = outgoing_links[(j + 11) % 12][i][2]; // south from above
            temp_links.noc1_in_east   = outgoing_links[j][(i + 1) % 13][3]; // west from the left
            temp_links.noc1_in_south  = outgoing_links[(j + 1) % 12][i][0]; // north from below
            temp_links.noc0_in_west   = outgoing_links[j][(i + 12) % 13][1]; // east from the right

            temp_links.noc1_out_north->setName("noc1_north_" + to_string(j) + "_to_" + to_string((j + 11) % 12) + "_y_" + to_string(i) + "_x");
            temp_links.noc0_out_east->setName("noc0_east_" + to_string(j) + "_y_" + to_string(i) + "_to_" + to_string((i + 1) % 13) + "_x");
            temp_links.noc0_out_south->setName("noc0_south_" + to_string(j) + "_to_" + to_string((j + 1) % 12) + "_y_" + to_string(i) + "_x");
            temp_links.noc1_out_west->setName("noc1_west_" + to_string(j) + "_y_" + to_string(i) + "_to_" + to_string((i + 12) % 13) + "_x");

            // PCIe Node
            if (i == 0 and j == 4){
                // TODO: Should this be a DramNode? New special type?
                _nodes[j][i] = std::shared_ptr<PcieNode>(new PcieNode({j,i}, temp_links));
                _pcie_node = _nodes[j][i]; // pcie node
                continue;
            }

            // Router nodes
            if (i == 0) {
                _nodes[j][i] = std::shared_ptr<RouterNode>(new RouterNode({j, i}, temp_links));
                continue;
            }

            // DRAM Nodes
            if (j == 0 or j == 6) {
                if (i == 1 or i == 4 or i == 7 or i == 10) {
                    _nodes[j][i] = std::shared_ptr<DramNode>(new DramNode({j,i}, temp_links, dram_internals.at(dram_index[dram_counter]), dram_index[dram_counter]));
                    _dram_nodes[dram_index[dram_counter]][0] = _nodes[j][i];
                    dram_counter++;
                }
                else {
                    _nodes[j][i] = std::shared_ptr<RouterNode>(new RouterNode({j,i}, temp_links));
                }

                continue;
            }

            // Core Nodes
            _nodes[j][i] = std::shared_ptr<CoreNode>(new CoreNode({j,i}, temp_links));
        }
    }
}

[[deprecated]]
void Chip::printLinks() {
    for (int j = 0; j < 12; j++) {
        for (int i = 0; i < 13; i++) {
            Node::NodeLinks & temp_links = _nodes[j][i]->external_links;

            cout << "Node (" << j << ", " << i << "): Link: ";
            cout << "noc1_out_north: ";
            for (auto & [pipe_id, bw]: temp_links.noc1_out_north->getMappedPipes()){
                cout << pipe_id << ",";
            }
            cout << endl;

            cout << "Node (" << j << ", " << i << "): Link: ";
            cout << "noc0_out_east: ";
            for (auto &[pipe_id, bw] : temp_links.noc0_out_east->getMappedPipes()){
                cout << pipe_id << ",";
            }
            cout << endl;

            cout << "Node (" << j << ", " << i << "): Link: ";
            cout << "noc0_out_south: ";
            for (auto &[pipe_id, bw] : temp_links.noc0_out_south->getMappedPipes()){
                cout << pipe_id << ",";
            }
            cout << endl;

            cout << "Node (" << j << ", " << i << "): Link: ";
            cout << "noc1_out_west: ";
            for (auto &[pipe_id, bw] : temp_links.noc1_out_west->getMappedPipes()){
                cout << pipe_id << ",";
            }
            cout << endl;

            cout << "Node (" << j << ", " << i << "): Link: ";
            cout << "noc0_in_north: ";
            for (auto &[pipe_id, bw] : temp_links.noc0_in_north->getMappedPipes()){
                cout << pipe_id << ",";
            }
            cout << endl;
            
            cout << "Node (" << j << ", " << i << "): Link: ";
            cout << "noc1_in_east: ";
            for (auto &[pipe_id, bw] : temp_links.noc1_in_east->getMappedPipes()){
                cout << pipe_id << ",";
            }
            cout << endl;

            cout << "Node (" << j << ", " << i << "): Link: ";
            cout << "noc1_in_south: ";
            for (auto &[pipe_id, bw] : temp_links.noc1_in_south->getMappedPipes()){
                cout << pipe_id << ",";
            }
            cout << endl;

            cout << "Node (" << j << ", " << i << "): Link: ";
            cout << "noc0_in_west: ";
            for (auto &[pipe_id, bw]: temp_links.noc0_in_west->getMappedPipes()){
                cout << pipe_id << ",";
            }
            cout << endl;
        }
    }
}

YAML::Emitter& operator << (YAML::Emitter& out, const std::unordered_map<long unsigned int, float>& map) {
    out << YAML::BeginMap;
    for (const auto & it: map) {
        out << YAML::Key << it.first;
        out << YAML::Value << it.second;
    }
    out << YAML::EndMap;
    return out;
}


YAML::Emitter& operator << (YAML::Emitter& out, const std::shared_ptr<Link>& l) {
    if(l.get() == nullptr) {
        out << YAML::BeginMap;
        out << YAML::Key << "num_occupants";
        out << YAML::Value << 0;
        out << YAML::Key << "total_data_in_bytes";
        out << YAML::Value << 0;
        out << YAML::Key << "mapped_pipes";
        out << YAML::Value << YAML::BeginMap << YAML::EndMap;
        out << YAML::Key << "max_link_bw";
        out << YAML::Value << 0;
        out << YAML::EndMap;
        return out;
    }
	out << YAML::BeginMap;
    out << YAML::Key << "num_occupants";
    out << YAML::Value << l->getNumOccupants();
    out << YAML::Key << "total_data_in_bytes";
    out << YAML::Value << l->getTotalData();
    out << YAML::Key << "mapped_pipes";
    out << YAML::Value << l->getMappedPipes();
    out << YAML::Key << "max_link_bw";
    out << YAML::Value << l->getTotalCapacity();
    out << YAML::EndMap;
	return out;
}

YAML::Emitter& operator << (YAML::Emitter& out, const Node::NodeLinks& nl) {
    out << YAML::Key << "noc0_in_north";
    out << YAML::Value << nl.noc0_in_north;
    out << YAML::Key << "noc0_out_south";
    out << YAML::Value << nl.noc0_out_south;
    out << YAML::Key << "noc0_in_west";
    out << YAML::Value << nl.noc0_in_west;
    out << YAML::Key << "noc0_out_east";
    out << YAML::Value << nl.noc0_out_east;
    out << YAML::Key << "noc1_in_south";
    out << YAML::Value << nl.noc1_in_south;
    out << YAML::Key << "noc1_out_north";
    out << YAML::Value << nl.noc1_out_north;
    out << YAML::Key << "noc1_in_east";
    out << YAML::Value << nl.noc1_in_east;
    out << YAML::Key << "noc1_out_west";
    out << YAML::Value << nl.noc1_out_west;
	return out;
}

YAML::Emitter& operator<<(YAML::Emitter& out, const GridLoc& loc) {
    out << YAML::BeginSeq;
    out << loc.x;
    out << loc.y;        
    out << YAML::EndSeq;
    return out;
}

YAML::Emitter& operator << (YAML::Emitter& out, const std::shared_ptr<Node>& n) {
	out << YAML::BeginMap;
    out << YAML::Key << "location";
    out << YAML::Value << n->soc_location;
    out << YAML::Key << "type";
    out << YAML::Value << n->node_type;
    if(n->node_type == "dram") {
        auto dram_node = std::static_pointer_cast<DramNode>(n);
        out << YAML::Key << "dram_channel";
        out << YAML::Value << dram_node->channel;
        out << YAML::Key << "dram_subchannel";
        out << YAML::Value << dram_node->subchannel;
    }
    out << YAML::Key << "op_name";
    std::string op_name = n->mapped_op != nullptr ? n->mapped_op->name : "";
    out << YAML::Value << op_name;
    out << YAML::Key << "op_cycles";
    std::string op_cycles = n->mapped_op != nullptr ? std::to_string(n->mapped_op->estimated_cycles) : "";
    out << YAML::Value << op_cycles;
    out << YAML::Key << "links";
    out << YAML::Value;
     	out << YAML::BeginMap;
        out << YAML::Value << n->external_links;
        out << YAML::Key << "noc0_link_in";
        out << YAML::Value << n->noc0_link_in;
        out << YAML::Key << "noc0_link_out";
        out << YAML::Value << n->noc0_link_out;
        out << YAML::Key << "noc1_link_in";
        out << YAML::Value << n->noc1_link_in;
        out << YAML::Key << "noc1_link_out";
        out << YAML::Value << n->noc1_link_out;
        if(n->node_type == "eth") {
            auto ethernet_node = std::static_pointer_cast<EthNode>(n);
            out << YAML::Key << "from_ethernet";
            out << YAML::Value << ethernet_node->from_ethernet;
            out << YAML::Key << "to_ethernet";
            out << YAML::Value << ethernet_node->to_ethernet;
        }
        if(n->node_type == "pcie") {
            auto pcie_node = std::static_pointer_cast<PcieNode>(n);
            out << YAML::Key << "noc0_noc2axi";
            out << YAML::Value << pcie_node->noc0_noc2axi;
            out << YAML::Key << "noc1_noc2axi";
            out << YAML::Value << pcie_node->noc1_noc2axi;
            out << YAML::Key << "pcie_inout";
            out << YAML::Value << pcie_node->pcie_inout;
        }
        out << YAML::EndMap;
    out << YAML::EndMap;
	return out;
}

YAML::Emitter& operator<<(YAML::Emitter& out, const std::shared_ptr<gs_dram_channel> dram_channel) {
    out << YAML::BeginMap;
    out << YAML::Key << "channel_id";
    out << YAML::Value << dram_channel->channel_id;
    out << YAML::Key << "subchannels";
    out << YAML::Value;
    out << YAML::BeginSeq;
        out << YAML::BeginMap;
        out << YAML::Key << "noc0_noc2axi";
        out << YAML::Value << dram_channel->noc0_noc2axi;
        out << YAML::Key << "noc1_noc2axi";
        out << YAML::Value << dram_channel->noc1_noc2axi;
        out << YAML::EndMap;
    out << YAML::EndSeq;

    out << YAML::Key << "dram_inout";
    out << YAML::Value << dram_channel->dram_inout;
    out << YAML::EndMap;
	return out;
}

YAML::Emitter& operator<<(YAML::Emitter& out, const std::shared_ptr<wh_dram_channel> dram_channel) {
    out << YAML::BeginMap;
    out << YAML::Key << "channel_id";
    out << YAML::Value << dram_channel->channel_id;
    out << YAML::Key << "subchannels";
    out << YAML::Value;
    out << YAML::BeginSeq;
    for (const auto & sc: dram_channel->noc2axi) {
        out << YAML::BeginMap;
        out << YAML::Key << "noc0_noc2axi";
        out << YAML::Value << sc.at(0);
        out << YAML::Key << "noc1_noc2axi";
        out << YAML::Value << sc.at(1);
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "dram0_inout";
    out << YAML::Value << dram_channel->dram0_inout;
    out << YAML::Key << "dram1_inout";
    out << YAML::Value << dram_channel->dram1_inout;
    out << YAML::EndMap;
	return out;
}

YAML::Emitter& operator<<(YAML::Emitter& out, const std::shared_ptr<DramInternal> dram_channel) {
    if(std::dynamic_pointer_cast<gs_dram_channel>(dram_channel) != nullptr) {
        auto c = std::dynamic_pointer_cast<gs_dram_channel>(dram_channel);
        out << c;
    }
    else if (std::dynamic_pointer_cast<wh_dram_channel>(dram_channel) != nullptr) {
        auto c = std::dynamic_pointer_cast<wh_dram_channel>(dram_channel);
        out << c;
    }
    return out;
}

void Chip::outputYaml(const std::string &filename, int chip_id) const {
    std::ofstream ostrm(filename, std::ios::binary);
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "arch";
    out << YAML::Value << this->arch_name;
    out << YAML::Key << "chip_id";
    out << YAML::Value << chip_id;
    out << YAML::Key << "slowest_op_cycles";
    out << YAML::Value << this->getLongestOpCycles();
    out << YAML::Key << "bw_limited_op_cycles";
    out << YAML::Value << this->getBwLimitedOpCycles();
    out << YAML::Key << "nodes";
    out << YAML::Value;
    out << YAML::BeginSeq;
    for (const auto & it0: _nodes) {
        for (const auto & it1: it0.second) {
            out << it1.second;
        }
    }
    out << YAML::EndSeq;
    out << YAML::Key << "dram_channels";
    out << YAML::Value;
        out << YAML::BeginSeq;
        for (const auto & dram_channel: this->dram_internals) {
            out << dram_channel;
        }
        out << YAML::EndSeq;
    out << YAML::EndMap;
    ostrm << out.c_str();
}

void Chip::mapGrid(std::string grid_name, std::shared_ptr<Grid> grid) {
    grids_by_name[grid_name] = grid;
    grid->map(*this);
}

std::shared_ptr<Link> Chip::getWorstLink(int longest_op_cycles) const {
    std::vector<std::shared_ptr<Link>> links_sorted_by_percent_capacity = this->links_sorted_by_percent_capacity;
    std::sort(links_sorted_by_percent_capacity.begin(), links_sorted_by_percent_capacity.end(),
        [longest_op_cycles](auto lhs, auto rhs) { 
            return lhs->getPercentCapacity(longest_op_cycles) > rhs->getPercentCapacity(longest_op_cycles);
        }
    );

    return links_sorted_by_percent_capacity.front();
}

std::vector<std::shared_ptr<Link>> Chip::getWorstLinks(int longest_op_cycles, unsigned int num_links) {
    std::sort(links_sorted_by_percent_capacity.begin(), links_sorted_by_percent_capacity.end(),
        [longest_op_cycles](auto lhs, auto rhs) { 
            return lhs->getPercentCapacity(longest_op_cycles) > rhs->getPercentCapacity(longest_op_cycles);
        }
    );
    auto end = links_sorted_by_percent_capacity.size() <= num_links ? links_sorted_by_percent_capacity.end() : links_sorted_by_percent_capacity.begin() + num_links;
    return std::vector<std::shared_ptr<Link>>(links_sorted_by_percent_capacity.begin(), end);
}

std::shared_ptr<Grid> Chip::getLongestOp(){
    int longest_op_cycles = 1;
    std::shared_ptr<Grid> longest_op;

    for (auto & [grid_name, grid]: grids_by_name){
        //cout << "grid: " << grid_name << endl;
        if (auto op = dynamic_cast<Op*>(grid.get())) {
            if(op->estimated_cycles > longest_op_cycles) {
                longest_op_cycles = op->estimated_cycles;
                longest_op = grid;
            }
        }
    }
    assert(longest_op_cycles > 0);
    return longest_op;
}

int Chip::getLongestOpCycles() const {
    int longest_op_cycles = 0;
    std::shared_ptr<Grid> longest_op;

    for (auto & [grid_name, grid]: grids_by_name){
        //cout << "grid: " << grid_name << endl;
        if (auto op = dynamic_cast<Op*>(grid.get())) {
            if(op->estimated_cycles > longest_op_cycles) {
                longest_op_cycles = op->estimated_cycles;
                longest_op = grid;
            }
        }
    }
    return longest_op_cycles;
}

int Chip::getBwLimitedOpCycles() const {
    int longest_op_cycles = getLongestOpCycles();
    
    if(longest_op_cycles < 1) {
        return 0;
    }

    auto worst_link = getWorstLink(longest_op_cycles);
    if(worst_link->getPercentCapacity(longest_op_cycles) > 1.0) {
        longest_op_cycles = worst_link->getPercentCapacity(longest_op_cycles) * longest_op_cycles;
    }
    
    return longest_op_cycles;
}

void Chip::report() {
/*
    auto longest_op = getLongestOp();
    int longest_op_cycles = longest_op->cyclesPerTensor();
    cout << "longest op: " << longest_op->name << " cycles: " << longest_op_cycles << endl;
    this->slowest_op_cycles = longest_op_cycles;

    auto worst_link = getWorstLink(longest_op_cycles);
    cout << "worst link: cap: " << worst_link->getPercentCapacity(longest_op_cycles) << " capacity/cycle: " << worst_link->getTotalCapacity()<< endl;
    for (auto & [pipe_id, bw]: worst_link->getMappedPipes()){
        cout << pipe_id << ": " << bw << endl;
    }
    cout << endl;

    auto worst_links = getWorstLinks(longest_op_cycles, 10);
    cout << "Top 10 Worst links:" << endl;
    for (auto & link: worst_links){
        cout << link->getName() << ": " << link->getPercentCapacity(longest_op_cycles) << " capacity; " << link->getNumOccupants() << " pipes; " << link->getTotalData() << " Bytes agregate" << endl;
    }
    cout << endl;

    if(worst_link->getPercentCapacity(longest_op_cycles) > 1.0) {
        longest_op_cycles = worst_link->getPercentCapacity(longest_op_cycles) * longest_op_cycles;
    }
    
    cout << "CPT: " << longest_op_cycles << endl;
    this->bw_limited_op_cycles = longest_op_cycles;
    */
}

}

