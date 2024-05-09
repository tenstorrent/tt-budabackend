# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
from dataclasses import dataclass
import os
import shutil
import sys
from typing import List, Optional

from logger_utils import COLORS, logger, print_progress_bar
from perf_test_base import TEMPLATE_NETLIST_DIR, REPO_ROOT
from perf_sweep import (
    run_single_test,
    check_test_pass,
)

sys.path.insert(1, REPO_ROOT)
sys.path.insert(2, TEMPLATE_NETLIST_DIR)
sys.path.insert(3, TEMPLATE_NETLIST_DIR + "/test_modules/")

from generate_data_movement_perf_tests import (
    generate_data_movement_perf_tests,
    DataMovementPerfTestType,
)


PERF_TEST_BIN_PATH = "./build/test/loader/tests/test_netlist_program"


@dataclass
class DataMovementPerfResults:
    pipe_perf_report_path: str
    netlist_path: str
    pipegen_yaml_path: str
    blob_yaml_path: str


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
    return [
        PERF_TEST_BIN_PATH,
        "--netlist",
        netlist_path,
        "--silicon",
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
    my_env = dict(os.environ)
    my_env["TT_BACKEND_PERF_ANALYZER"] = "1"

    my_env["TT_BACKEND_NETLIST_ANALYZER"] = "1"

    my_env["TT_BACKEND_ENABLE_DRAINER_OP"] = "1"

    my_env["NET2PIPE_FORCE_POST_TM"] = "1"

    run_single_test(command, log_file_path=log_path, timeout=500, myenv=my_env)
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

    return get_perf_test_on_single_netlist(netlist_path, tt_builds_dir)


def get_perf_test_on_single_netlist(
    netlist_path: str, tt_builds_dir: str
) -> Optional[DataMovementPerfResults]:
    """
    Gathers perf test result on a single netlist without running test.

    Parameters
    ----------
    netlist_path:
        Path to netlist.
    tt_buils_dir:
        Directory where are stored test output files.
    """
    netlist_dir = os.path.dirname(netlist_path)
    test_name = os.path.basename(netlist_dir)
    test_build_dir = os.path.join(tt_builds_dir, test_name)

    if not os.path.exists(test_build_dir):
        return None

    pipe_perf_report_path = os.path.join(
        test_build_dir, "perf_results", "analyzer_results", "test_op", "pipe_perf_report.txt"
    )

    if not os.path.exists(pipe_perf_report_path):
        shutil.rmtree(test_build_dir)
        print(f"Did not find perf results for {netlist_path} in dir {test_build_dir}, removing it.")
        return None

    pipegen_yaml_path = os.path.join(test_build_dir, "temporal_epoch_0", "overlay", "pipegen.yaml")
    blob_yaml_path = os.path.join(test_build_dir, "temporal_epoch_0", "overlay", "blob.yaml")

    return DataMovementPerfResults(
        pipe_perf_report_path=pipe_perf_report_path,
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
        with open(pipe_perf.pipe_perf_report_path, "r") as pipe_report_file:
            for line_index, line_str in enumerate(pipe_report_file):
                print_func = print_yellow if line_index <= 1 else print_blue
                print_func(line_str)
        print("=" * 120)


def run_perf_tests_on_netlists(
    test_dir: str, clean_run: bool, skip_run: bool, start_idx: int
) -> None:
    """
    Runs the perf tests on all the netlists in a given directory.

    Parameters
    ----------
    test_dir:
        Directory in which to scan for netlists to run perf tests on.
    skip_run:
        Whether to skip running the perf tests if results are not there.
    clean_run:
        Whether to clean the results directory before running the perf tests.
    start_idx:
        From where to start when running perf tests, can be used to parallelize perf tests on multiple machines.
    """
    tt_builds_dir = os.path.join(test_dir, "tt_build_dir")

    if clean_run:
        tt_builds_dir = create_clean_directory(test_dir, "tt_build_dir")

    netlist_paths = read_netlist_paths_from_test_dir(test_dir)
    total_num_netlists = len(netlist_paths) - start_idx

    perf_results = []

    for idx, netlist_path in enumerate(sorted(netlist_paths, key=lambda x: os.path.basename(x))):
        if idx < start_idx:
            # skipping first start_idx netlists
            continue
        print_progress_bar(idx - start_idx, total_num_netlists)
        try:
            pipe_perf = get_perf_test_on_single_netlist(
                netlist_path=netlist_path, tt_builds_dir=tt_builds_dir
            )
            found_pipe_perf_result = pipe_perf is not None
            if not found_pipe_perf_result:
                logger.info(f"Did not find perf results for {netlist_path} in dir {tt_builds_dir}")
                if not skip_run:
                    logger.info(f"Running perf test on {netlist_path}")
                    pipe_perf = run_perf_test_on_single_netlist(
                        netlist_path=netlist_path, tt_builds_dir=tt_builds_dir
                    )
                    found_pipe_perf_result = pipe_perf is not None
                else:
                    logger.info(f"Skipping run for {netlist_path}")
            else:
                logger.info(f"Found existing perf results for {netlist_path} in dir {tt_builds_dir}, skipping run.")

        except Exception as e:
            print(f"Error while running perf test on netlist `{netlist_path}`: {e}")

        if pipe_perf:
            perf_results.append(pipe_perf)

    print_perf_results(perf_results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--netlist-dir", required=True, help="Directory containing netlists relative to the root"
    )
    parser.add_argument(
        "--pipe-type",
        required=False,
        default=None,
        help="Pipe type to generate netlists for",
    )
    parser.add_argument(
        "--clean-run",
        required=False,
        default=False,
        action="store_true",
        help="Clean the results directory before running the perf tests",
    )
    parser.add_argument(
        "--skip-run",
        required=False,
        default=False,
        action="store_true",
        help="Skip running the perf tests if results are not there",
    )
    parser.add_argument(
        "--netlist-start-idx",
        required=False,
        default=0,
        type=int,
        help="From where to start when running perf tests, can be used to parallelize perf tests on multiple machines",
    )
    parser.add_argument(
        "--harvested-rows",
        required=False,
        default=0,
        type=int,
        help="Number of harvested rows on the grid for which to generate tests",
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
            harvested_rows=args.harvested_rows,
        )

    run_perf_tests_on_netlists(
        args.netlist_dir,
        clean_run=args.clean_run,
        skip_run=args.skip_run,
        start_idx=args.netlist_start_idx,
    )
