#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

# A script to run RBG generated standalone op tests for perf benchmarking.

import argparse
import sys
import signal, time
import glob, json
import os, yaml, re, subprocess, shlex
# from compile import run as compile
# from run import run as run_test
from datetime import datetime
import pprint
import shutil
import itertools
import multiprocessing
from pathlib import Path
from run_in_perf_mode import LOG_FILE

import perf_lib

from prettytable import PrettyTable
from prettytable import PLAIN_COLUMNS
import xlsxwriter

Exit_Success = 0
Exit_Failure = 1

ROOT = os.environ["ROOT"] if "ROOT" in os.environ else subprocess.getoutput("git rev-parse --show-toplevel")
ROOT = str(Path(__file__).parent.absolute()) + "/../.."

sys.path.append(f"{ROOT}/scripts")

from util import make

#################################################################################
# Nightly Regression Ops                                                        #
#################################################################################

# Add tests to this list when they are ready to be included in nightly perf regression
ALL_OPS_FOR_REGRESSION = [
    'tt_eltwise_binary_bcast_op',
    'tt_eltwise_binary_op',
    'tt_eltwise_nary_op',
    'tt_eltwise_unary_op',
    'tt_eltwise_unary_sfpu_op',
    'tt_matmul_accumulate_op',
    'tt_matmul_op',
    'tt_reduce_accumulate_op',
    'tt_reduce_op',
    'tt_transpose_xy_op'
]

# These are the perflib yaml (cpp/rbg-generated) ops to be included for regression when run with --perflib-yamls
# Thought about being fancy here and generating this with loops, but being explicit
# makes it easier to copy these to/from the command line for individual testing of ops.
ALL_PERFLIB_OPS_FOR_REGRESSION = [
    'tt_matmul_op',
    'tt_eltwise_unary_sfpu_op<SfpuOp::Exp, DstMode::Tile>',
    'tt_eltwise_unary_sfpu_op<SfpuOp::Gelu, DstMode::Tile>',
    # No RBG Support Yet 'tt_eltwise_unary_sfpu_op<SfpuOp::Reciprocal, DstMode::Tile>',
    # No RBG Support Yet 'tt_eltwise_unary_sfpu_op<SfpuOp::Sqrt, DstMode::Tile>',
]


# Ops we care about for perf-bert
# ^ cat perf_bert.log | grep "tt_.*_op" | grep cpp | sed "s#\.# #g"  | sed "s#.*tt_#tt_#g" | sed "s#>.*#>#g" | sed "s#  .*##g" | sort -u
# tt_batched_matmul_op
# tt_eltwise_binary_bcast_op<BinaryOp::Add, Dim::R, DstMode::Tile>
# tt_eltwise_binary_bcast_op<BinaryOp::Add, Dim::ZR, DstMode::Tile>
# tt_eltwise_binary_bcast_op<BinaryOp::Multiply, Dim::C, DstMode::Tile>
# tt_eltwise_binary_bcast_op<BinaryOp::Multiply, Dim::R, DstMode::Tile>
# tt_eltwise_binary_bcast_op<BinaryOp::Multiply, Dim::RC, DstMode::Tile>
# tt_eltwise_binary_bcast_op<BinaryOp::Subtract, Dim::C, DstMode::Tile>
# tt_eltwise_binary_op<BinaryOp::Add, DstMode::Tile>
# tt_eltwise_binary_op<BinaryOp::Multiply, DstMode::Tile>
# tt_eltwise_unary_op<DstMode::Tile>
# tt_eltwise_unary_sfpu_op<SfpuOp::Exp, DstMode::Tile>
# tt_eltwise_unary_sfpu_op<SfpuOp::Gelu, DstMode::Tile>
# tt_eltwise_unary_sfpu_op<SfpuOp::Reciprocal, DstMode::Tile>
# tt_eltwise_unary_sfpu_op<SfpuOp::Sqrt, DstMode::Tile>
# tt_gather_2D_planes_op
# tt_hslice_block_op
# tt_matmul_op
# tt_reduce_op<ReduceFunc::Avg, Dim::C>
# tt_reduce_op<ReduceFunc::Sum, Dim::C>
# tt_scatter_2D_planes_op
# tt_transpose_xy_op

# TODO List of stuff ###################################

# Resolve Common Args into Tests / Variants
# Send email (function to return email recipients)
# [x] Generate XLS with results
# [x] error checking/reporting.  Catch if fail RBG generation, and don't try to compile and run test.
# [x] Runtime/Compile Time number of jobs remaining/run
# Directed mode for RBG somehow
# [x] Results table changes, percentage compare against model
# [x] Copy compile time logs to perf_tests_out
# [x] Make log names more consistent between compile/run (compile_test_perf_matmul_ff2.log, run_test_op_name_tt_matmul_op_variant__testname_perf_bert_ff2_0.log)
# RBG : Disable tile checker for when perf decouple mode is enabled (since would fail)
# runtime arguments for rbg tests (like --min-inputs/--max-inputs)
# Generate --min-tensor-dim-c and --max-tensor-dim-c from something like --force-tensor-dim-c or --const-tensor-dim-c
# Explicitly specify perf-decouple-modes for different ops (target_op, feeder_op, drainer_op). Right now feeder/drainer are hardcoded in script.


#################################################################################
# Top-Level Configuration                                                       #
#################################################################################

DEV_ALLOW_DEFAULT = 0   # For development, allow default perf config file.
DEBUG = 0               # Extra verbosity during debug/dev

# Excel built-in colors
GREEN='#00B050'
YELLOW='#FFFF00'
RED='#FF0000'

pp = pprint.PrettyPrinter(indent=4)

if "DEVICE_RUNNER" not in os.environ:
    device_runner = "Model"
    os.environ["DEVICE_RUNNER"] = device_runner
else:
    device_runner = os.environ["DEVICE_RUNNER"]

device_runner_lc = device_runner.lower()

FILE                    = os.path.basename(__file__)
PERF_DUMP_FLAGS         = ["--dump-perf-events-intermediate"]
COMPILE_AND_RUN_FLAGS   = ["--compile-backend"]
LLK_OP_PERF_TESTS_MODES = ["input_bw", "compute_throughput", "output_bw"] # FIXME Resolve during integration
PERF_CONFIG_DIR         = f"{ROOT}/perf_lib/ops"
PERF_OUTDIR             = "perf_tests_out"
RBG_OUTDIR              = f"build/test/rbg/test/gen"
RBG_PATH                = f"build/test/rbg/test/rbg"
TEST_DIR                = "model/tests"

# Python Generated Tests
OP_TEST_YAMLS_DIR       = f"{ROOT}/model/tests/op_tests/op_yamls"
TEST_GEN_SCRIPT_PATH    = f"{ROOT}/model/tests/op_tests/generate_tests.py"


#################################################################################
# Helper Functions                                                              #
#################################################################################

# Some code to run a process safely and return return-code to caller.
# NEW - Force pass to ignore return code in some known cases (perf tests with decoupling
# gives expected tile mismatches, need to disable tile checking eventually).
def run_command(cmd_str, state, log_file, force_pass=False):

    # Convert to list since must use shell=False for kill signals to work.
    cmd_list = shlex.split(cmd_str)
    timed_out = False

    print("(DRY_RUN)" if state.dry_run else "", f"running: \"{cmd_str}\"  Result: ", flush=True, end='')

    if not state.dry_run:

        # Test flow has built in 2hr timeout via set_device_timeout(). Overide it with env-var here,
        # and set subprocess timeout 1 min longer so device timeout is hit first (nice assert in log).
        subprocess_timeout = state.timeout + 60
        os.environ["TIMEOUT"] = str(state.timeout)

        with open(log_file, 'a') as log:
            run_process = subprocess.Popen(cmd_list, shell=False, stdout=log, stderr=subprocess.STDOUT)
        try:
            run_process.wait(subprocess_timeout)
        except subprocess.TimeoutExpired:
            # Explicitly send SIGTERM, debugger.cpp handles this signal to serialize intermediate data
            run_process.send_signal(signal.SIGTERM)
            try:
                run_process.wait(3)  # Give 3 seconds to write out data and exit normally
            except subprocess.TimeoutExpired:
                run_process.send_signal(
                    signal.SIGUSR1
                )  # special signal for compile_run.py to kill child
                time.sleep(1)
                run_process.send_signal(signal.SIGKILL)
            timed_out = True
        except KeyboardInterrupt:
            run_process.send_signal(signal.SIGKILL)
            run_process.wait()
            raise

        ret_code = run_process.returncode

        # Parse log file in case this is a test, to see if timeout was hit, for reporting purposes.
        with open(log_file) as log:
            for line in log:
                if "Device TIMEOUT reached" in line:
                    timed_out = True
                    break

        # Print pass/fail and log file path only in the case of failure.
        if (ret_code == 0):
            print(perf_lib.style("OK", color="green", bold=True), flush=True)
        elif force_pass:
            print(perf_lib.style("FORCED_OK", color="green", bold=True), flush=True)
            ret_code = 0
        else:
            if timed_out:
                print(perf_lib.style("FAIL_TIMEOUT", color="red", bold=True), flush=True, end='')
            else:
                print(perf_lib.style("FAIL", color="red", bold=True), flush=True, end='')
            print(f" (See: {log_file})", flush=True)

    else:
        ret_code = 0
        print(perf_lib.style("SKIPPED", color="green", bold=True), flush=True)

    return ret_code

# Conditionally print a message if debug enabled.
def debug_print(msg: str):
    if DEBUG:
        print(msg)



