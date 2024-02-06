// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include "analyzer_api.hpp"

#include "netlist_analyzer/analyzer/analyzer.hpp"

namespace analyzer {
namespace api {
void run_per_core_checks(Analyzer pAnalyzer){};
void run_grid_checks(Analyzer pAnalyzer){};
Analyzer::Analyzer() { impl_ = std::make_shared<AnalyzerImpl>(analyzer::AnalyzerImpl()); }

void Analyzer::assign_grid(GridConfig grid_config, int chip_id) { impl_->assign_grid(grid_config, chip_id); }
void Analyzer::assign_edge(EdgeConfig edge_config, int chip_id) { impl_->assign_edge(edge_config, chip_id); }

void Analyzer::place_chip(int chip_id) { impl_->place_chip(chip_id); }
void Analyzer::load_pipes_for_chip(int chip_id, const std::string &pipegen_yaml_path) { impl_->load_pipes_for_chip(chip_id, pipegen_yaml_path); }
void Analyzer::route_chip(int chip_id) { impl_->route_chip(chip_id); }

void Analyzer::test_chip(int chip_id) { impl_->test_chip(chip_id); }
void Analyzer::analyze_chip(int chip_id) { impl_->analyze_chip(chip_id); }
void Analyzer::serialize_chip(int chip_id, const std::string& filename) { impl_->serialize_chip(chip_id, filename); }
}  // namespace api
}  // namespace analyzer
