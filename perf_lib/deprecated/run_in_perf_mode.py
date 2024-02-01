#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import subprocess
import signal
import sys
import os
import json
import glob, re
from collections import OrderedDict
from datetime import datetime
import perf_lib

# from run_op_perf_tests import Test
# import run_op_perf_tests

LOG_FILE = "perf_test_default.log"
TEST_OUT_DIR_IDENTIFIER = "using output_dir: "
PERF_OUT_DIR_IDENTIFIER = "Perf output directory = "
PERF_NUM_EPOCHS_IDENTIFIER = "Perf num epochs = "
GLOBAL_STATE_IDENTIFIER = "Writing global_state to "
PERF_SCRIPT_LOG_PREFIX = f"{os.path.basename(__file__)} :"

# Skip ops: encoder_0_attention_scores_softmax_gather_2D_exp0

DEBUG = 0

def print(*argv):
    input_str = ""
    for arg in argv:
        input_str += str(arg)
    print(PERF_SCRIPT_LOG_PREFIX, input_str, flush=True)

'''
In sweep mode, it generates all the possible decoupling modes.

Inputs:
    run_normal_perf_mode:   Will add one test with no decoupling modes.
    binary_op:              Determines whether to decouple each operand separately.
Output:
    2d list where first dim is settings for all tests, and second dim determines decouplings in each test. e.g:
    [
        [Unp0Math, Unp1Math],
        [MathPack]
    ]

'''

def get_all_perf_modes(run_normal_perf_mode=False, binary_op = False):
    all_modes = []
    
    if run_normal_perf_mode:
        # First run the test in normal perf mode
        all_modes.append(["None"])        
    
    if binary_op:
        all_modes.append(["Unp0Math"])
        all_modes.append(["Unp1Math"])
        all_modes.append(["Unp0Math", "Unp1Math"])
    else:
        all_modes.append(["Unp0Math", "Unp1Math"])
    
    all_modes.append(["MathPack"])
    all_modes.append(["Unp0Math", "Unp1Math", "MathPack"])

    return all_modes


'''
Parses the output log file and searches for PERF_OUT_DIR_IDENTIFIER to get the test output directory.
This will be used in --test-all-ops mode to find the list of all the ops, and find the summary report.
'''
def get_test_perf_output_dir(string: str = PERF_OUT_DIR_IDENTIFIER):

    line_with_addr = None
    
    with open(LOG_FILE) as log:
        for line in log:
            if string in line:
                line_with_addr = line
                break
    
    assert line_with_addr is not None
    line_with_addr = line_with_addr.replace(string, "")
    if '\n' in line_with_addr:
        line_with_addr = line_with_addr.replace('\n', '')
    return line_with_addr

# TODO: Can merge with previous function
'''
Parses the output log file and searches for PERF_NUM_EPOCHS_IDENTIFIER to get the number of epochs.
This will be used in --test-all-ops mode to find the list of all the ops, and find the summary report.
'''
def get_test_num_epochs():
    
    line_with_epoch = None
    
    with open(LOG_FILE) as log:
        for line in log:
            if PERF_NUM_EPOCHS_IDENTIFIER in line:
                line_with_epoch = line
                break
    
    assert line_with_epoch is not None
    line_with_epoch = line_with_epoch.rstrip('\n')
    line_with_epoch = line_with_epoch.replace(PERF_NUM_EPOCHS_IDENTIFIER, "")
    print("Number of Epochs = ", int(line_with_epoch))
    return int(line_with_epoch)

# def write_piped_data_to_log(proc):
#     with open (LOG_FILE, 'a') as log_file:
#         for line in proc.stdout:
#             sys.stdout.write(line.decode(sys.stdout.encoding))
#             log_file.write(line.decode(sys.stdout.encoding))


