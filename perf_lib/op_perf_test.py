#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import sys
import os
from datetime import datetime
from typing import List, Dict, Union
import yaml
import importlib
from perf_sweep import (
    get_op_type_from_arg,
    get_reblocking_type_from_arg,
    get_template,
    get_perf_test_mode,
    get_dram_assignment_mode,
)
from ops import *
from perf_test_base import *
from sweep_params import get_op_test_sweep_vars
from logger_utils import print_progress_bar
from elasticsearch import Elasticsearch

# Imports util from verif/template_netlist/
sys_add_path(REPO_ROOT, True)
sys_add_path(TEMPLATE_NETLIST_DIR, True)
from util import (
    create_netlist_from_single_config,
    PerfTestType,
    PerfOpSweepConfig,
    ReblockTM,
)
from generate_tests import generate_all_configs

sys_add_path(f"{REPO_ROOT}/ci", True)
from repo import ES_ENDPOINT, ES_USERNAME, ES_PASSWORD

BBE_PERF_INDEX_NAME = "spatial-ci-perf"
PERF_SCRIPT_LOG_PREFIX = f"{os.path.basename(__file__)} :"
ROOT_DIR=f"{os.getcwd()}/"

def get_target_perf_label_from_type(op_type_str: str):
    if op_type_str == 'dram_read':
        return 'target-dram-read-bw'
    elif op_type_str == 'dram_write':
        return 'target-dram-write-bw'
    else:
        return 'target-average-math-utilization'

def get_perf_metric_from_op_type(op_type_str) -> PerfMetrics:
    if op_type_str == 'dram_read':
        return PerfMetrics.input0_bw
    elif op_type_str == 'dram_write':
        return PerfMetrics.output_bw
    else:
        return PerfMetrics.average_math_utilization

def get_perf_targets(state, test_name, test_group):
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
                        "test_name.keyword": test_name
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
                        "test_group.keyword": test_group
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
    res = es_client.search(index=BBE_PERF_INDEX_NAME, body=search_query)
    num_results = len(res['hits']['hits'])

    assert num_results == 1
    assert state.target_label in res['hits']['hits'][0]['_source'], "target perf does not exists in database"

    perf_target = res['hits']['hits'][0]['_source'][state.target_label]
    logger.info(f"Got the latest perf targets for test_name {test_name}, test_group {test_group} = {perf_target}")
    return perf_target

def run_single_config(state, name, test_config_and_perf):
    
    test_config = TestConfig(name, test_config_and_perf)
    sweep_vars = get_op_test_sweep_vars(state.op_type, state.reblocking_mode_perf_lib, test_config.parameters)
    perf_sweep_config = PerfOpSweepConfig(
        sweep_en=True,
        sweep_vars=sweep_vars,
        op_type=state.perf_test_type,
        perf_test_mode=state.perf_test_mode,
        reblocking_mode=state.reblocking_mode,
        dram_assignment_mode=get_dram_assignment_mode(state.dram_assignment_mode),
    )
    module_name = "test_perf"
    config_list = generate_all_configs(
        module_name=module_name,
        output_dir=state.netlists_output_dir,
        random_seed=state.random_seed,
        max_num_configs=None,
        dump_config_yaml=False,
        perf_config = perf_sweep_config,
        arch=state.arch_name,
    )
    if (len(config_list) > 1):
        logger.error("ERROR: Each op-test config must only produce a single test")
        logger.error("ERROR: Following configs were generated:")
        logger.info(config_list)
    assert len(config_list) == 1, "Each op-test config must only produce a single test"
    
    test_module = importlib.import_module(module_name)

    test_path_information = [create_netlist_from_single_config(
        template_yaml=get_template(test_module, state.perf_test_type, state.perf_test_mode),
        output_dir=ROOT_DIR + state.netlists_output_dir,
        config=config_list[0],
        test_id=name,
        use_shared_dir=True,
    )]
    
    for test_dir, yaml_file_name in test_path_information:
        attr = get_attr(state.op_type, state.reblocking_mode_perf_lib, config_list[0])
        netlist_path = f"{test_dir}/{yaml_file_name}"
        test_base_name = test_config.ci_config.test_base_name
        test_group = test_config.ci_config.test_group
        generate_perf_input_config_and_modify_netlist(test_config_and_perf, attr.target_op_name, netlist_path, test_base_name=test_base_name, test_group=test_group)

