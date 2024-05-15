#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
import os
import json
from typing import List, Dict, Tuple, Set
from perf_test_base import get_perf_info
from copy import deepcopy
from postprocess_api import *
from logger_utils import logger, ASSERT
from perf_comparison import (
    GraphPerf,
)

class CoreCoord:
    def __init__ (self, x, y):
        self.x      : int = x
        self.y      : int = y
    
    def __hash__(self):
        return hash(str(self.x) + str(self.y))

    def __eq__(self, other):
        if isinstance(other, CoreCoord):
            return ((self.x == other.x) and (self.y == other.y))
        return False

    def print(self):
        return str(self.x) + "-" + str(self.y)

    @classmethod
    def from_core_label(cls, label: str):
        ASSERT("-" in label)
        delim_idx = label.index('-')
        x = (int)(label[:delim_idx])
        y = (int)(label[(delim_idx+1):])
        return cls(x, y)

class Core:
    def __init__ (self, coord: CoreCoord):
        self.coord: CoreCoord = coord

class EpochInfo:
    def __init__(self, path):
        self.path = path

        runtime_table_path = path + RUNTIME_FILE_NAME
        ASSERT(os.path.exists(runtime_table_path), f"runtime_table path does not exist {runtime_table_path}")
        with open(runtime_table_path) as runtime_table_file:
            self.runtime_table_json = json.load(runtime_table_file)
        
        self.graph_name         : str = self.runtime_table_json[per_epoch_events_key][graph_name_key]
        self.local_epoch_id     : int = int(self.runtime_table_json[per_epoch_events_key][local_epoch_id_key])
        self.global_epoch_id    : int = int(self.runtime_table_json[per_epoch_events_key][global_epoch_id_key])
        self.program_name       : str = self.runtime_table_json[per_epoch_events_key][program_name_key]
        self.program_id         : int = int(self.runtime_table_json[per_epoch_events_key][program_id_key])
        self.chip_id            : int = int(self.runtime_table_json[per_epoch_events_key][device_id_key])
        self.aiclk              : int = int(self.runtime_table_json[per_epoch_events_key][aiclk_key])

    def print(self):
        logger.debug(f"Epoch properties:")
        logger.debug(f"chip_id: {self.chip_id}")
        logger.debug(f"aiclk: {self.aiclk}")
        logger.debug(f"program_name: {self.program_name}")
        logger.debug(f"graph_name: {self.graph_name}")
        logger.debug(f"program_id: {self.program_id}")
        logger.debug(f"global_epoch_id: {self.global_epoch_id}")
        logger.debug(f"local_epoch_id: {self.local_epoch_id}")

class Epoch:
    def __init__(self, path):
        self.info: EpochInfo = EpochInfo(path)
        self.perf: GraphPerf = GraphPerf(path + RUNTIME_FILE_NAME)

    def print(self):
        self.info.print()

# class Queue:
#     def __init__ (self, path):
#         self.name = ""