'''
Takes a command as input and runs it as a subprocess.
Dumps the output of this process will be redirected to LOG_FILE.
'''
def run_single_test(cmd, timeout = None, myenv = None):
    # run_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    cmd_str = " ".join(cmd)
    print(f" running: {cmd_str} (See: {LOG_FILE})\n", flush=True)

    with open(LOG_FILE, 'a') as log_file:
        run_process = subprocess.Popen(cmd, stdout=log_file, stderr=subprocess.STDOUT, env=myenv)
    try:
        # write_piped_data_to_log(run_process)
        run_process.wait(timeout)
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
        returncode = Exit_Failure
        timed_out = True
    except KeyboardInterrupt:
        run_process.send_signal(signal.SIGKILL)
        run_process.wait()
        raise

'''
Takes a single op name and the test command, and runs a sweep of all decouplings for this op.
Inputs:
    op_name:                The name of the op to sweep all_decouplings.
    cmd:                    Test command that --perf-op-mode will be appended to.
    run_normal_perf_mode:   If set, it will run one additional test in the beginning with no decouplings.
'''
def single_op_run_all_perf_modes(op_name, cmd, run_normal_perf_mode=True, timeout=None):
    print(f"Running all perf modes for OP: {op_name}")
    all_perf_modes = get_all_perf_modes(run_normal_perf_mode=run_normal_perf_mode)
    num_configs = len(all_perf_modes)
    for perf_mode_idx in range(num_configs):
        new_config = perf_lib.get_perf_mode_cmd_line_arg(
            {op_name: all_perf_modes[perf_mode_idx]}
        )
        # new_config = perf_lib.get_perf_mode_cmd_line_arg([op_name], [all_perf_modes[perf_mode_idx]])
        print(f"    Running perf-mode {all_perf_modes[perf_mode_idx]} with perf mode config: {new_config}")
        cmd_this_config = cmd.copy()
        cmd_this_config += [new_config]
        if perf_mode_idx > 0:
            cmd_this_config += [" --skip-compile-hlk-ops "]
        
        run_single_test(cmd_this_config, timeout)  

'''
Iterates over all the ops of a graph.
Initially runs the graph in normal perf mode.
Find the number of epochs, list of ops per epoch.
Then run each op in all perf modes.
'''
def iterate_over_all_ops(state):
    
    command_normal_mode = state.cmd.copy()
    print("Running test in normal perf mode")
    print("Writing the log in ", LOG_FILE)
    # Run the normal perf mode initially
    run_single_test(command_normal_mode, state.timeout)
    # Get the number of epochs and list of ops per epoch
    (num_epochs, all_ops) = get_all_ops_per_epoch_from_run(state)

    # Have to keep track of the last op that we sweep over.
    # This is needed because in addition to hlk recompilation of the new op in the new decoupling mode,
    # we need to recompile the last op without any decouplings.
    last_op = None
    state.cmd += [" --skip-compile-hlk-ops "]
    for epoch in range(num_epochs):
        for op_name in all_ops[epoch]:
            print(f"Running the perf-sweep for op with name: {op_name}")
            all_modes = get_all_perf_modes(run_normal_perf_mode=False)
            
            if last_op is not None:
                # Add the last op to the list of decouplings with special mode "None"
                # This will ensure we recompile this op in normal perf mode
                perf_mode_config = perf_lib.get_perf_mode_cmd_line_arg({
                    op_name: all_modes[0],
                    last_op: ["None"],
                })
                print(f"    Running perf-mode {all_modes[0]} with perf mode config: {perf_mode_config}")
                cmd_this_config = state.cmd.copy() + [perf_mode_config]
                run_single_test(cmd_this_config, state.timeout)
                for mode in all_modes[1:]:
                    perf_mode_config = perf_lib.get_perf_mode_cmd_line_arg({
                        op_name: mode,
                    })
                    print(f"    Running perf-mode {mode} with perf mode config: {perf_mode_config}")
                    cmd_this_config = state.cmd.copy() + [perf_mode_config]
                    run_single_test(cmd_this_config, state.timeout)
            else:
                single_op_run_all_perf_modes(op_name, state.cmd.copy(), run_normal_perf_mode=False)
            
            last_op = op_name


