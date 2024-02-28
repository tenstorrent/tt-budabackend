#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import subprocess
import signal
import sys
import os
from typing import List
import yaml
import importlib
from perf_test_base import *
from ops import *
import csv
import shutil
import json
import time
from sweep_params import *
from perf_comparison import GraphPerf, AllOpPerf
from logger_utils import logger, COLORS, print_progress_bar
from perf_report import PerfReport
from enum import Enum

LOG_FAILED_TESTS_FILE_NAME = "all_failed_tests.yaml"
SWEEP_FILE_NAME = "perf_sweep_summary.yaml"
CSV_FILE_NAME = "perf_sweep_summary_sorted.csv"
XLSX_FILE_NAME = "perf_sweep_summary_sorted.xlsx"
CSV_FILE_NAME_TEMP = "perf_sweep_summary.csv"
CSV_FILE_NAME_SECOND_RUN = "perf_sweep_summary_compared_with_previous_run.csv"
XLSX_FILE_NAME_SECOND_RUN = "perf_sweep_summary_compared_with_previous_run.xlsx"
PERF_OUT_DIR_IDENTIFIER = "Perf output directory: "
TEST_OUTPUT_DIR = "tt_build/last_test_output_dir"

sys_add_path(REPO_ROOT, True)
sys_add_path(TEMPLATE_NETLIST_DIR, True)
sys_add_path(TEMPLATE_NETLIST_DIR + "/test_modules/", True)

from util import (
    create_netlist_from_single_config,
    load_yaml_test_configs,
    PerfTestType,
    PerfTestMode,
    PerfOpSweepConfig,
    ReblockTM,
    DramAssignment,
) # Imports util from verif/template_netlist/

from generate_tests import generate_all_configs

num_passed_tests = 0
failed_test_ids = []

class ReportType(Enum):
    Regular  = 1
    Combined = 2
    Diff     = 3
    
