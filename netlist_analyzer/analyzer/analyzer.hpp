// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <set>

#include "chip.hpp"
#include "queue.hpp"
#include "common/analyzer_api_types.hpp"
#include "op.hpp"
#include "util_types.h"

namespace analyzer {

//! Top-Level Implementation of Analyzer class -- Holds the state information of current state

class AnalyzerImpl {
   public:
    AnalyzerImpl();
    ~AnalyzerImpl(){};

    void assign_grid(GridConfig grid_config, int chip_id);
    void assign_edge(EdgeConfig edge_config, int chip_id);
    void place_chip(int chip_id);
    void load_pipes_for_chip(int chip_id, const std::string &build_dir_path);
    void route_chip(int chip_id);
    void test_chip(int chip_id);
    void analyze_chip(int chip_id);
    void serialize_chip(int chip_id, const std::string& filename);

   private:
    bool m_initialized = false;
    std::unordered_map<int, std::unordered_map<std::string, std::shared_ptr<Grid>>> chip_id_to_grids;
    std::unordered_map<int, Chip> chip_id_to_chip; // FIXME: Can remove once map functions moved to mapper API
    std::unordered_map<int, std::set<std::pair<std::string, std::string>>> chip_id_to_grid_pairs;
};

}  // namespace analyzer