def run_triplet(state):
    
    print(f"Running the test in normal perf mode")
    run_single_test(state.cmd, state.timeout)
    
    del_pos = state.run_triplet_mode.find(',')
    assert del_pos != -1, "The two ops must be separated by a comma"
    first_op = state.run_triplet_mode[:del_pos]
    second_op = state.run_triplet_mode[del_pos+1:] # Skip the '-' between op names
    first_decoupling = perf_lib.get_feeder_decouplings()
    second_decoupling = perf_lib.get_drainer_decouplings()
    new_config = perf_lib.perf_lib.get_perf_mode_cmd_line_arg(
        {first_op: first_decoupling,
        second_op: second_decoupling}
    )
    print(f"Running op-name {first_op} in mode {first_decoupling}, and op-name {second_op} in mode {second_decoupling}")
    print(f"Running with perf mode config: {new_config}")
    state.cmd += [new_config]
    state.cmd += [" --skip-compile-hlk-ops "]
    run_single_test(state.cmd, state.timeout)

'''
Used when iterating over all ops and running each in triplet mode.
Find the feeder/drainer ops per op from the test output log in LOG_FILE then
compute the resulting triplet perf mode for running or matching against results.
'''
def get_op_triplet_perf_modes(state, include_flag : bool):

    op_triplet_perf_modes   = {}
    (num_epochs, all_ops)   = get_all_ops_per_epoch_from_run(state)
    global_state_json       = get_test_perf_output_dir(GLOBAL_STATE_IDENTIFIER)

    assert os.path.isfile(global_state_json), f"Could not open {global_state_json}"

    with open(global_state_json) as input_file:
        data = json.load(input_file)

        # More thought needed for how to support multiple graphs. Can get list of ops from cores_to_ops.yaml which is ordered
        # but come in different order from global_state.json under a graph hierarchy. How to support multiple graphs? For now
        # just use the list of ops from cores_to_ops.yaml which is in correct order, and ensure it is found in graoph_id_0.
        assert len(data["graphs"]) == 1 , "currently only support 1 graph per run"
        assert "graph_id_0" in data["graphs"], "expected to find graph_id_0"
        graph_nodes = data["graphs"]["graph_id_0"]["nodes"]
        all_op_names = []

        # Iterate through all graph nodes and determine real ops, to compare against cores_to_ops.yaml. We don't use this now
        # but it was an idea in the start.
        for name, val in graph_nodes.items():
            if "op_model" in val:
                assert val["type"] != "" , f"type was empty string for op name: {name} which has op_model. This is not expected"
                all_op_names.append(name)

        # Sanity check to see if ops found from global_state.json are the same as those reported in cores_to_ops.yaml when flattened across epochs
        flattened_all_ops = [j for sub in all_ops for j in sub]
        assert sorted(flattened_all_ops) == sorted(all_op_names) , "unexpected difference between op lists from global_state.json and cores_to_ops.yaml"

        prev_ops_decoupled = []

        for epoch in range(num_epochs):
            for op_name in all_ops[epoch]:

                assert op_name in graph_nodes , f"Could not find {op_name} in global_state.json graph nodes"
                op_node         = graph_nodes[op_name]
                input_nodes     = op_node["input_nodes"] if "input_nodes" in op_node else []
                output_nodes    = op_node["output_nodes"] if "output_nodes" in op_node else []

                # Figure out Feeder and Drainer ops. Ignore those not found in flattened_all_ops that aren't real ops.
                feeder_ops  = [node for node in input_nodes if node in flattened_all_ops]
                drainer_ops = [node for node in output_nodes if node in flattened_all_ops]

                name = op_node["name"] if "name" in op_node else ""
                assert name == op_name , f"Unexpected difference between op names ({name} and {op_name})"

                if DEBUG:
                    type = op_node["type"] if "type" in op_node else ""
                    feeder_ops_str = ",".join(feeder_ops)
                    drainer_ops_str = ",".join(drainer_ops)
                    print(f"get_op_triplet_perf_modes() op_name: %-60s type: %-40s feeders: %-70s drainers: %-60s" % (name, type, feeder_ops_str, drainer_ops_str))

                # Use an ordered dict to guarantee same order between calls to this function (run, and report generation)
                op_decouplings = OrderedDict()

                # For all previously configured ops, set their decouple mode to None to ensure they are
                # recompiled in normal perf mode. May be overridden by decouple mode next if op is revisited.
                for op in prev_ops_decoupled:
                    op_decouplings[op] = ["None"]
                prev_ops_decoupled.clear()

                # Apply feeder and drainer op decouplings, like standalone op triplet mode.
                for op in feeder_ops:
                    op_decouplings[op] = perf_lib.get_feeder_decouplings()
                    prev_ops_decoupled.append(op)

                for op in drainer_ops:
                    op_decouplings[op] = perf_lib.get_drainer_decouplings()
                    prev_ops_decoupled.append(op)

                perf_mode_config = perf_lib.get_perf_mode_cmd_line_arg(op_decouplings, include_flag)
                op_triplet_perf_modes[op_name] = perf_mode_config
                if DEBUG:
                    print(f"Determined op_name: {op_name} op_triplet_perf_modes = {op_triplet_perf_modes[op_name]}")

    if (DEBUG>1):
        print("Returning op_triplet_perf_modes: ", op_triplet_perf_modes)

    return (op_triplet_perf_modes, num_epochs, all_ops)