@dataclass
class PerfSweepDescriptor:
    '''
    PerfSweepDescriptor represents an option configuration descriptor for perf sweep.
    '''
    def __init__(self, script_args: List, cmd_args: List):
        self.state = self.parse_args_and_generate_state(script_args)
        self.perf_sweep_config: PerfOpSweepConfig = None
        self.without_tm_dir: str = ""
        self.output_dir: str = ""
        self.template_yaml: str = ""
        self.module_name: str = "test_perf"
        self.arch_name: str = os.getenv("ARCH_NAME", "grayskull")
        self.cmd: List = cmd_args.copy()
        self.all_modes_output_directory: List = []
        
        self.run_prologue: bool = self.state.run_prologue
        self.run_single_op: bool = self.state.run_single_op
        self.run_verbose_perf_trace: bool = self.state.run_verbose_perf_trace
        self.tm_test: bool = self.state.tm_test
        self.keep_build_dirs: bool = self.state.keep_build_dirs
        self.op_type_str: str = self.state.op_type
        self.reblocking_str: str = self.state.reblocking_mode
        self.output_dir: str = self.state.output_dir
        self.config_file: str = self.state.config_file
        self.config_only: bool = self.state.config_only
        self.config_overrides: str = self.state.config_overrides
        self.no_decouplings: bool = self.state.no_decouplings
        self.decouple: bool = not self.no_decouplings
        self.dram_assignment_mode: str = self.state.dram_assignment_mode
        self.skip_constraint_solver: bool = self.state.skip_constraint_solver
        self.verbose_report: bool = self.state.verbose_report
        self.start_from_index: int = self.state.start_from_index
        self.random_seed: int = self.state.random_seed
        self.update_existing_csv_path: str = self.state.update_existing_csv_path
        self.compare_with_csv: str = self.state.compare_with_csv
        self.perf_test_mode: PerfTestMode = get_perf_test_mode(self.state)
        self.reuse_fw_bin: bool = self.state.reuse_fw_bin
        
        self.op_type: OpType = get_op_type_from_arg(self.op_type_str, OpType)
        self.perf_test_type: PerfTestType = get_op_type_from_arg(self.op_type_str, PerfTestType)
        self.reblocking_mode: ReblockTM = get_reblocking_type_from_arg(self.reblocking_str, ReblockTM)
        self.reblocking_mode_perf_lib: ReblockTMPerfLib = get_reblocking_type_from_arg(self.reblocking_str, ReblockTMPerfLib)
        self.test_suite_directory: str = self.output_dir
        self.csv_output_path: str = self.test_suite_directory + "/" + CSV_FILE_NAME
        self.csv_temp_output_path: str = self.test_suite_directory + "/" + CSV_FILE_NAME_TEMP
        self.excel_output_path: str = self.test_suite_directory + "/" + XLSX_FILE_NAME
        self.diff_csv_path: str = os.path.join(self.test_suite_directory, CSV_FILE_NAME_SECOND_RUN)
        self.diff_xlsx_path: str = os.path.join(self.test_suite_directory, XLSX_FILE_NAME_SECOND_RUN)
    
    # Parse the command line arguments and return an argument namespace
    def parse_args_and_generate_state(self, script_args):
        # base arguments
        parser = argparse.ArgumentParser(description="")
        parser.add_argument("--output-dir", type=str, default="perf_lib/perf_sweep/", help="The output directory for sweep. It will contain the final results and all netlists and test logs.")
        # sweep arguments
        parser.add_argument("--op-type", type=str, default="",
            help="The op type to run the sweep for. Currently following ops are supported: matmul, matmul_bias, binary, unary, dram, dram_read, dram_write, fused_op_0, fused_op_1, fused_op_2, pcie, matmul_sparse, matmul_sparse_nz_counts, reduce_max, splice, tilizer, matmul_quant, matmul_bias_quant, quantization.",
        )
        parser.add_argument("--run-prologue", action="store_true", default=False, help="Run each test in prologue mode: No feeders. Only a drainer op that is decoupled. All input operands are in prologue mode (prefetched into l1).")
        parser.add_argument("--run-single-op", action="store_true", default=False, help="Run each test with only a single op reading and writing to dram.")

        parser.add_argument("--no-decouplings", action="store_true", default=False, help="Disables the decouplings for feeders and drainer.")
        parser.add_argument("--run-verbose-perf-trace", action="store_true", default=False,
            help="Run each test in perf sweep with --dump-perf-intermediate and --perf-level 1. This will increase the overhead of collecting perf, but will record wait-for-tile events."
        )
        parser.add_argument("--dram-assignment-mode", type=str, default="normal", help="Select how to allocate dram banks to each queue buffer. Options: normal (a hardcoded allocation), round_robin, random.")
        parser.add_argument("--verbose-report", action="store_true", default=False,
            help="If enabled, the final csv report will contain two sets of reports, one for the best core in the grid and one for the worst. It will also include the perf measurements for the last microbatch."
        )
        parser.add_argument("--reblocking-mode", type=str, default="Normal", help="Currently only supported when the target op is a datacopy. The available reblockings: normal (no reblockings), gather, hstack, broadcast_r.")
        parser.add_argument("--tm-test", action="store_true", default=False, help="Run in tm perf test mode. For each test with TMs another test with equivalent dims without TMs will be run as baseline. WARNING: has not been maintained.")
        parser.add_argument("--compare-with-csv", type=str, default="", help=f"If enabled, append perf results to the csv file in path <--netlists-output-dir>/{CSV_FILE_NAME_SECOND_RUN}. This should be used when comparing the results of two different sweeps.")

        parser.add_argument("--keep-build-dirs", action="store_true", default=False, help=f"Store all test output build directories. If this is not enabled, all output directories will overwrite each other")
        parser.add_argument("--skip-constraint-solver", action="store_true", default=False,
            help="Skip the constraint solver. In this mode, all variables inside sweep_params for the op must be overriden."
        )
        parser.add_argument("--random-seed", type=int, default=0, help="Random seed for z3 solver.")
        parser.add_argument("--timeout", type=int, default=None, help="Timeout in seconds to wait before terminating each test within the sweep.")

        parser.add_argument("--config-overrides", type=str, default="", help="""Dictionary with string keys and list values in json format.
                                                                        Example: --config_overrides '{"key1": [1, 2, 3], "key2": [4, 5, 6]}'""")
        parser.add_argument("--config-only", action="store_true", default=False, help="Only generate config file and exit.")
        parser.add_argument("--config-file", type=str, default="", help="Path to a yaml file with a dictionary with string keys and list values." )
        parser.add_argument("--start-from-index", type=int, default=0, help="Start from the given index in the config list.")
        parser.add_argument("--update-existing-csv-path", type=str, default="", help="Path to a csv file with the results of a previous sweep. The results of the current sweep will be appended to this file.")
        parser.add_argument("--reuse-fw-bin", action="store_true", default=False, help="Reuse BRISC and NCRISC binaries between the tests executed in a single perf sweep run.")

        state = parser.parse_args(script_args)
        
        assert state.op_type != ""
        
        return state
    
    # Generate a PerfOpSweepConfig that will be further used when generating z3 template parameters to run.
    def generate_perf_op_sweep_config(self, sweep_en=True):
        self.perf_sweep_config = PerfOpSweepConfig(
            sweep_en=sweep_en,
            sweep_vars=get_perf_sweep_vars_for_op(self.op_type, self.reblocking_mode_perf_lib),
            op_type=self.perf_test_type,
            perf_test_mode=self.perf_test_mode,
            reblocking_mode=self.reblocking_mode,
            dram_assignment_mode=get_dram_assignment_mode(self.dram_assignment_mode),
            skip_constraint_solver=self.skip_constraint_solver,
        )
    
    def set_template_yaml(self, test_module):
        self.template_yaml = get_template(test_module, self.perf_test_type, self.perf_test_mode)
        
    def set_without_tm_dir(self, without_tm_dir: str):
        self.without_tm_dir = without_tm_dir
        
    def set_output_dir(self, output_dir: str):
        self.output_dir = output_dir

'''
Parses the output log file and searches for PERF_OUT_DIR_IDENTIFIER to get the test output directory.
This will be used in --test-all-ops mode to find the list of all the ops, and find the summary report.
'''
def get_test_perf_output_dir(log_file_path, string: str = PERF_OUT_DIR_IDENTIFIER):

    line_with_addr = None
    
    with open(log_file_path, "r") as log:
        for line in log:
            if string in line:
                line_with_addr = line
                break
    
    assert line_with_addr is not None
    line_with_addr = line_with_addr[line_with_addr.find(PERF_OUT_DIR_IDENTIFIER):]
    line_with_addr = line_with_addr.replace(string, "")
    if '\n' in line_with_addr:
        line_with_addr = line_with_addr.replace('\n', '')
    return line_with_addr

def is_number(num_str: str):
    if num_str.isnumeric():
        return True
    else:
        try:
            float(num_str)
            return True
        except:
            return False
        
