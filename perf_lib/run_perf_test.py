#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
import os
import json
from datetime import datetime
from typing import List, Union
import yaml
from perf_test_base import *
import shutil
from elasticsearch import Elasticsearch
import pathlib
from subprocess import Popen, PIPE, STDOUT, TimeoutExpired, CalledProcessError
from signal import SIGTERM, SIGKILL, signal
import traceback

from ci.repo import ES_ENDPOINT, ES_USERNAME, ES_PASSWORD

INDEX_NAME = "spatial-ci"
BBE_PERF_INDEX_NAME = "spatial-ci-perf"
BBE_PERF_INDEX_LOCAL_TEST_NAME = "test-spatial-ci"

run_process = None

def kill_subprocess():
    if run_process is not None:
        # for line in run_process.stdout:
        #     print(line, end='', flush=True)
        run_process.send_signal(SIGKILL)
        os.killpg(os.getpgid(run_process.pid), SIGTERM)
        run_process.wait()

signal(SIGTERM, kill_subprocess)

TEST_TYPE_TO_TARGET_KEY_ON_DB = {
    # 'graph': 'target-backend-samples-per-second',
    'graph': 'target-samples-per-second-excluding-last-epoch-of-each-program',
    'op': 'target-average-math-utilization',
    'dram-rd': 'target-dram-read-bw',
    'dram-wr': 'target-dram-write-bw',
    'input0-bw': 'target-input0-bw',
    'output-bw': 'output-bw',
}

def get_index(state):
    if state.use_test_database:
        return BBE_PERF_INDEX_LOCAL_TEST_NAME
    else:
        return BBE_PERF_INDEX_NAME

'''
Takes a command as input and runs it as a subprocess.
Dumps the output of this process will be redirected to log_file.
'''
def run_test(cmd, running_on_ci, timeout = None):
    # run_process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    cmd_str = " ".join(cmd)
    print(f"Running command: {cmd_str}")

    return_code = 1
    try:
        run_process = Popen(cmd, stdout=PIPE, universal_newlines=True, stderr=STDOUT)
        if running_on_ci:
            (stdout, stderr) = run_process.communicate(timeout=timeout)
            return_code = run_process.poll()
            if stdout is not None:
                for line in stdout:
                    print(line, end='', flush=True)
            if stderr is not None:
                for line in stderr:
                    print(line, end='', flush=True)
        else:
            for line in run_process.stdout:
                print(line, end='', flush=True)
            return_code = run_process.wait(timeout)

    except TimeoutExpired:
        print("Timeout reached")
        os.killpg(os.getpgid(run_process.pid), SIGTERM)
        run_process.wait()
    except KeyboardInterrupt:
        print("Keyboard interrupt")
        kill_subprocess()
        raise

    if return_code:
        raise ValueError(f"Test failed with return code {return_code}")

def get_perf_targets(state):
    search_query = {
        "size": 1,
        "query": {
            "bool": {
            "must": [],
            "filter": [
                {
                "bool": {
                    "should": [
                    {
                        "match_phrase": {
                        "test_name.keyword": state.test_name
                        }
                    }
                    ],
                    # "minimum_should_match": 1
                }
                },
                {
                "bool": {
                    "should": [
                    {
                        "match_phrase": {
                        "test_group.keyword": state.group_name
                        }
                    }
                    ],
                    # "minimum_should_match": 1
                }
                },
            ],
            "should": [],
            "must_not": []
            }
        },
        "sort": [
            {
            "@timestamp": {
                "order": "desc",
                "unmapped_type": "boolean"
            }
            }
        ],
    }
    es_client = Elasticsearch([ES_ENDPOINT], http_auth=(ES_USERNAME, ES_PASSWORD))
    res = es_client.search(index=get_index(state), body=search_query)
    num_results = len(res['hits']['hits'])
    if state.force_update_target:
        print(f"Force target update is enabled. Skipping perf check and will upload the new perf target to database")
        return None
    assert num_results <= 1, "The query from database must either return a single result or none"
    if num_results == 1:
        if TEST_TYPE_TO_TARGET_KEY_ON_DB[state.perf_test_type] in res['hits']['hits'][0]['_source']:
            perf_target = res['hits']['hits'][0]['_source'][TEST_TYPE_TO_TARGET_KEY_ON_DB[state.perf_test_type]]
            print(f"Got the latest perf targets for test_name {state.test_name}, test_group {state.group_name} = {perf_target}")
            return perf_target
        else:
            print(f"Perf targets for test_name {state.test_name}, test_group {state.group_name} were not found in database")
            return None
    else:
        print(f"No perf entries for test_name {state.test_name}, test_group {state.group_name} was found in database")
        if state.local_run and not state.use_test_database:
            raise Exception("In local run the target performance must exist in database")
        return None

