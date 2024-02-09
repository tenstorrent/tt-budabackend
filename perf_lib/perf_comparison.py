#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
import os
import json
from typing import List, Dict, Tuple, Union
from dataclasses import dataclass
from perf_test_base import PerfResults, get_diff_percentage, ROUND, get_average, get_worse
import re
from collections import OrderedDict
from copy import deepcopy
from postprocess_api import *
from logger_utils import ASSERT, logger
from enum import Enum

MAX_NUM_INPUTS_PERF = 8
MAX_NUM_OUTPUTS = 1

table_alignment    = "\t\t%-10s "
table_alignment   += "%-25s %-30s %-30s %-30s"
table_labels       = (
    "Input-idx",
    "Math-util", "Execution-cycles", "Total-wait-for-tile", "Total-wait-for-free-tile",
)

table_labels_diff  = (
    "Input-idx",
    "Math-util-change (%)", "Execution-cycles-change (%)", "Total-wait-for-tile-change (%)", "Total-wait-for-free-tile-change (%)",
)

class AccuracyFlags():
    def __init__(self):
        self.InvalidPipeBw = False
        self.EthernetOutput = []
        self.NotAllInputsRecorded = False

    def append_ethernet_output(self, outputs: List[str]):
        for output in outputs:
            if output not in self.EthernetOutput:
                self.EthernetOutput.append(output)

    def str(self):
        output = ""
        if self.InvalidPipeBw:
            output += "<< BW for some pipes not measured. >>"
        
        if self.NotAllInputsRecorded:
            output += f"<< Op has more than 8 input operands. Input operands with indices >= {MAX_NUM_INPUTS_PERF} are excluded from the analysis. >>"
        
        if len(self.EthernetOutput) > 0:
            output += f"<< Ethernet output pipe. Output pipe measurement may be inaccurate. For accurate pipe measurements see input pipes of ops {self.EthernetOutput} >>"
        
        return output

class CoreCoord():
    def __init__(self, x, y, op_name=""):
        self.x = x
        self.y = y
        self.op_name = op_name

    @classmethod
    def from_core_id_str(cls, core_id_str):
        core_op_regex = r"^(\d+)[-,](\d+)-?(\S*)$"
        if core_id_str[-1] == ':':
            core_id_str = core_id_str[:-1]
        regex_match = re.match(core_op_regex, core_id_str)
        assert regex_match is not None
        x, y, op_name = regex_match.group(1, 2, 3)
        return cls(x, y, op_name)
    
    def str(self, coord_only=False):
        if self.op_name and not coord_only:
            return f"{self.x},{self.y}-{self.op_name}"
        else:
            return f"{self.x},{self.y}"

    def __eq__(self, other):
        eq = True
        eq &= self.x == other.x
        eq &= self.y == other.y
        eq &= self.op_name == other.op_name
        return eq
    
    def __hash__(self):
        return hash((self.x, self.y, self.op_name))

class AppendMode(Enum):
    Average = 1,
    Min = 2,

def comparator(first, second, append_mode: AppendMode, *args):
    if append_mode == AppendMode.Average:
        return get_average(first, second, args[0])
    elif append_mode == AppendMode.Min:
        return get_worse(first, second, args[1])
    else:
        raise ValueError(f"Invalid AppendMode {append_mode}")

class InputPerf:
    def __init__(self, input_str, input_info, target_core: CoreCoord):
        self.num_inputs_averaged = 1
        self.input_str = input_str
        assert '-' in self.input_str
        delim_idx = self.input_str.index('-')
        self.input_idx = int(self.input_str[(delim_idx+1):])
        self.perf_results: PerfResults = PerfResults()

        self.perf_results.target_core                = target_core.str(coord_only=True)
        self.perf_results.target_input               = self.input_idx
        self.perf_results.math_utilization           = input_info[math_util_key]
        self.perf_results.execution_cycles           = input_info[runtime_key]
        self.perf_results.total_wait_for_tile        = input_info[wait_for_tile_key]
        self.perf_results.total_wait_for_free_tile   = input_info[wait_for_free_tile_key]

    def __eq__(self, other):
        equal = True
        equal &= self.input_str == other.input_str
        equal &= self.perf_results.target_core == other.perf_results.target_core
        if (not equal):
            print("Error: The two input reports are not identical.")
            print("First report:")
            self.print()
            print("Second report:")
            other.print()
        return equal
    
    def append_to_input(self, other, append_mode: AppendMode):
        
        ASSERT(self.input_str == other.input_str)
        ASSERT(self.perf_results.target_core == other.perf_results.target_core)

        if append_mode == AppendMode.Min:
            is_slower = other.perf_results.execution_cycles > self.perf_results.execution_cycles
            if is_slower:
                self.perf_results = other.perf_results
        
        else:
            self.perf_results.math_utilization = comparator(
                self.perf_results.math_utilization,
                other.perf_results.math_utilization,
                append_mode,
                self.num_inputs_averaged,
                True,
            )
            self.perf_results.execution_cycles = comparator(
                self.perf_results.execution_cycles,
                other.perf_results.execution_cycles,
                append_mode,
                self.num_inputs_averaged,
                False,
            )
            self.perf_results.total_wait_for_tile = comparator(
                self.perf_results.total_wait_for_tile,
                other.perf_results.total_wait_for_tile,
                append_mode,
                self.num_inputs_averaged,
                False,
            )
            self.perf_results.total_wait_for_free_tile = comparator(
                self.perf_results.total_wait_for_free_tile,
                other.perf_results.total_wait_for_free_tile,
                append_mode,
                self.num_inputs_averaged,
                False,
            )
        
        self.num_inputs_averaged += 1

    def diff(self, other):
        input_perf_diff = deepcopy(self)
        input_perf_diff.perf_results = input_perf_diff.perf_results.diff(other.perf_results)
        return input_perf_diff
    
    def print(self):
        values_to_print = (
            self.perf_results.target_input,
            ROUND(self.perf_results.math_utilization, 4), ROUND(self.perf_results.execution_cycles, 4), self.perf_results.total_wait_for_tile, self.perf_results.total_wait_for_free_tile,
        )
        print(table_alignment % values_to_print)

    def get_report_map(self, diff=False):
        report = OrderedDict()
        if diff:
            report["math-util-change (%)"] = ROUND(self.perf_results.math_utilization, 4)
            report["execution-cycles-change (%)"] = ROUND(self.perf_results.execution_cycles, 4)
            report["Total-wait-for-tile-change (%)"] = self.perf_results.total_wait_for_tile
            report["Total-wait-for-free-tile-change (%)"] = self.perf_results.total_wait_for_free_tile
        else:
            report["math-util"] = ROUND(self.perf_results.math_utilization, 4)
            report["execution-cycles"] = ROUND(self.perf_results.execution_cycles, 4)
            report["Total-wait-for-tile"] = self.perf_results.total_wait_for_tile
            report["Total-wait-for-free-tile"] = self.perf_results.total_wait_for_free_tile
        return report

