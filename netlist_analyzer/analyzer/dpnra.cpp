// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <memory>

#include "inc/dpnra.hpp"

#include "chip.hpp"
#include "op.hpp"
#include "queue.hpp"


#include "utils/logger.hpp"
#include "pipe_inference.hpp"
#include "pipe_mapper.hpp"

#include "pipegen_yaml_loader.hpp"

using namespace analyzer;

namespace dpnra {

Analyzer::Analyzer(std::string arch) {
    this->arch = arch;
}

void Analyzer::assign_grid(GridConfig grid_config, int chip_id) {
    log_debug(tt::LogAnalyzer, "Assigning Grid: {}", grid_config);
    // Base case --> new chip or new grid vector
    if (chip_id_to_grids.find(chip_id) == chip_id_to_grids.end()) {
        chip_id_to_grids.insert({chip_id, {}});  // Create a new chip_id
    }
    // create grid object
    log_assert(
        chip_id_to_grids.at(chip_id).find(grid_config.name) == chip_id_to_grids.at(chip_id).end(),
        "grid={} being placed must be a unique name/identifier",
        grid_config.name);
    if (grid_config.type == GridType::Op) {
        log_assert(
            grid_config.op_type != "invalid",
            "Found invalid op type from grid_config={}", 
            grid_config
        );
        auto op = std::make_shared<Op>(OpParams{
            .name = grid_config.name,
            .type = grid_config.op_type,
            .grid_size_y = grid_config.shape.y,
            .grid_size_x = grid_config.shape.x,
            .grid_loc_y = grid_config.loc.y,
            .grid_loc_x = grid_config.loc.x,
            .grid_transpose = grid_config.grid_transpose,
            .estimated_cycles = grid_config.estimated_cycles,
        });
        chip_id_to_grids.at(chip_id).insert({grid_config.name, std::static_pointer_cast<Grid>(op)});
    } else if (grid_config.type == GridType::Queue) {
        auto queue = std::make_shared<Queue>(QueueParams{
            .name = grid_config.name,
            .grid_size_y = grid_config.shape.y,
            .grid_size_x = grid_config.shape.x,
            .dram_channels = grid_config.dram_channels,
        });
        chip_id_to_grids.at(chip_id).insert({grid_config.name, std::static_pointer_cast<Grid>(queue)});
    } else {
        log_fatal("Unsupported grid_config={} in place_grid", grid_config);
    }
};
void Analyzer::assign_edge(EdgeConfig edge_config, int chip_id) {
    log_debug(tt::LogAnalyzer, "Assigning Edge: {}", edge_config);
    // Base case --> new chip or new edge vector
    if (chip_id_to_grid_pairs.find(chip_id) == chip_id_to_grid_pairs.end()) {
        chip_id_to_grid_pairs.insert({chip_id, {}});
    }
    chip_id_to_grid_pairs.at(chip_id).insert({
        edge_config.input,
        edge_config.output,
    });
}

void Analyzer::place_chip(int chip_id) {
    log_debug(tt::LogAnalyzer, "Placing Chip: {}", chip_id);
    // Place all the grids onto the chip
    chip_id_to_chip.insert({chip_id, Chip(this->arch)});

    log_assert(
        chip_id_to_grids.find(chip_id) != chip_id_to_grids.end(), "Cannot find any grids for chip_id={}", chip_id);

    for (const auto& [grid_name, grid] : chip_id_to_grids.at(chip_id)) {
        //chip_id_to_grids.at(chip_id).at(grid.first)->map(chip_id_to_chip.at(chip_id));
        chip_id_to_chip.at(chip_id).mapGrid(grid_name, grid);
    }
}

void Analyzer::load_pipes_for_chip(int chip_id, const std::string &pipegen_yaml_path) {
    log_debug(tt::LogAnalyzer, "Loading Pipes for Chip: {}", chip_id);

     log_assert(
            chip_id_to_chip.find(chip_id) != chip_id_to_chip.end(),
            "Cannot find chip_id={}",
            chip_id);

    auto & chip = chip_id_to_chip.at(chip_id);
    // Load pipes
    log_info(tt::LogAnalyzer, "Loading pipegen.yaml for chip_id: {}: {}", chip_id, pipegen_yaml_path);
    EpochPipes epoch_pipes = load_pipegen_yaml(chip, pipegen_yaml_path);
    
    // map Pipes
    for (auto &p : epoch_pipes.pipes) {
        mapGenericPipe(chip, p.get());
    }
}

void Analyzer::load_pipes_for_chips(const std::string &pipegen_yaml_path) {
    log_debug(tt::LogAnalyzer, "Loading Pipes for Chips");

    // Load pipes
    //log_info(tt::LogAnalyzer, "Loading pipegen.yaml for chip_id: {}: {}", chip_id, pipegen_yaml_path);
    auto & arch_chip = chip_id_to_chip.at(0); // TODO: this assumes all chips are identical archs
    EpochPipes epoch_pipes = load_pipegen_yaml(arch_chip, pipegen_yaml_path); // TODO: this assumes all chips are identical archs
    
    // map Pipes
    for (auto &p : epoch_pipes.pipes) {
        auto & chip = chip_id_to_chip.at(p->chip_location);
        mapGenericPipe(chip, p.get());
    }

    // map ethernet Pipes
    for (auto &p : epoch_pipes.ethernet_pipes) {
        auto & input_chip = chip_id_to_chip.at(p->input_chip_id);
        mapEthernetPipe(input_chip, p->input_chip_id, p.get());
        if(p->input_chip_id != p->output_chip_id) { // Avoid double mapping
            auto & output_chip = chip_id_to_chip.at(p->output_chip_id);
            mapEthernetPipe(output_chip, p->output_chip_id, p.get());
        }
    }
}

void Analyzer::route_chip(int chip_id) {
    log_debug(tt::LogAnalyzer, "Routing Chip: {}", chip_id);
    // Route all the edges onto the chip
    if (chip_id_to_chip.find(chip_id) == chip_id_to_chip.end()) {
        chip_id_to_chip.insert({chip_id, Chip(this->arch)});
    }
    log_assert(
        chip_id_to_grid_pairs.find(chip_id) != chip_id_to_grid_pairs.end(),
        "Cannot find any edges for chip_id={}",
        chip_id);

    std::vector<Pipe*> pipes;
    for (const auto& grid_pair : chip_id_to_grid_pairs.at(chip_id)) {
        // Find grids
        log_assert(
            chip_id_to_grids.find(chip_id) != chip_id_to_grids.end(), "Cannot find any grids for chip_id={}", chip_id);

        log_assert(
            chip_id_to_grids.at(chip_id).find(grid_pair.first) != chip_id_to_grids.at(chip_id).end(),
            "Cannot find input={} in chip_id={}",
            grid_pair.first,
            chip_id);
        log_assert(
            chip_id_to_grids.at(chip_id).find(grid_pair.second) != chip_id_to_grids.at(chip_id).end(),
            "Cannot find output={} in chip_id={}",
            grid_pair.second,
            chip_id);
        // Create pipes
        auto current_pipes = routeData(
            chip_id_to_grids.at(chip_id).at(grid_pair.first).get(),
            chip_id_to_grids.at(chip_id).at(grid_pair.second).get());
        pipes.insert(pipes.begin(), current_pipes.begin(), current_pipes.end());
    }
    // map Pipes
    for (auto& p : pipes) {
        mapPipe(chip_id_to_chip.at(chip_id), p);
    }
}
void Analyzer::test_chip(int chip_id) {
    log_info(tt::LogAnalyzer, "Testing Chip: {}", chip_id);
    log_assert(
        chip_id_to_chip.find(chip_id) != chip_id_to_chip.end(), "Cannot find chip_id={}", chip_id);
    chip_id_to_chip.at(chip_id).printLinks();
}

void Analyzer::analyze_chip(int chip_id) { 
    log_info(tt::LogAnalyzer, "Analyzing Chip: {}", chip_id);
    log_assert(
        chip_id_to_chip.find(chip_id) != chip_id_to_chip.end(), "Cannot find chip_id={}", chip_id);
    chip_id_to_chip.at(chip_id).report();
};
void Analyzer::serialize_chip(int chip_id, const std::string& filename) { 
    log_info(tt::LogAnalyzer, "Serializing Chip: {}", chip_id);
    log_assert(
        chip_id_to_chip.find(chip_id) != chip_id_to_chip.end(), "Cannot find chip_id={}", chip_id);
    chip_id_to_chip.at(chip_id).outputYaml(filename, chip_id);
};

}  // namespace dpnra