'''
Iterates over all the ops of a graph.
Initially runs the graph in normal perf mode.
Find the number of epochs, list of ops per epoch.
Find the feeder/drainer ops per op
Then run each op in triplet mode with all feeders/drainers decoupled.
'''
def iterate_over_all_ops_all_triplets(state):

    command_normal_mode = state.cmd.copy()
    print("Running test in normal perf mode")
    print("Writing the log in ", LOG_FILE)
    # Run the normal perf mode initially. Update env to dump global_state.json file for triplet mode
    my_env = os.environ.copy()
    my_env["DUMP_PASSES"] = "1"

    # Run the normal perf mode initially
    run_single_test(command_normal_mode, state.timeout, my_env)

    # Parse global_state.json from normal-mode run to get feeder/drainer ops, and triplet perf mode config.
    (op_triplet_perf_modes, num_epochs, all_ops) = get_op_triplet_perf_modes(state, True)

    total_num_ops = len([j for sub in all_ops for j in sub])
    time_now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"{time_now} Started iterate_over_all_ops_all_triplets() num_epochs: {num_epochs}. List of all the ops ({total_num_ops}) to sweep over: {all_ops}")

    state.cmd += [" --skip-compile-hlk-ops "]

    op_idx = 1
    for epoch in range(num_epochs):
        for op_name in all_ops[epoch]:
            assert op_name in op_triplet_perf_modes , f"Could not find {op_name} in op_triplet_perf_modes"
            perf_mode_config = op_triplet_perf_modes[op_name]

            # Ability to skip ops. Or only include some ops and skip everything else. Useful for debug, avoiding hangs, known issues, etc.
            if ((op_name in state.skip_op) or (state.incl_op and op_name not in state.incl_op)):
                print(f"    {op_idx}/{total_num_ops} Skipping triplet-mode for op_name: {op_name} with perf mode config: {perf_mode_config}")
                op_idx += 1
                continue

            print(f"    {op_idx}/{total_num_ops} Running triplet-mode for op_name: {op_name} with perf mode config: {perf_mode_config}")
            cmd_this_config = state.cmd.copy() + [perf_mode_config]
            run_single_test(cmd_this_config, state.timeout)
            op_idx += 1