def get_largest_bw_bound_factor_and_operand(
    input_bw,
    brisc_input_bw,
    output_bw,
    brisc_output_bw,
):
    biggest_bw_factor = 1
    biggest_bw_factor_operand = "N/A"
    any_unreported = False
    for i in range (MAX_NUM_INPUTS_PERF):
        input_brisc_bw = brisc_input_bw[i]
        input_trisc_bw = input_bw[i]
        if input_brisc_bw == 0 or input_trisc_bw == 0:
            any_unreported = True
        pipe_recorded = input_brisc_bw != "N/A"
        required_recorded = input_trisc_bw != "N/A"
        if pipe_recorded != required_recorded:
            any_unreported = True
        if input_brisc_bw != "N/A" and input_trisc_bw != "N/A" and input_brisc_bw != 0 and input_trisc_bw != 0:
            if input_trisc_bw > input_brisc_bw:
                factor = input_trisc_bw / input_brisc_bw
                if factor > biggest_bw_factor:
                    biggest_bw_factor = factor
                    biggest_bw_factor_operand = "input-" + str(i)
    
    if output_bw == 0 or brisc_output_bw == 0:
        any_unreported = True
    pipe_recorded = brisc_output_bw != "N/A"
    required_recorded = output_bw != "N/A"
    if pipe_recorded != required_recorded:
        any_unreported = True
    if output_bw != "N/A" and output_bw != 0 and brisc_output_bw != "N/A" and brisc_output_bw != 0:
        if biggest_bw_factor < (output_bw / brisc_output_bw):
            biggest_bw_factor = output_bw / brisc_output_bw
            biggest_bw_factor_operand = "output"
    return biggest_bw_factor, biggest_bw_factor_operand, any_unreported

def get_scaled_values(
    total_runtime,
    average_math_utilization,
    first_input_recorded,
    last_input_recorded,
    biggest_bw_factor,
):
    if total_runtime != "N/A" and biggest_bw_factor != "N/A":
        total_runtime_bw_scaled = total_runtime * biggest_bw_factor
    else:
        total_runtime_bw_scaled = "N/A"

    if total_runtime_bw_scaled != "N/A" and last_input_recorded != "N/A" and first_input_recorded != "N/A":
        runtime_per_input_bw_scaled = total_runtime_bw_scaled / (last_input_recorded - first_input_recorded + 1)
    else:
        runtime_per_input_bw_scaled = "N/A"

    if average_math_utilization != "N/A" and biggest_bw_factor != "N/A":
        average_math_utilization_bw_scaled = average_math_utilization / biggest_bw_factor
    else:
        average_math_utilization_bw_scaled = "N/A"
    
    return total_runtime_bw_scaled, runtime_per_input_bw_scaled, average_math_utilization_bw_scaled
            

