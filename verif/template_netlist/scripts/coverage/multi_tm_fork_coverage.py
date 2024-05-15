# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import argparse
from dataclasses import dataclass
from itertools import repeat
import itertools
import os
import yaml

import multiprocessing as mp

from typing import Dict, Iterator, Tuple, Any, List, Union

RED='\033[0;31m'
GRN='\033[0;32m'
YLW='\033[0;33m'
RST='\033[0m'
SELECTOR_SPLIT_TOKEN = '.'

NetlistNode = Dict[str, Any]
TMKey = Tuple[str, ...]
TMCoverageDict = Dict[TMKey, int]

__all_tms = ['transpose', 'r_broadcast', 'c_broadcast', 't_broadcast', 'hslice', 'vslice', 'hstack', 'vstack']
__supported_tms = ['transpose', 'r_broadcast', 'hslice', 'hstack']

@dataclass(frozen=True)
class MultiTmTestConfig:
    test_op_selector: str
    input_count: int

    @staticmethod
    def create_from_string(test_type: str) -> MultiTmTestConfig:
        if test_type == "dram_dc":
            return DramInputDatacopyConfig()
        elif test_type == "dram_matmul":
            return DramInputMatmulConfig()
        elif test_type == "dc_dc":
            return DatacopyDatacopyConfig()
        elif test_type == "dc_matmul":
            return DatacopyMatmulConfig()
        else:
            raise RuntimeError(f"Unsupported multi TM test config: '{test_type}'.")

@dataclass(frozen=True, init=False)
class DramInputDatacopyConfig(MultiTmTestConfig):
    def __init__(self) -> None:
        super().__init__(
            test_op_selector="graphs.test_multi_tm_dram_datacopy.datacopy{}",
            input_count=1
        )

@dataclass(frozen=True, init=False)
class DramInputMatmulConfig(MultiTmTestConfig):
    def __init__(self) -> None:
        super().__init__(
            test_op_selector="graphs.test_multi_tm_dram_matmul.op{}",
            input_count=2
        )

@dataclass(frozen=True, init=False)
class DatacopyDatacopyConfig(MultiTmTestConfig):
    def __init__(self) -> None:
        super().__init__(
            test_op_selector="graphs.test_multi_tm_datacopy_datacopy.op{}",
            input_count=1
        )

@dataclass(frozen=True, init=False)
class DatacopyMatmulConfig(MultiTmTestConfig):
    def __init__(self) -> None:
        super().__init__(
            test_op_selector="graphs.test_multi_tm_datacopy_matmul.op{}",
            input_count=2
        )

def tm_dict_to_tm_str(tm_dict: Union[str, Dict]) -> str:
    # only broadcast will not be returned by this
    for tm in __all_tms:
        if tm in tm_dict:
            return tm
    # we now determine which broadcast the current tm is
    tm_factor = tm_dict["broadcast"]
    if "r" in tm_factor:
        return "r_broadcast"
    if "c" in tm_factor:
        return "c_broadcast"
    if "z" in tm_factor:
        return "t_broadcast"

def read_netlist_node(netlist_dict: NetlistNode, test_op: str) -> NetlistNode:
    node = netlist_dict
    for node_name in test_op.split(SELECTOR_SPLIT_TOKEN):
        if node_name not in node:
            raise RuntimeError(f"Invalid test op selector.")
        node = node[node_name]
    return node

def make_empty_tm_coverage_dict(fork: int) -> TMCoverageDict:
    coverage_dict = {}
    for key in itertools.product(__supported_tms, repeat=2*fork):
        coverage_dict[key] = 0
    return coverage_dict

def read_tms_from_op(netlist_dict: NetlistNode, test_op: str, input_idx: int, fork: int) -> TMKey:
    fork_tms = []
    for fork_idx in range(fork):
        test_op_node = read_netlist_node(netlist_dict, test_op.format(fork_idx))
        input_tms = test_op_node[f"input_{input_idx}_tms"]
        fork_tms.append(tuple(
            [tm_dict_to_tm_str(tm_dict) for tm_dict in input_tms]
        ))
    return sum(fork_tms, ())

def process_single_netlist(netlist_path: str, input_count: int, test_op: str, fork: int) -> List[TMKey]:
    assert os.path.exists(netlist_path), f"Netlist: '{netlist_path}' does not exist."
    with open(netlist_path, "r") as file:
        netlist_dict = yaml.safe_load(file.read())
        return [
            read_tms_from_op(
                netlist_dict=netlist_dict,
                test_op=test_op,
                input_idx=input_idx,
                fork=fork,
            )
            for input_idx in range(input_count)
        ]

def read_netlist_paths_from_test_dir(test_dir: str) -> Iterator[str]:
    for test_netlist in os.listdir(test_dir):
        # skip logs dir and anything which is not a directory
        dir_path = os.path.join(test_dir, test_netlist)
        if not os.path.isdir(dir_path) or test_netlist == "logs":
            continue
        
        netlist_id = test_netlist.split("_")[1]
        netlist_path = os.path.join(dir_path, f"netlist_{netlist_id}.yaml")
        yield netlist_path

def reduce_coverage(netlist_input_coverage: List[TMKey], input_count: int, fork: int) -> List[TMCoverageDict]:
    total_counts = [0, 0]
    coverage_result = [make_empty_tm_coverage_dict(fork) for _ in range(input_count)]
    for netlist_coverage in netlist_input_coverage:
        for input_idx, tm_key in enumerate(netlist_coverage):
            if tm_key in coverage_result[input_idx]:
                coverage_result[input_idx][tm_key] += 1
                total_counts[input_idx] += 1
    return coverage_result

def multi_tm_test_coverage(test_dir: str, input_count: int, test_op: str, fork: int) -> List[TMCoverageDict]:
    with mp.Pool() as pool:
        netlist_input_coverage = pool.starmap(
            process_single_netlist, 
            zip(
                read_netlist_paths_from_test_dir(test_dir),
                repeat(input_count),
                repeat(test_op),
                repeat(fork)
            )
        )
        print(f"Total num of coverages: {len(netlist_input_coverage)}")
    return reduce_coverage(netlist_input_coverage, input_count, fork)

def print_coverage_result(coverage_result: List[TMCoverageDict], input_count: int) -> None:
    format_tm = lambda tm: ", ".join(tm)
    format_val = lambda x: f"{GRN if x > 0 else RED}{x:^15d}{RST}"

    # print header
    print("{:60s} | ".format("TM combination") + " | ".join(["{:15s}".format(f" Frequency [{input_idx}] ") for input_idx in range(input_count)]))
    print((66 + 18 * input_count) * "=")

    # print coverage table
    for tm in coverage_result[0].keys():
        print(f"{format_tm(tm):60s} | " + " | ".join([f"{format_val(coverage_result[input_idx][tm])}" for input_idx in range(input_count)]))

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--test-dir", required=True, type=str)
    parser.add_argument("--test-type", required=True, type=str)
    parser.add_argument("--fork", default=1, type=int)
    args = parser.parse_args()

    test_type_config = MultiTmTestConfig.create_from_string(args.test_type)

    print(test_type_config)

    coverage_result = multi_tm_test_coverage(
        test_dir=args.test_dir,
        input_count=test_type_config.input_count,
        test_op=test_type_config.test_op_selector,
        fork=args.fork,
    )
    print(f"Test dir: '{args.test_dir}")
    print(f"Input count: {test_type_config.input_count}")
    print(f"Test op selector: {test_type_config.test_op_selector}")
    print_coverage_result(coverage_result, test_type_config.input_count)

if __name__ == "__main__":
    main()
        