# Extend one dict by another and return extended dict
def merge_dicts(a, b, path=None):
    "merges b into a"
    if path is None: path = []
    for key in b:
        if key in a:
            if isinstance(a[key], dict) and isinstance(b[key], dict):
                merge_dicts(a[key], b[key], path + [str(key)])
            elif a[key] == b[key]:
                pass # same leaf value
            else:
                a[key] = b[key] # Override A with B
                # raise Exception('Conflict at %s' % '.'.join(path + [str(key)]))
        else:
            a[key] = b[key]
    return a


# By request, give the command for running this script for each single test
def get_single_test_rerun_cmd(test, state):

    # Get list of current arguments for this script
    args = ' '.join(state.script_args)

    # Remove a bunch of flags that will be automatically provided next.
    args = re.sub("\s*--all-ops", '', args)
    args = re.sub("\s*--op\s+\S+", '', args)
    args = re.sub("\s*--test\s+\S+", '', args)
    args = re.sub("\s*--skip-build", '', args)
    args = re.sub("\s*--j\s*\d+", '', args)
    args = re.sub("\s*-j\s*\d+", '', args)
    args = re.sub("\s*--runs-per-test\s*\d+", '', args)

    script = sys.argv[0]

    cmd = f"DEVICE_RUNNER={device_runner} {script} {args} -j1 --op {test.op_name} --test {test.name}"

    # If any run command arguments provided, add them at the end of the command line, as expected.
    if state.run_cmd_args:
        cmd += ' -- ' + ' '.join(state.run_cmd_args)

    print(f"INFO: Rerun test cmd (compile+run+report): \"{cmd}\"")


# Check if this test name is targeted with --test or --test-regex flags, to skip undesired tests.
def is_this_test_targeted(state, test_name):

    # Can supply multiple test names with --test, look through them all.
    if state.test and test_name not in state.test:
        return False

    # Can supply multiple regexes with --test-regex, look through them all.
    if state.test_regex:
        test_matched_regex = False
        for test_regex in state.test_regex:
            if re.search(test_regex, test_name):
                test_matched_regex = True
        if not test_matched_regex:
            return False

    return True


#################################################################################
# Classes                                                                       #
#################################################################################

# Suites -> Ops -> Tests