class CorePerf:
    def __init__(self, core_str, core_info, aiclk):
        self.core_coord = CoreCoord.from_core_id_str(core_str)
        self.all_inputs: Dict[str, InputPerf] = {}
        self.num_cores_averaged         = 1
        self.aiclk                      = aiclk
        ASSERT(aiclk != "N/A")
        self.first_input_recorded       = core_info[first_input_recorded_key]
        self.last_input_recorded        = core_info[last_input_recorded_key]
        self.average_math_utilization   = core_info[average_utilization_key]
        self.total_runtime              = core_info[total_runtime_key]
        self.model_num_cycles           = core_info[model_num_cycles]
        self.model_math_utilization     = core_info[model_math_utilization]
        self.input_bw                   = []
        self.brisc_input_bw             = []
        self.trisc_input_tensor_size    = []
        self.trisc_output_tensor_size   = core_info[trisc_output_tensor_size_key]
        self.trisc_input_bw_from_runtime = []
        self.trisc_output_bw_from_runtime = "N/A"
        self.out_of_memory              = core_info[out_of_memory_key]
        if (core_info[last_pack_last_input_key] != "N/A" and core_info[first_unpack_first_input_key] != "N/A"):
            self.first_unpack_to_last_pack  = core_info[last_pack_last_input_key] - core_info[first_unpack_first_input_key]
        else:
            self.first_unpack_to_last_pack = "N/A"
        self.inaccuracy = AccuracyFlags()
        
        if self.total_runtime != "N/A" and self.last_input_recorded != "N/A" and self.first_input_recorded != "N/A":
            self.runtime_per_input = self.total_runtime / (self.last_input_recorded - self.first_input_recorded + 1)
        else:
            self.runtime_per_input = "N/A"
        
        for i in range(MAX_NUM_INPUTS_PERF):
            if input_bw_key(i) in core_info:
                self.input_bw.append(core_info[input_bw_key(i)])
            else:
                self.input_bw.append("N/A")
            
            if trisc_input_tensor_size_key(i) in core_info:
                self.trisc_input_tensor_size.append(core_info[trisc_input_tensor_size_key(i)])
                if (self.trisc_input_tensor_size[i] != "N/A" and self.first_unpack_to_last_pack != "N/A"):
                    self.trisc_input_bw_from_runtime.append(self.trisc_input_tensor_size[i] / (self.first_unpack_to_last_pack / (self.aiclk / 1000.0)))
                else:
                    self.trisc_input_bw_from_runtime.append("N/A")
            else:
                self.trisc_input_tensor_size.append("N/A")
                self.trisc_input_bw_from_runtime.append("N/A")
            
            if (brisc_input_bw_key(i) in core_info):
                self.brisc_input_bw.append(core_info[brisc_input_bw_key(i)])
            else:
                self.brisc_input_bw.append("N/A")            

        if output_bw_key in core_info:
            self.output_bw                  = core_info[output_bw_key]
        else:
            self.output_bw                  = "N/A"
        if brisc_output_bw_key in core_info:
            self.brisc_output_bw            = core_info[brisc_output_bw_key]
        else:
            self.brisc_output_bw            = "N/A"
        
        if packer_overlay_decoupled_output_bw_key in core_info:
            self.packer_overlay_decoupled_output_bw            = core_info[packer_overlay_decoupled_output_bw_key]
        else:
            self.packer_overlay_decoupled_output_bw            = "N/A"
        
        if self.trisc_output_tensor_size != "N/A" and self.first_unpack_to_last_pack != "N/A":
            self.trisc_output_bw_from_runtime = self.trisc_output_tensor_size / (self.first_unpack_to_last_pack / (self.aiclk / 1000.0))
        else:
            self.trisc_output_bw_from_runtime = "N/A"
        
        if self.brisc_output_bw != "N/A":
            ASSERT(self.packer_overlay_decoupled_output_bw, "Both brisc and packer recorded output bw in overlay decouple mode")
        
        self.biggest_bw_factor, self.biggest_bw_factor_operand, self.inaccuracy.InvalidPipeBw = get_largest_bw_bound_factor_and_operand(
            self.get_trisc_input_bw(),
            self.brisc_input_bw,
            self.get_trisc_output_bw(),
            self.get_output_decouple_bw(),
        )
        self.total_runtime_bw_scaled, self.runtime_per_input_bw_scaled, self.average_math_utilization_bw_scaled = get_scaled_values(
            self.total_runtime,
            self.average_math_utilization,
            self.first_input_recorded,
            self.last_input_recorded,
            self.biggest_bw_factor,
        )

        for input_str, input_info in core_info.items():
            if input_str[:len('input-')] == 'input-':
                self.all_inputs[input_str] = InputPerf(input_str, input_info, self.core_coord)
    

    def append_core_perf(self, other, append_mode: AppendMode):
        
        def is_append_valid(self, other):
            is_valid = (
                self.first_input_recorded == other.first_input_recorded and
                self.last_input_recorded == other.last_input_recorded and
                self.all_inputs.keys() == other.all_inputs.keys() and
                not other.out_of_memory
            )
            return is_valid
        
        ASSERT(self.core_coord == other.core_coord)
        if not is_append_valid(self, other):
            return
        
        ASSERT(self.all_inputs.keys() == other.all_inputs.keys())

        if append_mode == AppendMode.Min:
            if other.first_unpack_to_last_pack == "N/A":
                is_slower == True
            elif self.first_unpack_to_last_pack == "N/A":
                is_slower = False
            else:
                is_slower = other.first_unpack_to_last_pack > self.first_unpack_to_last_pack
            
            if is_slower:
                for input_str in self.all_inputs:
                    self.all_inputs[input_str] = other.all_inputs[input_str]

                self.average_math_utilization = other.average_math_utilization
                self.total_runtime = other.total_runtime
                self.input_bw = other.input_bw
                self.brisc_input_bw = other.brisc_input_bw
                self.trisc_input_bw_from_runtime = other.trisc_input_bw_from_runtime
                self.output_bw = other.output_bw
                self.brisc_output_bw = other.brisc_output_bw
                self.packer_overlay_decoupled_output_bw = other.packer_overlay_decoupled_output_bw
                self.trisc_output_bw_from_runtime = other.trisc_output_bw_from_runtime
                self.first_unpack_to_last_pack = other.first_unpack_to_last_pack
                self.biggest_bw_factor = other.biggest_bw_factor
                self.biggest_bw_factor_operand = other.biggest_bw_factor_operand
                self.total_runtime_bw_scaled = other.total_runtime_bw_scaled
                self.runtime_per_input_bw_scaled = other.runtime_per_input_bw_scaled
                self.average_math_utilization_bw_scaled = other.average_math_utilization_bw_scaled
                self.inaccuracy = other.inaccuracy
                self.out_of_memory = other.out_of_memory

        else:
            for input_str in self.all_inputs:
                self.all_inputs[input_str].append_to_input(other.all_inputs[input_str], append_mode)
            
            self.average_math_utilization = comparator(
                self.average_math_utilization,
                other.average_math_utilization,
                append_mode,
                self.num_cores_averaged,
                True,
            )
            self.total_runtime = comparator(
                self.total_runtime,
                other.total_runtime,
                append_mode,
                self.num_cores_averaged,
                False,
            )

            ASSERT(len(self.input_bw) == len(other.input_bw))
            ASSERT(len(self.brisc_input_bw) == len(other.brisc_input_bw))
            ASSERT(len(self.trisc_input_bw_from_runtime) == len(other.trisc_input_bw_from_runtime))

            for i in range(len(self.input_bw)):
                self.input_bw[i] = comparator(
                    self.input_bw[i],
                    other.input_bw[i],
                    append_mode,
                    self.num_cores_averaged,
                    True,
                )
            for i in range(len(self.brisc_input_bw)):
                self.brisc_input_bw[i] = comparator(
                    self.brisc_input_bw[i],
                    other.brisc_input_bw[i],
                    append_mode,
                    self.num_cores_averaged,
                    True,
                )
            for i in range(len(self.trisc_input_bw_from_runtime)):
                self.trisc_input_bw_from_runtime[i] = comparator(
                    self.trisc_input_bw_from_runtime[i],
                    other.trisc_input_bw_from_runtime[i],
                    append_mode,
                    self.num_cores_averaged,
                    True,
                )
            self.output_bw = comparator(
                self.output_bw,
                other.output_bw,
                append_mode,
                self.num_cores_averaged,
                True
            )
            self.brisc_output_bw = comparator(
                self.brisc_output_bw,
                other.brisc_output_bw,
                append_mode,
                self.num_cores_averaged,
                True
            )
            self.packer_overlay_decoupled_output_bw = comparator(
                self.packer_overlay_decoupled_output_bw,
                other.packer_overlay_decoupled_output_bw,
                append_mode,
                self.num_cores_averaged,
                True
            )
            self.trisc_output_bw_from_runtime = comparator(
                self.trisc_output_bw_from_runtime,
                other.trisc_output_bw_from_runtime,
                append_mode,
                self.num_cores_averaged,
                True
            )
            self.first_unpack_to_last_pack = comparator(
                self.first_unpack_to_last_pack,
                other.first_unpack_to_last_pack,
                append_mode,
                self.num_cores_averaged,
                False
            )
            self.biggest_bw_factor, self.biggest_bw_factor_operand, self.inaccuracy.InvalidPipeBw = get_largest_bw_bound_factor_and_operand(
                self.get_trisc_input_bw(),
                self.brisc_input_bw,
                self.get_trisc_output_bw(),
                self.get_output_decouple_bw()
            )
            self.total_runtime_bw_scaled, self.runtime_per_input_bw_scaled, self.average_math_utilization_bw_scaled = get_scaled_values(
                self.total_runtime,
                self.average_math_utilization,
                self.first_input_recorded,
                self.last_input_recorded,
                self.biggest_bw_factor
            )

        self.num_cores_averaged += 1
    
    def get_output_decouple_bw(self):
        if self.brisc_output_bw != "N/A":
            return self.brisc_output_bw
        elif self.packer_overlay_decoupled_output_bw != "N/A":
            return self.packer_overlay_decoupled_output_bw
        else:
            return "N/A"
    
    def get_trisc_input_bw(self, operand_idx = None):
        if ((operand_idx is not None) and operand_idx >= MAX_NUM_INPUTS_PERF):
            logger.warning(f"Performance results for operand_idx {operand_idx} for op {self.core_coord.op_name} is not recorded. Infra only supports up to {MAX_NUM_INPUTS_PERF} number of input operands.")
            return "N/A"
        if (self.last_input_recorded != "N/A" and self.first_input_recorded != "N/A" and self.last_input_recorded - self.first_input_recorded == 0):
            single_input = (self.last_input_recorded - self.first_input_recorded == 0)
        else:
            single_input = False
        if single_input:
            return self.trisc_input_bw_from_runtime[operand_idx] if (operand_idx is not None) else self.trisc_input_bw_from_runtime
        else:
            return self.input_bw[operand_idx] if (operand_idx is not None) else self.input_bw
    
    def get_brisc_input_bw(self, operand_idx: None):
        if ((operand_idx is not None) and operand_idx >= MAX_NUM_INPUTS_PERF):
            logger.warning(f"Performance results for operand_idx {operand_idx} for op {self.core_coord.op_name} is not recorded. Infra only supports up to {MAX_NUM_INPUTS_PERF} number of input operands.")
            return "N/A"
        return self.brisc_input_bw[operand_idx] if operand_idx is not None else self.brisc_input_bw

    def get_trisc_output_bw(self):
        if (self.last_input_recorded != "N/A" and self.first_input_recorded != "N/A" and self.last_input_recorded - self.first_input_recorded == 0):
            single_input = (self.last_input_recorded - self.first_input_recorded == 0)
        else:
            single_input = False
        if single_input:
            return self.trisc_output_bw_from_runtime
        else:
            return self.output_bw
    
    def get_full_perf_results_for_input(self, target_input) -> PerfResults:
        assert target_input in self.all_inputs
        perf_results = self.all_inputs[target_input].perf_results
        perf_results.first_input_recorded = self.first_input_recorded
        perf_results.last_input_recorded = self.last_input_recorded
        perf_results.average_math_utilization = self.average_math_utilization
        perf_results.total_runtime = self.total_runtime
        perf_results.input0_bw = self.input_bw[0]
        perf_results.input1_bw = self.input_bw[1]
        perf_results.output_bw = self.output_bw
        return perf_results
    
    def __eq__(self, other):
        equal = True
        equal &= self.core_coord.str()      == other.core_coord.str()
        equal &= len(self.all_inputs)       == len(other.all_inputs)
        
        if(self.first_input_recorded != "N/A" and other.first_input_recorded != "N/A" and self.last_input_recorded != "N/A" and other.last_input_recorded != "N/A"):
            equal &= self.first_input_recorded  == other.first_input_recorded
            equal &= self.last_input_recorded   == other.last_input_recorded
        
        for input_str in set(self.all_inputs.keys()).union(set(other.all_inputs.keys())):
            if input_str in self.all_inputs.keys() and input_str in other.all_inputs.keys():
                equal &= self.all_inputs[input_str] == other.all_inputs[input_str]
        if (not equal):
            print("Error: The two core reports are not identical.")
            print("First report:")
            self.print()
            print("Second report:")
            other.print()
        return equal
    
    def diff(self, other):
        core_diff = deepcopy(self)
        core_diff.average_math_utilization  = get_diff_percentage(core_diff.average_math_utilization, other.average_math_utilization)
        core_diff.total_runtime             = get_diff_percentage(core_diff.total_runtime, other.total_runtime)
        for input_str in set(self.all_inputs.keys()).union(other.all_inputs.keys()):
            if input_str in self.all_inputs.keys() and input_str in other.all_inputs.keys():
                core_diff.all_inputs[input_str] = core_diff.all_inputs[input_str].diff(other.all_inputs[input_str])
        return core_diff
    
    def print(self, diff=False):
        print(f"Core {self.core_coord.str()}")
        if diff:
            print(f"\t\tFirst-input-recorded = {self.first_input_recorded}, Last-input-recorded = {self.last_input_recorded}")
            print(f"\t\t- Average-math-utilization-change\t (%) = {ROUND(self.average_math_utilization, 4)}")
            print(f"\t\t- Total-runtime-change\t\t\t\t (%) = {ROUND(self.total_runtime, 4)}")
        else:
            print(f"\t\tFirst-input-recorded = {self.first_input_recorded}, Last-input-recorded = {self.last_input_recorded}")
            print(f"\t\t- Average-math-utilization\t = {ROUND(self.average_math_utilization, 4)}")
            print(f"\t\t- Total-runtime\t\t\t\t = {self.total_runtime}")
        if (len(self.all_inputs) > 0):
            if diff:
                print(table_alignment % table_labels_diff)
            else:
                print(table_alignment % table_labels)
        for input in self.all_inputs.values():
            input.print()
        
    def get_report_map(self, diff=False):
        report = OrderedDict()
        report["first-input-recorded"] = self.first_input_recorded
        report["last-input-recorded"] = self.last_input_recorded
        if diff:
            report["average-math-utilization-change (%)"] = ROUND(self.average_math_utilization, 4)
            report["total-runtime-change (%)"] = ROUND(self.total_runtime, 4)
        else:
            report["average-math-utilization"] = ROUND(self.average_math_utilization, 4)
            report["total-runtime"] = self.total_runtime
        for input_str, input_info in self.all_inputs.items():
            report[input_str] = input_info.get_report_map(diff=diff)
        return report

