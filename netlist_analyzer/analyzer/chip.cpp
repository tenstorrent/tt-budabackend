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

Chip::Chip(std::string arch, uint32_t harvesting_mask) {
    this->arch_name = arch;

    if (arch == "grayskull") {
        createFromYaml("device/grayskull_10x12.yaml", harvesting_mask);
    } else if (arch == "wormhole_b0") {
        createFromYaml("device/wormhole_b0_8x10.yaml", harvesting_mask);
    } else {
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

uint32_t Chip::get_harvested_noc_rows(uint32_t harvesting_mask) {
    std::vector<uint32_t> harv_to_noc_loc;
    if (this->arch_name == "grayskull") {
        harv_to_noc_loc = {5, 7, 4, 8, 3, 9, 2, 10, 1, 11 };
    } else if (this->arch_name == "wormhole_b0") {
        harv_to_noc_loc = {11, 1, 10, 2, 9, 3, 8, 4, 7, 5 };
    }
    uint32_t harv_noc_rows = 0;

    for (size_t pos=0; pos<harv_to_noc_loc.size(); ++pos) {
        bool is_row_harvested = harvesting_mask & 0x1;
        if (is_row_harvested) {
            harv_noc_rows |= (1 << harv_to_noc_loc[pos]);
        }
        harvesting_mask = harvesting_mask >> 1;
    }
    return harv_noc_rows;
}

void Chip::createFromYaml(std::string path_of_device_descriptor_file, uint32_t harvesting_mask) {
    std::ifstream fdesc(path_of_device_descriptor_file);
    if (fdesc.fail()) {
        throw std::runtime_error("Error: device descriptor file " + path_of_device_descriptor_file + " does not exist!");
    }
    fdesc.close();

    YAML::Node device_descriptor_yaml = YAML::LoadFile(path_of_device_descriptor_file);
    std::string arch_name_value = device_descriptor_yaml["arch_name"].as<std::string>();
    std::transform(arch_name_value.begin(), arch_name_value.end(), arch_name_value.begin(), ::tolower); // convert to lowercase
    if (this->arch_name != arch_name_value) {
        log_fatal("Arch name mismatch! Expect {}, found {} in device descriptor.", this->arch_name, arch_name_value);
    }

    this->grid_size.x = device_descriptor_yaml["grid"]["x_size"].as<int>(); // 10 for wh, 13 for gs
    this->grid_size.y = device_descriptor_yaml["grid"]["y_size"].as<int>(); // 12 for wh/gs

    std::vector<std::vector<CoreType>> type_vec(this->grid_size.x, std::vector<CoreType>(this->grid_size.y, CoreType::UNKNOWN));

    auto pcie_cores_yaml = device_descriptor_yaml["pcie"].as<std::vector<std::string>>();
    for (const auto &core_string : pcie_cores_yaml) {
        auto coord = GridLoc::from_str(core_string);
        type_vec[coord.x][coord.y] = CoreType::PCIE;
    }

    auto arc_cores_yaml = device_descriptor_yaml["arc"].as<std::vector<std::string>>();
    for (const auto &core_string : arc_cores_yaml) {
        auto coord = GridLoc::from_str(core_string);
        type_vec[coord.x][coord.y] = CoreType::ARC;
    }

    auto router_only_cores_yaml = device_descriptor_yaml["router_only"].as<std::vector<std::string>>();
    for (const auto &core_string : router_only_cores_yaml) {
        auto coord = GridLoc::from_str(core_string);
        type_vec[coord.x][coord.y] = CoreType::ROUTER_ONLY;
    }

    auto eth_cores_yaml = device_descriptor_yaml["eth"].as<std::vector<std::string>>();
    std::vector<GridLoc> eth_cores;
    for (const auto &core_string : eth_cores_yaml) {
        auto coord = GridLoc::from_str(core_string);
        type_vec[coord.x][coord.y] = CoreType::ETH;
        eth_cores.push_back(GridLoc(coord.y, coord.x));
        //core_descriptor.l1_size = eth_l1_size; TODO need this?
    }

    int current_dram_channel = 0;
    std::vector<std::vector<GridLoc>> dram_cores;
    for (auto channel_it = device_descriptor_yaml["dram"].begin(); channel_it != device_descriptor_yaml["dram"].end(); ++channel_it) {
        dram_cores.push_back({});
        const auto &dram_cores_yaml = (*channel_it).as<std::vector<std::string>>();
        for (unsigned int i = 0; i < dram_cores_yaml.size(); i++) {
            const auto &dram_core = dram_cores_yaml.at(i);
            auto coord = GridLoc::from_str(dram_core);
            type_vec[coord.x][coord.y] = CoreType::DRAM;
            dram_cores.back().push_back(GridLoc(coord.y, coord.x));
        }
        current_dram_channel++;
    }

    // Skipping this part since default soc descriptor doesn't have it
    /*
    auto harvested_cores_yaml = device_descriptor_yaml["harvested_workers"].as<std::vector<std::string>>();
    for (const auto &core_string : harvested_cores_yaml) {
        auto coord = GridLoc::from_str(core_string);
        type_vec[coord.x][coord.y] = CoreType::HARVESTED;
    }
    */

    auto worker_cores_yaml = device_descriptor_yaml["functional_workers"].as<std::vector<std::string>>();
    for (const auto &core_string : worker_cores_yaml) {
        auto coord = GridLoc::from_str(core_string);
        type_vec[coord.x][coord.y] = CoreType::WORKER;
    }

    // device_desc_runtime yaml files don't have router_only nodes.
    // We assume all other nodes are present, so what's left are router_only nodes.
    // Skipping this part since it doesn't apply to default soc descriptor
    /*
    int num_of_unknown = 0;
    for (int i = 0; i < this->grid_size.x; i++) {
        for (int j = 0; j < this->grid_size.y; j++) {
            if (type_vec[i][j] == CoreType::UNKNOWN) {
                log_warning(tt::LogAnalyzer, "Unknown core at {}-{}, assuming it's router_only.", i, j);
                type_vec[i][j] = CoreType::ROUTER_ONLY;
                num_of_unknown++;
            }
        }
    }
    assert(num_of_unknown <= 4);
    */

    uint32_t harvested_rows = get_harvested_noc_rows(harvesting_mask);
    //std::cout << "harvesting_mask = " << harvesting_mask << ", harvested_rows = " << harvested_rows << std::endl;

    // Extract rows to harvest from mask, taken from extract_rows_to_remove() in runtime/runtime_utils.cpp
    // Check if harvesting config is legal for GS and WH
    log_assert(!((harvested_rows & 1) || (harvested_rows & 64) || (harvested_rows & 0xFFFFF000)), "For grayskull and wormhole, only rows 1-5 and 7-11 can be harvested");
    vector<int> row_coordinates_to_remove;
    int row_coordinate = 0;
    int tmp = harvested_rows;
    while (tmp) {
        if (tmp & 1)
            row_coordinates_to_remove.push_back(row_coordinate);

        tmp = tmp >> 1;
        row_coordinate++;
    }
    /*
    std::cout << "harvested rows: ";
    for (int i: row_coordinates_to_remove) {
        std::cout << i << " ";
    }
    std::cout << std::endl;
    */

    // Create grid translation
    int row_index = 0;
    for (int j = 0; j < this->grid_size.y; j++) {
        if (std::find(row_coordinates_to_remove.begin(), row_coordinates_to_remove.end(), j) != row_coordinates_to_remove.end()) {
            for (int i = 0; i < this->grid_size.x; i++) {
                if (type_vec[i][j] == CoreType::WORKER) {
                    type_vec[i][j] = CoreType::HARVESTED;
                }
            }
            continue;
        }
        int col_index = 0;
        for (int i = 0; i < this->grid_size.x; i++) {
            if (type_vec[i][j] == CoreType::WORKER) {
                _core_to_chip_translation[row_index][col_index] = std::make_pair(j,i);
                col_index++;
            }
        }
        if (!_core_to_chip_translation[row_index].empty()) {
            row_index++;
        }
    }

    this->core_grid_size.y = _core_to_chip_translation.size();    // should be 10 for WH/GS
    this->core_grid_size.x = _core_to_chip_translation[0].size(); // should be 8 for WH, 12 for GS

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
    for (int j = 0; j < this->grid_size.y; j++) {
        for (int i = 0; i < this->grid_size.x; i++) {
            int j_above = (j + this->grid_size.y - 1) % this->grid_size.y;
            int j_below = (j + 1) % this->grid_size.y;
            int i_left = (i + this->grid_size.x - 1) % this->grid_size.x;
            int i_right = (i + 1) % this->grid_size.x;

            Node::NodeLinks temp_links;
            temp_links.noc1_out_north = outgoing_links[j][i][0];
            temp_links.noc0_out_east  = outgoing_links[j][i][1];
            temp_links.noc0_out_south = outgoing_links[j][i][2] ;
            temp_links.noc1_out_west  = outgoing_links[j][i][3];

            temp_links.noc0_in_north  = outgoing_links[j_above][i][2]; // south from above
            temp_links.noc1_in_east   = outgoing_links[j][i_right][3]; // west from the right
            temp_links.noc1_in_south  = outgoing_links[j_below][i][0]; // north from below
            temp_links.noc0_in_west   = outgoing_links[j][i_left][1]; // east from the left

            temp_links.noc1_out_north->setName("noc1_north_" + to_string(j) + "_to_" + to_string(j_above) + "_y_" + to_string(i) + "_x");
            temp_links.noc0_out_east->setName("noc0_east_" + to_string(j) + "_y_" + to_string(i) + "_to_" + to_string(i_right) + "_x");
            temp_links.noc0_out_south->setName("noc0_south_" + to_string(j) + "_to_" + to_string(j_below) + "_y_" + to_string(i) + "_x");
            temp_links.noc1_out_west->setName("noc1_west_" + to_string(j) + "_y_" + to_string(i) + "_to_" + to_string(i_left) + "_x");

            // Router Nodes (+ ARC)
            if (type_vec[i][j] == CoreType::ROUTER_ONLY || type_vec[i][j] == CoreType::ARC) {
                _nodes[j][i] = std::make_shared<RouterNode>(GridLoc(j, i), temp_links);
                continue;
            }

            // DRAM Nodes
            if (type_vec[i][j] == CoreType::DRAM) {
                _nodes[j][i] = std::make_shared<DramNode>(GridLoc(j,i), temp_links);
                continue;
            }

            // Eth Nodes
            if (type_vec[i][j] == CoreType::ETH) {
                _nodes[j][i] = std::make_shared<EthNode>(GridLoc(j,i), temp_links);
                continue;
            }

            // PCIe Node
            if (type_vec[i][j] == CoreType::PCIE) {
                // TODO: SHould this be a DramNode? New special type?
                _nodes[j][i] = std::make_shared<PcieNode>(GridLoc(j,i), temp_links);
                _pcie_node = _nodes[j][i]; // pcie node
                continue;
            }

            // Harvested Nodes
            if (type_vec[i][j] == CoreType::HARVESTED) {
                _nodes[j][i] = std::make_shared<CoreNode>(GridLoc(j,i), temp_links, true); // harvested
                continue;
            }

            // Core Nodes
            _nodes[j][i] = std::make_shared<CoreNode>(GridLoc(j,i), temp_links);
        }
    }

    // grayskull doesn't have eth cores so this is effectively skipped
    for (size_t i = 0; i < eth_cores.size(); i++) {
        _eth_nodes[i] = _nodes[eth_cores[i].y][eth_cores[i].x];
    }

    // Dram channel and subchannel mapping
    for (size_t channel_id = 0; channel_id < dram_cores.size(); channel_id++) {
        for (size_t i = 0; i < dram_cores[channel_id].size(); i++) {
            _dram_nodes[channel_id][i] = _nodes[dram_cores[channel_id][i].y][dram_cores[channel_id][i].x];
        }
        if (arch_name_value == "wormhole_b0") {
            dram_internals.push_back(make_shared<wh_dram_channel>(channel_id));
        } else if (arch_name_value == "grayskull") {
            dram_internals.push_back(make_shared<gs_dram_channel>(channel_id));
        }
        registerLinks(dram_internals.at(channel_id)->getInternalLinks());
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
    if (n->node_type == "core") {
        auto core_node = std::static_pointer_cast<CoreNode>(n);
        if (core_node->is_harvested) {
            out << YAML::Key << "harvested";
            out << YAML::Value << core_node->is_harvested;
        }
    }
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