class Op:
    def __init__ (self, coord, core_attr):
        self.name       : str = core_attr[op_name_key]
        self.cores      : Dict[CoreCoord, Core] = {coord: Core(coord)}

        self.inputs             : List[str] = []
        self.outputs            : List[str] = []
        self.input_ops          : List[str] = []
        self.output_ops         : List[str] = []
        self.input_queues       : List[str] = []
        self.output_queues      : List[str] = []
        self.ethernet_output    : List[str] = []

        input_map : Dict[int, Tuple[str, str]] = {}
        largest_idx = 0
        for input_val in core_attr[cores_to_ops_inputs_key]:
            name = input_val[cores_to_ops_operand_name_key]
            type = input_val[cores_to_ops_operand_type_key]
            index = input_val[cores_to_ops_operand_index_key]
            input_map[index] = (name, type)
            largest_idx = max(largest_idx, index)
        for i in range(largest_idx + 1):
            ASSERT(i in input_map, f"Input index {i} is not initialized")
            name, type = input_map[i]
            self.inputs.append(name)
            if type == op_type_label:
                self.input_ops.append(name)
            elif type == queue_type_label:
                self.input_queues.append(name)
            else:
                raise ValueError(f"Invalid input type {type} for op {self.name} input index {i}")
        
        num_inputs = core_attr[num_inputs_key]
        ASSERT(num_inputs == len(self.inputs), f"Inconsistant num input operands for op {self.name} num-inputs {num_inputs} number of inputs provided {len(self.inputs)}")

        num_outputs = core_attr[num_outputs_key]
        for output_val in core_attr[cores_to_ops_outputs_key]:
            name = output_val[cores_to_ops_operand_name_key]
            type = output_val[cores_to_ops_operand_type_key]
            self.outputs.append(name)
            if type == op_type_label:
                self.output_ops.append(name)
            elif type == queue_type_label:
                self.output_queues.append(name)
            else:
                raise ValueError(f"Invalid output type {type} for op {self.name}")
        ASSERT(num_outputs == len(self.outputs), f"Inconsistant num output operands for op {self.name} num-outpus {num_outputs} numbert of outputs provided {len(self.outputs)}")
        
    def append_core(self, coord):
        ASSERT(coord not in self.cores)
        self.cores[coord] = Core(coord)
    
    def get_grid_size(self):
        ASSERT(len(self.cores) > 0, "Unexpected op with 0x0 grid size")
        
        grid_map: Dict[int, List[int]] = {}
        for coord in self.cores:
            x, y = int(coord.x), int(coord.y)
            if y in grid_map:
                grid_map[y].append(x)
            else:
                grid_map[y] = [x]
                
        col_indices = [col for col in grid_map.values()]
        ASSERT(all(len(col_indices[i]) == len(col_indices[0]) for i in range(len(col_indices))), f"Unexpected non-rectangular grid for op {self.name}")

        grid_num_rows = len(grid_map.keys())
        grid_num_cols = len(col_indices[0])

        return [grid_num_rows, grid_num_cols]
            

    def print(self):
        logger.debug(f"Op properties:")
        logger.debug(f"name: {self.name}")
        logger.debug(f"Cores: {[i.print() for i in self.cores.keys()]}")
        logger.debug(f"inputs: {self.inputs}")
        logger.debug(f"outputs: {self.outputs}")

class ForkNode:
    def __init__(
        self,
        local_id: int,
        graph_name: str,
        fork_name: str,
        join_name: str,
        ops_after_join_name: Set[str],
        first_path: Set[str],
        second_path: Set[str]
    ):
        self.local_id           : int = local_id
        self.graph_name         : str = graph_name
        self.fork_op_name       : str = fork_name
        self.join_op_name       : str = join_name
        self.ops_after_join_name: Set[str] = ops_after_join_name
        self.first_fork_path    : Set[str] = first_path
        self.second_fork_path   : Set[str] = second_path
        
        self.fork_op_output_bw_original: int
        self.fork_op_output_bw_decoupled: int

    def get_all_nodes(self) -> Set[str]:
        all_nodes = set().union(self.first_fork_path, self.second_fork_path, self.ops_after_join_name)
        all_nodes.add(self.fork_op_name)
        all_nodes.add(self.join_op_name)
        return all_nodes