class AnalyzerOpPerf:
    def __init__ (self, core_perf: CorePerf, op_report_mode: AppendMode):
        self.op_report_mode: AppendMode = op_report_mode
        coord: CoreCoord = core_perf.core_coord # CoreCoord(core_perf.core_coord.x, core_perf.core_coord.y)

        self.op_name: str = core_perf.core_coord.op_name
        self.core_perf: Dict[CoreCoord, CorePerf] = {coord: core_perf}
        
        self.first_input_recorded: Union[int, str] = core_perf.first_input_recorded
        self.last_input_recorded: Union[int, str] = core_perf.last_input_recorded
        
        self.average_math_utilization: Union[float, int, str] = core_perf.average_math_utilization
        self.total_runtime: Union[int, float, str] = core_perf.total_runtime
        self.first_unpack_to_last_pack: Union[int, float, str] = core_perf.first_unpack_to_last_pack

        self.input_bw: List[Union[float, int, str]] = core_perf.input_bw
        self.brisc_input_bw: List[Union[float, int, str]] = core_perf.brisc_input_bw

        self.output_bw: Union[float, int, str] = core_perf.output_bw
        self.brisc_output_bw: Union[float, int, str] = core_perf.brisc_output_bw
        self.packer_overlay_decoupled_output_bw: Union[float, int, str] = core_perf.packer_overlay_decoupled_output_bw
        
        self.trisc_input_tensor_size: Union[float, int, str] = core_perf.trisc_input_tensor_size
        self.trisc_output_tensor_size: Union[float, int, str] = core_perf.trisc_output_tensor_size
        self.trisc_input_bw_from_runtime: Union[float, int, str] = core_perf.trisc_input_bw_from_runtime
        self.trisc_output_bw_from_runtime: Union[float, int, str] = core_perf.trisc_output_bw_from_runtime

        self.biggest_bw_factor: Union[float, int, str] = core_perf.biggest_bw_factor
        self.biggest_bw_factor_operand: str = core_perf.biggest_bw_factor_operand
        
        self.total_runtime_bw_scaled: Union[int, float, str] = core_perf.total_runtime_bw_scaled
        self.average_math_utilization_bw_scaled: Union[int, float, str] = core_perf.average_math_utilization_bw_scaled

        self.runtime_per_input: Union[int, float, str] = core_perf.runtime_per_input
        self.runtime_per_input_bw_scaled: Union[int, float, str] = core_perf.runtime_per_input_bw_scaled

        self.model_num_cycles: Union[float, int, str] = core_perf.model_num_cycles
        self.model_math_utilization: Union[float, int, str] = core_perf.model_math_utilization

        self.inaccuracy = core_perf.inaccuracy
    
    def append_core_perf(self, other: CorePerf):
        
        coord: CoreCoord = other.core_coord # CoreCoord(other.core_coord.x, other.core_coord.y)
        ASSERT(coord not in self.core_perf, f"Core coord {coord.str()} already exists in op perf for op {self.op_name}")
        num_cores_already_exist = len(self.core_perf)
        
        self.core_perf.update({coord: other})
        valid = (self.first_input_recorded == other.first_input_recorded) and (self.last_input_recorded == other.last_input_recorded)
        if not valid:
            self.average_math_utilization = "N/A"
            self.total_runtime = "N/A"
            self.input_bw = ["N/A"] * len(self.input_bw)
            self.brisc_input_bw = ["N/A"] * len(self.input_bw)
            self.output_bw = "N/A"
            self.brisc_output_bw = "N/A"
            self.packer_overlay_decoupled_output_bw = "N/A"
            self.model_num_cycles = "N/A"
            self.runtime_per_input = "N/A"
            self.biggest_bw_factor = "N/A"
            self.biggest_bw_factor_operand = "N/A"
            self.inaccuracy = AccuracyFlags()
        else:
            if (self.op_report_mode == AppendMode.Min):
                if other.total_runtime == "N/A":
                    is_slower = True
                elif self.total_runtime == "N/A":
                    is_slower = False
                else:
                    is_slower = other.total_runtime > self.total_runtime
                
                if not is_slower:
                    return
                
                self.average_math_utilization = other.average_math_utilization
                self.total_runtime = other.total_runtime
                self.first_unpack_to_last_pack = other.first_unpack_to_last_pack

                
                ASSERT(len(self.input_bw) == len(other.input_bw),
                    f"Error merging the two input_bw lists for op {coord.op_name}. Current num input_bw {len(self.input_bw)}, incomming num_input_bw {len(other.input_bw)}")
                ASSERT(len(self.brisc_input_bw) == len(other.brisc_input_bw),
                    f"Error merging the two brisc_input_bw lists for op {coord.op_name}. Current num input_bw {len(self.brisc_input_bw)}, incomming num_input_bw {len(other.brisc_input_bw)}")
                ASSERT(len(self.trisc_input_bw_from_runtime) == len(other.trisc_input_bw_from_runtime),
                    f"Error merging the two trisc_input_bw_from_runtime lists for op {coord.op_name}. Current num trisc_input_bw_from_runtime {len(self.trisc_input_bw_from_runtime)}, incomming trisc_input_bw_from_runtime {len(other.trisc_input_bw_from_runtime)}")
                self.input_bw = other.input_bw
                self.brisc_input_bw = other.brisc_input_bw
                self.trisc_input_bw_from_runtime = other.trisc_input_bw_from_runtime
                
                self.output_bw = other.output_bw
                self.brisc_output_bw = other.brisc_output_bw
                self.packer_overlay_decoupled_output_bw = other.packer_overlay_decoupled_output_bw
                self.trisc_output_bw_from_runtime = other.trisc_output_bw_from_runtime
                self.runtime_per_input = other.runtime_per_input

                self.biggest_bw_factor = other.biggest_bw_factor
                self.biggest_bw_factor_operand = other.biggest_bw_factor_operand
                self.average_math_utilization_bw_scaled = other.average_math_utilization_bw_scaled
                self.total_runtime_bw_scaled = other.total_runtime_bw_scaled
                self.runtime_per_input_bw_scaled = other.runtime_per_input_bw_scaled                

                self.model_num_cycles = other.model_num_cycles
                self.model_math_utilization = other.model_math_utilization
                self.inaccuracy = other.inaccuracy
            
            elif self.op_report_mode == AppendMode.Average:
                self.average_math_utilization = comparator(
                    self.average_math_utilization,
                    other.average_math_utilization,
                    num_cores_already_exist,
                    True
                )
                self.total_runtime = comparator(
                    self.total_runtime,
                    other.total_runtime,
                    num_cores_already_exist,
                    False
                )
                self.first_unpack_to_last_pack = comparator(
                    self.first_unpack_to_last_pack,
                    other.first_unpack_to_last_pack,
                    num_cores_already_exist,
                    False
                )
                
                ASSERT(len(self.input_bw) == len(other.input_bw),
                    f"Error merging the two input_bw lists for op {coord.op_name}. Current num input_bw {len(self.input_bw)}, incomming num_input_bw {len(other.input_bw)}")
                ASSERT(len(self.brisc_input_bw) == len(other.brisc_input_bw),
                    f"Error merging the two brisc_input_bw lists for op {coord.op_name}. Current num input_bw {len(self.brisc_input_bw)}, incomming num_input_bw {len(other.brisc_input_bw)}")
                
                for op_idx in range(len(self.input_bw)):
                    self.input_bw[op_idx] = comparator(
                        self.input_bw[op_idx],
                        other.input_bw[op_idx],
                        num_cores_already_exist,
                        True
                    )
                
                for op_idx in range(len(self.brisc_input_bw)):
                    self.brisc_input_bw[op_idx] = comparator(
                        self.brisc_input_bw[op_idx],
                        other.brisc_input_bw[op_idx],
                        num_cores_already_exist,
                        True
                    )
                
                for op_idx in range(len(self.trisc_input_bw_from_runtime)):
                    self.trisc_input_bw_from_runtime[op_idx] = comparator(
                        self.trisc_input_bw_from_runtime[op_idx],
                        other.trisc_input_bw_from_runtime[op_idx],
                        num_cores_already_exist,
                        True
                    )
                
                self.output_bw = comparator(
                    self.output_bw,
                    other.output_bw,
                    num_cores_already_exist,
                    True
                )
                self.brisc_output_bw = comparator(
                    self.brisc_output_bw,
                    other.brisc_output_bw,
                    num_cores_already_exist,
                    True
                )
                self.packer_overlay_decoupled_output_bw = comparator(
                    self.packer_overlay_decoupled_output_bw,
                    other.packer_overlay_decoupled_output_bw,
                    num_cores_already_exist,
                    True
                )
                self.trisc_output_bw_from_runtime = comparator(
                    self.trisc_output_bw_from_runtime,
                    other.trisc_output_bw_from_runtime,
                    num_cores_already_exist,
                    True
                )
                self.runtime_per_input = comparator(
                    self.runtime_per_input, 
                    other.runtime_per_input,
                    num_cores_already_exist,
                    False
                )

                self.biggest_bw_factor, self.biggest_bw_factor_operand, self.inaccuracy.InvalidPipeBw = get_largest_bw_bound_factor_and_operand(
                    self.get_trisc_input_bw(),
                    self.brisc_input_bw,
                    self.get_trisc_output_bw(),
                    self.get_output_decouple_bw()
                )
                self.total_runtime_bw_scaled, self.runtime_per_input_bw_scaled, self.average_math_utilization_bw_scaled = get_scaled_values(
                    self.total_runtime,
                    self.average_math_utilization,
                    self.first_input_recorded,
                    self.last_input_recorded,
                    self.biggest_bw_factor
                )
                
            else:
                raise Exception(f"Invalid AnalyzerOpPerfMode {self.op_report_mode}")

    
    def get_output_decouple_bw(self):
        if self.brisc_output_bw != "N/A":
            return self.brisc_output_bw
        elif self.packer_overlay_decoupled_output_bw != "N/A":
            return self.packer_overlay_decoupled_output_bw
        else:
            return "N/A"
    
    def get_trisc_input_bw(self, operand_idx = None):
        if ((operand_idx is not None) and operand_idx >= MAX_NUM_INPUTS_PERF):
            logger.warning(f"Performance results for operand_idx {operand_idx} for op {self.op_name} is not recorded. Infra only supports up to {MAX_NUM_INPUTS_PERF} number of input operands.")
            return "N/A"
        if (self.last_input_recorded != "N/A" and self.first_input_recorded != "N/A" and self.last_input_recorded - self.first_input_recorded == 0):
            single_input = (self.last_input_recorded - self.first_input_recorded == 0)
        else:
            single_input = False
        if single_input:
            return self.trisc_input_bw_from_runtime[operand_idx] if (operand_idx is not None) else self.trisc_input_bw_from_runtime
        else:
            return self.input_bw[operand_idx] if (operand_idx is not None) else self.input_bw

    def get_brisc_input_bw(self, operand_idx: None):
        if ((operand_idx is not None) and operand_idx >= MAX_NUM_INPUTS_PERF):
            logger.warning(f"Performance results for operand_idx {operand_idx} for op {self.op_name} is not recorded. Infra only supports up to {MAX_NUM_INPUTS_PERF} number of input operands.")
            return "N/A"
        return self.brisc_input_bw[operand_idx] if operand_idx is not None else self.brisc_input_bw
    
    def get_trisc_output_bw(self):
        if (self.last_input_recorded != "N/A" and self.first_input_recorded != "N/A" and self.last_input_recorded - self.first_input_recorded == 0):
            single_input = (self.last_input_recorded - self.first_input_recorded == 0)
        else:
            single_input = False
        if single_input:
            return self.trisc_output_bw_from_runtime
        else:
            return self.output_bw
    