def parse_existing_csv(csv_path, attr_labels, perf_results_labels, attr_report_this_run):
    assert os.path.exists(csv_path)
    with open(csv_path, 'r') as csv_file:
        csv_reader = csv.reader(csv_file)
        csv_list = [rows[:] for rows in csv_reader]

    num_expected_attr = len(attr_labels)
    assert list(csv_list[0]) == list(attr_labels + perf_results_labels)
    
    # Skip the first row which are labels
    all_attr_perf_reports_prev = [row for row in csv_list[1:]]
    
    attrs_this_run = [[str(element) for element in row[1:]] for row in attr_report_this_run]
    
    # Order the previous report with the same attr order as this report
    all_attr_perf_reports_prev.sort(key=lambda row: attrs_this_run.index(row[1:num_expected_attr]))
    
    all_attr_reports_prev = [row[:num_expected_attr] for row in all_attr_perf_reports_prev]
    all_perf_reports_prev = [row[num_expected_attr:] for row in all_attr_perf_reports_prev]
            
    return all_attr_reports_prev, all_perf_reports_prev


def get_op_type_from_arg(op_name_arg: str, class_name):
    if op_name_arg.lower() == "matmul":
        return class_name.Matmul
    elif op_name_arg.lower() == "matmul_bias":
        return class_name.MatmulBias
    elif op_name_arg.lower() == "binary":
        return class_name.Binary
    elif op_name_arg.lower() == "unary":
        return class_name.Unary
    elif (
        op_name_arg.lower() == "dram" or
        op_name_arg.lower() == "dram_read" or
        op_name_arg.lower() == "dram_write"
    ):
        return class_name.Dram
    elif op_name_arg.lower() == "fused_op_0":
        return class_name.Fused0
    elif op_name_arg.lower() == "fused_op_1":
        return class_name.Fused1
    elif op_name_arg.lower() == "fused_op_2":
        return class_name.Fused2
    elif op_name_arg.lower() == "pcie":
        return class_name.Pcie
    elif op_name_arg.lower() == "matmul_sparse":
        return class_name.MatmulSparse
    elif op_name_arg.lower() == "matmul_sparse_nz_counts":
        return class_name.MatmulSparseNzCounts
    elif op_name_arg.lower() == "reduce_max":
        return class_name.ReduceMax
    elif op_name_arg.lower() == "splice":
        return class_name.Splice
    elif op_name_arg.lower() == "tilizer":
        return class_name.Tilizer
    elif op_name_arg.lower() == "matmul_quant":
        return class_name.MatmulQuant
    elif op_name_arg.lower() == "matmul_bias_quant":
        return class_name.MatmulBiasQuant
    elif op_name_arg.lower() == "quant":
        return class_name.Quant
    else:
        raise ValueError("Invalid module name")

def get_perf_test_mode(state):
    if state.run_prologue:
        return PerfTestMode.prologue
    elif state.run_single_op:
        return PerfTestMode.single_op
    else:
        return PerfTestMode.feeder_drainer

def get_dram_assignment_mode(dram_assignment):
    if dram_assignment == "normal":
        return DramAssignment.Normal
    elif dram_assignment == "round_robin":
        return DramAssignment.RoundRobin
    elif dram_assignment == "random":
        return DramAssignment.Random
    else:
        raise ValueError("Invalid dram assignment mode")
    
def get_reblocking_type_from_arg(reblocking: str, class_name):
    if reblocking.lower() == "normal":
        return class_name.Normal
    elif reblocking.lower() == "gather":
        return class_name.Gather
    elif reblocking.lower() == "hstack":
        return class_name.Hstack
    elif reblocking.lower() == "broadcast_r":
        return class_name.Broadcast_r
    else:
        raise ValueError("Invalid reblocking mode")

def get_template(test_module, perf_test_type: PerfTestType, perf_test_mode: PerfTestMode):
    if perf_test_type not in test_module.templates:
        raise ValueError(f"Invalid test type {perf_test_type}")
    if perf_test_mode not in test_module.templates[perf_test_type]:
        raise ValueError(f"Test mode {perf_test_mode} not supported for perf test type {perf_test_type}")
    
    return TEMPLATE_NETLIST_DIR + test_module.templates[perf_test_type][perf_test_mode]


def get_excel_configs(verbose_report: bool, report_type: ReportType):
    report_config = {
        "freeze_pane": "A2",
        "larger_is_better_labels": ["average-math-utilization"],
        "labels_to_highlight_graded_color": ["average-math-utilization"],
        "labels_to_bold": ["average-math-utilization", "total-runtime"],
    }
    if report_type == ReportType.Regular:
        if verbose_report:
            labels_to_separate = {
                "output_df": [1],
                "input-idx": [1],
                "core-label": [2],
                "input0-bw-across-cores-total-runtime": [1]
            }
        elif not verbose_report:
            labels_to_separate = {
                "output_df": [1],
                "first-input-idx-recorded": [1],
                "input0-bw-across-cores-total-runtime": [1]
            }

        report_config["labels_to_separate"] = labels_to_separate

    elif report_type == ReportType.Combined:
        if  verbose_report:
            labels_to_separate = {
                "output_df": [1],
                "input-idx": [],
                "core-label": [2, 4],
                "input0-bw-across-cores-total-runtime": [],
            }
        elif not verbose_report:
            labels_to_separate = {
                "output_df": [1],
                "first-input-idx-recorded": [],
                "input0-bw-across-cores-total-runtime": []
            }
        report_config["labels_to_separate"] = labels_to_separate

    elif report_type == ReportType.Diff:
        if verbose_report:
            labels_to_separate = {
                "output_df": [1],
                "input-idx": [],
                "core-label": [2, 4],
                "input0-bw-across-cores-total-runtime": [],
                "diff-input-idx%": [1]
            }
        elif not verbose_report:
            labels_to_separate = {
                "output_df": [1],
                "first-input-idx-recorded": [],
                "input0-bw-across-cores-total-runtime": [],
                "diff-first-input-idx-recorded%": [1]
            }

        report_config["labels_to_separate"] = labels_to_separate
    
    else:
        raise ValueError(f"Unrecognized report type {report_type}")
    
    return report_config

