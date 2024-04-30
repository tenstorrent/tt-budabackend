#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
import re
import sys
import yaml
import argparse
from enum import Enum
from typing import Dict, Any, List
import dataclasses
from dataclasses import dataclass
import subprocess
import hashlib
import time
from perf_ci_utils import (
    PerfTestInfo,
    all_test_types,
    test_types_with_target_op,
    is_number, 
    fetch_latest_perf_target,
    get_commits_after,
    fetch_latest_commit_hash,
    get_multi_test_names,
    post_run_upload_perf_results,
)
cwd = os.getcwd()
sys.path.append(f"{cwd}/ci/")
# Import apis from ci/utils.py
from utils import (
    ErrorDebugger,
    should_upload_perf_results,
)

MY_LOG_PREFIX = "[PERF_POST_CI]"

# These perf targets are extracted from the CI database during test build
CI_PERF_TARGETS_FILE_NAME = "all_perf_targets.yaml"

f_perf_post_ci = sys.stdout

ERROR_DEBUGGER = ErrorDebugger()

class PerfTargetSearchResult(Enum):
    PerfTargetFound = 0
    TestGroupNotFound = 1
    TestNameNotFound = 2
    BuildFileNotFound = 3
    
@dataclass
class PostRunTestInfo(PerfTestInfo):
    perf_output_dir: str = ""
    target_op_name: str = ""
    multi_test_dir: str = ""
    commit_hash: str = ""
    commit_msg: str = ""
    branch_name: str = ""
    build_id: str = ""
    local_run: bool = False

    def set_build_id(self):
        hash_val = hashlib.sha256(str(time.time()).encode('utf-8')).hexdigest()[:8]
        self.build_id = f"{self.arch_name}_silicon_{self.tag}-{hash_val}"
        
    def set_commit_hash(self):
        self.commit_hash = subprocess.check_output(['git', 'rev-parse', 'HEAD']).strip().decode('utf-8')
    
    def set_commit_message(self):
        self.commit_msg = subprocess.check_output(['git', 'log', '-1', '--pretty=%B']).strip().decode('utf-8')
    
    def set_branch_name(self):
        self.branch_name = subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD']).strip().decode('utf-8')

    def should_force_update_targets(self):
        """
        Determine if we should update the perf targets of this test run
        
        - For push: 
            check if commit message has [update_push_perf_target: <comma-separated test_names>] and self.test_name is included
            
        - For nightly: 
            check if any commit message since the last run has [update_push_perf_target: <comma-separated test_names>], and self.test_name is included in any
        """
        
        assert self.arch_name and self.tag
        regex_str = f"\[update_perf_target_{self.arch_name}_{self.tag}: (.*?)\]"
        rx = re.compile(regex_str)
        if self.tag == "push":
            matches = rx.findall(self.commit_msg)
            return any([self.test_name in match for match in matches])
        
        elif self.tag == "nightly":
            matches: List[str] = []
            commit_hash_previous_run = fetch_latest_commit_hash(self.test_group, self.test_name)
            
            # If the test was never run before, that means it's a new test
            # Force update the targets
            if commit_hash_previous_run is None:
                return True
            
            commits_after_previous_run = get_commits_after(commit_hash_previous_run)
            for commit in commits_after_previous_run:
                matches += rx.findall(commit)
            return any([self.test_name in match for match in matches])
        else:
            raise ValueError(f"Invalid tag {self.tag}")
        
    def copy(self):
        my_copy = PostRunTestInfo(**dataclasses.asdict(self))
        return my_copy
    
def get_perf_target_from_build(test_group: str, test_name: str, build_dir: str):
    all_perf_targets_yaml = build_dir + "/" + "perf_targets" + "/" + CI_PERF_TARGETS_FILE_NAME
    
    if not os.path.isfile(all_perf_targets_yaml): 
        return PerfTargetSearchResult.BuildFileNotFound, None

    with open(all_perf_targets_yaml, "r") as file:
        perf_targets_map = yaml.safe_load(file)
        file.close()
        
    if test_group not in perf_targets_map:
        return PerfTargetSearchResult.TestGroupNotFound, None
    
    test_group_perf_targets_map = perf_targets_map[test_group]
    
    if test_name not in test_group_perf_targets_map:
        return PerfTargetSearchResult.TestNameNotFound, None
    
    assert "perf_target" in test_group_perf_targets_map[test_name] and "value" in test_group_perf_targets_map[test_name]["perf_target"]
    perf_target_value = test_group_perf_targets_map[test_name]["perf_target"]["value"]
    assert is_number(perf_target_value)
    
    return PerfTargetSearchResult.PerfTargetFound, perf_target_value