class GraphPerf:
    def __init__(self, runtime_report_path):
        self.all_cores: Dict[CoreCoord, CorePerf] = {}
        self.analyzer_op_perf: Dict[str, AnalyzerOpPerf] = {}
        if (runtime_report_path == ""):
            self.device_id              = "N/A"
            self.aiclk                  = "N/A"
            self.num_cycles_per_tensor  = "N/A"
            self.num_tensors_per_second = "N/A"
            self.global_epoch_ids = [-1]
        else:
            with open(runtime_report_path) as runtime_file:
                runtime_json = json.load(runtime_file)
                self.device_id              = runtime_json[per_epoch_events_key]["device-id"]
                self.graph_name             = runtime_json[per_epoch_events_key]["graph-name"]
                self.program_names          = [runtime_json[per_epoch_events_key]["program-name"]]
                self.aiclk                  = runtime_json[per_epoch_events_key]["AICLK"]
                self.num_cycles_per_tensor  = runtime_json[per_epoch_events_key]["num-cycles-per-input"]
                self.global_epoch_ids       = [runtime_json[per_epoch_events_key]["epoch-global-id"]]
                if self.num_cycles_per_tensor != "N/A" and self.aiclk != "N/A":
                    self.num_tensors_per_second = (1.0 / self.num_cycles_per_tensor) * self.aiclk * 1000000
                else:
                    self.num_tensors_per_second = "N/A"
                for core_str, core_info in runtime_json.items():
                    if core_str != "per-epoch-events":
                        coord = CoreCoord.from_core_id_str(core_str)
                        core_perf = CorePerf(core_str, core_info, self.aiclk)
                        self.all_cores[coord] = core_perf

    @classmethod
    def from_core_map(cls, all_cores, global_epoch_id):
        graph = cls("")
        graph.all_cores = all_cores
        graph.global_epoch_ids = [global_epoch_id]
        return graph

    def populate_analyzer_op_perf(self):
        self.analyzer_op_perf = {}
        for coord, core_perf in self.all_cores.items():
            op_name = coord.op_name
            if op_name in self.analyzer_op_perf:
                self.analyzer_op_perf[op_name].append_core_perf(core_perf)
            else:
                self.analyzer_op_perf[op_name] = AnalyzerOpPerf(core_perf, AppendMode.Min)
    
    def append_graph(self, other, append_mode: AppendMode):
        ASSERT(self.aiclk == other.aiclk)
        ASSERT(self.device_id == other.device_id)
        ASSERT(self.all_cores.keys() == other.all_cores.keys())
        ASSERT(self.graph_name == other.graph_name)
        ASSERT(len(other.program_names) == 1)
        ASSERT(len(other.global_epoch_ids) == 1)

        self.num_cycles_per_tensor = comparator(
            self.num_cycles_per_tensor,
            other.num_cycles_per_tensor,
            append_mode,
            len(self.global_epoch_ids),
            False,
        )
        self.num_tensors_per_second = comparator(
            self.num_tensors_per_second,
            other.num_tensors_per_second,
            append_mode,
            len(self.global_epoch_ids),
            True,
        )
        for core_coord in self.all_cores:
            self.all_cores[core_coord].append_core_perf(other.all_cores[core_coord], append_mode)


        self.global_epoch_ids += other.global_epoch_ids
        self.program_names += other.program_names

    
    def __eq__(self, other):
        equal = True
        equal &= self.device_id == other.device_id
        equal &= self.aiclk == other.aiclk
        equal &= len(self.all_cores) == len(other.all_cores)
        for coord, core in self.all_cores.items():
            equal &= coord in other.all_cores
            equal &= core == other.all_cores[coord]
        if (not equal):
            print("Error: The two graph reports are not identical.")
            print("First report:")
            self.print()
            print("Second report:")
            other.print()
        return equal
    
    def diff(self, other):
        graph_diff = deepcopy(self)
        graph_diff.num_cycles_per_tensor    = ROUND(get_diff_percentage(graph_diff.num_cycles_per_tensor, other.num_cycles_per_tensor), 4)
        graph_diff.num_tensors_per_second   = ROUND(get_diff_percentage(graph_diff.num_tensors_per_second, other.num_tensors_per_second), 4)
        graph_diff.all_cores.clear()
        for coord, core in self.all_cores.items():
            # assert coord in other.all_cores
            if coord in other.all_cores:
                graph_diff.all_cores[coord] = core.diff(other.all_cores[coord])
        return graph_diff
    
    def print(self, diff=False):
        print(f"\tdevice-id = {self.device_id}, aiclk = {self.aiclk}")
        if diff:
            print(f"\t- Num-cycles-per-tensor-change\t (%) = {ROUND(self.num_cycles_per_tensor, 4)}")
            print(f"\t- Num-tensors-per-second-change\t (%) = {ROUND(self.num_tensors_per_second, 4)}")
        else:
            print(f"\tNum-cycles-per-tensor\t = {self.num_cycles_per_tensor}")
            print(f"\tNum-tensors-per-second\t = {self.num_tensors_per_second}")
        for coord, core in self.all_cores.items():
            print(f"  -------- Core {core.core_coord.str()}--------")
            core.print(diff=diff)
    
    def get_report_map(self, diff=False):
        report = OrderedDict()
        report["device-id"] = self.device_id
        report["aiclk"] = self.aiclk
        if diff:
            report["num-cycles-per-tensor-change (%)"] = ROUND(self.num_cycles_per_tensor, 4)
            report["num-tensors-per-second-change (%)"] = ROUND(self.num_tensors_per_second, 4)
        else:
            report["num-cycles-per-tensor"] = self.num_cycles_per_tensor
            report["num-tensors-per-second"] = self.num_tensors_per_second
        for coord, core in self.all_cores.items():
            report[core.core_coord.str()] = core.get_report_map(diff=diff)
        return report

    def get_cores_with_op_name(self, target_op_name) -> List[str]:
        all_target_core_labels = []
        for core_coord, core_perf in self.all_cores.items():
            if core_coord.op_name == target_op_name:
                all_target_core_labels.append(core_coord.str())
        return all_target_core_labels
    
    # Does not guarantee that every core will have recorded this last input
    def get_last_input_recorded_across_all_cores(self):
        last_input_recorded = -1
        for core_coord, core_perf in self.all_cores.items():
            if core_perf.last_input_recorded == "N/A":
                continue
            elif core_perf.last_input_recorded > last_input_recorded:
                last_input_recorded = core_perf.last_input_recorded
        return last_input_recorded

    def get_perf_results_for_target_op_last_input(self, target_op_name) -> Tuple[List[str], List[PerfResults]]:
        target_cores = self.get_cores_with_op_name(target_op_name)
        last_input_recorded = self.get_last_input_recorded_across_all_cores()
        last_input_recorded_str = f"input-{last_input_recorded}"
        all_perf_results = []
        for core_coord_str in target_cores:
            core_coord = CoreCoord.from_core_id_str(core_coord_str)
            assert core_coord in self.all_cores
            core_perf = self.all_cores[core_coord]
            assert last_input_recorded_str in core_perf.all_inputs, "Not all cores recorded the last input"
            perf_results = core_perf.get_full_perf_results_for_input(last_input_recorded_str)
            all_perf_results.append(perf_results)
        return target_cores, all_perf_results