def get_excel_worksheet_name(sweep_desc: PerfSweepDescriptor, extra_str: str = ""):
    op_type_map = dict([(op_type, OpType(op_type).name) for op_type in OpType])

    assert sweep_desc.op_type in op_type_map
    op_str = op_type_map[sweep_desc.op_type]

    arch_str = ""
    arch_name = sweep_desc.arch_name.lower()
    if arch_name == "grayskull":
        arch_str  = "gs"
    elif arch_name == "wormhole" or arch_name == "wormhole_a0":
        arch_str = "wha0"
    elif arch_name == "wormhole_b0":
        arch_str = "whb0"
    else:
        raise ValueError(f"Unexpected ARCH NAME {arch_name}")
    
    run_type = "fd"
    if sweep_desc.run_prologue:
        run_type = "prl"
    elif sweep_desc.run_single_op:
        run_type = "sop"

    name = [op_str, arch_str, run_type]
    if extra_str:
        name.append(extra_str)

    return "-".join(name)

# Sweep over solution space and generate all viable test parameters that satisfy the constraints
def get_configs(sweep_desc: PerfSweepDescriptor):
    if sweep_desc.config_file:
        assert os.path.exists(sweep_desc.config_file) 
        with open(sweep_desc.config_file, 'r') as file:
            configs_dict = yaml.safe_load(file)
        configs_dict = configs_dict[sweep_desc.start_from_index:]
        return configs_dict

    start_time = time.time()
    logger.info(f"Generating the test-configs using the module {sweep_desc.module_name}")
    assert sweep_desc.module_name != "", "If input test-configs-yaml option is not specified, input test-module must be provided"
    logger.info(f"Writing the configs in following path: {sweep_desc.output_dir}")
    
    config_dict = generate_all_configs(
        module_name=sweep_desc.module_name,
        output_dir=sweep_desc.output_dir,
        random_seed=sweep_desc.random_seed,
        max_num_configs=None,
        dump_config_yaml=True,
        perf_config=sweep_desc.perf_sweep_config,
        arch=sweep_desc.arch_name,
    )
    elapsed_time = time.time() - start_time
    logger.info(f"Finished generating all configs. Elapsed time: {elapsed_time:.2f} seconds")
    return config_dict

'''
Takes a command as input and runs it as a subprocess.
Dumps the output of this process will be redirected to log_file.
'''
def run_single_test(cmd, log_file_path, timeout = None, myenv = None):
    # run_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    cmd_str = " ".join(cmd)
    logger.info(f"{COLORS['YELLOW']}Running command: {cmd_str} {COLORS['RESET']}(log: {log_file_path})")

    with open(log_file_path, 'a') as log_file:
        run_process = subprocess.Popen(cmd, stdout=log_file, stderr=subprocess.STDOUT, env=myenv)
    try:
        # write_piped_data_to_log(run_process)
        run_process.wait(timeout)
    except subprocess.TimeoutExpired:
        logger.error("Timeout reached")
        os.killpg(os.getpgid(run_process.pid), signal.SIGTERM)
        run_process.wait()
        raise Exception("Timeout")
    except KeyboardInterrupt:
        logger.error("Keyboard interrupt")
        run_process.send_signal(signal.SIGKILL)
        os.killpg(os.getpgid(run_process.pid), signal.SIGTERM)
        run_process.wait()
        raise Exception("Keyboard interrupt")

def check_test_pass(log_file_path):
    with open(log_file_path, "r") as log:
        for line in log:
            if "Finished perf postprocessor successfully" in line:
                return True
    return False

def run_test_from_netlist(netlist_path, log_file_path, decouple, op_type, run_light, reblocking_mode: ReblockTMPerfLib, output_dir, reuse_fw_bin, fw_bin_dir) -> str:
    env = os.environ.copy()

    if reuse_fw_bin:
        # BRISC and NCRISC compilation depend on the perf configuration; since we use the same perf configuration for a single perf_sweep run, we can compile them
        # only for the first test, and then reuse it for the rest to speed up the execution. Although ERISC compilation depends on the perf configuration as well,
        # we currently use the default binaries due to the following issue tenstorrent/budabackend#1707;
        # Once it is resolved, we should start caching ERISC as well
        env.update({f"BRISC_BIN_CACHE_DIR": f"{fw_bin_dir}/brisc", "NCRISC_BIN_CACHE_DIR": f"{fw_bin_dir}/ncrisc"})

    if op_type in [OpType.MatmulSparse, OpType.MatmulSparseNzCounts]:
        test_cmd = ["./build/test/verif/op_tests/test_op", "--skip-check", "--skip-golden"]
    else:
        test_cmd = ["./build/test/loader/tests/test_netlist_program"]
    test_cmd += ["--netlist", f"{str(netlist_path)}"]
    test_cmd += ["--silicon"]
    if run_light:
        test_cmd += ["--dump-perf-events"]
        test_cmd += ["--perf-level", "0"]
    else:
        test_cmd += ["--dump-perf-events-intermediate"]
        test_cmd += ["--perf-level", "1"]
    if reblocking_mode == ReblockTMPerfLib.Gather:
        test_cmd += ["--perf-op-mode", "feeder0:MathPack,feeder1:MathPack,drainer:UnpMath,target_op:UnpMath-MathPack"]
    elif op_type == OpType.Dram or op_type == OpType.Pcie:
        test_cmd += ["--perf-op-mode", "target_op:UnpMath-MathPack"]
    elif reblocking_mode == ReblockTMPerfLib.Hstack:
        test_cmd += ["--perf-op-mode", "feeder0:MathPack,feeder1:MathPack,drainer:UnpMath,target_op:UnpMath-MathPack"]
    elif reblocking_mode == ReblockTMPerfLib.Broadcast_r:
        test_cmd += ["--perf-op-mode", "feeder0:MathPack,feeder1:MathPack,drainer:UnpMath,target_op:UnpMath-MathPack"]
    elif decouple:
        num_input_operand = get_num_input_operands(op_type)
        test_cmd += ["--perf-op-mode"]
        decouple_str = ""
        for i in range(num_input_operand):
            decouple_str += f"feeder{i}:MathPack,"
        decouple_str += "drainer:UnpMath"
        test_cmd += [decouple_str]
    if output_dir is not None:
        test_cmd += ["--outdir", output_dir]
    run_single_test(test_cmd, log_file_path, myenv=env)
    return test_cmd

