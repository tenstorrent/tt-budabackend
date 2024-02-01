# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
from dataclasses import dataclass
from functools import cmp_to_key
import os
import shutil
import sys
from typing import Dict, List, Optional, Tuple
import yaml

from logger_utils import COLORS, logger, print_progress_bar
from perf_test_base import TEMPLATE_NETLIST_DIR
from perf_sweep import (
    run_single_test,
    check_test_pass,
    get_perf_results_from_test,
    PerfResults,
)

sys.path.insert(1, TEMPLATE_NETLIST_DIR)
sys.path.insert(2, TEMPLATE_NETLIST_DIR + "/test_modules/")

from generate_data_movement_perf_tests import (
    generate_data_movement_perf_tests,
    DataMovementPerfTestType,
)


PERF_TEST_BIN_PATH = "./build/test/loader/tests/test_netlist_program"


@dataclass
class DataMovementPerfResults:
    perf_results: Dict[str, List[PerfResults]]
    netlist_path: str
    pipegen_yaml_path: str
    blob_yaml_path: str


def parse_num_feeders_targets_drainers(netlist_path: str) -> Tuple[int, int, int]:
    """
    Returns the number of feeder and drainers op defined in the netlists (in that order).
    Asserts if no target op is found.

    Parameters
    ----------
    netlist_path:
        Path to netlist yaml.
    """
    with open(netlist_path, "r") as file:
        netlist_dict = yaml.safe_load(file.read())

    num_feeders = 0
    num_drainers = 0
    num_target_ops = 0

    for graph_name in netlist_dict["graphs"]:
        for op_name in netlist_dict["graphs"][graph_name]:
            if op_name.startswith("feeder"):
                num_feeders += 1
            elif op_name.startswith("drainer"):
                num_drainers += 1
            elif op_name.startswith("target_op"):
                num_target_ops += 1

    assert num_target_ops, "Failed to find target op in the netlist"

    return num_feeders, num_target_ops, num_drainers


def get_single_perf_test_command(netlist_path: str, output_dir: str) -> List[str]:
    """
    Returns a command to run a perf test on a single netlist.

    Parameters
    ----------
    netlist_path:
        Path to netlist.
    output_dir:
        Output directory where to store compilation output files and perf results.
    """
    num_feeders, num_target_ops, num_drainers = parse_num_feeders_targets_drainers(netlist_path)
    feeder_decouple = ",".join([f"feeder{idx}:MathPack" for idx in range(num_feeders)])
    drainer_decouple = ",".join([f"drainer{idx}:UnpMath" for idx in range(num_drainers)])
    target_op_decouple = ",".join(
        [f"target_op{idx}:UnpMath-MathPack" for idx in range(num_target_ops)]
    )
    decoupling_commands = [x for x in [feeder_decouple, drainer_decouple, target_op_decouple] if x]
    perf_mode = ",".join(decoupling_commands)
    return [
        PERF_TEST_BIN_PATH,
        "--netlist",
        netlist_path,
        "--silicon",
        "--dump-perf-events",
        "--perf-level",
        "0",
        "--perf-op-mode",
        perf_mode,
        "--outdir",
        output_dir,
    ]


def execute_perf_test_command(command: List[str], log_path: str) -> bool:
    """
    Runs a single perf test command and return whether the run as successfull.

    Parameters
    ----------
    command:
        Command to run.
    log_path:
        Path to log file on which to append process stdout.
    """
    run_single_test(command, log_file_path=log_path, timeout=500)
    return check_test_pass(log_file_path=log_path)


def run_perf_test_on_single_netlist(
    netlist_path: str, tt_builds_dir: str
) -> Optional[DataMovementPerfResults]:
    """
    Runs a perf test on a single netlist.

    Parameters
    ----------
    netlist_path:
        Path to netlist.
    tt_buils_dir:
        Directory where to create subdir for this netlist backend compilation output files.
    """
    netlist_dir = os.path.dirname(netlist_path)
    test_name = os.path.basename(netlist_dir)
    test_build_path = create_clean_directory(tt_builds_dir, test_name)
    log_file_path = create_empty_file(netlist_dir, "test_perf_log.log")

    command = get_single_perf_test_command(netlist_path, test_build_path)

    if not execute_perf_test_command(command, log_file_path):
        logger.error(f"Failed running perf tests on netlist `{netlist_path}`.")
        return None

    _, num_target_ops, _ = parse_num_feeders_targets_drainers(netlist_path)
    target_op_perf = {}
    for target_op_idx in range(num_target_ops):
        target_op_name = f"target_op{target_op_idx}"
        _, perf_results = get_perf_results_from_test(
            log_file_path=log_file_path,
            attr=None,
            override_target_op_name=target_op_name,
        )
        target_op_perf[target_op_name] = perf_results
    pipegen_yaml_path = os.path.join(test_build_path, "temporal_epoch_0", "overlay", "pipegen.yaml")
    blob_yaml_path = os.path.join(test_build_path, "temporal_epoch_0", "overlay", "blob.yaml")

    return DataMovementPerfResults(
        perf_results=target_op_perf,
        netlist_path=netlist_path,
        pipegen_yaml_path=pipegen_yaml_path,
        blob_yaml_path=blob_yaml_path,
    )


