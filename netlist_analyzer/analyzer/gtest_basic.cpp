// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
// gtests
#include <gtest/gtest.h>
#include <memory>

#include "chip.hpp"
#include "queue.hpp"
#include "op.hpp"

#include "pipegen_yaml_loader.hpp"

#include "pipe_inference.hpp"
#include "pipe_mapper.hpp"

using namespace analyzer;

TEST(BasicSuite, DISABLED_CreateGS) {
    Chip* c = new Chip("grayskull");
    printf("%llX\n", (unsigned long long) c);
    delete c;  
}

namespace {
void test_map(Chip & chip) {

    // DRAM -> Eltwise -> DRAM
    // Create Grids
    Queue input_queue(
        {
            .name = "input_queue",
            .grid_size_y = 2,
            .grid_size_x = 2,
            .dram_channels = {0, 1, 2, 3},
        }
    );

    Queue output_queue(
        {
            .name = "output_queue",
            .grid_size_y = 2,
            .grid_size_x = 2,
            .dram_channels = {4, 5, 6, 7},
        }
    );
    Op unary_op(
        {
            .name = "unary_datacopy",
            .type = "datacopy",
            .grid_size_y = 2,
            .grid_size_x = 2,
            .grid_loc_y = 0,
            .grid_loc_x = 0,
        }
    );

    // dram io queue type
    
    // map Grids
    input_queue.map(chip);
    output_queue.map(chip);
    unary_op.map(chip);

    // Create pipes
    auto pipes0 = routeData(&input_queue, &unary_op);
    auto pipes1 = routeData(&unary_op, &output_queue);

    std::vector<Pipe*> pipes;
    pipes.insert(pipes.begin(), pipes0.begin(), pipes0.end());
    pipes.insert(pipes.end(), pipes1.begin(), pipes1.end());

    // map Pipes
    for (auto &p : pipes) {
        mapPipe(chip, p);
    }

    //chip.printLinks();
}
}

TEST(BasicSuite, DramUnaryDram) {
    Chip c = Chip("grayskull");
    test_map(c);
}

TEST(BasicSuite, DramUnaryDramOutputYaml) {
    Chip test_chip = Chip("grayskull");
    test_map(test_chip);

    test_chip.outputYaml("analyzer_output.yaml", 0);
}

TEST(BasicSuite, PipegenYamlRead) {
    Chip c = Chip("grayskull");

    // Load pipes
    EpochPipes epoch_pipes = load_pipegen_yaml(c, "./netlist_analyzer/tests/pipegen_yamls/dram_unary_dram_pipegen.yaml");
    
    // map Pipes
    for (auto &p : epoch_pipes.pipes) {
        mapPipe(c, p);
    }

    // c.printLinks();
}

TEST(BasicSuite, YamlReadOutputYamlReport) {
    Chip c = Chip("grayskull");

    // Load pipes
    EpochPipes epoch_pipes = load_pipegen_yaml(c, "./netlist_analyzer/tests/pipegen_yamls/dram_unary_dram_pipegen.yaml");
    
    // map Pipes
    for (auto &p : epoch_pipes.pipes) {
        mapPipe(c, p);
        //std::cout << p;
    }

    c.outputYaml("analyzer_output.yaml", 0);
}

TEST(BasicSuite, DramMatmulDramMcast) {
    Chip c = Chip("grayskull");

    // Load pipes
    EpochPipes epoch_pipes = load_pipegen_yaml(c, "./netlist_analyzer/tests/pipegen_yamls/dram_matmul_dram_pipegen.yaml");
    
    // map Pipes
    for (auto &p : epoch_pipes.pipes) {
        mapGenericPipe(c, p.get());
        //std::cout << p;
    }

    c.outputYaml("analyzer_output.yaml", 0);
}
