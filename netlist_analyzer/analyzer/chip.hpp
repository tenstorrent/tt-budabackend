// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// chip.hpp
#pragma once

#include <unordered_map>
#include <memory>
#include <string>

#include "node.hpp"
#include "link.hpp"

#include "grid.hpp"

namespace analyzer {

    class Chip {
        public:
            Chip(std::string arch);
            Chip(Chip const&) = default;
            ~Chip() = default;

            std::shared_ptr<Node> getNode(int y, int x) const;
            std::shared_ptr<Node> getNode(const GridLoc & g) const{
                return getNode(g.y, g.x);
            }
            std::shared_ptr<Node> getCoreNode(int y, int x) const;
            std::shared_ptr<Node> getCoreNode(const GridLoc & g) const {
                return getCoreNode(g.y, g.x);
            }
            std::shared_ptr<Node> getDramNode(int channel, int subchannel = 0) const;
            std::shared_ptr<Node> getEthNode(int channel) const;
            std::shared_ptr<Node> getPcieNode() const;

            //void analyze();
            void report();
            void printLinks();
            void outputYaml(const std::string &filename, int chip_id) const;
            std::shared_ptr<Link> getWorstLink(int cycles_per_input) const;
            std::vector<std::shared_ptr<Link>> getWorstLinks(int cycles_per_input, unsigned int num_links);

            void mapGrid(std::string grid_name, std::shared_ptr<Grid> grid);
            std::shared_ptr<Grid> getLongestOp();
            std::vector<std::shared_ptr<Grid>> getLongestOps(int num_ops);

            int getLongestOpCycles() const;
            int getBwLimitedOpCycles() const;

            Shape getGridSize() const {
                return grid_size;
            };

        private:

            void registerLinks(const std::shared_ptr<Link> link) {
                links_sorted_by_percent_capacity.push_back(link);
            }
            void registerLinks(const std::vector<std::shared_ptr<Link>>& links) {
                for(const auto& link: links) {
                    links_sorted_by_percent_capacity.push_back(link);
                }
            }

            std::vector<std::shared_ptr<DramInternal>> dram_internals;

            std::string arch_name;

            int slowest_op_cycles;
            int bw_limited_op_cycles;
            
            std::vector<std::shared_ptr<Link>> links_sorted_by_percent_capacity;
            std::vector<std::shared_ptr<Op>> ops_sorted_by_cycles_per_tensor;

            std::unordered_map<std::string, std::shared_ptr<Grid>> grids_by_name;

            void createDefaultGS();
            void createWHB0();

            Shape grid_size;
            Shape core_grid_size;
            std::unordered_map<int, std::unordered_map<int, std::shared_ptr<Node>>> _dram_nodes;
            std::unordered_map<int, std::shared_ptr<Node>> _eth_nodes;
            std::shared_ptr<Node> _pcie_node;
            std::unordered_map<int, std::unordered_map<int, std::shared_ptr<Node>>> _nodes;
            std::unordered_map<int, std::unordered_map<int, std::pair<int, int>>> _core_to_chip_translation;            

    };
}