# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
from typing import List, Dict
import subprocess
from signal import SIGTERM, SIGKILL
import shutil
import json
from enum import Enum
from perf_comparison import (
    PERF_INFO_FILE_NAME,
    per_epoch_events_key,
)
from postprocess_api import *
import csv
import yaml
from logger_utils import logger, ASSERT, COLORS

TEST_OUTPUT_DIR = "tt_build/last_test_output_dir"

class AnalysisMode(Enum):
    OverlayDecouple     = 1
    ForkJoin            = 2

def get_analysis_mode(arg: str) -> AnalysisMode:
    if arg.lower() == "overlay_decouple":
        return AnalysisMode.OverlayDecouple
    elif arg.lower() == "fork_join":
        return AnalysisMode.ForkJoin
    else:
        raise ValueError(f"Invalid analysis mode: {arg}")

class AnalysisConfig:
    def __init__(self, state):
        logger.debug("Initializing analysis configs")
        self.timeout            :int = state.timeout
        self.mode               :AnalysisMode = get_analysis_mode(state.analysis_mode)
        self.output_dir         :str = state.output_dir
        self.test_build_dir     :str = None if state.keep_all_build_directories else state.output_dir + f"/{TEST_OUTPUT_DIR}/"
        self.target_graphs      :List[str] = list(state.target_graphs.split(","))
        self.target_programs    :List[str] = list(state.target_programs.split(","))
        self.excluded_ops       :List[str] = list(state.exclude_op_list.split(","))
        self.test_cmd           :str = state.cmd
        self.skip_run           :bool = state.skip_test_run
        self.input_perf_results_dir: str = state.input_test_perf_results

        if self.skip_run:
            ASSERT(self.input_perf_results_dir != "", "If --skip-run is set, the input perf results directory path must be provided")
        else:
            ASSERT(len(state.cmd) > 0, f"There must be a valid test command provided to the sweep after a -- separator. Invalid test command {state.cmd}")
            ASSERT(self.output_dir != "", "--output-dir must be set if not running in --skip-test-run mode")

        self.reset_silicon_after_hangs :bool = state.reset_silicon_after_hang

        ASSERT(self.output_dir != "", "output test directory must be provided")
        if self.output_dir[-1] != "/":
            self.output_dir += "/"
        if os.path.exists(self.output_dir):
            shutil.rmtree(self.output_dir)
        os.makedirs(self.output_dir)
        if self.test_build_dir is not None:
            assert "tt_build" in self.test_build_dir
            if (self.test_build_dir[-1] != "/"):
                self.test_build_dir += "/"
            if os.path.exists(self.test_build_dir):
                shutil.rmtree(self.test_build_dir, ignore_errors=True)

'''
Takes a command as input and runs it as a subprocess.
Dumps the output of this process will be redirected to log_file.
'''
def run_single_test(cmd, log_file_path, timeout = None, myenv = None):
    # run_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    cmd_str = " ".join(cmd)
    env_var_str = ""
    for key, val in os.environ.items():
        if key in PERF_ANALYZER_ENV_VARS:
            env_var_str += f"{key}={val} "
    logger.info(f"Running test with command: {cmd_str}")
    logger.info(f"Running test with env vars: {env_var_str}")
    logger.info(f"Test log path: {log_file_path}")

    with open(log_file_path, 'a') as log_file:
        run_process = subprocess.Popen(cmd, stdout=log_file, stderr=subprocess.STDOUT, env=myenv)
    try:
        # write_piped_data_to_log(run_process)
        run_process.wait(timeout)
    except subprocess.TimeoutExpired:
        logger.error("Timeout reached")
        os.killpg(os.getpgid(run_process.pid), SIGTERM)
        run_process.wait()
        raise Exception("Timeout")
    except KeyboardInterrupt:
        logger.error("Keyboard interrupt")
        run_process.send_signal(SIGKILL)
        os.killpg(os.getpgid(run_process.pid), SIGTERM)
        run_process.wait()
        raise Exception("Keyboard interrupt")

"""
Run silicon reset script
Must only be used on machines that have a reset dongle
"""
def reset_silicon(log_file_path):
    cmd = [f"device/bin/silicon/reset.sh"]
    run_single_test(cmd, log_file_path)

def analysis_enabled_for_graph(analysis_config: AnalysisConfig, program: str, graph: str):
    program_en = (
        analysis_config.target_programs == "*" or
        program in analysis_config.target_programs
    )
    graph_en = (
        analysis_config.target_graphs == "*" or
        graph in analysis_config.target_graphs
    )
    return program_en and graph_en

"""
Run the original test and get the perf_info map
"""
def run_original_test(analysis_config: AnalysisConfig, output_dir: str, log_path: str):

    cmd_current_test = analysis_config.test_cmd.copy()
    os.environ[PERF_OUTPUT_DIR_ENV_VAR] = output_dir
    if analysis_config.test_build_dir is not None:
        cmd_current_test += [f"--outdir", analysis_config.test_build_dir]
    run_single_test(cmd_current_test, log_path, None)
    perf_info_path = f"{output_dir}/perf_results/{PERF_INFO_FILE_NAME}"
    ASSERT(
        os.path.exists(perf_info_path),
        f"perf_info output file does not exist under {perf_info_path}. This might be because the test itself failed or it was run without perf dump enabled. Please check the test log."
    )
    perf_info = None
    with open(perf_info_path) as perf_info_file:
        perf_info = yaml.load(perf_info_file, Loader=yaml.FullLoader)

    del os.environ[PERF_OUTPUT_DIR_ENV_VAR]
    
    return perf_info

def get_perf_output_dir_path(current_dir:str, program, graph):
    perf_info_path = f"{current_dir}/perf_results/{PERF_INFO_FILE_NAME}"
    if not os.path.exists(perf_info_path):
        logger.error(f"perf_info output file does not exist under {perf_info_path}. This might be because the test itself failed. Please check the test log.")
        raise Exception(f"")
    with open(perf_info_path) as perf_info_file:
        perf_info = yaml.load(perf_info_file, Loader=yaml.FullLoader)
    
    assert program in perf_info
    assert graph in perf_info[program]
    assert "output_directory" in perf_info[program][graph]

    perf_output_dir = perf_info[program][graph]["output_directory"]
    return perf_output_dir + "/"

def get_global_epoch_id(runtime_file_path):
    with open(runtime_file_path) as runtime_file:
        runtime_data = json.load(runtime_file)
    return runtime_data[per_epoch_events_key]["epoch-global-id"]

def append_to_csv_file(
    output_dir: str,
    data: List[str],
):
    with open(output_dir, 'a', encoding='UTF8', newline='') as csv_file:
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(list(data))

def append_to_csv_file_multiple_entries(
    output_dir: str,
    data: List[List[str]],
):
    for entry in data:
        append_to_csv_file(output_dir, entry)

def write_to_json_file(
    output_dir: str,
    json_data
):
    with open(output_dir, "w") as json_file:
        json.dump(json_data, json_file, indent=4)
        json_file.close()

def load_from_json_file(
    input_dir: str,
):
    with open(input_dir, "r") as json_file:
        data = json.load(json_file)
        json_file.close()

    return data

def get_analysis_csv_labels(analysis_config: AnalysisConfig) -> List[str]:
    if analysis_config.mode == AnalysisMode.OverlayDecouple:
        label = ("global_epoch_id", "program_name", "graph_name", "core_coord", "op_name", "first_to_last_input", "total_runtime", "math_utilization_all_inputs")
    else:
        raise Exception(f"Invalid analysis mode {analysis_config.mode}")
    
    return list(label)
    