'''
Used when iterating over all ops in a graph, returns number of epochs and list of ops per epoch.
Finds the output test directory from the test output log in LOG_FILE.
Finds the number of epochs from the test output log in LOG_FILE.
Finds the list of ops from cores_to_ops dump in the initial normal perf mode test dump.

The cores_to_ops file lists all cores as with the following format:
<core_id>: <OP_NAME> <Additional descriptions>
e.g.
1-1: matmul (logical = 0-0)
1-2: matmul (logical = 1-0)
6-1: bias (logical = 0-5)
6-2: bias (logical = 1-5)
'''
def get_all_ops_per_epoch_from_run(state):
    # Find the output directory in the test log
    perf_out_dir = get_test_perf_output_dir()
    # Find the number of epochs in the test log
    num_epochs = get_test_num_epochs()
    all_ops = []
    # For each epoch, parse the cores_to_ops yaml file to get a list of all the op names
    # all_ops is a 2d list with the first dim for epochs and second dim, all the ops in each epoch.
    for epoch in range(num_epochs):
        ops_current_epoch = []
        epoch_perf_dir = perf_out_dir + f"epoch_{epoch}/"
        cores_to_ops_path = epoch_perf_dir + "cores_to_ops.json"
        print("Reading the ops from ", cores_to_ops_path)
        with open(cores_to_ops_path) as cores_to_ops:
            data = json.load(cores_to_ops)
            for core_coord, desc in data.items():
                op_name = desc["op-name"]
                if op_name not in ops_current_epoch and op_name != "Invalid":
                    ops_current_epoch.append(op_name)
        
        all_ops.append(ops_current_epoch)
    
    return (num_epochs, all_ops)


'''
First figures out the perf modes for each op that represent their triplet mode (constructed from op's feeders/drainers)
Next figure out map of each op's perf modes (cmd line args) to perf mode labels constructed at runtime
Lastly, return map of ops to their specific triplet perf mode labels that are found in performance reports .json files from runtime
'''
def get_op_to_triplet_label_map(state):

    # Parse global_state.json from normal-mode run to get feeder/drainer ops, and triplet perf mode config.
    (op_triplet_perf_modes, num_epochs, all_ops) = get_op_triplet_perf_modes(state, False)

    op_to_triplet_label_map = {}

    # Loop over ops in the order of the graph and create desired mapping.
    for epoch in range(num_epochs):
        for op_name in all_ops[epoch]:

            # Ability to skip ops. Useful for avoiding hangs, known issues, etc.
            if ((op_name in state.skip_op) or (state.incl_op and op_name not in state.incl_op)):
                continue

            assert op_name in op_triplet_perf_modes , f"Could not find {op_name} in op_triplet_perf_modes"
            perf_mode_config    = op_triplet_perf_modes[op_name]

            # Generate decouple mode str/label on the fly
            perf_mode_label = perf_lib.get_decouple_mode_name(perf_mode_config)
            op_to_triplet_label_map[op_name] = perf_mode_label

            if DEBUG:
                print("op_to_triplet_label_map op_name: ", op_name, " perf_mode_config: ", perf_mode_config, " perf_mode_label: ", perf_mode_label)

    return op_to_triplet_label_map

# Print a table showing the perf mode representing triplet-mode for each op, since it will be listed
# as shortform in results summary table as "triplet_mode"
def print_op_to_triplet_label_map(op_to_triplet_label_map: dict):

    print("\nPrinting op_to_triplet_label_map to show which perf results are shown for each op in triplet mode...\n")
    print(f"%-5s %-60s %-20s %-50s" %("ID", "OP_NAME", "LABEL", "PERF_MODE"))
    print("=" * 200)

    for idx, (op_name, perf_mode) in enumerate(op_to_triplet_label_map.items()):
        print(f"%-5s %-60s %-20s %-50s" %(idx, op_name[0:60], "triplet_mode", perf_mode))


