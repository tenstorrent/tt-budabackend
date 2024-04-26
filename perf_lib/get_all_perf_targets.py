#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
import yaml
import argparse
from typing import List
import logging
from logger_utils import logger, print_progress_bar, ASSERT
from perf_ci_utils import (
    post_ci_command_key,
    get_ci_dir,
    all_test_types,
    PerfTestInfo,
    get_test_metric,
    get_multi_test_names,
    get_test_suite_yaml_file_name,
    fetch_latest_perf_target,
)
# This could be called as a make procedure
# Log conditionally
g_allow_logs = False

cwd = os.getcwd()
        
# Each entry in the output yaml will have the test name, along with the perf target for the test and other test info
def get_output_entry(test_info: PerfTestInfo, perf_target: float):
    ASSERT(test_info.test_type in all_test_types, f"Invalid test type {test_info.test_type}")
    test_name = test_info.test_name
    test_type = test_info.test_type
    entry = {
        test_name: {
            "test_type": test_type,
            "perf_target": {
                "metric": get_test_metric(test_type),
                "value": perf_target,
            }
        }
    }
    return entry

def get_tests_with_perf_targets(arch: str, tag: str, bbe_path: str, suppress_logs=False) -> List[PerfTestInfo]:
    ci_perf_test_dir = get_ci_dir(arch, tag)
    test_suite_yaml_name = get_test_suite_yaml_file_name(arch, tag)
    test_suite_path = ci_perf_test_dir + "/" + test_suite_yaml_name
    ASSERT(os.path.isfile(test_suite_path), f"{test_suite_path} is not a valid file")
    with open(test_suite_path, "r") as test_yaml:
        test_suite_data = yaml.safe_load(test_yaml)
        test_yaml.close()
    # Currently there should only be 1 test group perf test_suite_yaml
    test_group = list(test_suite_data.keys()).pop()
    all_perf_target_tests = []
    for key in test_suite_data[test_group]:
        # Tests that have perf targets should have an analyze_script command
        if (not isinstance(test_suite_data[test_group][key], dict)) or (not post_ci_command_key in test_suite_data[test_group][key]):
            continue
        test_base_name = key
        test_params = test_suite_data[test_group][test_base_name]
        multi_test_dir_key = 'multi_test_dir'
        multi_test_dir = ""
        if multi_test_dir_key in test_params and test_params[multi_test_dir_key]:
            multi_test_dir = bbe_path + "/" + test_params[multi_test_dir_key]
        # Need to get every test under the test_base_name - some tests are multi test, and have multiple tests under the same test_base_name bracket
        test_names = get_multi_test_names(test_base_name=test_base_name, multi_test_dir=multi_test_dir, suppress_logs=suppress_logs)
        # Tests under the same test_base_name have the same test type
        for test_name in test_names:
            test_info = PerfTestInfo(arch_name=arch, tag=tag, test_group=test_group, test_name=test_name)
            # We need the test type to fetch the perf targets
            post_ci_command: List[str] = test_params[post_ci_command_key]
            ASSERT("--test-type" in post_ci_command, "post ci command should include --test-type parameter")
            test_info.test_type = post_ci_command[post_ci_command.index("--test-type") + 1]
            ASSERT(test_info.test_type in all_test_types, f"invalid test type {test_info.test_type}")
            all_perf_target_tests.append(test_info)
    
    return all_perf_target_tests

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--output-dir", type=str, default=f"{cwd}/build/perf_targets/", help="The output directory for the perf targets yaml.")
    parser.add_argument("--arch-names", nargs="*", default=["grayskull", "wormhole_b0"], help="The arch names for the test suites to generate perf targets for.")
    parser.add_argument("--tags", nargs="*", default=["push", "nightly"], help="The tags (push, nightly) for the test suites to generate perf targets for.")
    parser.add_argument("--allow-logs", action="store_true", default=False)
    args = parser.parse_args()
    
    local_run = not bool(int(os.environ.get("CI_RUN", "0")))
    
    os.makedirs(args.output_dir, exist_ok=True)
    
    g_allow_logs = args.allow_logs
    
    if not g_allow_logs:
        logger.setLevel(logging.WARNING)
    
    output_yaml_path = f"{args.output_dir}" + "/all_perf_targets.yaml"
    archs = [arch.lower() for arch in args.arch_names]
    tags = [tag.lower() for tag in args.tags]
    
    logger.info(f"Using output directory {output_yaml_path}")
    perf_targets_map = {}
    
    all_suites_to_check = [(arch, tag) for arch in archs for tag in tags]
    
    for suite_id, (arch, tag) in enumerate(all_suites_to_check):
        print_progress_bar(suite_id, len(all_suites_to_check))
        logger.info(f"Fetching perf targets for test suite: {get_test_suite_yaml_file_name(arch, tag)}")
        
        # Suppress logs if we're running locally - some multi-tests may not exist if they weren't generated by the user
        # These multi-tests should exist when running on CI, generated by the build_cmd
        all_perf_target_tests: List[PerfTestInfo] = get_tests_with_perf_targets(arch=arch, tag=tag, bbe_path=cwd, suppress_logs=(not g_allow_logs or local_run))
        
        # For every test with perf targets in the test suite, query the database to get the latest perf targets
        for test_info in all_perf_target_tests:
            ASSERT(test_info.has_valid_test_type(), "Tests that have perf targets must have a test type")
            
            logger.info(f"Fetching perf targets for test: {test_info.test_name}")
            
            # Query CI database to fetch perf targets
            perf_target = fetch_latest_perf_target(test_info.test_group, test_info.test_name, test_info.test_type)

            if perf_target is None:
                if not local_run:
                    logger.warn(f"{test_info.test_name} does not have perf targets in database. Maybe this is a newly added tests?")
                    continue
                else:
                    logger.fatal("In local run the target performance must exist in database")
                    exit(1)
            
            logger.info(f"Got perf target for {test_info.test_name} = {perf_target}")
            
            if test_info.test_group not in perf_targets_map:
                perf_targets_map[test_info.test_group] = {}
            perf_targets_map[test_info.test_group].update(get_output_entry(test_info, perf_target))
            
    print_progress_bar(len(all_suites_to_check), len(all_suites_to_check))
    
    with open(output_yaml_path, "w") as file:
        yaml.dump(perf_targets_map, file, indent=4)
        
    logger.info(f"Success, dumped all perf targets in {output_yaml_path}")
