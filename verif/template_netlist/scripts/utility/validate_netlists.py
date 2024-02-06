#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
For netlists generated from test module checks if they satisfy current state of solver constraints.
NOTE: should be run from `budabackend/verif/template_netlist`
Example:
python scripts/utility/validate_netlists.py
    --netlists-path ../../pregenerated/group_1/softmax/wormhole_b0/
    --module-name test_modules.graph_tests.test_softmax
"""
from __future__ import annotations

from dataclasses import dataclass
import argparse
import importlib
import multiprocessing as mp
import os
import yaml
from itertools import repeat
from typing import Callable, Dict, Tuple

from z3 import Int, Solver, sat, set_option, unsat

from extract_model_vars_from_netlist import extract_model_vars_from_netlist
from test_modules.common.custom_logger import make_custom_logger
from test_modules.common.data_formats import DataFormat
from test_modules.common.solution_search import TestModuleWrapper

# Max number of processes to be used.
MAX_PROCESS_COUNT = int(os.getenv("PARALLEL_WORKERS_COUNT", os.cpu_count()))
mp_lock: mp.Lock = None

RED = "\033[0;31m"
GRN = "\033[0;32m"
YLW = "\033[0;33m"
INFO = "\033[1;33m"
ENDL = "\033[0m"  # No color
PASS = f"{GRN}<PASSED>{ENDL}"
FAIL = f"{RED}<FAILED>{ENDL}"

non_netlist_names = ["netlist_queues", "blob", "pipegen", "queue_to_consumer", "test_configs"]


@dataclass
class Workload:
    netlist: str
    arch: str
    module_name: str


def __init_mp_pool_workers(lock: mp.Lock):
    global mp_lock
    mp_lock = lock


def __get_netlist_archs(netlist_path: str) -> list[str]:
    """Parses architecture names from given netlist."""
    try:
        with open(netlist_path, "r") as input_yaml_file:
            yaml_dict = yaml.safe_load(input_yaml_file.read())
            devices_key = "devices"
            arch_key = "arch"
            arch = yaml_dict[devices_key][arch_key]

            if not type(arch) is list:
                arch = [arch]

            arch = [x.lower() for x in arch]
            return arch
    except:
        raise RuntimeError(f"Cannot properly parse arch names for {netlist_path}!")


def __find_netlists(root_dir: str, existing_names: set[str]) -> list[str]:
    """Finds netlists recursively starting from the given root folder."""

    def get_netlist_name(netlist_path: str) -> str:
        return os.path.basename(netlist_path)[:-5]

    netlist_paths = []

    for entry_path in os.listdir(root_dir):
        full_entry_path = os.path.join(root_dir, entry_path)
        if os.path.isdir(full_entry_path):
            netlist_paths.extend(__find_netlists(full_entry_path, existing_names))
        elif full_entry_path.endswith(".yaml"):
            netlist_name = get_netlist_name(entry_path)
            if netlist_name not in existing_names and netlist_name not in non_netlist_names:
                netlist_paths.append(full_entry_path)
                existing_names.add(netlist_name)

    return netlist_paths


def __preprocess_config(config: dict) -> None:
    """Converts DataFormat strings and bools to ints, removes location and arch vars from config."""
    to_remove = []
    for var_name, var_val in config.items():
        if "df_" in var_name:
            config[var_name] = DataFormat[var_val].value
        elif isinstance(var_val, bool):
            config[var_name] = int(var_val)
        elif "loc_var" in var_name or "_var_" not in var_name:
            to_remove.append(var_name)

    for var_name in to_remove:
        del config[var_name]


def __create_constraint_model(test_module_name: str, arch: str) -> Tuple[TestModuleWrapper, str]:
    """Creates solver with added constraints from test module."""
    test_module = importlib.import_module(test_module_name)

    constraint_model: Callable = getattr(test_module, "constraint_model", None)
    valid_config_callback: Callable = getattr(test_module, "valid_config_callback", None)
    template_yaml: str = getattr(test_module, "template_yaml", None)

    # Initialize solver options.
    set_option("smt.arith.random_initial_value", True)
    set_option("smt.random_seed", 0)

    # Create solver instance and add test module constraints to it.
    solver = Solver()
    solver_vars = {x: Int(x) for x in getattr(test_module, "solver_var_names", [])}
    constraint_model(solver, solver_vars, arch)

    # Create logger.
    logger = make_custom_logger(f"{test_module_name}.{arch}", True, False)

    return (
        TestModuleWrapper(
            solver=solver,
            solver_vars=solver_vars,
            logger=logger,
            valid_config_callback=valid_config_callback,
        ),
        template_yaml,
    )


def __check_config_validity(module_wrapper: TestModuleWrapper, netlist_config: Dict) -> bool:
    """Checks if config satisfies solver constraints."""
    # Remember previous solver context before adding config values.
    module_wrapper.solver.push()

    # Add concrete values from netlist config.
    for var_name, var_val in netlist_config.items():
        module_wrapper.solver.add(module_wrapper.solver_vars[var_name] == var_val)

    # Check if baseline constraints are satisfied.
    result = module_wrapper.solver.check()

    if result == unsat:
        return False

    # Prepack what solver returned into appropriate form.
    model = module_wrapper.solver.model()
    solver_vars_values = {}
    for name, z3_var in module_wrapper.solver_vars.items():
        try:
            solver_vars_values[name] = model[z3_var].as_long()
        except:
            raise RuntimeError(f"Failed to extract variable from model: '{name}', '{z3_var}'.")

    config = dict(solver_vars_values)

    # Check valid_config_callback. Protect with lock to properly write log.
    with mp_lock:
        is_config_valid = module_wrapper.valid_config_callback(config, module_wrapper.logger)

    # Restore previous solver state.
    module_wrapper.solver.pop()

    # If baseline constraints and valid_config_callback are satisfied, netlist is valid.
    return result == sat and is_config_valid


def run_single_test_worker(workload: Workload) -> bool:
    """Create constraint model for this module and arch against which config extracted from netlists will be checked."""
    module_wrapper, template_yaml = __create_constraint_model(workload.module_name, workload.arch)
    netlist_config = extract_model_vars_from_netlist(workload.netlist, template_yaml)
    __preprocess_config(netlist_config)

    passed = __check_config_validity(module_wrapper, netlist_config)

    with mp_lock:
        module_wrapper.logger.info(f"{workload.netlist}: {PASS if passed else FAIL}")

    return passed


def run_tests(netlists_path: str, module_name: str) -> None:
    """For netlists in passed folder check if they satisfy constraints."""
    total_fail = 0
    total_pass = 0

    # Works if test_dir contains subdir or just netlists or is single netlist.
    if os.path.isdir(netlists_path):
        netlists = __find_netlists(netlists_path, set())
    else:
        netlists = [netlists_path]

    workload = []
    for netlist in netlists:
        archs = __get_netlist_archs(netlist)
        for arch in archs:
            workload.append(Workload(netlist, arch, module_name))

    lock = mp.Lock()

    with mp.Pool(
        processes=MAX_PROCESS_COUNT,
        initializer=__init_mp_pool_workers,
        initargs=(lock,),
    ) as pool:
        results = pool.map(run_single_test_worker, workload)

    total_pass = sum(results)
    total_fail = len(netlists) - total_pass

    print("\n\nSummary:")
    print(f"{GRN}\tPassed: {total_pass} / {total_fail + total_pass}{ENDL}")
    print(f"{RED}\tFailed: {total_fail} / {total_fail + total_pass}{ENDL}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--netlists-path", required=True, type=str)
    parser.add_argument("--module-name", required=True, help="Test Module Name")
    args = parser.parse_args()

    run_tests(args.netlists_path, args.module_name)


if __name__ == "__main__":
    main()
