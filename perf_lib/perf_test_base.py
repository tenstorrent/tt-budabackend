# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0


from copy import deepcopy
from typing import List, Union, Dict
from dataclasses import dataclass
from enum import Enum
import os, sys
from logger_utils import logger, ASSERT
import yaml
from postprocess_api import *

FP32_TILE_SIZE = 32 * 32 * 4 + 32
FP16_TILE_SIZE = 32 * 32 * 2 + 32
BFP8_TILE_SIZE = 32 * 32 * 1 + 32 + 4*16
BFP4_TILE_SIZE = 32 * 16     + 32 + 4*16
BFP2_TILE_SIZE = 32 * 8      + 32 + 4*16

Exit_Failure = 1
TEMPLATE_NETLIST_DIR=f"{os.getcwd()}/verif/template_netlist/"

sys.path.insert(1, TEMPLATE_NETLIST_DIR)
sys.path.insert(2, TEMPLATE_NETLIST_DIR+"/test_modules/")

def get_tile_size(data_format):
    if data_format == "Float32" or data_format == "RawUInt32":
        return FP32_TILE_SIZE
    elif data_format == "Float16" or data_format == "Float16_b":
        return FP16_TILE_SIZE
    elif data_format == "Bfp8" or data_format == "Bfp8_b":
        return BFP8_TILE_SIZE
    elif data_format == "Bfp4" or data_format == "Bfp4_b":
        return BFP4_TILE_SIZE
    elif data_format == "Bfp2" or data_format == "Bfp2_b":
        return BFP2_TILE_SIZE
    else:
        raise "Invalid data-format"
class TMType(Enum):
    no_tms = 1
    
    hslice = 2
    vslice = 3
    hstack = 4
    vstack = 5
    broadcast_r = 6
    broadcast_c = 7
    broadcast_z = 8

class OpType(Enum):
    Matmul               = 1
    Binary               = 2
    Unary                = 3
    Dram                 = 4
    TM                   = 5
    MatmulBias           = 6
    Fused0               = 7
    Fused1               = 8
    Fused2               = 9
    Pcie                 = 10
    MatmulSparse         = 11
    ReduceMax            = 12
    Splice               = 13
    MatmulSparseNzCounts = 14
    Tilizer              = 15

class ReblockTMPerfLib(Enum):
    Normal         = 1
    Gather         = 2
    Hstack         = 3
    Broadcast_r    = 4

class ReduceDim(Enum):
    r   = 1
    c   = 2
    z   = 3

def get_num_input_operands(test_type: OpType):
    if (
        test_type == OpType.Unary or
        test_type == OpType.TM or
        test_type == OpType.Dram or
        test_type == OpType.Pcie or
        test_type == OpType.ReduceMax or
        test_type == OpType.Tilizer
    ):
        return 1
    elif (
        test_type == OpType.Matmul or
        test_type == OpType.Binary
    ):
        return 2
    elif (
        test_type == OpType.MatmulBias or
        test_type == OpType.Fused0 or
        test_type == OpType.MatmulSparse or
        test_type == OpType.MatmulSparseNzCounts or 
        test_type == OpType.Splice
    ):
        return 3
    elif (
        test_type == OpType.Fused2
    ):
        return 5
    else:
        raise ValueError(f"Invalid test_type. This function is missing the number of operands for test_type {test_type.name}")

def VAR(var_name, operand_index):
    if operand_index == -1:
        return "output_" + var_name
    elif operand_index >= 0:
        return f"input{operand_index}_" + var_name
    else:
        raise ValueError("Invalid operand index")

def is_binary_op(test_type: OpType):
    num_input_operands = get_num_input_operands(test_type)
    return num_input_operands > 1

def get_ublock_order_str(ublock_order):
    if ublock_order == 'r':
        return "0-r"
    elif ublock_order == 'c':
        return "1-c"
    else:
        raise ValueError("Invalid ublock order")

class PerfError(Exception):
    def __init__(self, observed, expected, message) -> None:
        self.observed = observed
        self.expected = expected
        super().__init__(message)

class InvalidComparison(PerfError):
    pass

class TestFailed(Exception):
    pass
class PerfMetrics(Enum):
    average_math_utilization = 1
    input0_bw = 2
    output_bw = 3

def get_raise_type(perf_metric: PerfMetrics):
    if (
        perf_metric == PerfMetrics.average_math_utilization or
        perf_metric == PerfMetrics.input0_bw or
        perf_metric == PerfMetrics.output_bw
    ):
        return PerfError
    else:
        raise Exception("Invalid performance metric")

def get_diff_percentage(first, second):
    if type(first) != str and type(second) != str:
        if first == 0:
            result = "N/A"
        else:
            result = (second-first) / first * 100.0
    else:
        result = "N/A"
    return result

def get_average(first, second, weight):
    if first == "N/A" or second == "N/A":
        return "N/A"
    else:
        return (first * weight + second) / (weight + 1)