def modify_test_command(state):

    initial_cmd = state.cmd.copy()
    initial_cmd += [f"--perf-output-dir", f"{state.perf_output_dir}"]

    if state.perf_target == None:
        initial_cmd += [f"--skip-perf-check"]
    else:
        initial_cmd += [f"--perf-target", f"{str(state.perf_target)}"]

    state.cmd = initial_cmd

def get_test_configs(perf_output_dir):
    # assert os.path.exists(perf_output_dir)
    test_config_path = perf_output_dir + "/test_config.yaml"
    if (not os.path.exists(test_config_path)):
        print(f"test config file not found")
        return {}

    test_config = {}
    with open(test_config_path) as test_config_file:
        test_config = yaml.load(test_config_file)

    return test_config

def get_op_postprocessor_key(runtime_config, target_op_name: str):
    all_keys = []
    for key, value in runtime_config.items():
        if target_op_name in key:
            all_keys.append(key)

    return all_keys
    # assert False, f"Op with name {target_op_name} was not found in perf-runtime report"

def upload_perf_results(state):
    host_name = ""
    perf_info_file_path = f"{state.perf_output_dir}/perf_results/perf_info_all_epochs.yaml"
    perf_file = pathlib.Path(perf_info_file_path)
    print(f"tag = {state.tag}, branch_name = {state.branch_name}")
    enable_dump = ((state.tag == "nightly" or state.branch_name == "origin/master") and not state.local_run) or state.use_test_database

    if not enable_dump:
        print("Skipping performance results, since tag is not nightly and branch_name is not origin/master or running ci locally")
        return

    TIMESTAMP = datetime.utcnow()
    perf_result = dict()
    es_log = Elasticsearch([ES_ENDPOINT], http_auth=(ES_USERNAME, ES_PASSWORD))
    try:

        if state.perf_test_type == "graph":
            print("Checking if backend report exists for graph perf mode")
            host_output_dir = state.perf_output_dir + "/perf_results/host/"
            host_summary_file_path = host_output_dir + "host_summary_report.json"
            assert os.path.exists(host_summary_file_path)
            with open(host_summary_file_path) as host_summary_file:
                summary_report = json.load(host_summary_file)
                assert "samples-per-second" in summary_report
                # samples_per_second = summary_report["samples-per-second"]
                # new_perf_target = perf_target
                # if perf_target is None or samples_per_second > perf_target:
                #     new_perf_target = samples_per_second
                perf_result.update({"backend-samples-per-second": summary_report["samples-per-second"]})
                # perf_result.update({"target-backend-samples-per-second": new_perf_target})

            with open(perf_info_file_path) as perf_info_file:
                perf_info_yaml = yaml.load(perf_info_file, Loader=yaml.FullLoader)
                assert "global-events" in perf_info_yaml
                assert "samples-per-second-excluding-last-epoch-of-each-program" in perf_info_yaml["global-events"]
                samples_per_second_excluding_last_epoch = perf_info_yaml["global-events"]["samples-per-second-excluding-last-epoch-of-each-program"]
                new_perf_target_excluding_last_epoch = state.perf_target
                if state.perf_target is None or samples_per_second_excluding_last_epoch > state.perf_target:
                    new_perf_target_excluding_last_epoch = samples_per_second_excluding_last_epoch
                perf_result.update({"samples-per-second-excluding-last-epoch-of-each-program": samples_per_second_excluding_last_epoch})
                perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["graph"]: new_perf_target_excluding_last_epoch})
            print("Checking if performance report exists for test", state.test_name, "at", perf_file)
        if perf_file.exists():
            print("Found perf file 1")
            with open(perf_file) as perfdump:
                perf_data = yaml.load(perfdump, Loader=yaml.FullLoader)
                assert "global-events" in perf_data
                assert "host-name" in perf_data["global-events"]
                host_name = perf_data["global-events"]["host-name"]
                for program in perf_data.keys():
                    if program == "global-events":
                        continue
                    epochs = perf_data[program]
                    for epoch in epochs:
                        data = perf_data[program][epoch]
                        assert 'output_directory' in data
                        state.perf_output_dir = data['output_directory']
                        if state.perf_test_type == "op":

                            runtime_file_path = state.perf_output_dir + "/runtime_table.json"
                            # assert runtime_file_path.exists()
                            assert os.path.exists(state.perf_output_dir + "/runtime_table.json")
                            with open(runtime_file_path) as runtime_file:
                                runtime_json = json.load(runtime_file)
                                target_cores = get_op_postprocessor_key(runtime_json, state.target_op_name)
                                # target_inputs = get_op_largest_input_idx_key(runtime_json, target_cores)
                                min_utilization = None
                                max_runtime = None
                                for target_idx, target_core in enumerate(target_cores):
                                    # target_input = target_inputs[target_idx]
                                    assert target_core in runtime_json
                                    assert 'average-math-utilization' in runtime_json[target_core]
                                    average_math_utilization = ROUND(runtime_json[target_core]["average-math-utilization"], 2)
                                    total_runtime = runtime_json[target_core]["total-runtime"]
                                    if total_runtime == "N/A":
                                        total_runtime = 0
                                    if not min_utilization or average_math_utilization < min_utilization:
                                        min_utilization = average_math_utilization
                                        max_runtime = total_runtime

                                perf_result.update({"average_math_utilization": min_utilization})
                                perf_result.update({"total_runtime": max_runtime})
                                new_perf_target = state.perf_target
                                if state.perf_target is None or min_utilization > state.perf_target:
                                    new_perf_target = min_utilization
                                perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["op"]: new_perf_target})
                        elif state.perf_test_type == "graph":
                            for datum in data:
                                if datum == 'Number of cycles per input  (Skipping first input)':
                                    perf_result.update({"cycles_per_tensor": perf_data[program][epoch][datum]})
                                elif datum == 'Number of inputs per second (Skipping first input)':
                                    perf_result.update({"tensors_per_second": perf_data[program][epoch][datum]})
                                elif datum == 'Number of cycles (First unpack to last pack) for last input':
                                    perf_result.update({"cycles_for_last_input": perf_data[program][epoch][datum]})
                                elif datum == 'Largest epoch binary queue empty among all cores':
                                    perf_result.update({"largest_epoch_queue_empty": perf_data[program][epoch][datum]})
                                else:
                                    perf_result.update({datum: perf_data[program][epoch][datum]})
                        elif (
                            state.perf_test_type == "dram-rd" or
                            state.perf_test_type == "dram-wr" or
                            state.perf_test_type == "input0-bw" or
                            state.perf_test_type == "output-bw"
                        ):
                            runtime_file_path = state.perf_output_dir + "/runtime_table.json"
                            # assert runtime_file_path.exists()
                            assert os.path.exists(state.perf_output_dir + "/runtime_table.json")
                            with open(runtime_file_path) as runtime_file:
                                runtime_json = json.load(runtime_file)
                                target_cores = get_op_postprocessor_key(runtime_json, state.target_op_name)
                                for target_idx, target_core in enumerate(target_cores):
                                    # target_input = target_inputs[target_idx]
                                    assert target_core in runtime_json
                                    assert 'trisc-bw-operand-input-0' in runtime_json[target_core]
                                    assert 'trisc-bw-operand-output-0' in runtime_json[target_core]
                                    input0_bw = runtime_json[target_core]["trisc-bw-operand-input-0"]
                                    output_bw = runtime_json[target_core]["trisc-bw-operand-output-0"]
                                    target_perf_results = state.perf_target
                                    if state.perf_test_type == "dram-rd":
                                        perf_result.update({"dram-read-bw": input0_bw})
                                        if state.perf_target is None or input0_bw > state.perf_target:
                                            target_perf_results = input0_bw
                                        perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["dram-rd"]: target_perf_results})
                                    elif state.perf_test_type == "dram-wr":
                                        perf_result.update({"dram-write-bw": output_bw})
                                        if state.perf_target is None or output_bw > state.perf_target:
                                            target_perf_results = output_bw
                                        perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["dram-wr"]: target_perf_results})
                                    elif state.perf_test_type == "input0-bw":
                                        perf_result.update({"input0-bw": input0_bw})
                                        if state.perf_target is None or input0_bw > state.perf_target:
                                            target_perf_results = input0_bw
                                        perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["input0-bw"]: target_perf_results})
                                    elif state.perf_test_type == "output-bw":
                                        perf_result.update({"output-bw": output_bw})
                                        if state.perf_target is None or output_bw > state.perf_target:
                                            target_perf_results = output_bw
                                        perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["output-bw"]: target_perf_results})

        if len(perf_result) > 0:
            perf_result.update({"commit_hash": state.commit_hash})
            perf_result.update({"test_name": state.test_name})
            perf_result.update({"@timestamp": TIMESTAMP})
            perf_result.update({"build_id": state.build_id})
            perf_result.update({"test_group": state.group_name})
            perf_result.update({"host-name": host_name})
            test_config = get_test_configs(state.perf_output_dir)
            perf_result.update(test_config)
            print("Uploading the following perf results:")
            print(perf_result)
            es_log.indices.create(
                index=get_index(state), ignore=400
            )  # 400 means ignore error if index already exists
            record = es_log.index(index=get_index(state), body=perf_result)
    except Exception as e:
        print(f"Performance data was not successfully uploaded due to following error:\n {e}")
        raise Exception("Error occured")


