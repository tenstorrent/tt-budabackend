// SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
//
// SPDX-License-Identifier: Apache-2.0
#include <vector>
#include <memory>
#include <cassert>

#include "yaml-cpp/yaml.h"

#include "chip.hpp"
#include "pipe.hpp"

#include "pipegen_yaml_loader.hpp"

namespace analyzer {

 EpochPipes load_pipegen_yaml(Chip& c, std::string filename) {

    EpochPipes result;

    std::vector<YAML::Node> pipegen_yaml_list = YAML::LoadAllFromFile(filename);

    struct buffer {
        std::uint64_t id;
        GridLoc core_coord;
        std::int32_t chip_id;
        std::int32_t dram_channel = -1;
        std::int32_t dram_sub_channel = -1;
        std::int32_t dram_bank = 0;
        std::int32_t ethernet_channel = -1;
        std::uint32_t num_tiles;
        std::int32_t tile_size;
        std::int32_t buffer_t_factor;
        std::int32_t prefetch_type = 0;
    };

    std::unordered_map<std::uint64_t, buffer> buffers;
    for (const auto & pipegen_yaml: pipegen_yaml_list) {
        for (const auto & it : pipegen_yaml) {
            std::string key = it.first.as<std::string>();
            auto yaml_node = it.second;
            
            // Process graph name
            if(key == "graph_name") {
                result.graph_name = it.second.as<std::string>();
                continue;
            }

            // Process buffer
            const std::string buffer_string = "buffer_";
            if(key.compare(0, buffer_string.size(), buffer_string) == 0) {
                //uint64_t buffer_id = std::strtoull(key.substr(buffer_string.length()).c_str());
                uint64_t buffer_id = yaml_node["uniqid"].as<uint64_t>();
                buffers[buffer_id].id = buffer_id;
                buffers[buffer_id].core_coord = {yaml_node["core_coordinates"][0].as<int>(), yaml_node["core_coordinates"][1].as<int>()};
                buffers[buffer_id].chip_id = yaml_node["chip_id"][0].as<int>();
                buffers[buffer_id].tile_size = yaml_node["tile_size"].as<int>();
                const int replicate = std::max(yaml_node["replicate"].as<int>(), 1);
                const int scg = yaml_node["scatter_gather_num_tiles"].as<int>();
                const int tiles_per_input =  yaml_node["tiles_per_input"] ? yaml_node["tiles_per_input"].as<int>() : 0;
                buffers[buffer_id].buffer_t_factor = tiles_per_input / (replicate * scg);
                buffers[buffer_id].num_tiles = scg;
                
                // Handle prologed DRAM
                if(yaml_node["prefetch_type"] and yaml_node["prefetch_type"].as<int>() == 1) {
                    buffers[buffer_id].prefetch_type = yaml_node["prefetch_type"].as<int>();
                }
                else {
                    buffers[buffer_id].prefetch_type = 0;
                }

                // dram_io_flag
                if(yaml_node["dram_io_flag"].as<int>() == 1) {
                    buffers[buffer_id].dram_channel = yaml_node["dram_chan"].as<int>();
                    buffers[buffer_id].dram_sub_channel = yaml_node["dram_sub_chan"].as<int>();
                    buffers[buffer_id].dram_bank = yaml_node["dram_addr"].as<uint64_t>() < 1024 * 1024 * 1024 ? 0 : 1;
                }
                // dram_io_flag_is_remote for pcie on WH
                if(yaml_node["dram_io_flag_is_remote"] and yaml_node["dram_io_flag_is_remote"].as<int>() == 1) {
                    buffers[buffer_id].dram_channel = 255; // 255 is the PCIe sentinel value, carryover from the pipegen spec for Grayskull
                }

                // ethernet flag
                if(yaml_node["ethernet_chan"]) {
                    buffers[buffer_id].ethernet_channel = yaml_node["ethernet_chan"].as<int>();
                }

                // Handle Scatter
                const bool is_scatter = yaml_node["is_scatter"].as<int>() == 1;
                if(is_scatter) { // expand buffers
                    const int num_buffers = yaml_node["replicate"].as<int>();
                    for (int b = 1; b < num_buffers; b++) {
                        const uint64_t replicated_buffer_id = buffer_id + b * scg;
                        buffers[replicated_buffer_id] = buffers[buffer_id];
                        buffers[replicated_buffer_id].id = replicated_buffer_id;
                    }
                }
                continue;
            }
        }
    }

    // Second pass to process pipes
    for (const auto & pipegen_yaml: pipegen_yaml_list) {
        for (const auto & it : pipegen_yaml) {
            std::string key = it.first.as<std::string>();
            auto yaml_node = it.second;

            // Process pipe
            const std::string pipe_string = "pipe_";
            //std::cout << "key: " << key << std::endl;
            
            if(key.compare(0, pipe_string.size(), pipe_string) == 0) {
                uint64_t pipe_id = yaml_node["id"].as<uint64_t>();
                const int pipe_periodic_repeat = yaml_node["pipe_periodic_repeat"].as<int>();
                const int pipe_consumer_repeat = yaml_node["pipe_consumer_repeat"].as<int>();
                const int incoming_noc_id = yaml_node["incoming_noc_id"].as<int>();
                const int outgoing_noc_id = yaml_node["outgoing_noc_id"].as<int>();
                const int incoming_vc = yaml_node["incoming_vc"].as<int>();
                const int outgoing_vc = yaml_node["outgoing_vc"].as<int>();
                int tile_size = -1;
                int input_t_factor = -1;
                log_assert(pipe_periodic_repeat <= 1 or pipe_consumer_repeat <= 1, "Cannot have pipe_periodic_repeat > 1 and pipe_consumer_repeat > 1");
                int pipe_repeat = std::max(pipe_periodic_repeat, pipe_consumer_repeat);

                if(yaml_node["ethernet_pipe"] and yaml_node["ethernet_pipe"].as<int>() == 1) {
                    log_assert(yaml_node["input_list"].size() == 1, "ethernet pipe must have 1 input");
                    const uint64_t input_id = yaml_node["input_list"][0].as<uint64_t>();

                    log_assert(yaml_node["output_list"].size() == 1, "ethernet pipe must have 1 output");
                    const uint64_t output_id = yaml_node["output_list"][0].as<uint64_t>();
                    

                    const int num_tiles = buffers[input_id].buffer_t_factor * buffers[input_id].num_tiles;
                    const int tile_size = buffers[input_id].tile_size;

                    const int input_chip_id  = buffers[input_id].chip_id;
                    const int input_eth_chan = buffers[input_id].ethernet_channel;
                    
                    const int output_chip_id  = buffers[output_id].chip_id;
                    const int output_eth_chan = buffers[output_id].ethernet_channel;
                    
                    result.ethernet_pipes.push_back(std::shared_ptr<EthernetPipe>(new EthernetPipe({
                        .pipe_id = pipe_id,
                        .num_tiles = num_tiles,
                        .tile_size = tile_size,
                        .input_chip_id = input_chip_id,
                        .input_eth_chan = input_eth_chan,
                        .output_chip_id = output_chip_id,
                        .output_eth_chan = output_eth_chan
                    })));

                    continue;
                }
                
                int pipe_dram_bank = -1; // Must only be from a single bank. Dire performance penalties if reading / writing from mixed banks

                std::vector<GridLoc> inputs;
                std::vector<int> inputs_num_tiles;
                bool post_tm_prolog_pipe = false;
                for (const auto input: yaml_node["input_list"]) {
                    uint64_t id = input.as<uint64_t>();
                    log_assert(tile_size == -1 or tile_size == buffers[id].tile_size, "Incorrect tile size");
                    tile_size = buffers[id].tile_size;

                    log_assert(input_t_factor == -1 or input_t_factor == buffers[id].buffer_t_factor, "Incorrect input_t_factor");
                    input_t_factor = buffers[id].buffer_t_factor;

                    if(buffers[id].dram_channel != -1) {
                        inputs.push_back(c.getDramNode(buffers[id].dram_channel, buffers[id].dram_sub_channel)->soc_location);
                        pipe_dram_bank = buffers.at(id).dram_bank;
                    }
                    else if(buffers[id].ethernet_channel != -1) {
                        inputs.push_back(c.getEthNode(buffers[id].ethernet_channel)->soc_location);
                    }
                    else {
                        inputs.push_back(c.getCoreNode(buffers[id].core_coord)->soc_location);
                    }
                    inputs_num_tiles.push_back(buffers[id].num_tiles);
                    post_tm_prolog_pipe |= buffers.at(id).prefetch_type;
                }
                const auto inputs_copy = inputs;
                const auto inputs_num_tiles_copy = inputs_num_tiles;
                for (int repeat = 1; repeat < pipe_repeat; repeat++) {
                    inputs.insert(inputs.end(), inputs_copy.begin(), inputs_copy.end());
                    inputs_num_tiles.insert(inputs_num_tiles.end(), inputs_num_tiles_copy.begin(), inputs_num_tiles_copy.end());
                }

                // TODO: This looks like it needs to be fully cleaned up for pipe_consuper_repeat
                // CHECK IF THE REPEAT IS ON THE CORRECT PIPES
                // Check if output scatter pipe
                bool output_scatter_pipe = yaml_node["output_list"][0].IsSequence();

                if(output_scatter_pipe) {
                    for(size_t c_rep = 0; c_rep < yaml_node["output_list"].size(); c_rep += pipe_consumer_repeat) {
                        std::vector<GridLoc> outputs;
                        
                        for (const auto output: yaml_node["output_list"][c_rep]) {
                            uint64_t id = output.as<uint64_t>();
                            log_assert(tile_size == buffers[id].tile_size, "Incorrect tile size");
                            
                            if(buffers[id].dram_channel != -1) {
                                outputs.push_back(c.getDramNode(buffers[id].dram_channel, buffers[id].dram_sub_channel)->soc_location);
                                pipe_dram_bank = buffers.at(id).dram_bank;
                            }
                            else if(buffers[id].ethernet_channel != -1) {
                                outputs.push_back(c.getEthNode(buffers[id].ethernet_channel)->soc_location);
                            }
                            else {
                                outputs.push_back(c.getCoreNode(buffers[id].core_coord)->soc_location);
                            }
                        }
                        GridLoc pipe_location;
                        if(yaml_node["ethernet_chan"]) {
                            pipe_location = c.getEthNode(yaml_node["ethernet_chan"].as<int>())->soc_location;
                        }
                        else {
                            pipe_location = c.getCoreNode(yaml_node["mcast_core_rc"][c_rep][1].as<int>(), yaml_node["mcast_core_rc"][c_rep][2].as<int>())->soc_location;
                        }
                        
                        int chip_location = yaml_node["mcast_core_rc"][c_rep][0].as<int>();
                        log_assert(outputs.size() == 1, "Expected a single output");
                        result.pipes.push_back(std::shared_ptr<Pipe>(new Pipe(
                            pipe_id,
                            inputs,
                            inputs_num_tiles,
                            1,
                            outputs,
                            tile_size,
                            pipe_location,
                            chip_location,
                            incoming_noc_id,
                            outgoing_noc_id,
                            incoming_vc,
                            outgoing_vc,
                            post_tm_prolog_pipe,
                            pipe_dram_bank
                        )));
                    }
                }
                else {
                    std::vector<GridLoc> outputs;
                    for (const auto output: yaml_node["output_list"]) {
                        //std::cout << "output: " << output << std::endl;
                        uint64_t id = output.as<uint64_t>();
/*
                        uint64_t id = output.IsSequence() ? 
                                      output[0].as<uint64_t>() :
                                      output.as<uint64_t>();
*/
                        log_assert(tile_size == buffers[id].tile_size, "Incorrect Tile size");
                        if(buffers[id].dram_channel != -1) {
                            outputs.push_back(c.getDramNode(buffers[id].dram_channel, buffers[id].dram_sub_channel)->soc_location);
                            pipe_dram_bank = buffers.at(id).dram_bank;
                        }
                        else if(buffers[id].ethernet_channel != -1) {
                            outputs.push_back(c.getEthNode(buffers[id].ethernet_channel)->soc_location);
                        }
                        else {
                            outputs.push_back(c.getCoreNode(buffers[id].core_coord)->soc_location);
                        }
                    }
                    GridLoc pipe_location;
                    if(yaml_node["ethernet_chan"]) {
                        pipe_location = c.getEthNode(yaml_node["ethernet_chan"].as<int>())->soc_location;
                    }
                    else {
                        pipe_location = c.getCoreNode(yaml_node["mcast_core_rc"][1].as<int>(), yaml_node["mcast_core_rc"][2].as<int>())->soc_location;
                    }
                    int chip_location = yaml_node["mcast_core_rc"][0].as<int>();
/*
                    GridLoc pipe_location = yaml_node["mcast_core_rc"][0].IsSequence() ? 
                                            c.getCoreNode(yaml_node["mcast_core_rc"][0][1].as<int>(), yaml_node["mcast_core_rc"][0][2].as<int>())->soc_location :
                                            c.getCoreNode(yaml_node["mcast_core_rc"][1].as<int>(), yaml_node["mcast_core_rc"][2].as<int>())->soc_location;
 */                   
                    result.pipes.push_back(std::shared_ptr<Pipe>(new Pipe(
                        pipe_id,
                        inputs,
                        inputs_num_tiles,
                        input_t_factor,
                        outputs,
                        tile_size,
                        pipe_location,
                        chip_location,
                        incoming_noc_id,
                        outgoing_noc_id,
                        incoming_vc,
                        outgoing_vc,
                        post_tm_prolog_pipe,
                        pipe_dram_bank
                    )));
                }
            }
        }
    }
    return result;  
}

}