class Graph:
    def __init__(self, path, name, chip_id, aiclk):
        self.name       : str = name
        self.queues     : List[str] = []
        self.ops        : Dict[str, Op] = {}
        self.cores      : Dict[CoreCoord, Core] = {}
        self.chip_id    : int = chip_id
        self.aiclk      : int = aiclk
        self.fork_nodes : List[ForkNode] = []
        
        cores_to_ops_path = path + CORES_TO_OPS_FILE_NAME
        with open(cores_to_ops_path) as cores_to_ops_file:
            self.cores_to_ops_json = json.load(cores_to_ops_file)
        for core_label, core_attr in self.cores_to_ops_json.items():

            # Empty Graph
            if core_label == "N/A":
                break
            
            op_name = core_attr[op_name_key]
            coord = CoreCoord.from_core_label(core_label)
            if op_name in self.ops:
                self.ops[op_name].append_core(coord)
            else:
                self.ops[op_name] = Op(coord, core_attr)
                self.queues = list(set(self.queues).union(set(self.ops[op_name].input_queues)))
                self.queues = list(set(self.queues).union(set(self.ops[op_name].output_queues)))

        for op_name, op in self.ops.items():
            for output_op in op.output_ops:
                if output_op not in self.ops:
                    self.ops[op_name].ethernet_output.append(output_op)
    
    def get_input_operands_for_op(self, op_name: str):
        ASSERT(op_name in self.ops, f"op with name {op_name} does not exist in graph {self.name}")
        return self.ops[op_name].inputs
    
    def get_output_operands_for_op(self, op_name: str):
        ASSERT(op_name in self.ops, f"op with name {op_name} does not exist in graph {self.name}")
        return self.ops[op_name].outputs
    
    def get_output_ops_for_op(self, op_name: str):
        ASSERT(op_name in self.ops, f"op with name {op_name} does not exist in graph {self.name}")
        all_outputs = self.ops[op_name].outputs
        output_ops = []
        for output in all_outputs:
            if not output in self.queues:
                output_ops.append(output)

        return output_ops
    
    def get_grid_size_for_op(self, op_name: str):
        ASSERT(op_name in self.ops, f"op with name {op_name} does not exist in graph {self.name}")
        return self.ops[op_name].get_grid_size()
    
    def is_queue(self, node_name: str):
        return node_name in self.queues
    
    def get_all_ops_for_graph(self) -> Dict[str, List[str]]:
        cores_to_ops_path = perf_output_dir + "/" + CORES_TO_OPS_FILE_NAME
        assert os.path.exists(cores_to_ops_path)
        
        all_ops = {}
        with open(cores_to_ops_path) as cores_to_ops_file:
            cores_to_ops = json.load(cores_to_ops_file)
            for core in cores_to_ops:
                
                # Empty graph
                if core == "N/A":
                    return {}
                
                assert "op-name" in cores_to_ops[core]
                op_name = cores_to_ops[core]["op-name"]
                if op_name not in all_ops:
                    all_ops.update({op_name: [core]})
                else:
                    all_ops[op_name].append(core)
        return all_ops

    def does_op_fork_to_unique_ops(self, op: Op):
        return len(set(op.output_ops)) > 1
    
    def more_than_one_unique_input_op(self, op: Op):
        return len(set(op.input_ops)) > 1

    def get_input_operand_idx_for_op(self, target_op: str, input_op: str):
        ASSERT(target_op in self.ops, f"op name {target_op} does not exist in graph {self.name}")
        ASSERT(input_op in self.ops[target_op].inputs, f"input op {input_op} is not feeding any input operands of op {target_op}")
        return self.ops[target_op].inputs.index(input_op)

    def any_output_ops(self, op: Op):
        for output in op.outputs:
            if not self.is_queue(output):
                return True
        return False

    def find_all_paths(self, all_ops_all_graphs: Dict[str, Op], current_op_name: str, join_op_name: str, path: List[str], visited: Set[str]):        
        if current_op_name == join_op_name:
            path_copy = deepcopy(path)
            return [path_copy]
        
        path += [current_op_name]
        visited.add(current_op_name)
        
        paths = []
        current_op = all_ops_all_graphs[current_op_name]
        for next_op in current_op.output_ops:
            if next_op not in visited:
                new_paths = self.find_all_paths(all_ops_all_graphs, next_op, join_op_name, path, visited)
                for new_path in new_paths:
                    paths.append(new_path)
        
        path.pop()
        return paths
    
    def populate_all_fork_join_paths(self, all_ops_all_graphs: Dict[str, Op]):
        for fork_op_name, fork_op in self.ops.items():
            if not self.does_op_fork_to_unique_ops(fork_op):
                continue
            for join_op_name, join_op in self.ops.items():
                if not self.more_than_one_unique_input_op(join_op):
                    continue
                
                # If fork op has N output ops, for each output op, find all paths to the join op.
                # If there are multiple output ops that have a path to the join op, then we have a fork.
                paths_from_each_output = []
                # Iterate unique output ops. Should skip in case on op feeds another op twice
                visited: set[str] = set()
                for op_after_fork_name in set(fork_op.output_ops):
                    if op_after_fork_name in visited:
                        continue
                    # print(f"HERE-4-Starting analysis for fork {fork_op_name}, join {join_op_name}, output op {op_after_fork_name}")
                    paths = self.find_all_paths(all_ops_all_graphs, op_after_fork_name, join_op_name, [], visited)
                    # print(f"HERE-6-result path analysis for fork {fork_op_name}, join {join_op_name}, output op {op_after_fork_name} = {paths}")
                    paths_from_each_output.append(paths)
                if len(paths_from_each_output) > 1:
                    # Each branch loop iterates over paths from each output op to the join op
                    # So each element inside paths_from_each_output is a list of paths from a single output op to the join op
                    # The sub-branch loop iterates over all paths from a single output op to the join op
                    for first_branch_idx in range(len(paths_from_each_output)):
                        for first_sub_branch_idx in range(len(paths_from_each_output[first_branch_idx])):
                            for second_branch_idx in range(first_branch_idx+1, len(paths_from_each_output)):
                                for second_sub_branch_idx in range(len(paths_from_each_output[second_branch_idx])):
                                    first_branch = paths_from_each_output[first_branch_idx][first_sub_branch_idx]
                                    second_branch = paths_from_each_output[second_branch_idx][second_sub_branch_idx]
                                    local_id = len(self.fork_nodes)
                                    # print(f"HERE-7-Adding fork {fork_op_name}, join {join_op_name}, first_branch = {set(first_branch)}, second_branch = {set(second_branch)}")
                                    ops_after_join = set(self.get_output_ops_for_op(join_op_name))
                                    self.fork_nodes.append(
                                        ForkNode(
                                            local_id=local_id,
                                            graph_name=self.name,
                                            fork_name=fork_op_name,
                                            join_name=join_op_name,
                                            ops_after_join_name=ops_after_join,
                                            first_path=set(first_branch),
                                            second_path=set(second_branch)
                                        )
                                    )

        # logger.info(f"graph {self.name}")
        # for node in self.fork_nodes:
        #     logger.info(f"fork node {node.fork_op_name}")
        #     logger.info(f"join node {node.join_op_name}")

    def print(self):
        logger.debug(f"Graph properties:")
        logger.debug(f"name: {self.name}")
        logger.debug(f"queues: {self.queues}")
        logger.debug(f"op names: {self.ops.keys()}")
        logger.debug(f"all ops: {[i.print() for i in self.ops.values()]}")
        logger.debug(f"chip_id: {self.chip_id}")
        logger.debug(f"aiclk: {self.aiclk}")

