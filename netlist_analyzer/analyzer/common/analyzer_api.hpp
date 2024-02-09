// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "common/analyzer_api_types.hpp"

namespace analyzer {
class AnalyzerImpl;
namespace api {
//! Top-Level Analyzer Interface class --> calls the underlying analyzer
class Analyzer {
   public:
    Analyzer();
    ~Analyzer() = default;

    void assign_grid(GridConfig grid_config, int chip_id);
    void assign_edge(EdgeConfig edge_config, int chip_id);

    void place_chip(int chip_id);
    void load_pipes_for_chip(int chip_id, const std::string &pipegen_yaml_path); //  { impl_->load_pipes_for_chip(chip_id, pipegen_yaml_path); };
    void route_chip(int chip_id);
    void test_chip(int chip_id); // calls printLinks
    void analyze_chip(int chip_id); // calls report
    void serialize_chip(int chip_id, const std::string& filename); // calls outputYaml

   private:
    std::shared_ptr<AnalyzerImpl> impl_;
};

void run_per_core_checks(Analyzer pAnalyzer);
void run_grid_checks(Analyzer pAnalyzer);
}  // namespace api
}  // namespace analyzer

//#include "netlist_analyzer/analyzer/analyzer.hpp"