# A collection of op variants is a suite
class Suite():

    def __init__(self, name: str):
        self.name = name
        self.ops = []
        self.result_idx = 1

        # XLSX Summary report related fields.
        self.xlsx_worksheet_row = 0
        self.xlsx_worksheet_col_widths = []
        self.xlsx_field_name_list = [] # Store a single row of field names
        self.xlsx_row_data_list = [] # Store a single result's row of field data
        self.xlsx_curr_worksheet_name = "" # Created just in time
        self.workbook = None
        self.worksheet = None

    # Load optional perf config file for the op if it exists
    def load_op_perf_configs(self, args, op_name: str, op_variant: str):

        op_perf_config_file         = f"{PERF_CONFIG_DIR}/{op_name}_perf_config.yaml"
        default_perf_config_file    = f"{PERF_CONFIG_DIR}/default_perf_config.yaml"
        perf_config_file            = None

        op_configs_list = []

        # If op has a perf config file, use it. Otherwise, use default perf config file.
        if os.path.isfile(op_perf_config_file):
            perf_config_file = op_perf_config_file
        elif DEV_ALLOW_DEFAULT and os.path.isfile(default_perf_config_file):
            perf_config_file = default_perf_config_file
        else:
            assert False, f"Could not find {op_perf_config_file} or {default_perf_config_file}, something is wrong."

        debug_print(f"Loading contents from {perf_config_file} for op_name: {op_name} op_variant: {op_variant}")
        yaml_data = yaml.safe_load(open(perf_config_file))

        if (DEBUG > 1):
            pp.pprint(yaml_data)

        # Load common config shared across all variants (optional)
        variant_common_config = yaml_data["common"] if "common" in yaml_data else None

        if (DEBUG > 1):
            pp.pprint(variant_common_config)

        # Determine the list of variants supported from the yaml file
        assert "variants" in yaml_data , "must define variants in perf config yaml"
        yaml_variants = yaml_data["variants"] # Shortcut

        for perf_config_variant in yaml_variants:

            # Decode the op name/flavor from the config yaml file, make sure yaml writer didn't put wrong op in the config
            # file, and if this is not a variant wanted to be run, skip it. Use the explicit variant if supplied, or all
            # variants for the op that are defined in config yaml, if the user running the script was not specific.
            # If default perf config yaml file was loaded, override fields to be suitable for op under test
            if perf_config_variant == "default":
                perf_config_op_name, perf_config_op_variant = [op_name, op_variant]
                debug_print(f"Overriding to be op_name: {op_name} op_variant: {op_variant}")
            else:
                perf_config_op_name, perf_config_op_variant = perf_lib.get_op_name_and_variant(perf_config_variant)
                assert perf_config_op_name == op_name , f"{op_perf_config_file} has unexpected op {perf_config_op_name}"

            if op_variant is not None and op_variant != perf_config_op_variant:
                debug_print(f"Skipping {perf_config_op_variant} from perf config file since not target op variant")
                continue

            test_common_config = yaml_variants[perf_config_variant]["common"] if "common" in yaml_variants[perf_config_variant] else None

            if (DEBUG > 1):
                pp.pprint(test_common_config)

            op_to_test = Op(name=perf_config_op_name, variant=perf_config_op_variant)

            assert "tests" in yaml_variants[perf_config_variant] , "must define tests data for each variant"

            # Capture rbg-config (required) and perf-config (optional) per test, and add to test object
            for test_name in yaml_variants[perf_config_variant]["tests"]:


                if not is_this_test_targeted(args, test_name):
                    continue

                # Development feature to multiply and get more tests
                for run_idx in range(args.runs_per_test):

                    yaml_test = yaml_variants[perf_config_variant]["tests"][test_name] # Shortcut
                    run_idx_resolved = run_idx if args.runs_per_test > 1 else None # Just pass None if running default 1 test to not apply label
                    test = Test(test_name=test_name, op_name=op_name, op_variant=op_variant, config_file=perf_config_file, run_idx=run_idx_resolved)

                    # Determine if RBG Test or CPP based directed test
                    if ("rbg-config" in yaml_test and not "cpp-test-config" in yaml_test):
                        test.set_rbg_config(yaml_test["rbg-config"], perf_config_variant, perf_config_file)
                    elif ("cpp-test-config" in yaml_test and not "rbg-config" in yaml_test):
                        test.set_cpp_test_config(yaml_test["cpp-test-config"], perf_config_variant, perf_config_file)
                    else:
                        assert False, f"must define either rbg-config or cpp-test-config for {perf_config_variant} test {test_name} in {op_perf_config_file}"

                    test.set_test_filename_info()

                    if "perf-config" in yaml_test:
                        test.set_perf_config(yaml_test["perf-config"])

                    test.set_log_files()

                    op_to_test.add_test(test)

            op_configs_list.append(op_to_test)

        # Make sure desired variant was found in the config file, otherwise a typo was likely
        assert len(op_configs_list) != 0 , f"Did not find {op_name}{op_variant} defined in {op_perf_config_file}"

        return op_configs_list

    # Load optional perf config file for the op if it exists (from new python generated test flow yaml files)
    def load_op_perf_configs_generated_tests(self, args, op_name: str, op_variant: str):

        # Need op name, and op variant. FIXME - These use to come from perf-config file.
        op_to_test = Op(name=op_name, variant=op_variant)

        test_yaml_files_pattern = f"{OP_TEST_YAMLS_DIR}/{op_name}/test*.yaml"

        all_test_yaml_files = glob.glob(test_yaml_files_pattern)
        assert len(all_test_yaml_files) != 0, f"Could not find any files under {test_yaml_files_pattern}"

        op_configs_list = []

        # Tests may be defined across many .yaml files
        for test_yaml_file in all_test_yaml_files:
            debug_print(f"Loading contents from {test_yaml_file} for op_name: {op_name}")
            test_yaml_contents = yaml.safe_load(open(test_yaml_file)) or {}
            if (DEBUG > 1):
                pp.pprint(test_yaml_contents)

            # Make sure tests were found
            assert test_yaml_contents , f"Did not find any tests for op_name: {op_name} under {test_yaml_files_pattern}"

            for (test_name, yaml_test) in test_yaml_contents.items():

                if not is_this_test_targeted(args, test_name):
                    continue

                # Extract and set the template variant of the op.
                op_variant = "<" + ",".join(yaml_test['template_args']) + ">" if 'template_args' in yaml_test else ""

                # Development feature to multiply and get more tests
                for run_idx in range(args.runs_per_test):

                    run_idx_resolved = run_idx if args.runs_per_test > 1 else None # Just pass None if running default 1 test to not apply label

                    test = Test(test_name=test_name, op_name=op_name, op_variant=op_variant, config_file=test_yaml_file, run_idx=run_idx_resolved)

                    test.set_gen_test_config(yaml_test)
                    test.set_test_filename_info()

                    if "perf-config" in yaml_test:
                        test.set_perf_config(yaml_test["perf-config"])

                    test.set_log_files()

                    # Only add perf tests if desired, and functional tests if desired.
                    if ((test.perf_config['perf-test'] and not args.no_perf_tests) or (not test.perf_config['perf-test'] and not args.no_func_tests)):
                        op_to_test.add_test(test)

        op_configs_list.append(op_to_test)

        return op_configs_list

    # Determine list of ops and variants to run tests for.
    def get_op_and_test_list_to_run(self, args):

        # Either run everything tagged for regression. Or if command line ops are specified
        # run the variants specified, or all variants of an op if no variant is specified.
        if args.perflib_yamls:
            list_of_ops = (ALL_PERFLIB_OPS_FOR_REGRESSION if args.all_ops else args.op)
        else:
            list_of_ops = (ALL_OPS_FOR_REGRESSION if args.all_ops else args.op)

        for op_str in list_of_ops:
            # Decode op name and variant from command line provided strings, then
            # get list of op variants and their tests.
            op_name, op_variant = perf_lib.get_op_name_and_variant(op_str)

            # Determine which set of op yamls we will read - those for generated tests, or perflib rbg/cpp based yaml tests.
            if args.perflib_yamls:
                op_configs = self.load_op_perf_configs(args, op_name, op_variant)
            else:
                op_configs = self.load_op_perf_configs_generated_tests(args, op_name, op_variant)

            self.ops.extend(op_configs)

            # The list for all ops is already variantized, so len 1 is expected.
            if args.all_ops:
                assert len(op_configs) == 1 , "Only 1 op config expected to be returned"

    # For informational purposes, print the final decided list of ops, variants and their tests.
    def print_ops_and_tests_to_run(self):

        print("\n%s : %s - Printing overview of ops and tests to run...\n" % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), FILE), flush=True)

        # FIXME - Consider using library to print nicer table with more info.
        print("%-5s %-30s %-45s %-60s %-10s %-15s" %("id", "op_name", "op_variant", "test_name", "type", "en_checkers"))
        print("=" * 170)

        tid = 0
        for op in self.ops:
            for test in op.tests:
                print("%-5d %-30s %-45s %-60s %-10s %-15s" %(tid, op.name, op.variant, test.name, test.test_type, False))
                tid +=1
        print("\n")

    # Build any prereq tools (rbg) if needed
    def build_prereqs(self, args):

        print("\n%s : %s - Going to build any prereqs...\n" % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), FILE), flush=True)

        ret_code = 0
        build_rbg = False
        for op in self.ops:
            for test in op.tests:
                if test.test_type == "RBG":
                    build_rbg = True

        if build_rbg:
            print("Going to build RBG...")
            ret_code |= make(target="rbg/test/rbg", timeout=args.timeout, j=args.jobs)

        return ret_code

    # Generate all tests (serially, very quick)
    def generate_all_tests(self, args):

        fail_count = 0

        # Only include RBG or PYTHON tests that need generation.
        test_list = []
        for op in self.ops:
            for test in op.tests:
                if test.test_type == "RBG" or test.test_type == "PYTHON":
                    test_list.append(test)

        print("\n%s : %s - Started generate_all_tests() with %d tests to generate.\n" % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), FILE, len(test_list)), flush=True)
        gen_tests_log_files = {}
        gen_tests_op_info = {}
        failed_op_generation = {}

        for test in test_list:

            if (DEBUG>1):
                print(f"INFO: Generating tests from yaml_file_name: {test.yaml_file} test_cpp_name: {test.cpp_file}")

            # Call RBG to generate the graph cpp file, or test generation script. If generation failed for the op, keep track
            # so attempt to compile + run will not be done.
            if test.test_type == "RBG":
                # RBG takes input yaml file as graph descriptor, generate it now.
                test.write_config_to_yaml(test.yaml_file)
                assert os.path.isfile(RBG_PATH) , f"Cannot find {RBG_PATH}, did you use --skip-build?"
                gen_graph_cmd       = f"{RBG_PATH} --config-graph-desc {test.yaml_file} --test-suffix {test.suffix} --graph-count 1 --tests-per-graph 1"
                log_file            = f"{PERF_OUTDIR}/gen_graph_{test.suffix}.log"
                ret_code = run_command(gen_graph_cmd, args, log_file)

                if ret_code != 0:
                    test.result = "FAILED"
                    test.failed_stage = "generate"
                    fail_count += 1

            elif test.test_type == "PYTHON":
                # For python tests, all tests for a single op are generated from one command, so just add
                # one test to the list, and flag the op as having been generated to avoid re-running.
                if (test.op_name not in gen_tests_log_files):
                    assert os.path.isfile(TEST_GEN_SCRIPT_PATH) , f"Cannot find {TEST_GEN_SCRIPT_PATH}"
                    gen_graph_cmd       = f"{TEST_GEN_SCRIPT_PATH} --op {test.op_name} -v"
                    log_file            = f"{PERF_OUTDIR}/gen_graphs_{test.op_name}.log"
                    gen_tests_log_files[test.op_name] = log_file

                    ret_code = run_command(gen_graph_cmd, args, log_file)

                    if ret_code == 0:
                        # Take this opportunity to parse test generation log files to find feeder/drainers ops, op variant per test.
                        op_info = test.get_op_info_from_generated_tests(log_file, args)
                        assert test.op_name not in gen_tests_op_info , "Should only populate gen_tests_op_info per op once"
                        gen_tests_op_info[test.op_name] = op_info
                    else:
                        failed_op_generation[test.op_name] = True
                        fail_count += 1

        # Go through list of tests and set feeder/drainer op names for runtime perf decouplings, and op variant in the
        # case of generated tests which is only known after generating them.
        for test in test_list:
            if test.test_type == "CPP" or test.test_type == "RBG":
                test.feeder_ops = ["feeder_op"]
                test.drainer_ops = ["drainer_op"]
            if test.test_type == "PYTHON":
                if test.op_name not in failed_op_generation:
                    assert test.op_name in gen_tests_op_info , f"Could not find {test.op_name} in {log_file}"
                    assert test.suffix in gen_tests_op_info[test.op_name] , f"Could not find {test.suffix} in {log_file}"
                    op_info = gen_tests_op_info[test.op_name][test.suffix]
                    test.feeder_ops = op_info["feeder_ops"]
                    test.drainer_ops = op_info["drainer_ops"]
                    (op_name, op_variant) = perf_lib.get_op_name_and_variant(op_info["op_variant"])
                    assert op_name == test.op_name , "op name from generated test log doesn't match yaml file"
                    # We now extract op variant from test yaml file earlier. Double check if version printed from generate_tests.py
                    # is the same result.
                    if test.op_variant != None and test.op_variant != op_variant:
                        print(f"Warning: test.op_variant: {test.op_variant} does not match value returned from generate_tests.py op_variant: {op_variant}")
                    test.op_variant = op_variant
                else:
                    test.result = "FAILED"
                    test.failed_stage = "generate"

        print(f"\nFinished generating tests with {fail_count} Failures.")

    # Used for parallel compile (eventually run) of tests. Take test_idx and return to caller to aid in
    # handling of results for multiprocessing pool workers in main process afterwards.
    def compile_or_run_test(self, args):

        test_idx, test, state, do_compile = args

        test_cpp_file, _    = os.path.splitext(test.cpp_file)
        log_prefix          = "compile" if do_compile else "run"
        log                 = f"{PERF_OUTDIR}/{log_prefix}_{test.suffix}.log"
        cmd_args            = ["--skip-run"] if do_compile else ["--skip-build"]
        cmd_args            += COMPILE_AND_RUN_FLAGS

        run_cmd_args        = state.run_cmd_args
        dry_run             = state.dry_run

        # Only enable perf dumping for perf tests. Or functional tests if requested.
        if (test.perf_config['perf-test'] or state.func_tests_dump_perf):
            cmd_args += PERF_DUMP_FLAGS

        # Put the test name argument earlier on command line as traditionally found.
        if test.test_type == "RBG" or test.test_type == "PYTHON":
            initial_args = [f"--test={test_cpp_file}"]
        elif test.test_type == "CPP":
            assert isinstance(test.cpp_test_config, list) , "cpp-test-config must be list of arguments"
            initial_args = test.cpp_test_config
        else:
            assert False , "unsupported test_type"

        # Support for running each test multiple times.
        if test.run_idx_label is not None:
            cmd_args += [f" --label {test.run_idx_label}"]

        command         = [ "./scripts/compile_and_run.py"] + initial_args + cmd_args + run_cmd_args
        command_str     = " ".join(command)

        print("(DRY_RUN)" if dry_run else "" , f"running: \"{command_str}\"")

        if not dry_run:
            with open(log, "w") as out_log:
                completed_process = subprocess.run(
                    command,
                    stdout=out_log,
                    stderr=out_log,
                )

                returncode = completed_process.returncode
        else:
                returncode = 0

        return (test_idx, test.test_type, test.suffix, cmd_args, log, returncode)



    # TODO - Refactor interface here a bit to compile_or_run_tests
    # Ideas:  Make function run_commands_in_parallel() that takes test name, commands, prefix (generate|compile|run) 
    # Parallel version.
    def compile_all_tests(self, args):

        test_list = []

        for op in self.ops:
            for test in op.tests:
                # Must skip tests that failed already in any prior stage. Also skip n-1 runs when running each test n run times.
                if (test.failed_stage or ((test.run_idx != None) and (test.run_idx != 0))):
                    continue

                test_list.append(test)

        test_list_failed_prev_stage = [test for op in self.ops for test in op.tests if test.failed_stage]
        test_list_indices = list(range(0, len(test_list))) # Needed due to multiprocessing pool to update correct test result.

        parallel_str = f"in parallel (jobs: {args.jobs})" if (args.jobs > 1) else "in serial"
        skipped_str = f" ({len(test_list_failed_prev_stage)} skipped due to earlier stage failure)" if len(test_list_failed_prev_stage) > 0 else ""
        print("\n%s : %s - Started compile_all_tests() with %d tests to compile %s%s...\n" % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), FILE, len(test_list), parallel_str, skipped_str), flush=True)

        # test_list = [test for op in self.ops for test in op.tests]
        do_compile = True

        # FIXME - Pull this out into common function.
        fail_count = 0
        with multiprocessing.Pool(args.jobs) as p:
            for test_idx, test_type, test_name, cmd_args, log, returncode in p.imap(
                self.compile_or_run_test, zip(test_list_indices, test_list, itertools.repeat(args), itertools.repeat(do_compile))
            ):

                if returncode == 0:
                    print(
                        perf_lib.style(f" {test_name}:", bold=True),
                        perf_lib.style("OK", color="green", bold=True),
                    )
                else:
                    if test_type == "RBG":
                        repro = (
                            subprocess.check_output(["grep", "<REPRO-CMD>", log])
                            .decode("utf-8")
                            .strip()
                        )

                        if cmd_args:
                            repro += " " + " ".join(cmd_args)

                    else:
                        repro = ""

                    print(
                        perf_lib.style(f" {test_name}:", bold=True),
                        perf_lib.style("FAIL", color="red", bold=True),
                        f"({returncode}):",
                        repro,
                        f" See Log: {log}"
                    )
                    fail_count += 1

                    test_list[test_idx].failed_stage = "compile" if do_compile else "runtime"
                    test_list[test_idx].result = "FAILED"

        print(f"\nFinished compiling tests with {fail_count} Failures.")


    # Run all tests serially (TODO: Parallel for Versim)
    def run_all_tests(self, args):

        # Flatten test list to avoid indented double loop.
        # Must skip tests that failed already in any prior stage
        test_list = [test for op in self.ops for test in op.tests if not test.failed_stage]
        test_list_failed_prev_stage = [test for op in self.ops for test in op.tests if test.failed_stage]

        skipped_str = f" ({len(test_list_failed_prev_stage)} skipped due to earlier stage failure)" if len(test_list_failed_prev_stage) > 0 else ""
        print("\n%s : %s - Started run_all_tests() with %d tests to run%s...." % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), FILE, len(test_list), skipped_str), flush=True)

        run_cmd_list = []
        fail_count = 0
        num_tests_total = 0
        num_tests_run = 0

        # Generate the run commands for all tests - if multiple perf mode will be multiple commands per test.
        for test in test_list:

            for perf_mode_idx, modes in enumerate(test.perf_decouple_modes):
                if (DEBUG>1):
                    print(f"Test {test.name} for Op: {test.op_name} {test.op_variant} mode_idx: {perf_mode_idx} going to run modes {modes}")

                # Combine perf modes for op under test, feeder and drainer ops.
                combined_perf_decouple_modes = {test.perf_decouple_op : modes}
                for feeder_op in test.feeder_ops:
                    combined_perf_decouple_modes[feeder_op] = ["MathPack"]
                for drainer_op in test.drainer_ops:
                    combined_perf_decouple_modes[drainer_op] = ["Unp0Math", "Unp1Math"]

                if not test.perf_config['perf-test']:
                    combined_perf_decouple_modes = {}

                perf_mode_cmd_line_arg = perf_lib.get_perf_mode_cmd_line_arg(combined_perf_decouple_modes)

                log_file = test.log_files[perf_mode_idx]
                run_cmd_str  = f"{ROOT}/scripts/compile_and_run.py "

                # Put the test name argument earlier on command line as traditionally found.
                if test.test_type == "RBG" or test.test_type == "PYTHON":
                    initial_args = [f"--test={test.executable_file}"]
                elif test.test_type == "CPP":
                    assert isinstance(test.cpp_test_config, list) , "cpp-test-config must be list of arguments"
                    initial_args = test.cpp_test_config
                else:
                    assert False , "unsupported test_type"

                run_cmd_str += " ".join(initial_args)
                run_cmd_str += f" --skip-build {perf_mode_cmd_line_arg}"
                run_cmd_str += " ".join(COMPILE_AND_RUN_FLAGS)

                # Set target AI clock on runtime command line
                if (device_runner_lc == "silicon"):
                    run_cmd_str += f" --target-aiclk {args.target_aiclk}"

                # Only enable perf dumping for perf tests. Or functional tests if requested.
                if (test.perf_config['perf-test'] or args.func_tests_dump_perf):
                    perf_dump_flags_str = " ".join(PERF_DUMP_FLAGS)
                    run_cmd_str += f" {perf_dump_flags_str}"

                if perf_mode_idx > 0:
                    run_cmd_str += " --skip-compile-hlk-ops"

                # Support for running each test multiple times.
                if test.run_idx_label is not None:
                    run_cmd_str += f" --label {test.run_idx_label}"

                # Apply any run command line arguments from script command line after "--"
                if args.run_cmd_args:
                    run_cmd_str += " " + " ".join(args.run_cmd_args)

                op_type = test.op_name + test.op_variant

                run_test_info_dict = {"cmd": run_cmd_str, "log": log_file, "name": test.name, "op_type": op_type}
                test.test_run_info_dicts.append(run_test_info_dict)
                num_tests_total += 1

        # Execute the run commands for all tests, potentially multiple commands for each test.
        for test in test_list:

            fails_for_test = 0
            # Run them in parallel (versim) or serially (silicon)
            for idx, test_run_info in enumerate(test.test_run_info_dicts):
                num_tests_run += 1
                timeout_str = f"(Timeout: {args.timeout}s)" if args.timeout else ""
                print("\nRunning Test (%d/%d) : %s for op: %s %s" % (num_tests_run, num_tests_total, test_run_info["name"], test_run_info["op_type"], timeout_str))
                test.date = datetime.now().strftime("%Y-%m-%d %H:%M:%S") # Capture start time for reporting.

                force_pass = True if test.test_type == "RBG" else False # RBG perf tests do not have way to relax comparison_config.
                ret_code = run_command(test_run_info["cmd"], args, test_run_info["log"], force_pass)

                # If any subtest failed, mark this test as failed.
                if ret_code != 0:
                    fails_for_test += 1
                    test.result = "FAILED"
                    test.failed_stage = "run"

            # FIXME - This is true for functional tests, for perf-tests the condition may be different 
            # (TBD: Check during runtime, or report-generation in this script)
            # If all subtests passed, only then mark this test as passed.
            if fails_for_test == 0:
                test.result = "PASSED"
            else:
                fail_count += fails_for_test

            # Informational print to show how to rerun just this test through this script's flow
            get_single_test_rerun_cmd(test, args)

        print(f"\nFinished running tests with {fail_count} Failures.")

    # Clean output dir for perf flow results, files.
    def clean_output_dir(self, args):

        dir = PERF_OUTDIR

        if not args.dry_run:
            print(f"INFO: Cleaning {dir} from previous run contents...")

            # Just remove files in dir, and not dir itself if it exists.
            if os.path.exists(dir) and os.path.isdir(dir):
                filelist = [ f for f in os.listdir(dir)]
                for f in filelist:
                    os.remove(os.path.join(dir, f))
            else:
                # If file with same name, remove it
                if (os.path.exists(dir) and not os.path.isdir(dir)):
                    os.remove(dir)
                os.mkdir(dir)


    # Print entry in perf report table given a test, epoch and perf mode. Some filtering in here to
    # print only results we care about, in a specific format.
    def print_perf_report_for_epoch(self, state, data: json, epoch_idx: int, test, table: PrettyTable):

        testname = test.unique_name # Use unique name that includes optional run_idx

        note = "" if "note" not in test.perf_config else test.perf_config["note"]
        test_op_type = test.op_name + test.op_variant
        test_op_basetype = test.op_name

        results, warnings = [], []

        # Double check op with desired name was found in any epoch.
        found_target_op = False

        per_epoch_events = data['per-epoch-events'] if 'per-epoch-events' in data else {}

        for core_op_key, inputs in data.items():

            # Skip things like per-epoch-events that are not actual cores. Just process cores.
            if not re.match("^([0-9]+)-([0-9]+)", core_op_key): continue

            [op_core, op_name] = perf_lib.get_op_core_and_name(core_op_key)

            # Skip over ops we don't care about. TODO - Consider making this configurable.
            if not re.match(r"target_op", op_name):
                continue
            else:
                found_target_op = True

            inputs_common_events = inputs["inputs-common-events"] if "inputs-common-events" in inputs else {}

            for input, input_data in inputs.items():

                # Skip things like inputs-common-events that are not actual inputs
                if not re.match("^input-([0-9]+)$", input): continue

                # Determine if we care about this input, and if not skip it.
                input_idx = perf_lib.get_input_idx(input)
                # if (state.input and input_idx not in state.input):
                #     continue

                for perf_mode, perf_mode_data in input_data.items():

                    # Just grab the fields we care about from inputs-common-events for this op/input.
                    if perf_mode in inputs_common_events:
                        op_type = inputs_common_events[perf_mode]["op-type"] if "op-type" in inputs_common_events[perf_mode] else ""
                    else:
                        op_type = ""

                    # Just grab the fields we care about from per-epoch-events for this op/input.
                    if perf_mode in per_epoch_events:
                        aiclk = per_epoch_events[perf_mode]["AICLK"] if "AICLK" in per_epoch_events[perf_mode] else ""
                    else:
                        aiclk = ""

                    result = {      "testname" : testname,
                                    "epoch" : epoch_idx,
                                    "input" : input_idx,
                                    "core" : op_core,
                                    "test_op_type" : test_op_type, # Op type the test is defined under (with variant added)
                                    "test_op_basetype" : test_op_basetype, # Op type the test is defined under
                                    "op_name" : op_name, # target_op, usually.
                                    "op_type" : op_type, # Actual op type for the op reported on, with variant.
                                    "perf_mode" : perf_mode,
                                    "note": note,
                                    "result" : test.result,
                                    "outdir" : test.output_dir,
                                    "aiclk" : aiclk
                    }

                    for perf_metric, perf_metric_data in perf_mode_data.items():

                        if (DEBUG>1):
                            print(f"Got input: {input} for op_core: {op_core} op_name: {op_name} {perf_mode} {perf_metric} : {perf_metric_data}")

                        # Avoid capturing strings like "Invalid Runtime Value"
                        if not isinstance(perf_metric_data, str):
                            result[perf_metric] = perf_metric_data
                        else:
                            result[perf_metric] = "N/A"

                    results.append(result)

        # Print all results, some with post-processing. And check device vs model here.
        for result in results:

            result_notes = []

            # Post process some data for printing to table
            math_util_str                   = ("%3.2f%%" % (result["math-utilization-first-unpack-to-last-pack"] * 100))
            model_prop_cycles               = result["model-prop-cycles-per-core"]
            device_unp2pack_cycles          = result["first-unpack-to-last-pack"]
            device_unp2pack_nowait_cycles   = result["first-unpack-to-last-pack-without-wait-tile"] if (state.perf_level >= 1) else 0

            unpack2pack_model_percent = 0 if not model_prop_cycles else (device_unp2pack_cycles - model_prop_cycles) / model_prop_cycles
            unpack2pack_model_percent_sign = "+" if unpack2pack_model_percent >= 0 else ""
            unpack2pack_model_str = ("%s%2.2fx" % (unpack2pack_model_percent_sign, unpack2pack_model_percent))
            unpack2pack_str = ("%d (%s)" % (device_unp2pack_cycles, unpack2pack_model_str))

            unpack2pack_no_wait_model_percent = 0 if not model_prop_cycles else (device_unp2pack_nowait_cycles - model_prop_cycles) / model_prop_cycles
            unpack2pack_no_wait_model_percent_sign = "+" if unpack2pack_no_wait_model_percent >= 0 else ""
            unpack2pack_no_wait_model_str = ("%s%2.2fx" % (unpack2pack_no_wait_model_percent_sign, unpack2pack_no_wait_model_percent))
            unpack2pack_no_wait_str = ("%d (%s)" % (device_unp2pack_nowait_cycles, unpack2pack_no_wait_model_str))

            test_type_str = ("perf" if test.perf_config['perf-test'] else "func")

            test.min_perf_chk_diff = unpack2pack_model_percent if unpack2pack_model_percent < test.min_perf_chk_diff else test.min_perf_chk_diff
            test.max_perf_chk_diff = unpack2pack_model_percent if unpack2pack_model_percent > test.max_perf_chk_diff else test.max_perf_chk_diff

            if (unpack2pack_model_percent <= 0):
                result_notes.append("better_than_model")

            # If this test has perf-checking enabled, and it has passed until this point, execute perf checks.
            perf_chk_en = test.perf_config['device-vs-model-en-checker']
            perf_chk_en_str = perf_chk_en if test.perf_config['perf-test'] else "-"

            if result["result"] != "FAILED" and test.perf_config['perf-test'] and perf_chk_en:

                perf_pass = True

                cycles_tolerance = test.perf_config['device-vs-model-cycles-rtol']

                if (unpack2pack_model_percent != 0):
                    if ((unpack2pack_model_percent) <= cycles_tolerance):
                        perf_pass &= True
                    else:
                        perf_pass &= False
                        result_notes.append(f"perf-cycles-fail(rtol:{cycles_tolerance})")
                else:
                    result_notes.append("model-is-zero")

                result['note'] += ",".join(result_notes)

                # If failed perf checks, mark this result and the overall test as failed.
                if not perf_pass:
                    result['result'] = "FAILED"
                    test.result = "FAILED"
                    test.failed_stage = "perf-check"

            if (result['note'] == ""):
                result['note'] = "N/A"

            # Truncated long strings (middle characters) printed to screen, full version to XLSX file.
            testname_truncated = perf_lib.truncate_field(55, 15, result['testname'])
            op_type_truncated = perf_lib.truncate_field(65, 15, result['op_type'])

            git_hash = perf_lib.get_git_hash()
            hostname = perf_lib.get_hostname()

            #########################################################
            # Write Summary to Stdout                               #
            #########################################################

            if (state.expanded_report):
                # Alternative Report, expanded over multiple lines, easier to fit on screen, but not tabular.
                print(f"ResultIdx: %s Status: %s for TestOpName: %s TestName: %s TestType: %s Note: %s" %(self.result_idx, result["result"], result['test_op_type'], result['testname'], test_type_str, result['note']))
                print(f"  OpType: %s  OpName: %s (%s %s) Core: %s" % (result['op_type'], result['op_name'], result['epoch'], result['input'], result['core']), end='')
                print(f"  ModelProp: %d DeviceUnp2Pack: %s" % (result["model-prop-cycles-per-core"], unpack2pack_str ), end='')
                if (state.perf_level >= 1):
                    print(f"  DeviceUnp2PackNoWait: %s" % (unpack2pack_no_wait_str), end='')
                print(f"  AICLK: %s PerfChkEn: %s DeviceMathUtil: %s" % (result['aiclk'], perf_chk_en_str, math_util_str))
                print(f"  Runner: %s HostName: %s GitCommit: %s TestDate: %s" % (device_runner_lc, hostname, git_hash, test.date))
                print(f"  PerfMode: %s" % (perf_mode))
                print("")
            else:

                testname_str = ("(P)" if test.perf_config['perf-test'] else "(F)") + " " + testname_truncated
                table_field_names = ["ID", "Result", "TestName"]
                table_row = [self.result_idx, result["result"], testname_str]

                table_field_names.append("OpType")
                table_row.append(op_type_truncated)
                table_field_names.append("OpName")
                table_row.append(result["op_name"])
                table_field_names.append("Ep-In")
                table_row.append(f"{result['epoch']}-{result['input']}")
                table_field_names.append("CoreX-Y")
                table_row.append(result['core'])
                table_field_names.append("ModelClks")
                table_row.append(result["model-prop-cycles-per-core"])
                table_field_names.append("Unp2Pack (vs Mdl)")
                table_row.append(unpack2pack_str)

                if (state.perf_level >= 1):
                    table_field_names.append("Unp2PackNoWait (vs Mdl)")
                    table_row.append(unpack2pack_no_wait_str)

                table_field_names.append("MathUtil")
                table_row.append(math_util_str)

                if (device_runner_lc == "silicon"):
                    table_field_names.append("AICLK")
                    table_row.append(result['aiclk'])

                table_field_names.append("Note")
                table_row.append(result["note"])

                # Add the completed list of field names and row to the table.
                table.field_names = table_field_names
                table.add_row(table_row)

            #########################################################
            # Write Summary to XLSX File                            #
            #########################################################

            # Ability to have different/expanded/more fields in XLSX file that otherwise wouldn't fit to screen.

            cycles_tolerance = test.perf_config['device-vs-model-cycles-rtol']
            device_math_util_rounded = round(result["math-utilization-first-unpack-to-last-pack"], 4)

            # TODO : ExperimentLabel
            # TODO : Block Sizes, test details.

            self.xlsx_add_column_and_data("ID", self.result_idx)
            self.xlsx_add_column_and_data("Result", result["result"])
            self.xlsx_add_column_and_data("Runner", device_runner_lc)
            self.xlsx_add_column_and_data("Hostname", hostname)
            self.xlsx_add_column_and_data("Commit", git_hash)
            self.xlsx_add_column_and_data("Date", test.date)
            self.xlsx_add_column_and_data("TestType", test_type_str)
            self.xlsx_add_column_and_data("TestName", result['testname'])
            self.xlsx_add_column_and_data("OpType", result['op_type'])
            self.xlsx_add_column_and_data("OpName", result['op_name'])
            self.xlsx_add_column_and_data("Epoch", result['epoch'])
            self.xlsx_add_column_and_data("Input", result['input'])
            self.xlsx_add_column_and_data("CoreXY", result['core'])
            self.xlsx_add_column_and_data("Model\nPropCycles", result['model-prop-cycles-per-core'])
            self.xlsx_add_column_and_data("Unpack2Pack\nCycles", device_unp2pack_cycles)
            self.xlsx_add_column_and_data("Device\nvs Model", unpack2pack_model_str)
            if (state.perf_level >= 1):
                self.xlsx_add_column_and_data("Unpack2Pack\nCycles\n(NoWaitTile)", device_unp2pack_nowait_cycles)
                self.xlsx_add_column_and_data("Device\nvs Model", unpack2pack_no_wait_model_str)
            self.xlsx_add_column_and_data("Device\nMathUtil", device_math_util_rounded)
            self.xlsx_add_column_and_data("PerfChkEn", perf_chk_en_str)
            self.xlsx_add_column_and_data("DeviceVsModel\nCyclesRTol", cycles_tolerance)
            if (device_runner_lc == "silicon"):
                self.xlsx_add_column_and_data("Device\nAICLK", result['aiclk'])
            self.xlsx_add_column_and_data("Note", result['note'])
            self.xlsx_add_column_and_data("PerfMode\nDecoupling", result['perf_mode'])

            # Done adding field names and data for row, write it to xlsx now with formatting.
            self.xlsx_write_data_for_row()

            self.result_idx += 1

        return (warnings, found_target_op)

    # Find all epochs for a given test and print their reports given a directory
    def generate_perf_report_for_test(self, state, test, dir, table: PrettyTable):

        perf_dumps = f"{dir}/perf_mode_report_epoch_*.json"
        num_epochs = len(glob.glob(perf_dumps))

        warnings = []

        if (num_epochs < 1):
            warnings.append(f"Warning: Could not find any epochs for test.name: {test.name} from {perf_dumps}, skipping.")
            return

        found_target_in_any_epoch = False

        for epoch_idx in range(num_epochs):

            input_file = f"{dir}/perf_mode_report_epoch_{epoch_idx}.json"

            if os.path.isfile(input_file):
                with open(input_file) as json_file:
                    data = json.load(json_file)
                    (epoch_warnings, found_target_op) = self.print_perf_report_for_epoch(state, data, epoch_idx, test, table)
                    found_target_in_any_epoch = True if found_target_op else found_target_in_any_epoch
                    warnings += epoch_warnings
            else:
                warnings.append(f"Warning: {input_file} does not exist. Skipping...")

        if not found_target_in_any_epoch:
            op_type = test.op_name + test.op_variant
            warnings.append(f"Did not find any target ops to report on in perf report for op_type: {op_type} testname: {test.name} in any")

        return warnings

    # Generate a tabular perf summary report for all tests.
    def generate_perf_report_all_tests(self, args):

        print("\n%s : %s - Started generate_perf_report_all_tests().\n" % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), FILE), flush=True)

        if args.dry_run or device_runner_lc not in ["versim", "silicon"]:
            print("Skipping report generation since not using versim or silicon device runner, or using dry-run")
            return

        self.workbook = xlsxwriter.Workbook(args.xlsx)

        all_warnings = []
        print("Note: Math Utilization for SFPU is not entirely accurate.")
        print("Note: Percentages shown are increase in device cycles compared to model. Lower is better. Negative means device results are better than model.")
        print("Legend:  (P: Perf-Test F: Func-Test.    Ep-In: Epochs-Inputs.   Unp2Pack: Device-Unpack-To-Pack-Cycles)\n")

        # Print separate ftable per op, to reduce width of table when printing to screen.
        for op in self.ops:

            table = PrettyTable()
            print_table_for_op = False

            # New XLSX Worksheet per op. Reset to top-most row for headers, and reset col widths
            self.xlsx_worksheet_row = 0
            self.xlsx_worksheet_col_widths = []
            self.xlsx_field_name_list = []
            self.xlsx_curr_worksheet_name = op.name

            for test in op.tests:

                # Must skip tests that failed already in any prior stage. Only include perf tests, or functional tests if desired.
                if (test.failed_stage or (not test.perf_config['perf-test'] and not args.func_tests_dump_perf)):
                    continue

                for perf_mode_idx, perf_mode in enumerate(test.perf_decouple_modes):
                    run_log_file = test.log_files[perf_mode_idx]
                    if os.path.isfile(run_log_file):
                        if (DEBUG>1):
                            print(f"TEST is {test.name} for op: {test.op_name} {test.op_variant} perf_mode: {perf_mode}  run_log_file: {run_log_file}")

                        output_dir = None
                        infile = open(run_log_file, "r")
                        for line in infile:
                            if "using output_dir:" in line:
                                m = re.match("using output_dir:\s*(\S+)\s*$", line)
                                if m:
                                    output_dir = m.group(1) + "/perf_results"
                        infile.close()

                        test.output_dir = output_dir

                        if not (output_dir):
                            all_warnings.append(f"Could not parse output dir from {run_log_file}, skipping it.")
                            continue

                        if not (os.path.exists(output_dir) and os.path.isdir(output_dir)):
                            all_warnings.append(f"The output dir {output_dir} seems invalid, could not find it.")
                            continue

                        all_warnings += self.generate_perf_report_for_test(args, test, output_dir, table)
                        print_table_for_op = True

                    else:
                        all_warnings.append(f"Run log {run_log_file} not found, skipping this test.")

            # After all data is collected, write the pretty table to the screen, unless expanded report is used
            # which is already printed during collection earlier.
            if (print_table_for_op and not state.expanded_report):
                print(f"\nResults for OpName: {op.name} with {len(op.tests)} Tests ...\n")
                table.align = "l"
                table._max_width = {"TestOpType" : 70, "TestName" : 60, "OpType": 70}
                # TODO - Just horizontal rule below headers
                # table.set_perf_lib.style(PLAIN_COLUMNS)
                # table.right_padding_width = 2
                print(table)

        # XLSX: After all the worksheets in the workbook are created, write it now.
        print(f"\nNote: Wrote perf summary to XLSX: {args.xlsx}")
        self.workbook.close()

        # Print out all warnings at the end, in case some tests had issues.
        if len(all_warnings) > 0:
            print("\nList of Warnings during perf report summary generation:\n")
            for warning in all_warnings:
                print(f"Warning: {warning}")


    # Generate a tabular pass/fail summary report for all tests.
    def generate_pass_fail_report_all_tests(self, args):

        print("\n%s : %s - Started generate_pass_fail_report_all_tests().\n" % (datetime.now().strftime("%Y-%m-%d %H:%M:%S"), FILE), flush=True)

        if args.dry_run:
            print("Skipping report generation since using dry-run")
            return Exit_Success

        test_list = [test for op in self.ops for test in op.tests]
        fail_count, pass_count = 0, 0

        table = PrettyTable()

        for idx, test in enumerate(test_list):
            test_op_type = test.op_name + test.op_variant
            failed_stage = test.failed_stage if test.failed_stage is not None else "-"
            test_type_str = ("perf" if test.perf_config['perf-test'] else "func")
            perf_chk_en = test.perf_config['device-vs-model-en-checker'] if test_type_str == "perf" else "-"

            # Workaround for long test names, truncate the middle set of characters
            testname_truncated = perf_lib.truncate_field(70, 15, test.unique_name)
            test_op_type = perf_lib.truncate_field(70, 15, test_op_type)

            testname_str = ("(P)" if test.perf_config['perf-test'] else "(F)") + " " + testname_truncated

            # Only show for versim/silicon device runner.
            if args.dry_run or device_runner_lc not in ["versim", "silicon"]:
                min_max_perf_chk_diff = "-"
            else:
                min_perf_chk_diff_sign = "+" if test.min_perf_chk_diff >= 0 else ""
                max_perf_chk_diff_sign = "+" if test.max_perf_chk_diff >= 0 else ""
                min_max_perf_chk_diff = "Min: %s%3.2fx / Max: %s%3.2fx" % (min_perf_chk_diff_sign, test.min_perf_chk_diff, max_perf_chk_diff_sign, test.max_perf_chk_diff)

            if (test_type_str == "perf"):
                cycles_tolerance = test.perf_config['device-vs-model-cycles-rtol']
                cycles_tolerance_sign = "+" if cycles_tolerance >= 0 else ""
                perf_chk_en_str = f"{perf_chk_en} / {cycles_tolerance_sign}{cycles_tolerance}x"
            else:
                cycles_tolerance = "-"
                perf_chk_en_str = f"{perf_chk_en} / {cycles_tolerance}"

            table.field_names = ["ID", "Result", "Runner", "FailedStage", "OpType", "TestName", "PerfChkEn/RTol","Min/Max PerfChkDiff"]
            table.add_row([idx+1, test.result, device_runner_lc, failed_stage, test_op_type, testname_str, perf_chk_en_str, min_max_perf_chk_diff])

            # print("%-5s %-15s %-20s %-65s %-7s %-9s %-45s" % (idx+1, test.result, failed_stage, test_op_type, test_type_str, perf_chk_en, test_name))
            # Anything that is not PASSED is considered a failure here. May consider changing this condition.
            if test.result == "PASSED":
                pass_count += 1
            else:
                fail_count += 1

        table.align = "l"
        print(table)
        fail_suffix = "s" if fail_count > 1 else ""
        pass_suffix = "es" if pass_count > 1 else ""
        print(f"\nFinished all tests with %d Failure%s and %d Pass%s." % (fail_count, fail_suffix, pass_count, pass_suffix))

        ret_code = Exit_Failure if fail_count != 0 else Exit_Success
        return ret_code

    # Simple interface to add column name and data for a row to be written to XLSX
    def xlsx_add_column_and_data(self, colname: str, data: int):
        if (self.xlsx_worksheet_row == 0):
            self.xlsx_field_name_list.append(colname)
        self.xlsx_row_data_list.append(data)

    # Calculate minimum required width for column in excel given data
    def xlsx_calc_and_set_col_width(self, idx: int, data: str):

        # Break into multiple lines and find max length, if user specified line break.
        data_lines = data.split("\n") if (type(data) == str) else [data]
        line_lens = [len(str(field)) for field in data_lines]
        max_len = max(line_lens)

        # Bump up small width columns a bit.
        data_len = int(max_len * 2) if max_len < 4 else max_len

        if (data_len > self.xlsx_worksheet_col_widths[idx]):
            self.xlsx_worksheet_col_widths[idx] = data_len
            self.worksheet.set_column(idx, idx, data_len)


    # Write out all the header and/or data for a row in xlsx file, with formatting based on results
    def xlsx_write_data_for_row(self):

        # Write header, and record col width as width of header
        if self.xlsx_worksheet_row == 0:

            # Create worksheet now, so that they only exist for ops with results.
            self.worksheet = self.workbook.add_worksheet(self.xlsx_curr_worksheet_name)

            cell_formats_combined = {'bold': True, 'align' : 'left', 'text_wrap' : True}
            cell_format = self.workbook.add_format(cell_formats_combined)

            for idx, name in enumerate(self.xlsx_field_name_list):
                self.xlsx_worksheet_col_widths.append(8)
                self.worksheet.write(self.xlsx_worksheet_row, idx, name, cell_format)
                self.xlsx_calc_and_set_col_width(idx, name)

            self.xlsx_worksheet_row += 1

        # Write the data for the row and update col width if data is wider.
        for idx, data in enumerate(self.xlsx_row_data_list):

            cell_formats_combined = {'align' : 'left'}

            if (self.xlsx_field_name_list[idx] == "Device\nMathUtil"):
                cell_formats_combined['num_format'] = '0.00%'
                if (data >= 0.6666):
                    cell_formats_combined['bg_color'] = GREEN
                elif (data >= 0.3333):
                    cell_formats_combined['bg_color'] = YELLOW
                else:
                    cell_formats_combined['bg_color'] = RED
            elif (self.xlsx_field_name_list[idx] == "Result"):
                cell_formats_combined['bg_color'] = (GREEN if data == "PASSED" else RED)
            elif (self.xlsx_field_name_list[idx] == "PerfChkEn"):
                cell_formats_combined['bg_color'] = (GREEN if data == True else YELLOW)
            elif (self.xlsx_field_name_list[idx] == "Device\nvs Model"):
                m = re.match("([-+]?\d+.\d+)x", data)
                increase = float(m.group(1)) if m else float('-inf')
                if increase <= 0.5:
                    cell_formats_combined['bg_color'] = GREEN
                elif increase <= 5.0:
                    cell_formats_combined['bg_color'] = YELLOW
                else:
                    cell_formats_combined['bg_color'] = RED
            elif (self.xlsx_field_name_list[idx] == "Commit"):
                cell_formats_combined['num_format'] = '@' # Text

            cell_format = self.workbook.add_format(cell_formats_combined)
            self.worksheet.write(self.xlsx_worksheet_row, idx, data, cell_format)
            self.xlsx_calc_and_set_col_width(idx, data)

        # Done writing, clear list for next row and increment row.
        self.xlsx_row_data_list = []
        self.xlsx_worksheet_row += 1