# Extract results and generate table summary of perf results on screen for all epochs, inputs, ops etc.
def generate_perf_report():

    time_now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"{time_now} Started generate_perf_report()")

    dir = get_test_perf_output_dir(TEST_OUT_DIR_IDENTIFIER)
    dir += "/perf_results"

    num_epochs = len(glob.glob(f"{dir}/perf_mode_report_epoch_*.json"))

    print(f"generate_perf_report() found num_epochs: {num_epochs}")

    # Get map of feeder/drainer ops per op, then perf modes to label map, then finally op to it's triplet mode perf label. A rather involved
    # chain of functions here. Do it again here, and not just during runtime so that the report can be generated standalone without re-running.
    run_all_triplets_report = state.test_all_ops and state.run_all_triplets
    op_to_triplet_label_map = get_op_to_triplet_label_map(state) if run_all_triplets_report else {}

    # Print op to triplet label map for informational purposes since in the table the long perf mode names
    # will be trimmed to be just "triplet_mode".
    print_op_to_triplet_label_map(op_to_triplet_label_map)

    testname = None

    warnings = []
    results = []

    for epoch_idx in range(num_epochs):

        input_file = f"{dir}/perf_mode_report_epoch_{epoch_idx}.json"
        assert os.path.isfile(input_file), f"Can't read {input_file}"

        with open(input_file) as json_file:
            data = json.load(json_file)

            for core_op_key, inputs in data.items():
                if core_op_key == "per-epoch-events":
                    continue
                [op_core, op_name] = get_op_core_and_name(core_op_key)

                # For assertion checking to make sure computed perf mode label matches one that is valid and in results.
                found_triplet_mode = False

                for input, input_data in inputs.items():

                    # Skip things like inputs-common-events that are not actual inputs
                    if not re.match("^input-([0-9]+)$", input): continue

                    # Determine if we care about this input, and if not skip it.
                    input_idx = get_input_idx(input)
                    if (state.report_input and input_idx not in state.report_input):
                        continue

                    for perf_mode, perf_mode_data in input_data.items():

                        # Only display triplet mode and normal_perf_mode when sweeping all ops as triplets
                        if run_all_triplets_report:

                            # Ability to skip ops. Useful for avoiding hangs, known issues, etc.
                            if ((op_name in state.skip_op) or (state.incl_op and op_name not in state.incl_op)):
                                found_triplet_mode = True # Bypass assertion.
                                continue

                            triplet_perf_mode_label = op_to_triplet_label_map[op_name]

                            if perf_mode not in [triplet_perf_mode_label, "normal_perf_mode"]:
                                continue
                            elif perf_mode == triplet_perf_mode_label:
                                found_triplet_mode = True
                                perf_mode = "triplet_mode" # Short form name to prevent table from being huge.

                        result = {      "testname" : testname,
                                        "epoch" : f"epoch-{epoch_idx}",
                                        "input" : input,
                                        "core" : op_core,
                                        "op_name" : op_name[0:47],
                                        "perf_mode" : perf_mode
                        }

                        for perf_metric, perf_metric_data in perf_mode_data.items():

                            if (DEBUG>1):
                                print(f"print_perf_report_for_epoch() got input: {input} for op_core: {op_core} op_name: {op_name} {perf_mode} {perf_metric} : {perf_metric_data}")

                            # Avoid capturing strings like "Invalid Runtime Value"
                            if not isinstance(perf_metric_data, str):
                                result[perf_metric] = perf_metric_data
                            else:
                                result[perf_metric] = "N/A"

                        # FIXME - Add to results list in order of all cores per op. Json file is ordered by core-xy.
                        results.append(result)

                # Make sure we found the triplet mode desired. There is issue with long directory names, not all are dumped at the time of writing, hope to fix soon.
                if run_all_triplets_report and not found_triplet_mode:
                    warnings.append(f"Could not find result in {input_file} for op_name: {op_name} with decoupling mode {triplet_perf_mode_label}")

    # Finished collecting all the results, now it is time to print them.

    print("\nPerf Summary Report follows...\n")
    # Print Header and Data
    print("%-6s %-10s %-10s %-7s %-50s %-12s %-15s %-15s %-10s %-10s %-10s %-10s %-50s" % ("",   "",      "",      "",     "",        "",      "",         "UNP2PACK", "",     "",       "",       "",       ""))
    print("%-6s %-10s %-10s %-7s %-50s %-12s %-15s %-15s %-10s %-10s %-10s %-10s %-50s" % ("",   "",      "",      "CORE", "",        "MODEL", "UNP2PACK", "NO_WAIT",  "MATH", "DEVICE", "DEVICE", "DEVICE", ""))
    print("%-6s %-10s %-10s %-7s %-50s %-12s %-15s %-15s %-10s %-10s %-10s %-10s %-50s" % ("ID", "EPOCH", "INPUT", "XY",   "OP_NAME", "PROP", "VS_MODEL",  "VS_MODEL", "UTIL", "UNPACK", "MATH",   "PACK",   "PERF_MODE"))
    print("=" * 230 , "\n")

    for idx, result in enumerate(results):

        # Post process some data for printing to table
        if (not result["math-utilization-first-unpack-to-last-pack"] or not isinstance(result["math-utilization-first-unpack-to-last-pack"], int)):
            math_util_str = "N/A"
        else:
            math_util_str = ("%3.2f%%" % (result["math-utilization-first-unpack-to-last-pack"] * 100))

        if (not result["model-prop-cycles-per-core"] or not isinstance(result["first-unpack-to-last-pack"], int)):
            unpack2pack_model_percent = 0
        else:
            unpack2pack_model_percent = (100 * result["first-unpack-to-last-pack"] / result["model-prop-cycles-per-core"])

        unpack2pack_str = ("%s (%2.0f%%)" % (result["first-unpack-to-last-pack"], unpack2pack_model_percent))

        if (not result["model-prop-cycles-per-core"] or not isinstance(result["first-unpack-to-last-pack-without-wait-tile"], int)):
            unpack2pack_no_wait_model_percent = 0
        else:
            unpack2pack_no_wait_model_percent = (100 * result["first-unpack-to-last-pack-without-wait-tile"] / result["model-prop-cycles-per-core"])

        if (not result["first-unpack-to-last-pack"] or not isinstance(result["first-unpack-to-last-pack"], int)):
            unpack2pack_no_wait_str = 0
        else:
            unpack2pack_no_wait_str = ("%s (%2.0f%%)" % (result["first-unpack-to-last-pack"], unpack2pack_no_wait_model_percent))
        unpack2pack_no_wait_str = ("%s (%2.0f%%)" % (result["first-unpack-to-last-pack"], unpack2pack_no_wait_model_percent))

        print("%-6d %-10s %-10s %-7s %-50s %-12s %-15s %-15s %-10s %-10s %-10s %-10s %-50s" %
        (idx+1, result["epoch"], result["input"], result["core"], result["op_name"], 
        result["model-prop-cycles-per-core"], unpack2pack_str, unpack2pack_no_wait_str,
        math_util_str, result["unpack-runtime"], result["math-runtime"], result["pack-runtime"], result["perf_mode"]))

    # If there were any warnings, print them now.
    num_warnings = len(warnings)
    if num_warnings > 0:
        print(f"\nFound {num_warnings} Warnings during report generation:\n")
        for warning in warnings:
            print(f"Warning: {warning}")


