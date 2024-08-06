#!usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
import sys
import subprocess
import pathlib
import yaml
import json
import socket
from datetime import datetime
from typing import Dict, Any, List, Union
from dataclasses import dataclass
from elasticsearch import Elasticsearch
from perf_test_base import (
    sys_add_path
)
cwd = os.getcwd()
sys_add_path(f"{cwd}/ci/", True)
from utils import (
    should_upload_perf_results,
    get_op_postprocessor_key,
    get_test_configs,
    ROUND,
    ErrorDebugger,
    TEST_TYPE_TO_TARGET_KEY_ON_DB,
)
from repo import (
    BBE_PERF_INDEX_NAME,
    ES_ENDPOINT,
    ES_USERNAME,
    ES_PASSWORD,
)

ERROR_DEBUGGER = ErrorDebugger()

post_ci_command_key = "analyze_script"
ci_base_dir = "ci/test-lists/silicon/"

all_test_types = (
    'graph',
    'op',
    'dram-rd',
    'dram-wr',
    'input0-bw',
    'output-bw',
)

test_types_with_target_op = (
    "op",
    "dram-rd",
    "dram-wr",
    "input0-bw",
    "output-bw",
)

@dataclass
class PerfTestInfo():
    arch_name: str = ""
    tag: str = ""
    test_group: str = ""
    test_name: str = ""
    test_type: str = ""
    
    def has_valid_test_type(self):
        return self.test_type in all_test_types
        
    def set_test_group(self):
        self.test_group = f"perf_infra_{self.arch_name}_silicon_{self.tag}"
    

def is_number(val):
    return isinstance(val, (int, float))

def is_valid_arch(arch: str):
    return arch.lower() in ["grayskull", "wormhole_b0", "blackhole"]    

def is_valid_tag(tag: str):
    return tag.lower() in ["push", "nightly"]

def get_ci_dir(arch: str, tag: str):
    assert is_valid_arch(arch)
    assert is_valid_tag(tag)
    return ci_base_dir + f"{tag}/" + f"{arch}/"

def get_test_suite_yaml_file_name(arch: str, tag: str):
    return f"perf_infra_{arch}_silicon_{tag}.yaml"

def get_test_metric(test_type: str):
    assert test_type in all_test_types
    return TEST_TYPE_TO_TARGET_KEY_ON_DB[test_type]

def get_multi_test_names(test_base_name: str, multi_test_dir="", suppress_logs=False) -> List[str]:
    """
    Extract all test names.
    
    If multi test, test_name = f"{test_base_name}.{netlist_name}" for every netlist under the multi_test_dir.
    
    If not multi test, test_name = test_base_name.
    """
    test_names = []
    if multi_test_dir == "":
        return [test_base_name]
    
    elif not os.path.isdir(multi_test_dir):
        ERROR_DEBUGGER.add_log_output(f"[GET TEST NAMES] Non-existent multi-test directory {multi_test_dir}")
        if not suppress_logs:
            ERROR_DEBUGGER.print_trace_info()
        return []
        
    multi_test_netlist_yamls = os.listdir(multi_test_dir)
    
    # Using the logic to construct multi-tests from ci/run.py
    test_names = [f"{test_base_name}.{netlist_name}" for netlist_name in multi_test_netlist_yamls if netlist_name.endswith(".yaml")]
    
    return test_names

def get_commits_after(commit_hash):
    """
    Get all the commits after <commit_hash>
    """
    try:
        # Run git log command to get all commits after the specified commit hash
        result = subprocess.check_output(['git', 'log', '--pretty=format:%H: %s', f'{commit_hash}..HEAD']).strip().decode('utf-8')
        
        # Check if the command was successful
            # Split the output into a list of commit hashes
        commit_hashes = result.strip("").split('\n')
        commit_hashes = [commit.strip() for commit in commit_hashes if commit not in " \n"]
        return commit_hashes
    
    except Exception as e:
        ERROR_DEBUGGER.add_log_output(f"[GET COMMITS AFTER] Could not get subsequent commits due to exception: {e}")
        ERROR_DEBUGGER.print_trace_info()
        return None
    