class Op():

    def __init__(self, name: str, variant: str):
        self.name = name
        self.variant = variant
        self.tests = []

    # Parse yaml and get Tests to run
    def add_test(self, test):
        self.tests.append(test) # extend() if we ever want to pass list of tests

    def print(self):
        num_tests = len(self.tests)
        print(f"INFO: Op details OpName: {self.name} Variant: {self.variant} Tests: {num_tests} are...")
        for test in self.tests:
            test.print()

    def __str__(self):
        num_tests = len(self.tests)
        return "INFO: OpName: %s Variant: %s Tests: %d" %(self.name, self.variant, num_tests)

class Test():

    def __init__(self, test_name: str, op_name: str, op_variant: str, config_file: str, run_idx: int):

        self.name = test_name
        self.op_name = op_name
        self.op_variant = op_variant if op_variant is not None else ""
        self.config_file = config_file
        self.compile_cmd = None
        self.run_cmd = None
        self.result = "PASSED"
        self.date = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.failed_stage = None
        self.min_perf_chk_diff = float('inf')
        self.max_perf_chk_diff = float('-inf')
        self.run_idx = run_idx
        self.run_idx_label = f"run_{self.run_idx}" if self.run_idx is not None else None
        self.unique_name = self.name + (f"_{self.run_idx_label}" if (self.run_idx_label is not None) else "")

        # List of dicts containing test-run info (cmd, log files)
        self.test_run_info_dicts = []

        self.log_files = [] # One per perf_decouple_mode
        # For generating/compiling/running tests
        self.suffix = None
        self.yaml_file = None
        self.cpp_file = None
        self.executable_file = None
        self.output_dir = None
        self.aiclk_per_device = []

        # Different perf decoupling modes the test will be run in.
        self.perf_decouple_modes = [["None"]] # A List of Lists
        self.perf_decouple_op = "target_op"

        self.test_type = "RBG"

        # Meant to differentiate betwen functional vs performance tests. perf tests do more, and decouple ops,
        self.perf_config = {"perf-test": False,
                            "device-vs-model-en-checker": False,
                            "device-vs-model-math-util-rtol": 0.5,
                            "device-vs-model-cycles-rtol": 0.5,
                            }
        self.rbg_config = {}
        self.cpp_test_config = {}
        self.gen_test_config = {}
        self.drainer_ops = []
        self.feeder_ops = []

        debug_print(f"Adding Test: op_name: {self.op_name} test_name: {self.name} op_variant: {self.op_variant} run_idx: {self.run_idx} config_file: {self.config_file}")

    # Take the performance config params and set on Test
    def set_perf_config(self, config):

        # Do we want these keys as their own? Already captured by perf-config
        if "perf-decouple-modes" in config:
            for mode in config["perf-decouple-modes"]:
                if mode not in self.perf_decouple_modes:
                    print(f"INFO: For test {self.name} adding perf-decouple-mode{mode}")
                    self.perf_decouple_modes.append(mode)

        if "perf-decouple-op" in config:
            self.perf_decouple_op = config["perf-decouple-op"]

        # Go through all perf-config key/val and replace defaults with test specific
        for key, val in config.items():
            self.perf_config[key] = val


    # Set log files, one per performance mode
    def set_log_files(self):
        for perf_mode_idx, perf_mode in enumerate(self.perf_decouple_modes):
            run_idx_label_str = f"_{self.run_idx_label}" if self.run_idx_label is not None else ""
            log_file = f"{PERF_OUTDIR}/run_test_{self.suffix}_{perf_mode_idx}{run_idx_label_str}.log"
            self.log_files.append(log_file)


    # Take the RBG config from test yaml and set on Test
    def set_rbg_config(self, config, perf_config_variant, perf_config_file):

        assert isinstance(config, list), "rbg-config is expected to be list"
        self.test_type = "RBG"

        # Support for using an include file.
        # Expecting 1 or 2 (if using include file first) entries
        assert (len(config) == 1 or len(config) == 2) , f"Expecting rbg-config to be length 1 or 2 list: {perf_config_file}"

        # If there are 2 entries in rbg-config list, make sure 1st one is include file, and load it. If there is just 1
        # entry, make sure it's the dict data.
        # FIXME - Add support for arbitrary number of items, most important just an include file and no test contents.
        # this is all kind of hacky right now.
        if (len(config) == 2):
            include_file = config[0]
            assert os.path.isfile(include_file) , f"{include_file} cannot be found"
            assert ".yaml" in include_file , f"{include_file} should be yaml file"
            base_config = yaml.safe_load(open(include_file))
            test_config = config[1]

            # Merge test specific data on top of include file data (base_config)
            rbg_config = merge_dicts(dict(base_config), test_config)

            debug_print(f"Merged rbg-config using include file: {include_file}")

        else:
            rbg_config = config[0]

            assert isinstance(config[0], dict) , "if rbg-config list is length 1, the entry should be dict data"

        assert "nodes" in rbg_config , f"expected nodes key in {perf_config_variant} : {self.config_file}"

        # Validate that op op variant being specified is actually tested, to catch typos. Also, replace OP_UNDER_TEST placeholder
        # string with desired op+variant name if using default perf config file. For development only, to avoid needing to add .yaml
        # files for all ops in order to quickly push them through this flow.
        new_config = rbg_config
        seen_desired_variant = False
        for node in new_config["nodes"]:
            assert "types" in node , f"expected types key in node in file {self.config_file}"
            for index, type in enumerate(node["types"]):
                if node["types"][index] == "OP_UNDER_TEST":
                    # Constrant the specialized op name with class name and variant
                    op_and_variant = self.op_name + (self.op_variant if self.op_variant is not None else "")
                    node["types"][index] = op_and_variant
                    seen_desired_variant = True

            if perf_config_variant in node["types"]:
                seen_desired_variant = True

        assert seen_desired_variant , f"Did not find {perf_config_variant} in any nodes types list : {perf_config_file}"
        self.rbg_config = new_config

    # Take the RBG config from test yaml and set on Test
    def set_cpp_test_config(self, config, perf_config_variant, perf_config_file):

        self.test_type = "CPP"

        assert isinstance(config, list), "cpp-test-config is expected to be list"
        self.cpp_test_config = config

    # Take the RBG config from test yaml and set on Test
    def set_gen_test_config(self, config):

        assert isinstance(config, dict), "gen-test-config is expected to be dict"
        self.test_type = "PYTHON"
        self.gen_test_config = config


    def print(self):
        print(f"TestName: {self.name} rbg_config: {self.rbg_config}")

    # Output test config to graph descriptor file that is input to RBG tool
    def write_config_to_yaml(self, outfile_path):
        outfile = open(outfile_path, "w")
        yaml.dump(self.rbg_config, outfile)
        outfile.close()

        return outfile_path

    # Set some filenames for yaml, generated cpp file, and executable that the test will use.
    def set_test_filename_info(self):

        # Variant will be used in filename, so replace some chracters with underscores
        variant_str = self.op_variant if self.op_variant is not None else "None"
        variant_str = re.sub('[ <>,:]', '_', variant_str)
        # variant_str = re.sub('[,]', '_', variant_str)
        # variant_str = re.sub('[:]', '_', variant_str)


        # Setup some file names for saving RBG per test output aside.

        if self.test_type == "RBG":
            self.suffix            = f"op_name_{self.op_name}_variant_{variant_str}_testname_{self.name}"
            self.yaml_file         = f"{PERF_OUTDIR}/rbg_graph_desc_{self.suffix}.yaml"
            self.cpp_file          = f"{RBG_OUTDIR}/rbg_{self.suffix}0"
            self.executable_file   = f"{RBG_OUTDIR}/rbg_{self.suffix}0"
        elif self.test_type == "CPP":
            # Extract testname from cpp-test-config args to compile_and_run.py
            self.suffix            = f"op_name_{self.op_name}_variant_{variant_str}_testname_{self.name}"

            testname = None
            for cmd_arg in self.cpp_test_config:
                m = re.match("--test=?\s?(\S+)", cmd_arg)
                if m:
                    testname = m.group(1)
            
            assert testname is not None, f"could not find --test=testname in cpp_test_config"

            self.yaml_file         = None
            self.cpp_file          = f"{testname}"
            self.executable_file   = f"{testname}"
        elif self.test_type == "PYTHON":
            # This name matches the name of the cpp files the generation script outputs.
            self.suffix            = f"test_gen_{self.op_name}_{self.name}"
            self.yaml_file          = None
            self.cpp_file           = f"{self.suffix}"
            self.executable_file    = f"{self.suffix}"
        else:
            assert False , "unsupported test_type"


    # For python-generated tests, feeder/drainer op names are variable, so parse generation logfile to capture the actual names
    def get_op_info_from_generated_tests(self, log_file, args):

        test_ops = {}
        feeder_ops = []
        drainer_ops = []
        test_path = None
        op_variant = None

        # Go through log, looking for feeders, drainers, and test name.  Test name will come last.
        assert os.path.isfile(log_file), f"Could not open {log_file}"
        with open(log_file) as log:
            for line in log:
                m = re.match(f"^Feeder Operand Name --\s*(.*)$", line)
                if m:
                    assert test_path == None , "Not expected to find feeder op before test name"
                    op_name = m.group(1)
                    feeder_ops.append(op_name)
                    continue

                m = re.match(f"^Drainer Operand Name --\s*(.*)$", line)
                if m:
                    assert test_path == None , "Not expected to find drainer op before test name"
                    op_name = m.group(1)
                    drainer_ops.append(op_name)
                    continue

                m = re.match(f"^Op Variant --\s*(.*)$", line)
                if m:
                    assert test_path == None , "Not expected to find drainer op before test name"
                    op_variant = m.group(1)
                    continue

                m = re.match(f"^Writing tests\s*(.*)$", line)
                if m:
                    test_path = m.group(1)
                    test_filename = os.path.basename(test_path)
                    test_name = os.path.splitext(test_filename)[0]

                    # Tests now support Ops reading/writing to DRAM without feeder/drainer, so these cannot be enforced.
                    # assert len(feeder_ops) > 0 , f"Did not find any feeder ops for {test_name} in {log_file}"
                    # assert len(drainer_ops) > 0 , f"Did not find any drainer ops for {test_name} in {log_file}"
                    assert op_variant != None , f"Did not find any Op Variant for {test_name} in {log_file}"

                    test_ops[test_name] = {}
                    test_ops[test_name]["feeder_ops"] = feeder_ops
                    test_ops[test_name]["drainer_ops"] = drainer_ops
                    test_ops[test_name]["op_variant"] = op_variant
                    # Ops and test name have been captured, clear variables and start capturing for next test now.
                    feeder_ops = []
                    drainer_ops = []
                    test_path = None

        return test_ops


