# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Utility functions related to running external processes.
"""
from __future__ import annotations

import json
import multiprocessing as mp
import os
import string
import subprocess
from dataclasses import dataclass
from itertools import repeat
from subprocess import TimeoutExpired
from typing import Dict, List

from verif.common.test_utils import DeviceArchs, get_logger

DEFAULT_BIN_DIR_NAME = "bin"
DEFAULT_TOP_LEVEL_BUILD_DIR = "./build"
DEFAULT_BIN_DIR = os.path.join(DEFAULT_TOP_LEVEL_BUILD_DIR, DEFAULT_BIN_DIR_NAME)
DEFAULT_WORMHOLE_B0_SOC = "./soc_descriptors/wormhole_b0_8x10.yaml"
DEFAULT_HARVESTED_WORMHOLE_B0_SOC = "./soc_descriptors/wormhole_b0_80_harvested.yaml"
DEFAULT_GRAYSKULL_SOC = "./soc_descriptors/grayskull_10x12.yaml"
DEFAULT_SUBPROCESS_TIMEOUT = 180
TIMEOUT_RETCODE = 1001
CUSTOM_ENV_CONFIGS_FILE = "./verif/common/custom_env_per_netlist.json"

logger = get_logger(__name__)

wormhole_b0_cluster_descriptors = [
    "./verif/multichip_tests/wh_multichip/large_cluster/32chip_wh_cluster_desc.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/12chip_wh_cluster_desc.yaml",
    "./wormhole_2chip_cluster_both_mmio.yaml",
    "./wormhole_2chip_cluster_trimmed.yaml",
    "./wormhole_2chip_cluster.yaml",
    "./wormhole_2x4_sequential_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_2chip_bidirectional_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_2x2_cluster_1_link_per_pair.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_3x3_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_4chip_linear_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_4chip_square_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_weird_intersecting_3x3_ring_cluster.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/12chip_wh_cluster_lab68.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/8chip_wh_cluster_jb11.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/4chip_wh_cluster_jb11.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/32chip_wh_cluster_issue_1559.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/32chip_wh_cluster_lab78.yaml",
]
grayskull_cluster_descriptors = [
    # Grayskull doesn't need cluster descriptors, passing empty string as argument.
    ""
]


def run_diff(folder1: str, folder2: str):
    """Runs difference on two given folders.

    Parameters
    ----------
    folder1: str
        First folder path.
    folder2: str
        Second folder path.

    Returns
    -------
    int, str
        Return code and diff command.
    """
    diff_cmd = __get_diff_cmd(folder1, folder2)
    return run_cmd(diff_cmd, ""), diff_cmd


def run_cmd(
    command: str, arch: str, custom_env: Dict = {}, timeout=DEFAULT_SUBPROCESS_TIMEOUT
) -> subprocess.CompletedProcess:
    env = custom_env
    if arch:
        env["ARCH_NAME"] = arch
    cmd = command.split()
    try:
        result = subprocess.run(cmd, capture_output=True, env=env, timeout=timeout)
    except TimeoutExpired:
        logger.error(f"{cmd} timed out")
        return subprocess.CompletedProcess(
            stdout="",
            stderr=f"Timed out after {timeout} seconds",
            returncode=TIMEOUT_RETCODE,
        )
    return result


def get_soc_file(arch: str) -> str:
    if arch == DeviceArchs.GRAYSKULL:
        return DEFAULT_GRAYSKULL_SOC
    elif arch == DeviceArchs.WORMHOLE_B0:
        return DEFAULT_WORMHOLE_B0_SOC
    else:
        assert False, f"Unsupported arch in __get_soc_file(): {arch}"


def get_cluster_descriptors(arch: str) -> list[str]:
    if arch == DeviceArchs.GRAYSKULL:
        return grayskull_cluster_descriptors
    elif arch == DeviceArchs.WORMHOLE_B0:
        return wormhole_b0_cluster_descriptors
    else:
        assert False, f"Unsupported arch in __get_cluster_descriptors(): {arch}"


def get_arch_bin_dir(builds_dir: str, arch: str) -> str:
    return os.path.join(builds_dir, arch, DEFAULT_BIN_DIR_NAME)


def verify_builds_dir(builds_all_archs_dir: str):
    for arch in DeviceArchs.get_all_archs():
        builds_dir = get_arch_bin_dir(builds_all_archs_dir, arch)
        if not os.path.exists(builds_dir):
            raise Exception(f"Builds dir {builds_dir} does not exist")


@dataclass
class WorkerResult:
    command: str
    error_code: int


def write_cmd_logs_to_file(command_error_pair: list[WorkerResult], log_path: str):
    with open(log_path, "a+") as log_file:
        for result in command_error_pair:
            log_file.write(f"{result.command}\n")
            if result.error_code:
                log_file.write(f"Command failed with error code: {result.error_code}\n")


def execute_in_parallel(
    func,
    args,
    initializer_func: function = None,
    initializer_var: mp.Value = None,
    serialize_for_debug: bool = False,
    log_file: str = None,
    heavy_workload: bool = False,
):
    """Execute a function in parallel using 'multiprocessing' library.

    Parameters
    ----------
    func: function
        Function to execute in parallel.
    args: list
        List of arguments to pass to the function.
    initializer_func: Optional[function]
        If functions use a shared variable which they need an initializer for, the initializer should be passed through this argument.
    initializer_var: Optional[mp.Value]
        If functions use a shared variable, it should be passed through this argument.
    serialize_for_debug: bool
        If this is set to True, the function will be executed serially in the main thread, and not in parallel.
        This can be used for easier debugging of worker functions.
    log_file: str
        Log file to write command logs to.
    heavy_workload: bool
        If this is set to True, the chunksize for the parallel execution will be set to 1.
        In general, when jobs are light (like comparing files), it is better not to set this flag.
        In that case, starmap will use its own heuristics to set chunksize to a larger value.
        For longer jobs, enabling this flag can lead to better overall performance.
    """
    total_run_count = len(args)
    run_count_sync_var = mp.Value("i", 0)
    if not serialize_for_debug:
        with mp.Pool(
            initializer=__init_parallel_run_sync_var,
            initargs=(run_count_sync_var, initializer_func, initializer_var),
        ) as pool:
            results = pool.starmap(
                __single_worker_with_logging,
                zip(repeat(func), args, repeat(total_run_count)),
                chunksize=1 if heavy_workload else None,
            )
    else:
        __init_parallel_run_sync_var(
            run_count_sync_var, initializer_func, initializer_var
        )
        results = []
        for arg in args:
            results.append(__single_worker_with_logging(func, arg, total_run_count))

    logger.info(f"Completed all {func.__name__}.")
    if len(results) > 0 and isinstance(results[0], WorkerResult):
        if log_file is not None:
            write_cmd_logs_to_file(results, log_file)
            logger.info(f"Command logs and errors written to {log_file}.")
        if any(result.error_code > 0 for result in results):
            logger.warning(
                f"Failed for {len([result for result in results if result.error_code != 0])}/{len(results)}."
            )
        else:
            logger.info(f"Failed for 0/{len(results)}.")

    return results


def __get_diff_cmd(folder1: str, folder2: str):
    return f"diff -rq {folder1} {folder2} --no-dereference"


def __single_worker_with_logging(worker_func, worker_args, total_run_count):
    """Wrapper function around worker function which adds logging on progress."""
    res = worker_func(worker_args)
    __log_running_progress(
        __inc_parallel_run_sync_var(), total_run_count, worker_func.__name__
    )

    return res


def __init_parallel_run_sync_var(
    worker_run_count: mp.Value,
    initializer_func: function = None,
    initializer_var: mp.Value = None,
):
    global global_worker_run_count
    global_worker_run_count = worker_run_count
    if initializer_func is not None and initializer_var is not None:
        initializer_func(initializer_var)


def __inc_parallel_run_sync_var():
    with global_worker_run_count.get_lock():
        global_worker_run_count.value += 1
        return global_worker_run_count.value


def __log_running_progress(current_iter: int, total_iters: int, item_name: str):
    """Log running progress of a parallel worker function.
    Exponentially increase logging frequency, so that we print more often when we are close to the end.
    The if-else statements below are much faster than using math.log10.
    """
    left_iters = total_iters - current_iter
    if left_iters < 10:
        print_step = 1
    elif left_iters < 100:
        print_step = 10
    elif left_iters < 1000:
        print_step = 100
    elif left_iters < 10000:
        print_step = 1000
    else:
        print_step = 10000

    if current_iter % print_step == 0 or current_iter == 1:
        logger.info(
            f"Finished running {item_name} on {current_iter} / {total_iters} items."
        )


def fetch_custom_env_config(netlist_name: str):
    with open(CUSTOM_ENV_CONFIGS_FILE, "r") as file:
        envs_per_netlists = json.load(file)
        if netlist_name in envs_per_netlists:
            return envs_per_netlists[netlist_name]
        # In case of blackbox test netlists, an MD5 hash is appended to the beginning of the netlist.
        # This might incur some false positives, but is unlikely to happen with a small set of netlists,
        # and it is also unlikely to cause any issues.
        # MD5 hash is a 32-character hex string.
        netlist_name_tokens = netlist_name.split("_")
        possible_md5_hash = netlist_name_tokens[0]
        if (
            len(netlist_name_tokens) > 1
            and len(possible_md5_hash) == 32
            and all(c in string.hexdigits for c in possible_md5_hash)
        ):
            new_netlist_name = "_".join(netlist_name_tokens[1:])
            if new_netlist_name in envs_per_netlists:
                return envs_per_netlists[new_netlist_name]
        return {}
