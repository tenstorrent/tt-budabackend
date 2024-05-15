#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
import os
from logger_utils import logger, print_progress_bar, ASSERT
from typing import List
from perf_test_base import *
from subprocess import Popen, PIPE, STDOUT, TimeoutExpired
from signal import SIGTERM
import re

def kill_subprocess(run_process: Popen):
    os.killpg(os.getpgid(run_process.pid), SIGTERM)
    
    
def run_test(cmd: List[str], allow_fail: bool, run_only: bool=False, timeout=None):
    cmd_arr = cmd.copy()
    if run_only:
        cmd_arr.append("--run-only")
    cmd_str = " ".join(cmd_arr)
    logger.info(f"Running command: {cmd_str}")
    try:
        run_process = Popen(cmd_str, stdout=PIPE, universal_newlines=True, stderr=STDOUT, shell=True, preexec_fn=os.setsid)
        return_code = run_process.wait(timeout=timeout)
    except:
        kill_subprocess(run_process)
        raise
    if return_code and not allow_fail:
        raise RuntimeError(f"Test failed with return code {return_code}")
    output = run_process.stdout.read()
    return output

'''
Run the test num_runs times and record the observed perf value of each run
'''
def loop_test_and_profile_perf(cmd: List[str], num_runs: int, allow_fail: bool, always_compile: bool, timeout: float, log_output: bool):
    perf_expected_output_re = re.compile(r'Expected (\d*\.?\d+) Observed (\d*\.?\d+)')
    perf_minbound_output_re = re.compile(r'Min-Bound (\d*\.?\d+) Observed (\d*\.?\d+)')
    observed_perf_values = []
    for run_id in range(num_runs):
        print_progress_bar(run_id, num_runs)
        run_only = run_id > 0 and not always_compile
        output = run_test(cmd, allow_fail, run_only, timeout)
        if log_output:
            logger.info(output)
        m = list(perf_expected_output_re.finditer(output))
        if not m:
            m = list(perf_minbound_output_re.finditer(output))
            
        ASSERT(len(m) <= 1, f"More than 1 perf value match found in output. Output: {output}")
        ASSERT(len(m) > 0, f"No perf value was found in output, please check that the netlist has performance-check configurations. Output: {output}")
        
        observed_perf_values.append(float(m[0].group(2)))
        
    print_progress_bar(num_runs, num_runs)
    return observed_perf_values

if __name__ == "__main__":
    try:
        idx = sys.argv.index("--")
        script_args = sys.argv[1:idx]
        cmd_args = sys.argv[(idx + 1) :]
    except ValueError:
        script_args = sys.argv[1:]
        cmd_args = []

    logger.info(f"cmd_args: {cmd_args}")
    logger.info(f"script_args: {script_args}")

    ASSERT(len(cmd_args) != 0, "Unexpected cmd_args empty: please enter a valid test command")
    
    # parse arguments
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--num-runs", type=int, default=10, help="The number of times to run the test")
    parser.add_argument("--timeout", type=int, default=None, help="Timeout for each test run in seconds")
    parser.add_argument('--allow-fail', action="store_true", default=False, help="Allow test to fail, useful when expected perf value is not known")
    parser.add_argument('--print', action="store_true", default=False, help="Print the output of each test run")
    parser.add_argument('--always-compile', action="store_true", default=False, help="By default only the first test run triggers compile, set this parameter to compile on every run")
    
    args = parser.parse_args(script_args)
    
    observed_perf_values = loop_test_and_profile_perf(cmd_args, args.num_runs, args.allow_fail, args.always_compile, args.timeout, args.print)
    
    logger.info(f"Average observed perf value: {sum(observed_perf_values) / len(observed_perf_values):.4f} | Max observed perf value: {max(observed_perf_values):.4f} | Min observed perf value: {min(observed_perf_values):.4f}")