def fetch_latest_test_result(test_group: str, test_name: str, log_file=sys.stdout):
    """
    Get the latest test result of a specific test from the ES database.
    
    The test result contains timestamp, aiclk, perf_targets, commit hash etc.
    """
    search_query = {
        "size": 1,
        "query": {
            "bool": {
                "must": [],
                "should": [],
                "must_not": [],
                "filter": [
                    {
                    "bool": {
                        "should": [
                                {
                                    "match_phrase": {
                                        "test_name.keyword": test_name
                                    }
                                }
                            ],
                        }
                    },
                    {
                    "bool": {
                        "should": [
                                {
                                    "match_phrase": {
                                        "test_group.keyword": test_group
                                    }
                                }
                            ],
                        }
                    },
                ],
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

    res = es_client.search(index=BBE_PERF_INDEX_NAME, body=search_query)

    num_results = len(res['hits']['hits'])
    
    assert num_results <= 1
    
    if num_results == 0:
        ERROR_DEBUGGER.add_log_output(f"[GET_LATEST_TEST_RESULT] No runs found in ES for {test_group} {test_name}", file=log_file)
        return None
    
    test_result = res['hits']['hits'][0]['_source']
    return test_result
    
def fetch_latest_perf_target(test_group: str, test_name: str, test_type: str, log_file=sys.stdout):
    assert test_type in all_test_types
    latest_test_result = fetch_latest_test_result(test_group, test_name, log_file)
    if latest_test_result is None:
        return None
    return latest_test_result[TEST_TYPE_TO_TARGET_KEY_ON_DB[test_type]]

def fetch_latest_commit_hash(test_group: str, test_name: str, log_file=sys.stdout):
    latest_test_result = fetch_latest_test_result(test_group, test_name, log_file)
    if latest_test_result is None:
        return None
    return latest_test_result["commit_hash"]

def post_run_upload_perf_results(
    test_name: str, 
    test_group: str, 
    perf_test_type: str,
    target_op_name: str,
    tag: str, 
    branch_name: str, 
    commit_hash: str, 
    build_id: str, 
    perf_output_dir: str, 
    perf_target: Union[int, float],
    local_run: bool, 
):
    """
    Upload perf results to the database.
    
    Perf results will only be uploaded if: 
    - we are running on a CI docker
    - we are running on origin/master or nightly
    - the test has required perf output files to extract perf results from
    """
    hostname = socket.gethostname()
    jobid = os.getenv("SLURM_JOB_ID")
    host_name = ""
    f_upload_perf_results = sys.stdout #open(f"./{test['name']}.upload_perf_results.log","w+")
    if perf_output_dir is not None and perf_output_dir != "":
        perf_info_file_path = f"{perf_output_dir}/perf_results/perf_info_all_epochs.yaml"
        perf_file = pathlib.Path(perf_info_file_path)
    else:
        return

    ERROR_DEBUGGER.add_log_output(f"[UPLOAD_PERF_RESULTS] tag = {tag}, branch_name = {branch_name}", file=f_upload_perf_results)
    enable_dump = should_upload_perf_results(branch_name, tag, local_run)
    if not enable_dump:
        ERROR_DEBUGGER.add_log_output("[UPLOAD_PERF_RESULTS] Skipping performance results, since tag is not nightly and branch_name is not origin/master or running ci locally", file=f_upload_perf_results)
        ERROR_DEBUGGER.print_trace_info()
        return

    TIMESTAMP = datetime.utcnow()
    perf_result = dict()
    es_log = Elasticsearch([ES_ENDPOINT], http_auth=(ES_USERNAME, ES_PASSWORD))
    try:
        if perf_test_type == "graph":
            ERROR_DEBUGGER.add_log_output("[UPLOAD_PERF_RESULTS] Checking if backend report exists for graph perf mode", file=f_upload_perf_results)
            host_output_dir = perf_output_dir + "/perf_results/host/"
            host_summary_file_path = host_output_dir + "host_summary_report.json"
            assert os.path.exists(host_summary_file_path)
            with open(host_summary_file_path) as host_summary_file:
                summary_report = json.load(host_summary_file)
                assert "samples-per-second" in summary_report
                perf_result.update({"backend-samples-per-second": summary_report["samples-per-second"]})

            with open(perf_info_file_path) as perf_info_file:
                perf_info_yaml = yaml.load(perf_info_file, Loader=yaml.FullLoader)
                assert "global-events" in perf_info_yaml
                assert "samples-per-second-excluding-last-epoch-of-each-program" in perf_info_yaml["global-events"]
                samples_per_second_excluding_last_epoch = perf_info_yaml["global-events"]["samples-per-second-excluding-last-epoch-of-each-program"]
                new_perf_target_excluding_last_epoch = perf_target
                if perf_target is None or samples_per_second_excluding_last_epoch > perf_target:
                    new_perf_target_excluding_last_epoch = samples_per_second_excluding_last_epoch
                perf_result.update({"samples-per-second-excluding-last-epoch-of-each-program": samples_per_second_excluding_last_epoch})
                perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["graph"]: new_perf_target_excluding_last_epoch})
        ERROR_DEBUGGER.add_log_output("[UPLOAD_PERF_RESULTS] Checking if performance report exists for test", test_name, "at", perf_file, file=f_upload_perf_results)
        if perf_file.exists():
            with open(perf_file) as perfdump:
                perf_data = yaml.load(perfdump, Loader=yaml.FullLoader)
                assert "global-events" in perf_data
                assert "host-name" in perf_data["global-events"]
                host_name = perf_data["global-events"]["host-name"]
                
                if perf_test_type in (
                    "op",
                    "dram-rd",
                    "dram-wr",
                    "input0-bw",
                    "output-bw",
                ):
                    assert target_op_name
                    
                for program in perf_data.keys():
                    if program == "global-events":
                        continue
                    epochs = perf_data[program]
                    for epoch in epochs:
                        data = perf_data[program][epoch]
                        assert 'output_directory' in data
                        perf_output_dir = data['output_directory']
                        if perf_test_type == "op":

                            runtime_file_path = perf_output_dir + "/runtime_table.json"
                            # assert runtime_file_path.exists()
                            assert os.path.exists(perf_output_dir + "/runtime_table.json")
                            with open(runtime_file_path) as runtime_file:
                                runtime_json = json.load(runtime_file)
                                target_cores = get_op_postprocessor_key(runtime_json, target_op_name)
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
                                new_perf_target = perf_target
                                if perf_target is None or min_utilization > perf_target:
                                    new_perf_target = min_utilization
                                perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["op"]: new_perf_target})
                        elif perf_test_type == "graph":
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
                            perf_test_type == "dram-rd" or
                            perf_test_type == "dram-wr" or
                            perf_test_type == "input0-bw" or
                            perf_test_type == "output-bw"
                        ):
                            runtime_file_path = perf_output_dir + "/runtime_table.json"
                            # assert runtime_file_path.exists()
                            assert os.path.exists(perf_output_dir + "/runtime_table.json")
                            with open(runtime_file_path) as runtime_file:
                                runtime_json = json.load(runtime_file)
                                target_cores = get_op_postprocessor_key(runtime_json, target_op_name)
                                for target_idx, target_core in enumerate(target_cores):
                                    # target_input = target_inputs[target_idx]
                                    assert target_core in runtime_json
                                    assert 'trisc-bw-operand-input-0' in runtime_json[target_core]
                                    assert 'trisc-bw-operand-output-0' in runtime_json[target_core]
                                    input0_bw = runtime_json[target_core]["trisc-bw-operand-input-0"]
                                    output_bw = runtime_json[target_core]["trisc-bw-operand-output-0"]
                                    target_perf_results = perf_target
                                    if perf_test_type == "dram-rd":
                                        perf_result.update({"dram-read-bw": input0_bw})
                                        if perf_target is None or input0_bw > perf_target:
                                            target_perf_results = input0_bw
                                        perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["dram-rd"]: target_perf_results})
                                    elif perf_test_type == "dram-wr":
                                        perf_result.update({"dram-write-bw": output_bw})
                                        if perf_target is None or output_bw > perf_target:
                                            target_perf_results = output_bw
                                        perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["dram-wr"]: target_perf_results})
                                    elif perf_test_type == "input0-bw":
                                        perf_result.update({"input0-bw": input0_bw})
                                        if perf_target is None or input0_bw > perf_target:
                                            target_perf_results = input0_bw
                                        perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["input0-bw"]: target_perf_results})
                                    elif perf_test_type == "output-bw":
                                        perf_result.update({"output-bw": output_bw})
                                        if perf_target is None or output_bw > perf_target:
                                            target_perf_results = output_bw
                                        perf_result.update({TEST_TYPE_TO_TARGET_KEY_ON_DB["output-bw"]: target_perf_results})
        if len(perf_result) > 0:
            perf_result.update({"commit_hash": commit_hash})
            perf_result.update({"test_name": test_name})
            perf_result.update({"@timestamp": TIMESTAMP})
            perf_result.update({"build_id": build_id})
            perf_result.update({"test_group": test_group})
            perf_result.update({"host-name": host_name})
            test_config = get_test_configs(perf_output_dir)
            perf_result.update(test_config)
            # print(perf_result)
            es_log.indices.create(
                index=BBE_PERF_INDEX_NAME, ignore=400
            )  # 400 means ignore error if index already exists
            record = es_log.index(index=BBE_PERF_INDEX_NAME, body=perf_result)
    except Exception as e:
        ERROR_DEBUGGER.add_log_output(f"[UPLOAD_PERF_RESULTS] Performance data was not successfully uploaded due to following error:\n {e} \n on host {hostname} for jobid {jobid}", file=f_upload_perf_results)
        ERROR_DEBUGGER.print_trace_info()