class EpochProp:
    def __init__(self, path):
        if (path == ""):
            self.path = ""
            self.graph_name = ""
            self.program_name = ""
            self.graph_perf = GraphPerf("")
        else:
            self.path = path
            indices = [i.start() for i in re.finditer('/', path)]
            assert len(indices) > 1
            self.graph_name = path[(indices[-2] + 1): (indices[-1])]
            
            if len(indices) > 2:
                self.program_name = path[(indices[-3] + 1): (indices[-2])]
            else:
                self.program_name = None

            self.graph_perf = GraphPerf(path)
    
    @classmethod
    def from_graph_perf(cls, program, graph, graph_data):
        epoch = cls("")
        epoch.program_name = program
        epoch.graph_name = graph
        epoch.graph_perf = graph_data
        return epoch

    def __eq__(self, other):
        equal = True
        equal &= self.graph_name == other.graph_name
        equal &= self.program_name == other.program_name
        equal &= self.graph_perf == other.graph_perf
        if (not equal):
            print("Error: The two epoch reports are not identical.")
            print("First report:")
            self.print()
            print("Second report:")
            other.print()
        return equal
    
    def diff(self, other):
        epoch_diff = deepcopy(self)
        epoch_diff.graph_perf = epoch_diff.graph_perf.diff(other.graph_perf)
        return epoch_diff

    def print(self, diff=False):
        print(f"======== Program-name: {self.program_name} ======== Graph-name: {self.graph_name} ====================")
        print(f"graph_name = {self.graph_name}, program_name = {self.program_name}, path = {self.path}")
        self.graph_perf.print(diff=diff)

    def get_report_map(self, diff=False):
        report = OrderedDict()
        report["program-name"] = self.program_name
        report["graph-name"] = self.graph_name
        report["performance"] = self.graph_perf.get_report_map(diff=diff)
        return report