def get_perf_results_from_test(log_file_path, attr, override_target_op_name=None) -> List[PerfResults]:
    target_op_name = override_target_op_name or attr.target_op_name
    test_output_dir = get_test_perf_output_dir(log_file_path)
    epoch_perf_dir = test_output_dir + f"0000_program0/0000_test_op/"
    runtime_file_path = epoch_perf_dir + "runtime_table.json"
    op_perf_table_path = epoch_perf_dir + "op_perf_table.json"
    graph_perf = GraphPerf(runtime_file_path)
    target_cores, all_perf_results = graph_perf.get_perf_results_for_target_op_last_input(target_op_name)

    op_perf = AllOpPerf(op_perf_table_path)
    for perf_result in all_perf_results:
        if isinstance(attr, TilizerAttr):
            if perf_result.total_runtime != "N/A":
                perf_result.total_runtime_per_tile = int(perf_result.total_runtime / attr.total_num_tiles)

        perf_result.input0_bw_across_cores_total_runtime = op_perf.ops_across_epochs[target_op_name].op_metrics["input0-bw-across-cores-total-runtime"]
        perf_result.input1_bw_across_cores_total_runtime = op_perf.ops_across_epochs[target_op_name].op_metrics["input1-bw-across-cores-total-runtime"]
        perf_result.output_bw_across_cores_total_runtime = op_perf.ops_across_epochs[target_op_name].op_metrics["output-bw-across-cores-total-runtime"]

    return target_cores, all_perf_results

# sort the without-tm-test results by the test ids of with-tm-test results
def sort_entries_without_tm_test(with_tms_report: PerfReport, without_tms_report: PerfReport):
    def get_entry_with_test_id(test_id):
        for entry in without_tms_report.entries:
            if entry[0] == test_id:
                return entry
        raise Exception("Test id must exist in list of attributes without TMs")

    assert len(with_tms_report) == len(without_tms_report)
    sorted_without_tms_report = without_tms_report.new_empty_report_with_same_configs()
    for entry in with_tms_report.entries:
        test_id = entry[0]
        without_tms_entry = get_entry_with_test_id(test_id)
        sorted_without_tms_report.add_entry(without_tms_entry)
        
    return sorted_without_tms_report

def run_single_config_and_collect_results(
    sweep_desc: PerfSweepDescriptor,
    config,
    test_id,
    all_attr,
    report: PerfReport,
):
    output_dir = sweep_desc.output_dir
    _, table_alignment = get_report_label_and_alignment(get_empty_attr(sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib), sweep_desc.verbose_report, sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib)
    sweep_file_path = output_dir + SWEEP_FILE_NAME
    log_failed_tests_path = output_dir + LOG_FAILED_TESTS_FILE_NAME
    (netlist_dir, netlist_name) = create_netlist_from_single_config(
        template_yaml=sweep_desc.template_yaml,
        output_dir=output_dir,
        config=config,
        test_id=test_id)
    
    netlist_path = netlist_dir + "/" + netlist_name
    log_file_path = netlist_dir + "/test.log"
    if os.path.exists(log_file_path):
        os.remove(log_file_path)
    
    test_build_output_dir = None if sweep_desc.keep_build_dirs else output_dir + f"/{TEST_OUTPUT_DIR}/"
    if test_build_output_dir is not None:
        assert "tt_build" in test_build_output_dir
        if os.path.exists(test_build_output_dir):
            shutil.rmtree(test_build_output_dir, ignore_errors=True)

    test_cmd = run_test_from_netlist(
        netlist_path=netlist_path,
        log_file_path=log_file_path,
        decouple=sweep_desc.decouple,
        op_type=sweep_desc.op_type,
        run_light=(not sweep_desc.run_verbose_perf_trace),
        reblocking_mode=sweep_desc.reblocking_mode_perf_lib,
        output_dir=test_build_output_dir,
        reuse_fw_bin=sweep_desc.reuse_fw_bin,
        fw_bin_dir=sweep_desc.output_dir + "/fw_bin"
    )

    attr = get_attr(sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib, config)

    found_duplicate = False
    for other_attr in all_attr:
        if other_attr == attr:
            found_duplicate = True
            break
    if found_duplicate:
        logger.warning("Not recording information for this run, because this config has already been recorded.")
        return False
    else:
        all_attr.append(attr)
    
    global num_passed_tests, failed_test_ids
    if check_test_pass(log_file_path):
        num_passed_tests += 1
        _, all_perf_results = get_perf_results_from_test(log_file_path, attr)
    else:
        failed_test_ids.append(test_id)
        logger.error(f"Test failed... Adding log and config to reproduce to {log_failed_tests_path}")
        all_perf_results = [PerfResults()]
        with open(log_failed_tests_path, 'a') as failed_tests_log:
            failed_tests_log.write(f"# Test with ID {test_id} failed!")
            failed_test_info = dict()
            # failed_test_yaml = dict()
            failed_test_info["cmd"] = test_cmd
            with open(log_file_path, 'r') as log_file:
                lines = log_file.read().splitlines()
            
            failed_test_info["log"] = [lines[-10:]]
            failed_test_info["config"] = config
            yaml.dump(failed_test_info, failed_tests_log)
                    

    min_perf_results, max_perf_results = find_min_max_perf_results(sweep_desc.reblocking_mode_perf_lib, sweep_desc.op_type_str, all_perf_results)
    
    attr_report = attr.get_attr_report_single_run(test_id)
    perf_report = get_perf_report_single_run(min_perf_results, max_perf_results, sweep_desc.verbose_report, sweep_desc.op_type)

    if sweep_desc.reblocking_mode == ReblockTM.Hstack or sweep_desc.reblocking_mode == ReblockTM.Broadcast_r:
        if check_test_pass(log_file_path):
            _, feeder0_all_perf_results = get_perf_results_from_test(log_file_path, attr, "feeder0")
        else:
            feeder0_all_perf_results = [PerfResults()]
        feeder0_min_perf_results, feeder0_max_perf_results = find_min_max_perf_results(sweep_desc.reblocking_mode_perf_lib, sweep_desc.op_type_str, feeder0_all_perf_results)
        feeder0_perf_report = get_perf_report_single_run(feeder0_min_perf_results, feeder0_max_perf_results, sweep_desc.verbose_report, sweep_desc.op_type)
        perf_report += feeder0_perf_report

    PerfReport.append_row_to_csv(sweep_desc.csv_temp_output_path, attr_report + perf_report)
    if sweep_desc.update_existing_csv_path:
        assert os.path.exists(sweep_desc.update_existing_csv_path), f"Path {sweep_desc.update_existing_csv_path} does not exist."
        # change index to match correct test id in csv
        attr_report_modified = (test_id + sweep_desc.start_from_index,) + attr_report[1:]
        PerfReport.append_row_to_csv(sweep_desc.update_existing_csv_path, attr_report_modified + perf_report)

    results = attr_report + perf_report
    report.add_entry(results)

    with open(sweep_file_path, 'a') as output_file:
        output_file.write(table_alignment % results)
        output_file.write("\n")
        
