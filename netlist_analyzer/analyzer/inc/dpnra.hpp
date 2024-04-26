// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <unordered_map>
#include <set>

#include "netlist_analyzer/analyzer/common/analyzer_api_types.hpp"
#include "netlist_analyzer/analyzer/chip.hpp"
#include "netlist_analyzer/analyzer/grid.hpp"

// data flow, place and route analyzer

namespace dpnra {
class Analyzer {
    public:
    Analyzer(std::string arch);
    ~Analyzer() = default;

    void assign_grid(analyzer::GridConfig grid_config, int chip_id);
    void assign_edge(analyzer::EdgeConfig edge_config, int chip_id);
    void place_chip(int chip_id);
    void load_pipes_for_chip(int chip_id, const std::string &build_dir_path);
    void load_pipes_for_chips(const std::string &pipegen_yaml_path);
    void route_chip(int chip_id);
    void test_chip(int chip_id);
    void analyze_chip(int chip_id);
    void serialize_chip(int chip_id, const std::string& filename);
    void run_per_core_checks();
    void run_grid_checks();

    private:
        std::string arch;
        std::unordered_map<int, std::unordered_map<std::string, std::shared_ptr<analyzer::Grid>>> chip_id_to_grids;
        std::unordered_map<int, analyzer::Chip> chip_id_to_chip; // FIXME: Can remove once map functions moved to mapper API
        std::unordered_map<int, std::set<std::pair<std::string, std::string>>> chip_id_to_grid_pairs;
};

}