def get_non_overlap_fork_joins(all_fork_joins: List[ForkNode]) -> List[List[ForkNode]]:
    groups: List[List[ForkNode]] = []
    all_nodes_in_group: List[Set[str]] = []

    for node in all_fork_joins:
        new_node_group_idx = 0
        for idx, group in enumerate(groups):
            intersection = node.get_all_nodes().intersection(all_nodes_in_group[idx])
            if intersection:
                new_node_group_idx += 1
            else:
                break
        if new_node_group_idx == len(groups):
            groups.append([node])
            all_nodes_in_group.append(node.get_all_nodes())
        else:
            groups[new_node_group_idx].append(node)
            all_nodes_in_group[new_node_group_idx] = set().union(all_nodes_in_group[new_node_group_idx], node.get_all_nodes())
        
    return groups

class AllGraphs:
    def __init__(self, path):
        self.all_graphs     : Dict[str, Graph] = {}
        self.graphs_root = path + "/graph_descriptor/"
        ASSERT(os.path.exists(self.graphs_root), f"graph_descriptor directory does not exist under {path}")

        perf_info = get_perf_info(path)
        ASSERT(os.path.exists(path + "/" + PERF_INFO_FILE_NAME), f"epoch info file {PERF_INFO_FILE_NAME} does not exist under path {path}")
        
        for program in perf_info.keys():
            if program == "global-events":
                continue
            assert "-" in program, "Format of program label in perf-info must be #-PROGRAM_NAME"
            
            program_name = program[program.find("-")+1:]
            epochs = perf_info[program]
            for epoch in epochs:
                assert "-" in epoch, "Format of program label in perf-info must be #-GRAPH_NAME"
                graph_name = epoch[epoch.find("-")+1:]
                epoch_data = perf_info[program][epoch]
                if graph_name in self.all_graphs:
                    continue
                else:
                    graph_path = self.graphs_root + graph_name + "/"
                    ASSERT(os.path.exists(graph_path), f"graph descriptor does not exist for graph {graph_path}")
                    device_id = epoch_data["device_id"]
                    aiclk = epoch_data["aiclk"]
                    new_graph = Graph(
                        path = graph_path,
                        name = graph_name,
                        chip_id = device_id,
                        aiclk = aiclk
                    )
                    self.all_graphs[graph_name] = new_graph

        all_ops = self.get_all_ops()
        for graph in self.all_graphs.values():
            graph.populate_all_fork_join_paths(all_ops)

    def get_all_ops(self):
        all_ops: Dict[str, Op] = {}
        for graph in self.all_graphs.values():
            for op_name, op in graph.ops.items():
                ASSERT(op_name not in all_ops, f"Duplicate op {op_name}")
                all_ops[op_name] = op
                
        return all_ops
    
    def get_all_fork_joins(self) -> List[ForkNode]:
        all_forks = []
        for graph in self.all_graphs.values():
            all_forks += graph.fork_nodes
        return all_forks