def get_worse(first, second, higher_is_better):
    if first == "N/A" or second == "N/A":
        return "N/A"
    else:
        if higher_is_better:
            return first if first <= second else second
        else:
            return second if first <= second else second

def ROUND(input, num_dec = 2):
    if input == "N/A":
        return "N/A"
    else:
        return round(input, num_dec)

def get_percentage(val):
    if val != "N/A":
        result = ROUND(val*100 ,2)
    else:
        result = "N/A"
    return result

@dataclass
class PerfResults:
    aiclk: int = 0
    target_core: str = "N/A"
    target_input: str = "N/A"

    math_utilization: Union[float, int, str] = "N/A"
    execution_cycles: Union[float, int, str] = "N/A"
    cycles_per_tile:  Union[int, str] = "N/A"

    total_wait_for_tile: Union[float, int, str] = "N/A"
    total_wait_for_free_tile: Union[float, int, str] = "N/A"

    first_input_recorded: Union[int, str] = "N/A"
    last_input_recorded: Union[int, str] = "N/A"
    average_math_utilization: Union[float, int, str] = "N/A"
    total_runtime: Union[int, str] = "N/A"
    total_runtime_per_tile: Union[int, str] = "N/A"
    output_bw: Union[float, int, str] = "N/A"
    input0_bw: Union[float, int, str] = "N/A"
    input1_bw: Union[float, int, str] = "N/A"

    input0_bw_across_cores_total_runtime: Union[float, int, str] = "N/A"
    input1_bw_across_cores_total_runtime: Union[float, int, str] = "N/A"
    output_bw_across_cores_total_runtime: Union[float, int, str] = "N/A"

    def diff(self, other):
        perf_diff = deepcopy(self)
        assert(self.target_core == other.target_core)
        assert(self.target_input == other.target_input)
        assert(self.first_input_recorded == other.first_input_recorded)
        assert(self.last_input_recorded == other.last_input_recorded)
        perf_diff.math_utilization          = get_diff_percentage(perf_diff.math_utilization, other.math_utilization)
        perf_diff.execution_cycles          = get_diff_percentage(perf_diff.execution_cycles, other.execution_cycles)
        perf_diff.total_wait_for_tile       = get_diff_percentage(perf_diff.total_wait_for_tile, other.total_wait_for_tile)
        perf_diff.total_wait_for_free_tile  = get_diff_percentage(perf_diff.total_wait_for_free_tile, other.total_wait_for_free_tile)
        perf_diff.average_math_utilization  = get_diff_percentage(perf_diff.average_math_utilization, other.average_math_utilization)
        perf_diff.total_runtime             = get_diff_percentage(perf_diff.total_runtime, other.total_runtime)      
        perf_diff.total_runtime_per_tile    = get_diff_percentage(perf_diff.total_runtime_per_tile, other.total_runtime_per_tile)  
        return perf_diff

@dataclass
class ComparisonConfig:
    perf_metrics_rtol = PerfResults(
        average_math_utilization=0.01,
        total_runtime=0.01,
        math_utilization=0.01,
        execution_cycles=0.01,
        input0_bw=0.01,
        output_bw=0.01,
    )

class TestConfig:
    def __init__(self, name, config):
        self.name = name
        self.parameters = self.generate_test_parameter_dict(config['test-parameters'])
        self.perf_results = PerfResults()
        self.comparison_config = ComparisonConfig()
        if 'math-utilization' in config['performance-metrics']:
            assert 'expected' in config['performance-metrics']['math-utilization']
            self.perf_results.math_utilization = config['performance-metrics']['math-utilization']['expected']
            if 'rtol' in config['performance-metrics']['math-utilization']:
                self.comparison_config.perf_metrics_rtol.math_utilization = config['performance-metrics']['math-utilization']['rtol']
        if 'average-math-utilization' in config['performance-metrics']:
            assert 'expected' in config['performance-metrics']['average-math-utilization']
            self.perf_results.average_math_utilization = config['performance-metrics']['average-math-utilization']['expected']
            if 'rtol' in config['performance-metrics']['average-math-utilization']:
                self.comparison_config.perf_metrics_rtol.average_math_utilization = config['performance-metrics']['average-math-utilization']['rtol']
        if 'total-runtime' in config['performance-metrics']:
            assert 'expected' in config['performance-metrics']['total-runtime']
            self.perf_results.total_runtime = config['performance-metrics']['total-runtime']['expected']
            if 'rtol' in config['performance-metrics']['total-runtime']:
                self.comparison_config.perf_metrics_rtol.total_runtime = config['performance-metrics']['total-runtime']['rtol']
        if 'execution-cycles' in config['performance-metrics']:
            assert 'expected' in config['performance-metrics']['execution-cycles']
            self.perf_results.execution_cycles = config['performance-metrics']['execution-cycles']['expected']
            if 'rtol' in config['performance-metrics']['execution-cycles']:
                self.comparison_config.perf_metrics_rtol.execution_cycles = config['performance-metrics']['execution-cycles']['rtol']
        if 'input0-bw' in config['performance-metrics']:
            assert 'expected' in config['performance-metrics']['input0-bw']
            self.perf_results.input0_bw = config['performance-metrics']['input0-bw']['expected']
            if 'rtol' in config['performance-metrics']['input0-bw']:
                self.comparison_config.perf_metrics_rtol.input0_bw = config['performance-metrics']['input0-bw']['rtol']
        if 'output-bw' in config['performance-metrics']:
            assert 'expected' in config['performance-metrics']['output-bw']
            self.perf_results.output_bw = config['performance-metrics']['output-bw']['expected']
            if 'rtol' in config['performance-metrics']['output-bw']:
                self.comparison_config.perf_metrics_rtol.output_bw = config['performance-metrics']['output-bw']['rtol']

        
    def generate_test_parameter_dict(self, config) -> Dict[str, List[int]]:
        parameters = {}
        for key, val in config.items():
            if isinstance(val, list):
                parameters[key] = val
            else:
                parameters[key] = [val]
        return parameters