def run_equivalent_test_without_tm(
    sweep_desc: PerfSweepDescriptor,
    tm_config,
    tm_test_id,
    all_attr,
    without_tms_report: PerfReport
):
    assert sweep_desc.op_type == PerfTestType.TM, "Currently TM perf tests are only supported for the op_type TM"
    attr = get_attr(sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib, tm_config)
    sweep_params = attr.get_sweep_parameters(True)
    config_list = generate_all_configs(
        module_name=sweep_desc.module_name,
        output_dir=sweep_desc.without_tm_dir,
        random_seed=sweep_desc.random_seed,
        max_num_configs=None,
        dump_config_yaml=False,
        perf_config=sweep_desc.perf_sweep_config,
        arch=sweep_desc.arch_name,
    )
    assert len(config_list) == 1, f"The sweep parameters should shrink the total space to exactly one valid config. Number of configs generated = {len(config_list)}"
    run_single_config_and_collect_results(
        sweep_desc==sweep_desc,
        config=config_list[0],
        test_id=tm_test_id,
        all_attr=all_attr,
        report=without_tms_report,
        output_dir=sweep_desc.without_tm_dir,
    )
    
def run_all_op_tests(sweep_desc: PerfSweepDescriptor, configs_dict, all_modes_reports: List[PerfReport]):
    
    sweep_file_path = sweep_desc.output_dir + SWEEP_FILE_NAME
    log_failed_tests_path = sweep_desc.output_dir + LOG_FAILED_TESTS_FILE_NAME
    if not os.path.exists(sweep_desc.output_dir):
        os.makedirs(sweep_desc.output_dir)
    if os.path.exists(sweep_file_path):
        os.remove(sweep_file_path)
    if os.path.exists(log_failed_tests_path):
        os.remove(log_failed_tests_path)
    
    all_attr = []

    all_attr_without_tms = []

    num_elements_each_attr = None
    num_elements_each_attr_without_tms = None
        
    label, table_alignment = get_report_label_and_alignment(get_empty_attr(sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib), sweep_desc.verbose_report, sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib)
    with open(sweep_file_path, "a") as output_file:
        output_file.write(table_alignment % label)
        output_file.write("\n")
    if sweep_desc.tm_test:
        tm_sweep_file_path = sweep_desc.without_tm_dir + SWEEP_FILE_NAME
        with open(tm_sweep_file_path, "a") as output_file:
            output_file.write(table_alignment % label)
            output_file.write("\n")

    total_num_configs = len(configs_dict)
    with_tms_report = all_modes_reports[0]

    for test_id, config in enumerate(configs_dict):
        print_progress_bar(test_id, total_num_configs)
        run_single_config_and_collect_results(
            sweep_desc=sweep_desc,
            config=config,
            test_id=test_id,
            all_attr=all_attr,
            report=with_tms_report,
        )
        if num_elements_each_attr is None and len(with_tms_report.entries) > 0:
            num_elements_each_attr = len(with_tms_report.entries[-1])
        else:
            assert num_elements_each_attr == len(with_tms_report.entries[-1])
        # print (f"{test_id + 1}/{total_num_configs} tests of current mode completed:", "%.2f" % ((test_id + 1) / total_num_configs * 100), "%\n")
        
        if sweep_desc.tm_test:
            logger.info (f"Running the equivalent test without TM's")
            assert(len(all_modes_reports) == 2)
            without_tms_report = all_modes_reports[1]
            run_equivalent_test_without_tm(
                sweep_desc=sweep_desc,
                tm_config=config,
                tm_test_id=test_id,
                all_attr=all_attr_without_tms,
                report=without_tms_report,
            )
            if num_elements_each_attr_without_tms is None and len(without_tms_report.entries) > 0:
                num_elements_each_attr_without_tms = len(without_tms_report.entries[-1])
            else:
                assert num_elements_each_attr == len(without_tms_report.entries[-1])
            logger.info (f"{test_id + 1}/{total_num_configs} tests without TM's completed:", "%.2f" % ((test_id + 1) / total_num_configs * 100), "%\n")
    
    print_progress_bar(total_num_configs, total_num_configs)
    
    if num_elements_each_attr is not None:
        with_tms_report.configs.add_label_sort_priority(get_empty_attr(sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib).get_sorted_labels())
        with_tms_report.sort_entries()
    if num_elements_each_attr_without_tms is not None:
        all_modes_reports[1] = sort_entries_without_tm_test(
            with_tms_report=with_tms_report,
            without_tms_report=without_tms_report
        )
        