class AllOpPerf:
    def __init__(self, op_table_file_path):
        with open(os.path.join(op_table_file_path)) as op_file:
            op_data = json.load(op_file)

        assert "epoch-information" in op_data
        assert "epoch-global-id" in op_data["epoch-information"]
        global_epoch_id = op_data["epoch-information"]["epoch-global-id"]
    
        self.ops_across_epochs = {}
        for op_label in op_data:
            if(not op_label == "epoch-information"):
                assert op_label not in self.ops_across_epochs
                self.ops_across_epochs[op_label] = OpPerf(op_label, op_data[op_label], global_epoch_id)

class OpPerf:
    def __init__(self, op_name, op_info, global_epoch_id):
        self.global_epoch_id = global_epoch_id
        self.op_name = op_name
        self.op_metrics = {
            "math-utilization-across-cores": op_info["math-utilization-across-cores"],
            "num-cores": op_info["num-cores"],
            "runtime-across-cores": op_info["runtime-across-cores"],
            "input0-bw-across-cores-total-runtime": op_info["trisc-bw-total-runtime-operand-input-0"],
            "input1-bw-across-cores-total-runtime": op_info["trisc-bw-total-runtime-operand-input-1"],
            "output-bw-across-cores-total-runtime": op_info["trisc-bw-total-runtime-operand-output-0"],
        }
        first_core = [x  for x in op_info if not x in self.op_metrics][0]
        self.first_input_recorded = op_info[first_core]["first-input-recorded"]
        self.last_input_recorded = op_info[first_core]["last-input-recorded"]

    def __eq__(self, other):
        equal = True
        equal &= self.op_metrics.keys() == other.op_metrics.keys()
        equal &= self.op_name == other.op_name
        if(self.first_input_recorded != "N/A" and other.first_input_recorded != "N/A" and self.last_input_recorded != "N/A" and other.last_input_recorded != "N/A"):
            # Inputs indices must be equal if they are not N/A
            equal &= self.first_input_recorded  == other.first_input_recorded
            equal &= self.last_input_recorded   == other.last_input_recorded

        if not equal:
            print("Error: The two OP reports are not identical.")
            print("First Report:")
            self.print()
            print("Second Report:")
            other.print()
        
        return equal

    def print(self, diff = False):
        print(" =============== OP " + self.op_name + " Global Epoch ID: " + str(self.global_epoch_id) + " ===============")
        print(f"\t - First Input Recorded \t = {self.first_input_recorded}")
        print(f"\t - Last Input Recorded \t = {self.last_input_recorded}")
        if(diff):    
            print(f"\t - Math Utilization Across Cores Change (%) \t = {ROUND(self.op_metrics['math-utilization-across-cores'], 4)}")
            print(f"\t - Runtime Across Cores Change (%) \t = {ROUND(self.op_metrics['runtime-across-cores'], 4)}")
            
        else:
            print(f"\t - Math Utilization Across Cores \t = {self.op_metrics['math-utilization-across-cores']}")
            print(f"\t - Core Count \t = {self.op_metrics['num-cores']}")
            print(f"\t - Runtime Across Cores \t = {self.op_metrics['runtime-across-cores']}")        
        print("\n")

    def diff(self, other):
        op_diff = deepcopy(self)
        op_diff.op_metrics["math-utilization-across-cores"] = ROUND(get_diff_percentage(op_diff.op_metrics["math-utilization-across-cores"], other.op_metrics["math-utilization-across-cores"]), 4)
        op_diff.op_metrics["runtime-across-cores"] = ROUND(get_diff_percentage(op_diff.op_metrics["runtime-across-cores"], other.op_metrics["runtime-across-cores"]), 4)
        op_diff.op_metrics["input0-bw-across-cores-total-runtime"] = ROUND(get_diff_percentage(op_diff.op_metrics["input0-bw-across-cores-total-runtime"], other.op_metrics["input0-bw-across-cores-total-runtime"]), 4)
        op_diff.op_metrics["input1-bw-across-cores-total-runtime"] = ROUND(get_diff_percentage(op_diff.op_metrics["input1-bw-across-cores-total-runtime"], other.op_metrics["input1-bw-across-cores-total-runtime"]), 4)
        op_diff.op_metrics["output-bw-across-cores-total-runtime"] = ROUND(get_diff_percentage(op_diff.op_metrics["output-bw-across-cores-total-runtime"], other.op_metrics["output-bw-across-cores-total-runtime"]), 4)
        
        return op_diff

    def get_report_map(self, diff = False):
        report = OrderedDict()
        perf = OrderedDict()
        if(diff):
            perf["math-utilization-across-cores-change (%)"] = self.op_metrics["math-utilization-across-cores"]
            perf["runtime-across-cores-change (%)"] = self.op_metrics["runtime-across-cores"]
            
        else:
            perf["math-utilization-across-cores"] = self.op_metrics["math-utilization-across-cores"]
            perf["runtime-across-cores"] = self.op_metrics["runtime-across-cores"]
        
        report["op-name"] = self.op_name
        report["performance"] = perf
        report["global-epoch-id"] = self.global_epoch_id
        return report
        