def run_sweep_and_check_perf(state):
    assert state.arch_name == "grayskull" or state.arch_name == "wormhole" or state.arch_name == "wormhole_b0"
    config_file_path = ROOT_DIR + f"/perf_lib/op_tests/{state.arch_name}/{state.op_name}"
    if state.reblocking_mode_perf_lib != ReblockTMPerfLib.Normal:
        config_file_path += f".{state.reblocking_mode.name.lower()}"
    if state.run_kernel_suite:
        config_file_path += ".kernel"
    if state.run_prologue:
        config_file_path += ".prologue"
    elif state.run_single_op:
        config_file_path += ".single_op"
    elif state.nightly:
        config_file_path += ".nightly"
    config_file_path += ".yaml"
    
    logger.info(f"Reading test generation config from {config_file_path}")
    assert os.path.exists(config_file_path)
    with open(config_file_path, 'r') as input_config:
        yaml_dict = yaml.safe_load(input_config)
    assert 'sweep-parameters' in yaml_dict, "All test parameters must be defined under sweep-perameters field"

    idx = 0
    num_tests = len(yaml_dict['sweep-parameters'])
    for name, test_config_and_perf in yaml_dict['sweep-parameters'].items():
        print_progress_bar(idx, num_tests)
        run_single_config(state, name, test_config_and_perf)
        idx += 1
    print_progress_bar(num_tests, num_tests)

def generate_perf_input_config_and_modify_netlist(op_perf_metric, op_name, netlist_path, test_base_name, test_group):
    netlist_name = netlist_path.split("/").pop()
    all_perf_configs = {}
    perf_config = {}
    netlist_perf_configs = {}
    perf_config["program-name"] = "program0"
    perf_config["graph-name"] = "test_op"
    perf_config.update(op_perf_metric["performance-metrics"])
    perf_config["target-ops"] = [op_name]
    perf_config["target-cores"] = []
    perf_config["target-inputs"] = [0]
    perf_config["test-group"] = test_group
    # ci/run.py
    perf_config["test-name"] = f"{test_base_name}.{netlist_name}"
    
    all_perf_configs["config-0"] = perf_config
    netlist_perf_configs["performance-check"] = all_perf_configs
    logger.info(f"Appending the perf config to netlist file under {netlist_path}")
    with open(netlist_path, 'a') as yaml_file:
        yaml.dump(netlist_perf_configs, yaml_file)

if __name__ == "__main__":

    try:
        idx = sys.argv.index("--")
        script_args = sys.argv[1:idx]
        cmd_args = sys.argv[(idx + 1) :]
    except ValueError:
        script_args = sys.argv[1:]
        cmd_args = []

    logger.info("Started running performance test generator")
    logger.debug(f"script command arguments: {script_args}")
    logger.debug(f"test command arguments: {cmd_args}")

    # parse arguments
    parser = argparse.ArgumentParser(description="")
    parser.add_argument("--timeout", type=int, default=None, help="timeout in seconds to wait for command to finish")
    parser.add_argument("--netlists-output-dir", type=str, default="perf_lib/op_tests/netlists", help="The output directory for netlists.")
    
    # Z3-Solver configs
    parser.add_argument("--random-seed", type=int, default=0, help="Random seed for solver")
    parser.add_argument("--max-num-configs", type=int, default=None, help="Maximum number of generated test configs")

    parser.add_argument('--op-name', required=True, type=str, help='Type of op for perf check. Currently only matmul is supported. To add new ops modify get_op_type_from_arg')
    parser.add_argument('--nightly', action="store_true", default=False, help='Run the nightly suite')
    parser.add_argument('--run-kernel-suite', action="store_true", default=False, help='Run suite for kernel performance tests')
    parser.add_argument('--run-prologue', action="store_true", default=False, help='Run tests in prologue mode')
    parser.add_argument("--run-single-op", action="store_true", default=False, help="Run single test with a single op without feeders or drainers")
    parser.add_argument("--dram-assignment-mode", type=str, default="normal", help="Dram assignment mode")
    parser.add_argument('--run-grid', action="store_true", default=False, help='Run tests in prologue mode')
    parser.add_argument("--reblocking-mode", type=str, default="Normal", help="Reblocking mode when running the sweep for op")

    state = parser.parse_args(script_args)

    if not os.path.exists(state.netlists_output_dir):
        os.makedirs(state.netlists_output_dir)
    state.cmd = sys.argv
    state.arch_name = os.getenv("ARCH_NAME", "grayskull")
    op_type_str = state.op_name
    state.perf_test_type = get_op_type_from_arg(op_type_str, PerfTestType)
    state.op_type = get_op_type_from_arg(op_type_str, OpType)
    state.target_label = get_target_perf_label_from_type(op_type_str)
    state.perf_metric = get_perf_metric_from_op_type(op_type_str)
    state.perf_test_mode = get_perf_test_mode(state)
    reblocking_str = state.reblocking_mode
    state.reblocking_mode = get_reblocking_type_from_arg(reblocking_str, ReblockTM)
    state.reblocking_mode_perf_lib = get_reblocking_type_from_arg(reblocking_str, ReblockTMPerfLib)
    
    run_sweep_and_check_perf(state)
    