def append_perf_diff(
    sweep_desc: PerfSweepDescriptor,
    base_attr_report,
    base_perf_report,
    new_attr_report,
    new_perf_report,
    perf_labels,
):
    # print(base_attr_report)
    # print(new_attr_report)
    assert get_empty_attr(sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib).check_entries_match(base_attr_report, new_attr_report)
    
    new_perf_labels = ()
    for label in perf_labels:
        new_label = f"diff-{label}%"
        new_perf_labels += (new_label,)
    perf_labels += new_perf_labels
    
    for row, perf_entry in enumerate(new_perf_report):
        diff_perf_entry = []
        for col, new_perf_val in enumerate(perf_entry):
            prev_perf_val = base_perf_report[row][col]
            new_perf_val_num = float(new_perf_val) if is_number(str(new_perf_val)) else new_perf_val
            prev_perf_val_num = float(prev_perf_val) if is_number(str(prev_perf_val)) else prev_perf_val
            diff_perf_val = get_diff_percentage(prev_perf_val_num, new_perf_val_num)
            diff_perf_entry.append(diff_perf_val)

        base_perf_report[row] += diff_perf_entry
    return perf_labels, base_perf_report

if __name__ == "__main__":
    try:
        idx = sys.argv.index("--")
        script_args = sys.argv[1:idx]
        cmd_args = sys.argv[(idx + 1) :]
    except ValueError:
        script_args = sys.argv[1:]
        cmd_args = []

    logger.info("Started running performance sweep")
    logger.debug(f"script command arguments: {script_args}")
    logger.debug(f"test command arguments: {cmd_args}")

    sweep_desc = PerfSweepDescriptor(script_args, cmd_args)

    logger.info(f"Running sweep for op type {sweep_desc.op_type} in output dir {sweep_desc.output_dir}")
    
    if sweep_desc.op_type == OpType.Tilizer:
        assert sweep_desc.perf_test_mode != PerfTestMode.feeder_drainer, "Feeder drainer currently not supported for tilizer op"

    if sweep_desc.reblocking_mode != ReblockTM.Normal:
        assert sweep_desc.op_type == OpType.Unary, "Reblocking sweeps are only supported for datacopy ops"
        
    sweep_desc.generate_perf_op_sweep_config()

    if sweep_desc.config_overrides:
        config_overrides = json.loads(sweep_desc.config_overrides)
        sweep_desc.perf_sweep_config.sweep_vars.update(config_overrides)

    test_module = importlib.import_module(sweep_desc.module_name)
    sweep_desc.set_template_yaml(test_module)

    if os.path.exists(sweep_desc.test_suite_directory):
        shutil.rmtree(sweep_desc.test_suite_directory)
    os.makedirs(sweep_desc.test_suite_directory)

    if os.path.exists(sweep_desc.csv_output_path):
        os.remove(sweep_desc.csv_output_path)
    if os.path.exists(sweep_desc.csv_temp_output_path):
        os.remove(sweep_desc.csv_temp_output_path)

    all_modes_reports: List[PerfReport] = []
    attr_label,_ = get_empty_attr(sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib).get_attr_labels_and_alignment()
    perf_label,_ = get_perf_labels_and_alignment(sweep_desc.verbose_report, sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib)

    total_label = attr_label + perf_label

    report_configs = get_excel_configs(sweep_desc, ReportType.Regular)
    with_tms_report = PerfReport(labels=total_label, name=get_excel_worksheet_name(sweep_desc))
    with_tms_report.set_configs(report_configs)
    all_modes_reports.append(with_tms_report)

    PerfReport.append_row_to_csv(sweep_desc.csv_temp_output_path, total_label)

    configs_dict = get_configs(sweep_desc)

    if sweep_desc.config_only:
        exit(0)
    
    start_time = time.time()
    if sweep_desc.run_prologue:
        logger.info("Running single op test with input queue prologue and with drainer")
        dir_name = "single_op_prologue_with_drainer"
        if sweep_desc.decouple:
            dir_name += "_with_decouplings"
        else:
            dir_name += "_without_decouplings"
        sweep_desc.set_output_dir(sweep_desc.test_suite_directory + f"/{dir_name}/")
        logger.info(f"Output directory is set to {sweep_desc.output_dir}")
        sweep_desc.all_modes_output_directory.append(sweep_desc.output_dir)
        run_all_op_tests(sweep_desc, configs_dict, all_modes_reports)
    elif sweep_desc.run_single_op:
        logger.info("Running single op test with input queue prologue without feeders and drainers")
        sweep_desc.set_output_dir(sweep_desc.test_suite_directory + "/single_op_prologue/")
        # TODO: check why we are doing this and if this is a typo
        assert sweep_desc.no_decouplings == False
        sweep_desc.decouple = False
        logger.info(f"Output directory is set to {sweep_desc.output_dir}")
        sweep_desc.all_modes_output_directory.append(sweep_desc.output_dir)
        run_all_op_tests(sweep_desc, configs_dict, all_modes_reports)
    else:
        dir_name = "feeder_drainer"
        if sweep_desc.decouple:
            dir_name += "_with_decouplings"
        else:
            dir_name += "_without_decouplings"
        sweep_desc.set_output_dir(sweep_desc.test_suite_directory + f"/{dir_name}/")
        sweep_desc.set_without_tm_dir(sweep_desc.test_suite_directory + f"/{dir_name}_without_tms/")
        sweep_desc.all_modes_output_directory.append(sweep_desc.output_dir)
        if sweep_desc.tm_test:
            without_tms_report = with_tms_report.new_empty_report_with_same_configs()
            all_modes_reports.append(without_tms_report)
            sweep_desc.all_modes_output_directory.append(sweep_desc.without_tm_dir)
            os.mkdir(sweep_desc.without_tm_dir)
        run_all_op_tests(sweep_desc, configs_dict, all_modes_reports)

    elapsed_time = time.time() - start_time
    logger.info(f"Finished running all generated tests. Elapsed time {elapsed_time:.2f} seconds")
    assert len(sweep_desc.all_modes_output_directory) == len(all_modes_reports)

    for i, report in enumerate(all_modes_reports):
        logger.info(f"Generating the csv file for mode index {i} in {sweep_desc.all_modes_output_directory[i] + CSV_FILE_NAME}")
        report.generate_csv(sweep_desc.all_modes_output_directory[i] + CSV_FILE_NAME)
        all_perf_labels = []
        all_entries = []
        assert len(report) == len(all_modes_reports[0]), "Reports to be combined should have the same length"
        all_perf_labels += all_modes_reports[i].labels[len(attr_label):]
        for entry in all_modes_reports[i]:
            if i == 0:
                all_entries.append(entry)
            elif i > 0:
                all_entries[i] += entry[len(attr_label):]

    if len(all_modes_reports) == 1:
        assert not sweep_desc.tm_test
        combined_report = all_modes_reports[0]
    else:
        assert sweep_desc.tm_test
        combined_report_configs = get_excel_configs(sweep_desc.verbose_report, ReportType.Combined)
        combined_report_configs.update({
            "label_sort_priority": attr_label[1:],
        })
        combined_report = PerfReport(labels=attr_label + all_perf_labels, initial_entries=all_entries, name=get_excel_worksheet_name(sweep_desc, "comb"))
        combined_report.set_configs(combined_report_configs)

    logger.info(f"Generating the csv file for combined modes in {sweep_desc.csv_output_path}")
    combined_report.generate_csv(sweep_desc.csv_output_path)
    # logger.info(f"Generating the excel file for combined modes in {state.excel_output_path}")
    # combined_report.create_excel_worksheet(state.excel_output_path)

    if sweep_desc.compare_with_csv != "":
        assert os.path.isfile(sweep_desc.compare_with_csv), f"Csv file {sweep_desc.compare_with_csv} does not exist or is not a valid csv file."
        with_tms_report = all_modes_reports[0]
        attr_report_this_run = [entry[0:len(attr_label)] for entry in with_tms_report.entries]
        perf_report_this_run = [entry[len(attr_label):] for entry in with_tms_report.entries]
        all_attr_reports_prev, all_perf_reports_prev = parse_existing_csv(sweep_desc.compare_with_csv, attr_label, perf_label, attr_report_this_run)
        prev_perf_label = perf_label
        perf_label, all_perf_reports_prev = append_perf_diff(
            sweep_desc=sweep_desc,
            base_attr_report=all_attr_reports_prev,
            base_perf_report=all_perf_reports_prev,
            new_attr_report=attr_report_this_run,
            new_perf_report=perf_report_this_run,
            perf_labels=perf_label,
        )
        
        assert len(with_tms_report.entries) == len(all_perf_reports_prev)
        attr_0_str = [[str(attr) for attr in row] for row in attr_report_this_run]
        attr_1_str = [[str(attr) for attr in row] for row in all_attr_reports_prev]

        if not get_empty_attr(sweep_desc.op_type, sweep_desc.reblocking_mode_perf_lib).check_entries_match(attr_report_this_run, all_attr_reports_prev):
            logger.warning("Could not generate comparison report because attributes of this run and previous run do not match.")
        else:
            diff_labels = with_tms_report.labels + list(perf_label)
            diff_report_configs = get_excel_configs(sweep_desc.verbose_report, ReportType.Diff)
            diff_entries = [with_tms_report.entries[i] + all_perf_reports_prev[i] for i in range(len(with_tms_report.entries))]
            diff_report = PerfReport(labels=diff_labels, initial_entries=diff_entries, name=get_excel_worksheet_name(sweep_desc, "diff"))
            diff_report.set_configs(diff_report_configs)
            logger.info(f"Generating the csv file for comparison between this run and and prev run in {sweep_desc.diff_csv_path}")
            diff_report.generate_csv(sweep_desc.diff_csv_path)
            # logger.info(f"Generating the excel file for comparison between this run and and prev run in {state.diff_xlsx_path}")
            # diff_report.create_excel_worksheet(state.diff_xlsx_path)   


    logger.info(f"Finished perf sweep. {num_passed_tests} tests passed, {len(failed_test_ids)} tests failed with test ids {failed_test_ids}")