def get_diff_report(run_0, run_1):
    diff_report = []
    assert len(run_0) == len(run_1)
    for idx in range(len(run_0)):
        data0 = run_0[idx]
        data1 = run_1[idx]
        diff = data0.diff(data1)
        diff_report.append(diff)
    return diff_report


def sort_all_epochs(all_epochs: List[EpochProp], best_to_worst):
    empty_val = float('-inf')
    if(not best_to_worst):
        empty_val *= -1
    all_epochs = sorted(all_epochs, key=lambda x: x.graph_perf.num_tensors_per_second if x.graph_perf.num_tensors_per_second != "N/A" else empty_val, reverse = best_to_worst)
    for epoch in all_epochs:
        epoch.graph_perf.all_cores = dict(sorted(epoch.graph_perf.all_cores.items(), key=lambda x: x[1].average_math_utilization if x[1].average_math_utilization != "N/A" else empty_val, reverse = best_to_worst))
        for core in epoch.graph_perf.all_cores.values():
            core.all_inputs = dict(sorted(core.all_inputs.items(), key=lambda x: x[1].perf_results.math_utilization if x[1].perf_results.math_utilization != "N/A" else empty_val, reverse=best_to_worst))
    return all_epochs

def sort_all_ops(all_ops: List[OpPerf], best_to_worst):
    empty_val = float('-inf')
    if(not best_to_worst):
        empty_val *= -1
    return sorted(all_ops, key = lambda x: x.op_metrics["math-utilization-across-cores"] if x.op_metrics["math-utilization-across-cores"] != "N/A" else empty_val, reverse = best_to_worst)
class AllPerfReports:
    def __init__(self, path: str, op_comparison: bool = False):
        self.all_epochs: List[EpochProp] = []
        self.ops_across_epochs : List[OpPerf] = []
        self.op_comparison = op_comparison

        if path != "":
            for root, dirs, files in os.walk(path, topdown = False):
                is_perf_dir = RUNTIME_FILE_NAME in files
                if is_perf_dir:
                    if(not self.op_comparison):
                        self.all_epochs.append(EpochProp(os.path.join(root, RUNTIME_FILE_NAME)))
                    else:
                        with open(os.path.join(root, "op_perf_table.json")) as op_file:
                            op_data = json.load(op_file)

                        assert "epoch-information" in op_data
                        assert "epoch-global-id" in op_data["epoch-information"]
                        global_epoch_id = op_data["epoch-information"]["epoch-global-id"]
                    
                        for op in op_data:
                            if(not op == "epoch-information"):
                                self.ops_across_epochs.append(OpPerf(op, op_data[op], global_epoch_id))
            self.ops_across_epochs = sorted(self.ops_across_epochs, key = lambda x: (x.op_name, x.global_epoch_id))
            assert len(self.all_epochs) or len(self.ops_across_epochs), f"No runtime report file was found under path {path}. Check the input --perf-output-dir-# args"
    
    @classmethod
    def from_all_epochs(cls, all_epochs: List[EpochProp]):
        prop = cls("")
        prop.all_epochs = all_epochs
        return prop

    def __eq__(self, other):
        if(not self.op_comparison):
            if(len(self.all_epochs) != len(other.all_epochs)):
                print( "The number of epochs across runs do not match")
                return False

            for epoch_idx, epoch in enumerate(self.all_epochs):
                if(epoch != other.all_epochs[epoch_idx]):
                    print("Epochs across runs cannot be compared (epoch properties are different)")
                    return False
        else:
            if(len(self.ops_across_epochs) != len(other.ops_across_epochs)):
                print("The total number of OPs across runs do not match")
                return False

            for op_idx, op in enumerate(self.ops_across_epochs):
                if(op != other.ops_across_epochs[op_idx]):
                    print("OPs across runs cannot be compared (OP properties are different)")
                    return False
        return True

    def print(self, diff = False):
        if(self.op_comparison):
            for op in self.ops_across_epochs:
                op.print(diff=diff)
        else:
            for epoch in self.all_epochs:
                epoch.print(diff=diff)

    def diff(self, other, sort_worst_to_best):
        if(self.op_comparison):
            diff_report = get_diff_report(self.ops_across_epochs, other.ops_across_epochs)
        else:
            diff_report = get_diff_report(self.all_epochs, other.all_epochs)
        
        if(self.op_comparison):
            diff_report = sort_all_ops(diff_report, not sort_worst_to_best)
        else:
            diff_report = sort_all_epochs(diff_report, not sort_worst_to_best)
        
        diff_report_obj = AllPerfReports.from_all_epochs(diff_report)
        return diff_report_obj
    
    def get_report_map(self, diff = False):
        json_report = OrderedDict()
        for entry_idx, data in enumerate(self.all_epochs):
            # data.print(diff = True)
            report_map = data.get_report_map(diff=diff)
            json_report.update(OrderedDict({f"entry-{entry_idx}": report_map}))
        return json_report

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
    parser.add_argument("--perf-output-dir-0", type=str, default="", help="The output directory for first perf results.")
    parser.add_argument("--perf-output-dir-1", type=str, default="", help="The output directory for second perf results.")
    parser.add_argument("--output-report-path", type=str, default="perf_diff.json", help="The output directory for second perf results.")
    parser.add_argument("--sort-best-to-worst", action="store_true", default=False, help="The results will be sorted in order of best to worst")
    parser.add_argument("--run-op-level-comparison", action = "store_true", default = False, help= "Clusters perf metrics by netlist operations instead of epochs.")
    state = parser.parse_args(script_args)
    
    run_1_perf = AllPerfReports(state.perf_output_dir_0, state.run_op_level_comparison)
    run_2_perf = AllPerfReports(state.perf_output_dir_1, state.run_op_level_comparison)

    assert run_1_perf == run_2_perf

    json_report = run_1_perf.diff(run_2_perf, not state.sort_best_to_worst)
    print("Difference between two perf-results:")
    json_report.print(diff=True)
    
    print(f"Writing the report in {state.output_report_path} ...", flush=True)
    json_report_map = json_report.get_report_map(diff=True)
    with open(state.output_report_path, 'w') as output_file:
        json.dump(json_report_map, output_file, indent=4)