def run_performance_test(state):

    state.perf_target = get_perf_targets(state)
    modify_test_command(state)
    try:
        run_test(state.cmd, state.ci, state.timeout)
    except ValueError:
        upload_perf_results(state)
        raise
    upload_perf_results(state)

def main():
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

    parser.add_argument("--test-name", type=str, required=True, help="The name of the test.")
    parser.add_argument("--group-name", type=str, default="", help="The name of the test group.")
    parser.add_argument("--perf-test-type", type=str, required=True, help="Type of performance test. It is required know what measurement to extract.")
    parser.add_argument("--commit-hash", type=str, default="", help="The commit hash that the test is running on.")
    parser.add_argument("--tag", type=str, default="push", help="The test suite tag.")
    parser.add_argument("--branch-name", type=str, default="", help="The branch name test is running.")
    parser.add_argument("--build-id", type=str, default="", help="Build id for the current run.")

    parser.add_argument("--target-op-name", type=str, default="", help="The name of the op to read the performance from, when the test_type is 'op'.")

    parser.add_argument("--timeout", type=int, default=None, help="timeout in seconds to wait for command to finish")
    parser.add_argument('--force-update-target', action="store_true", default=False, help='Force update the performance targets and skip the check')
    parser.add_argument('--ci', action="store_true", default=False, help='Running the script on ci')

    parser.add_argument('--use-test-database', action="store_true", default=False, help='Use the test database to read and upload the perf results')

    state = parser.parse_args(script_args)

    assert state.test_name != "", "--test-name must be populated with a non empty string"
    state.perf_output_dir = state.test_name + "/"
    state.local_run = not state.ci

    if state.ci:
        assert state.group_name     != ""
        assert state.build_id       != ""
        assert state.tag            != ""
        assert state.commit_hash    != ""

    assert (
        state.perf_test_type == 'graph' or
        state.perf_test_type == 'op' or
        state.perf_test_type == 'dram-rd' or
        state.perf_test_type == 'dram-wr' or
        state.perf_test_type == 'input0-bw' or
        state.perf_test_type == 'output-bw'
    )

    if state.perf_test_type == 'op':
        assert state.target_op_name != "", "In op mode, we must specify the name of the op to read the performance for"

    state.cmd = cmd_args

    if os.path.exists(state.perf_output_dir):
        shutil.rmtree(state.perf_output_dir)
    os.makedirs(state.perf_output_dir)

    run_performance_test(state)

if __name__ == "__main__":

    try:
        main()
    except:
        kill_subprocess()
        print(traceback.format_exc())