#################################################################################
# Main Program                                                                  #
#################################################################################


def main(args):
    ret_code = 0

    start_time = datetime.now()
    print("%s : %s - Started running. Log files will be under directory %s" % (start_time.strftime("%Y-%m-%d %H:%M:%S"), FILE, PERF_OUTDIR), flush=True)

    print(f"INFO: jobs: {args.jobs} timeout: {args.timeout}")

    suite_name = "nightly" if args.all_ops else "short"
    perf_suite = Suite(name=suite_name)

    # Run either all ops or selected ops from cmd line. Get the list of ops and their tests now.
    perf_suite.get_op_and_test_list_to_run(args)

    # If we just want to generate the report for previous results, do that now, otherwise run and generate report.
    if (DEBUG>0 or args.dry_run):
        perf_suite.print_ops_and_tests_to_run()

    # Build RBG tool first if any tests come from RBG
    if args.skip_build or args.dry_run:
        print("Skipping build of pre-requisites.")
    else:
        perf_suite.build_prereqs(args)

    # Generate, compile and run the tests now
    if args.skip_build:
        print("Skipping clean, generation and compile of tests...")
    else:
        if not args.skip_clean:
            perf_suite.clean_output_dir(args)
        perf_suite.generate_all_tests(args)
        perf_suite.compile_all_tests(args)
    if args.skip_run:
        print("\nSkipping run of tests...")
    else:
        perf_suite.run_all_tests(args)

    # Generate perf report when finished running (per perf mode, op, epoch, input, core, etc)
    perf_suite.generate_perf_report_all_tests(args)

    # Generate report on all tests that were run - just pass/fail per testcase.
    # if not args.skip_run:
    ret_code |= perf_suite.generate_pass_fail_report_all_tests(args)

    end_time = datetime.now()
    elapsed_time = (end_time - start_time)
    print("\n%s : %s - Finished running. Exiting with ret_code: %d" % (end_time.strftime("%Y-%m-%d %H:%M:%S"), FILE, ret_code), flush=True)
    print("%s - Total execution duration: %.3f s" % (FILE, elapsed_time.seconds + (elapsed_time.microseconds / 10000000)), flush=True)

    return ret_code