def is_dir_netlist_dir(test_dir: str, file_name: str) -> bool:
    """
    Returns if file located in the test dir is a file containing netlist or not.

    Parameters
    ----------
    test_dir:
        Parent directory.
    file_name:
        Name of the file inside of parent directory.
    """
    if not file_name.startswith("test_"):
        return False

    subdir_path = os.path.join(test_dir, file_name)
    if not os.path.isdir(subdir_path):
        return False

    return True


def read_netlist_paths_from_test_dir(test_dir: str) -> List[str]:
    """
    Returns paths to all the netlists located in the given directory.

    Parameters
    ----------
    test_dir:
        Directory in which to scan for netlists.
    """
    netlist_paths = []
    for test_netlist in os.listdir(test_dir):
        if not is_dir_netlist_dir(test_dir, test_netlist):
            continue

        netlist_id = test_netlist.split("_")[1]
        netlist_path = os.path.join(test_dir, test_netlist, f"netlist_{netlist_id}.yaml")

        netlist_paths.append(netlist_path)

    return netlist_paths


def create_clean_directory(*args: str) -> str:
    """
    Creates a dir path by joining individual segments and removes the directory if already exists.
    """
    dir_path = os.path.join(*args)
    if os.path.exists(dir_path):
        shutil.rmtree(dir_path)
    return dir_path


def create_empty_file(*args: str) -> str:
    """
    Creates a file path by joining individual segments and removes the files if already exists.
    """
    file_path = os.path.join(*args)
    if os.path.exists(file_path):
        os.remove(file_path)
    return file_path


def print_perf_results(perf_results: List[DataMovementPerfResults]) -> None:
    """
    Prints perf results as formatted table.

    Parameters
    ----------
    perf_results:
        List of all the perf results to print.
    """
    print_color = lambda x, c: print(f"{COLORS[c]}{x}{COLORS['RESET']}")
    print_green = lambda x: print_color(x, "GREEN")
    print_blue = lambda x: print_color(x, "CYAN")
    print_yellow = lambda x: print_color(x, "YELLOW")
    print("=" * 120)

    for pipe_perf in perf_results:
        print_green(f"Netlist: {pipe_perf.netlist_path}")
        print_green(f"Pipegen.yaml: {pipe_perf.pipegen_yaml_path}")
        print_green(f"Blob.yaml: {pipe_perf.blob_yaml_path}")
        for target_op, target_op_perf in pipe_perf.perf_results.items():
            print_blue(f"  {target_op}:")
            print(f"    input0_bw_across_cores_total_runtime: {target_op_perf[0].input0_bw_across_cores_total_runtime}")
            print(f"    input1_bw_across_cores_total_runtime: {target_op_perf[0].input1_bw_across_cores_total_runtime}")
            print(f"    output_bw_across_cores_total_runtime: {target_op_perf[0].output_bw_across_cores_total_runtime}")
            for target_core_perf in target_op_perf:
                print_yellow(f"    Core({target_core_perf.target_core})")
                print(f"      input0_bw: {target_core_perf.input0_bw}")
                print(f"      input1_bw: {target_core_perf.input1_bw}")
                print(f"      output_bw: {target_core_perf.output_bw}")
        print("=" * 120)


def run_perf_tests_on_netlists(test_dir: str) -> None:
    """
    Runs the perf tests on all the netlists in a given directory.

    Parameters
    ----------
    test_dir:
        Directory in which to scan for netlists to run perf tests on.
    """
    tt_builds_dir = create_clean_directory(test_dir, "tt_build_dir")

    netlist_paths = read_netlist_paths_from_test_dir(test_dir)
    total_num_netlists = len(netlist_paths)

    perf_results = []

    for idx, netlist_path in enumerate(netlist_paths, start=1):
        print_progress_bar(idx, total_num_netlists)
        pipe_perf = run_perf_test_on_single_netlist(
            netlist_path=netlist_path, tt_builds_dir=tt_builds_dir
        )
        if pipe_perf:
            perf_results.append(pipe_perf)

    print_perf_results(perf_results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--netlist-dir", required=False, help="Directory containing netlists relative to the root"
    )
    parser.add_argument(
        "--pipe-type",
        required=False,
        default=None,
        help="Pipe type to generate netlists for",
    )
    args = parser.parse_args()

    assert (
        "ARCH_NAME" in os.environ
    ), "ARCH_NAME env variable must be defined in order to run perf tests"

    if args.pipe_type is not None:
        create_clean_directory(args.netlist_dir)
        generate_data_movement_perf_tests(
            pipe_type=DataMovementPerfTestType[args.pipe_type],
            output_dir=args.netlist_dir,
            arch=os.environ["ARCH_NAME"],
        )
    run_perf_tests_on_netlists(args.netlist_dir)