class AllEpochs:
    def __init__(self, path):
        self.all_epochs     : Dict[int, Epoch] = {}
        self.all_graphs     : Dict[str, Graph] = {}
        self.programs       : Dict[int, List[int]] = {}

        ASSERT(os.path.exists(path), f"path {path} does not exist.")
        ASSERT(os.path.exists(path + "/device/"), f"device directory does not exist under {path}")
        ASSERT(os.path.exists(path + "/graph_descriptor/"), f"graph_descriptor directory does not exist under {path}")
        ASSERT(os.path.exists(path + "/" + PERF_INFO_FILE_NAME, f"epoch info file {PERF_INFO_FILE_NAME} does not exist under path {path}"))

        self.all_graphs = AllGraphs(path).all_graphs
        
        perf_info = get_perf_info(path)

        for program in perf_info.keys():
            if program == "global-events":
                continue
            logger.info(f"program_name {program}")
            assert "-" in program, "Format of program label in perf-info must be #-PROGRAM_NAME"
            
            program_name = program[program.find("-")+1:]
            epochs = perf_info[program]
            for epoch in epochs:
                assert "-" in epoch, "Format of program label in perf-info must be #-GRAPH_NAME"
                graph_name = epoch[epoch.find("-")+1:]
                ASSERT(graph_name in self.all_graphs, f"graph_name {graph_name} does not exist.")
                epoch_data = perf_info[program][epoch]
                epoch_perf_output_dir = epoch_data["output_directory"]
                ASSERT(os.path.exists(epoch_perf_output_dir))
                new_epoch = Epoch(epoch_perf_output_dir)
                global_epoch_id = epoch_data["global-epoch-id"]
                ASSERT(global_epoch_id not in self.all_epochs, f"Duplicate epochs found with the same global-epoch-id {global_epoch_id}")
                self.all_epochs[global_epoch_id] = deepcopy(new_epoch)
                if program_name in self.programs:
                    self.programs[program_name].append(global_epoch_id)
                else:
                    self.programs[program_name] = [global_epoch_id]
        
        logger.info(f"Finished generating all graphs. Total number of unique graphs: {len(self.all_graphs)}")
        logger.info(f"Total number of epochs found {len(self.all_epochs)}")
        logger.info(f"Total number of programs {len(self.programs)}")
        # logger.debug(f"All graphs: {[i.print() for i in self.all_graphs.values()]}")
        # logger.debug(f"All epochs: {[i.print() for i in self.all_epochs.values()]}")
        

if __name__ == "__main__":
    try:
        idx = sys.argv.index("--")
        script_args = sys.argv[1:idx]
        cmd_args = sys.argv[(idx + 1) :]
    except ValueError:
        script_args = sys.argv[1:]
        cmd_args = []

    print("cmd_args = ", cmd_args)
    print("script_args = ", script_args)

    # parse arguments
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--perf-output-dir", type=str, default="", help="The output directory path for perf results.")
    state = parser.parse_args(script_args)
    logger.info("Generating all graphs")
    perf_output_dir: str = state.perf_output_dir
    ASSERT(os.path.exists(perf_output_dir), f"Input perf output directory path does not exist {perf_output_dir}")
    
    all_epochs: AllEpochs = AllEpochs(perf_output_dir)
    