def compare_values(observed, expected, rtol: float, perf_metric: PerfMetrics):
    
    if not isinstance(observed, str):
        if not isinstance(expected, str):
            print(f"Comparing observed {perf_metric} with value {observed} to expected with value {expected} with rtol {rtol}")
            if abs(observed - expected) > rtol * abs(expected):
                error_message = f"Performance target was not met for {perf_metric}. Observed: {observed} Expected: {expected} rtol: {rtol}"
                raise get_raise_type(perf_metric)(observed, expected, error_message)
            else:
                print(f"Performance metric {perf_metric} between target and observed match.")
        else:
            print(f"WARNING: Skipping comparison for {perf_metric} since it was not populated in performance-metrics section of input test config")
    else:
        error_message = f"Performance metric comparison was not executed due to invalid observed: {observed} value"
        raise InvalidComparison(observed, expected, error_message)

def find_min_max_perf_results(reblocking_mode_perf_lib: ReblockTMPerfLib, op_type_str: str, all_perf_results):
    min_perf_results = None
    max_perf_results = None
    
    measurement_to_sort = "total_runtime"
    is_greater_better = False
    if (
        reblocking_mode_perf_lib == ReblockTMPerfLib.Gather or 
        reblocking_mode_perf_lib == ReblockTMPerfLib.Hstack or 
        reblocking_mode_perf_lib == ReblockTMPerfLib.Broadcast_r or
        op_type_str.lower() == "dram_read"
    ):
        measurement_to_sort = "input0_bw"
        is_greater_better = True

        for perf_results in all_perf_results:
            if max_perf_results is None or perf_results.input0_bw < max_perf_results.input0_bw:
                max_perf_results = perf_results
            if min_perf_results is None or perf_results.input0_bw > min_perf_results.input0_bw:
                min_perf_results = perf_results
    elif (
        op_type_str.lower() == "dram_write"
    ):
        measurement_to_sort = "output_bw"
        is_greater_better = True
        for perf_results in all_perf_results:
            if max_perf_results is None or perf_results.output_bw < max_perf_results.output_bw:
                max_perf_results = perf_results
            if min_perf_results is None or perf_results.output_bw > min_perf_results.output_bw:
                min_perf_results = perf_results
    elif (
        op_type_str.lower() == "matmul_sparse"
    ):
        measurement_to_sort = "average_math_utilization"
        is_greater_better = True

    for perf_results in all_perf_results:
        if is_greater_better:
            if max_perf_results is None or getattr(perf_results, measurement_to_sort) > getattr(max_perf_results, measurement_to_sort):
                max_perf_results = perf_results
            if min_perf_results is None or getattr(perf_results, measurement_to_sort) < getattr(min_perf_results, measurement_to_sort):
                min_perf_results = perf_results
        else:
            if max_perf_results is None or getattr(perf_results, measurement_to_sort) < getattr(max_perf_results, measurement_to_sort):
                max_perf_results = perf_results
            if min_perf_results is None or getattr(perf_results, measurement_to_sort) > getattr(min_perf_results, measurement_to_sort):
                min_perf_results = perf_results

    return min_perf_results, max_perf_results


def get_perf_info(perf_output_dir: str):
    perf_info_path = f"{perf_output_dir}/{PERF_INFO_FILE_NAME}"
    ASSERT(
        os.path.exists(perf_info_path),
        f"perf_info output file does not exist under {perf_info_path}. This might be because the test itself failed or it was run without perf dump enabled. Please check the test log."
    )

    perf_info = None
    with open(perf_info_path) as perf_info_file:
        perf_info = yaml.load(perf_info_file, Loader=yaml.FullLoader)

    return perf_info

def write_list_to_file(output_dir: str, file_name: str, inputs: List[str]) -> None:
    ASSERT(os.path.exists(output_dir), f"output directory does not exist {output_dir}.")
    file_path = output_dir + "/" + file_name
    with open(file_path, "w") as file:
        for input in inputs:
            file.write(input + "\n")