# In ci post run, we upload the perf results, and check if we need to force update perf targets
def perf_ci_post_run(post_run_test_info: PostRunTestInfo):
    if not should_upload_perf_results(branch_name=post_run_test_info_base.branch_name, tag=post_run_test_info_base.tag, local_run=post_run_test_info_base.local_run):
        return
    
    test_group = post_run_test_info.test_group
    test_name = post_run_test_info.test_name

    perf_target = None
    
    if post_run_test_info.should_force_update_targets():
        # setting perf_target to None will tell post_run_upload_perf_results to use the result of this run as the new perf target
        perf_target = None
        ERROR_DEBUGGER.add_log_output(f"{MY_LOG_PREFIX} Force updating perf target for test group {test_group} test name {test_name} to {perf_target}", file=f_perf_post_ci)

    else:
        build_dir = "./build"
        
        # Search the build file for the expected perf target
        search_result, ci_perf_target = get_perf_target_from_build(test_group=test_group, test_name=test_name, build_dir=build_dir)
        
        if search_result == PerfTargetSearchResult.PerfTargetFound:
            perf_target = None if ci_perf_target == 0 else ci_perf_target
            ERROR_DEBUGGER.add_log_output(f"{MY_LOG_PREFIX} Got the latest perf targets for test_name {test_name}, test_group {test_group} = {ci_perf_target}", file=f_perf_post_ci)
            
        elif search_result == PerfTargetSearchResult.BuildFileNotFound:
            ERROR_DEBUGGER.add_log_output(f"{MY_LOG_PREFIX} Unexpected perf targets build file not found, it should have been created in build. Fetching perf targets from db now...", file=f_perf_post_ci)
            ERROR_DEBUGGER.print_trace_info()
            perf_target = fetch_latest_perf_target(post_run_test_info.test_group, post_run_test_info.test_name, post_run_test_info.test_type)

        else:
            ERROR_DEBUGGER.add_log_output(f"{MY_LOG_PREFIX} No perf targets for test_name {test_name}, test_group {test_group} was found in build file. Re-fetching perf targets from db to make sure...", file=f_perf_post_ci)
            ERROR_DEBUGGER.print_trace_info()
            perf_target = fetch_latest_perf_target(post_run_test_info.test_group, post_run_test_info.test_name, post_run_test_info.test_type)
        
    
    ERROR_DEBUGGER.add_log_output(f"{MY_LOG_PREFIX} Passing perf target {perf_target} to post_run_upload_perf_results")
    # upload the perf target to the database
    # if perf_target is None, the perf_this_run will be set as the perf target in the ci database
    # otherwise, max(perf_target, perf_this_run) will be set as the perf target in the ci database
    post_run_upload_perf_results(
        test_name=test_name,
        test_group=test_group,
        perf_test_type=post_run_test_info.test_type,
        target_op_name=post_run_test_info.target_op_name,
        tag=post_run_test_info.tag,
        branch_name=post_run_test_info.branch_name,
        commit_hash=post_run_test_info.commit_hash,
        build_id=post_run_test_info.build_id,
        perf_output_dir=post_run_test_info.perf_output_dir,
        perf_target=perf_target,
        local_run=post_run_test_info.local_run
    )
    
# This script should only be run for tests that have perf targets to be uploaded/compared in the CI database
if __name__ == "__main__":
    ci_run = bool(int(os.environ.get("CI_RUN", "0")))
    exit_code = 0
    if ci_run:
        exit_code = os.environ.get("EXIT_CODE", None)
        assert exit_code is not None, "Unexpected EXIT_CODE not set in env when running on ci"
        
    if int(exit_code) != 0:
        ERROR_DEBUGGER.add_log_output(f"{MY_LOG_PREFIX} Not running perf post ci run because the test failed with exit code {exit_code}")
        ERROR_DEBUGGER.print_trace_info()
        exit(0)
    
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", type=str, required=True, choices=["grayskull", "wormhole_b0"], help="Arch name. Either grayskull or wormhole_b0")
    parser.add_argument("--tag", type=str, required=True, choices=["push", "nightly"], help="Test suite tag. Either push or nightly")
    parser.add_argument("--test-name", type=str, required=True, help="Name of the test as indicated in the test suite yaml")
    parser.add_argument("--test-type", type=str, required=True, choices=all_test_types, help=f"Type of test. Should be one of {all_test_types}")
    parser.add_argument("--target-op-name", type=str, default="", help="Name of target op. Required for all non-graph perf tests")
    parser.add_argument("--multi-test-dir", type=str, default="", help="Set this to the multi test output dir if the test is a multi test")
    args = parser.parse_args()
    
    post_run_test_info_base = PostRunTestInfo()
    post_run_test_info_base.arch_name = args.arch
    post_run_test_info_base.tag = args.tag
    post_run_test_info_base.test_type = args.test_type
    post_run_test_info_base.target_op_name = args.target_op_name
    
    if post_run_test_info_base.test_type in test_types_with_target_op:
        assert post_run_test_info_base.target_op_name != ""
    
    post_run_test_info_base.multi_test_dir = args.multi_test_dir
    post_run_test_info_base.local_run = (not ci_run)
    post_run_test_info_base.set_test_group()
    post_run_test_info_base.set_build_id()
    post_run_test_info_base.set_commit_hash()
    post_run_test_info_base.set_commit_message()
    post_run_test_info_base.set_branch_name()
    
    if not should_upload_perf_results(branch_name=post_run_test_info_base.branch_name, tag=post_run_test_info_base.tag, local_run=post_run_test_info_base.local_run):
        ERROR_DEBUGGER.add_log_output(f"{MY_LOG_PREFIX} Skipping perf ci post run because either we're running locally, or not running on master/nightly")
        exit(0)
    
    test_infos = []
    test_base_name = args.test_name
    
    # perf output dir for CI tests should be the test name
    # test name for multi-tests is set as <test_base_name>.<netlist_name>
    test_names = get_multi_test_names(test_base_name=test_base_name, multi_test_dir=post_run_test_info_base.multi_test_dir)
    for test_name in test_names:
        post_run_test_info = post_run_test_info_base.copy()
        post_run_test_info.test_name = test_name
        post_run_test_info.perf_output_dir = test_name
        test_infos.append(post_run_test_info)
    
    for post_run_test_info in test_infos:
        ERROR_DEBUGGER.add_log_output(f"{MY_LOG_PREFIX} Running perf post ci run with test descriptor: {post_run_test_info}")
        perf_ci_post_run(post_run_test_info)
    
    