if __name__ == "__main__":

    # Ability to pass any arbitrary args after "--" to compile_and_run.py command
    try:
        idx = sys.argv.index("--")
        script_args = sys.argv[1:idx]
        run_cmd_args = sys.argv[(idx + 1) :]
    except ValueError:
        script_args = sys.argv[1:]
        run_cmd_args = []

    # parse arguments
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--perflib-yamls", action="store_true", default=False, help="Use perflib op yamls for cpp and rbg generated tests rather than python generated op yamls tests")
    parser.add_argument("--op", type=str, action="append", default=None, help="Op to test (exact match, regex, etc) can use this arg multiple times.")
    parser.add_argument("--test", type=str, action="append", default=None, help="Test names to execute if found, otherwise all tests for op are run. Can use this arg multiple times.")
    parser.add_argument("--test-regex", type=str, action="append", default=None, help="Test names in regex form to execute if found, otherwise all tests for op are run. Can use this arg multiple times.")
    parser.add_argument("--all-ops", action="store_true", default=False, help="Test all ops")
    parser.add_argument("-j", "--jobs", default=1, type=int, help="Number of processes to use")
    parser.add_argument("--skip-build", action="store_true", default=False, help="Skip build of rbg tool")
    parser.add_argument("--skip-run", action="store_true", default=False, help="Skip test generation, compile and run")
    parser.add_argument("--timeout", type=int, default=7200, help="Timeout for processes in seconds (default: 7200)")
    parser.add_argument("--email", action="store_true", default=False, help="Send email with results")
    parser.add_argument("--dry-run", action="store_true", default=False, help="Don't build/run any tests just go through the flow.")
    parser.add_argument("--concise-report", action="store_true", default=False, help="More concise report generated to screen, less columns")
    parser.add_argument("--expanded-report", action="store_true", default=False, help="Expanded multi-line summary report per test, more difficult to grep, easier to fit on screen")
    parser.add_argument("--skip-clean", action="store_true", default=False, help="Skip cleaning perf_tests_out folder before running tests.")
    parser.add_argument("--debug", default=0, type=int, help="Debug level verbosity")
    parser.add_argument("--no-perf-tests", action="store_true", default=False, help="Exclude running of perf tests (default: False to include tests)")
    parser.add_argument("--no-func-tests", action="store_true", default=False, help="Exclude running of non-perf (functional) tests (default: False to include tests)")
    parser.add_argument("--runs-per-test", default=1, type=int, help="Number of runs per test case, default 1.")
    parser.add_argument("--func-tests-dump-perf", action="store_true", default=False, help="Enable perf dumping for functional (non-perf) tests. Default is False. Some overheard with enabling perf dump.")
    parser.add_argument("--target-aiclk", default=1200, type=int, help="Device target AICLK frequency to use (default 1200MHz).")
    parser.add_argument("--xlsx", default="ops_perf_summary.xlsx", type=str, help="Name of target XLSX file for perf summary to be written to (default: ops_perf_summary.xlsx)")

    state = parser.parse_args(script_args)

    # Search run_cmd_args for perf-level flag, so we can include extra data in summary table.
    perf_level = 0
    for idx, arg in enumerate(run_cmd_args):
        if arg == "--perf-level" and idx < (len(run_cmd_args) - 1):
            m = re.match('(\d+)', run_cmd_args[idx+1])
            if (m):
                perf_level = int(m.group(1))

    # To be safe, apply quotes around all run-cmd-args values, useful for preserving --vcd-dump-cores "*-1"
    for idx, arg in enumerate(run_cmd_args):
        run_cmd_args[idx] = arg if arg.startswith("--") else f"\"{arg}\""

    state.run_cmd_args = run_cmd_args
    state.script_args = script_args
    state.perf_level = perf_level

    DEBUG = state.debug

    debug_print(f"run_cmd_args: {run_cmd_args}")
    debug_print(f"script_args: {script_args}")

    if not state.all_ops and state.op is None:
        print("ERROR: Must use wither --all_ops or -op <opname>")
        exit(1)

    # This script uses --label to differentiate between --runs-per-test N runs, so cannot also pass one to compile_and_run.py
    if "--label" in script_args:
        assert state.runs_per_test == 1 , "Cannot use --runs-per-test>1 with --label in compile_and_run.py test args currently"

    assert not (state.test and state.test_regex) , "Should not use --test and --test-regex together, pick one"


    if state.xlsx.endswith(".xls"):
        assert False, "Use .xlsx suffix and not .xls suffix"

    if not state.xlsx.endswith(".xlsx"):
        state.xlsx += ".xlsx"

    ret_code = main(state)
    sys.exit(ret_code)