# Helper function to use regex to parse core and op name from perf results .json file
def get_op_core_and_name(core_op_key: str):

    m = re.match("^([0-9]+-[0-9]+)-(.*)$", core_op_key)
    if m:
        op_core = m.group(1)
        op_name = m.group(2)
    else:
        op_core = "0-0"
        op_name = "unknown"
        print("ERROR: could not parse {core_op_key}")

    return [op_core, op_name]

# Helper function to use regex to parse input index from perf results .json file
def get_input_idx(index_str : str):

    m = re.match("^input-([0-9]+)$", index_str)
    if m:
        idx = int(m.group(1))
    else:
        assert False , f"issue parsing input from {index_str}"

    return idx


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

    start_time = datetime.now()
    print("%s %s - Started running." % (PERF_SCRIPT_LOG_PREFIX, start_time.strftime("%Y-%m-%d %H:%M:%S")), flush=True)

    # parse arguments
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--run-all-perf-modes", action="store_true", default=False, help="Runs the input test in all perf-modes for the specified op.")
    parser.add_argument("--perf-op-name", type=str, default="", help="The target op name.")
    parser.add_argument("--binary-op", action="store_true", default=False, help="If set the op will test all MATH-UNP0, MATH-UNP1, and MATH-UNP modes.")
    parser.add_argument("--test-all-ops", action="store_true", default=False, help="The script will iterate over all the ops.")
    parser.add_argument("--timeout", type=int, default=None, help="timeout in seconds to wait for command to finish")
    parser.add_argument("--log-file", type=str, default="", help="The output file for writing the log into.")
    parser.add_argument("--run-triplet-mode", type=str, default="", help="Set --run-triplet-mode first_op_name,second_op_name to decouple the op in the middle from the rest of the graph.")
    parser.add_argument("--run-all-triplets", action="store_true", default=False, help="For each op run in triplet mode to decouple it from the rest of the graph. Usually used with test-all-ops")
    parser.add_argument("--report-only", action="store_true", default=False, help="Just generate the table summary report only instead of running. Assumes run previously.")
    parser.add_argument("--skip-report", action="store_true", default=False, help="Skip generating the table summary report at the end.")
    parser.add_argument("--report-input", default=[], type=int, action="append", help="Specific input to display in report, otherwise all are displayed.")
    parser.add_argument("--skip-op", default=[], type=str, action="append", help="Specific op name to skip when sweeping all ops. Useful to workaround hangs.")
    parser.add_argument("--incl-op", default=[], type=str, action="append", help="Specific op name to include (skipping all others) when sweeping all ops. Useful for more focused debug.")
    parser.add_argument("--skip-clean", action="store_true", default=False, help="Skip cleaning of previous LOG_FILE if it exists")

    # FIXME - Consider refactoring/renaming flags. Make --run-triplet-modes be single flag, and not take any op names, instead use --perf-op-name and have it automatically
    # find the feeder/drainer op for single op under test, or all ops via --test-all-ops. This is a bit more consistent with --run-all-perf-modes.
    state = parser.parse_args(script_args)
    state.cmd = cmd_args
    if state.log_file != "":
        LOG_FILE = state.log_file

    if not state.report_only and not state.skip_clean:
        if os.path.exists(LOG_FILE):
            print(f"Removing the existing log file \"{LOG_FILE}\"")
            os.remove(LOG_FILE)

        print(f"The output of each test run will be redirected to \"{LOG_FILE}\"")
        state.cmd += [" --dump-perf-events-intermediate"]
        print("Adding the --dump-perf-events-intermediate cmd option to all tests")

        # FIXME - Adjust this assert as needed.
        assert state.test_all_ops + state.run_all_perf_modes + (state.run_triplet_mode != "") <= 1, "Only one perf test mode must be selected."
        try:
            if state.run_all_perf_modes:
                print(f"Starting run_all_perf_modes testing")
                single_op_run_all_perf_modes(state.perf_op_name, state.cmd, run_normal_perf_mode=True)
                print(f"Finished running all modes for op with name {state.perf_op_name}")
            elif state.test_all_ops:
                if state.run_all_triplets:
                    print(f"Starting test_all_ops w/ run_all_triplets testing")
                    iterate_over_all_ops_all_triplets(state)
                    print(f"Finished test_all_ops w/ run_all_triplets testing")
                else:
                    print(f"Starting test_all_ops testing")
                    iterate_over_all_ops(state)
                    print(f"Finished test_all_ops testing")
            elif state.run_triplet_mode != "":
                print(f"Starting run_triplet_mode testing")
                run_triplet(state)
            else:
                print(f"No valid perf options specified. Exiting without running any tests.")
        except Exception as e:
            print("{}: ".format(type(e).__name__), e)
            sys.exit(2)

    # Generate and print the perf report summary to the screen. Probably also want to write to
    # XLS file for more details, columns, space etc.
    if not state.skip_report:
        generate_perf_report()

    end_time = datetime.now()
    elapsed_time = (end_time - start_time)
    print("\n%s %s - Finished running." % (PERF_SCRIPT_LOG_PREFIX, end_time.strftime("%Y-%m-%d %H:%M:%S")), flush=True)
    print("%s Total execution duration: %.3f s" % (PERF_SCRIPT_LOG_PREFIX, elapsed_time.seconds + (elapsed_time.microseconds / 10000000)), flush=True)
