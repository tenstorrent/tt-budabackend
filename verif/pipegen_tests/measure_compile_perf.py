#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Measuring pipegen compile performance, and comparing compile performance between original and v2
versions of pipegen.
"""
from __future__ import annotations

import argparse
import os
import time
from datetime import datetime

from verif.common.runner_pipegen import (
    PIPEGEN_BIN_NAME,
    PIPEGEN_MASTER_BIN_NAME,
    run_pipegen,
)
from verif.common.runner_utils import DEFAULT_BIN_DIR, DeviceArchs
from verif.common.test_utils import get_epoch_dir, get_logger, setup_logger

logger = get_logger(__name__)

PERF_LOG_FILE_NAME = "pipegen_compile_perf.log"
NUM_RUNS_PER_YAML = 3
PIPEGEN_TIMEOUT = 1800


def find_pipegen_yamls(net2pipe_out_dir: str, arch: str) -> list[str]:
    net2pipe_arch_out_dir = os.path.join(net2pipe_out_dir, arch)
    netlist_names = os.listdir(net2pipe_arch_out_dir)
    pipegen_yamls = []

    for netlist_name in netlist_names:
        netlist_dir = os.path.join(net2pipe_arch_out_dir, netlist_name)
        if not os.path.isdir(netlist_dir):
            continue
        epoch_id = 0
        epoch_dir = get_epoch_dir(netlist_dir, epoch_id)
        while os.path.isdir(epoch_dir):
            pipegen_yaml_path = os.path.join(epoch_dir, "pipegen.yaml")
            if os.path.isfile(pipegen_yaml_path):
                pipegen_yamls.append(pipegen_yaml_path)
            epoch_id = epoch_id + 1
            epoch_dir = get_epoch_dir(netlist_dir, epoch_id)

    return pipegen_yamls


def measure_pipegen_time(
    pipegen_yaml: str, out_dir: str, arch: str, pipegen_bin_name: str
) -> float:
    blob_yaml_path = f"{out_dir}/blob.yaml"

    pipegen_start_time = time.perf_counter()
    retcode, _ = run_pipegen(
        pipegen_yaml,
        blob_yaml_path,
        arch,
        0,
        pipegen_path=os.path.join(DEFAULT_BIN_DIR, pipegen_bin_name),
        timeout=PIPEGEN_TIMEOUT,
    )
    if retcode != 0:
        return -1
    pipegen_end_time = time.perf_counter()
    pipegen_time = pipegen_end_time - pipegen_start_time

    os.remove(blob_yaml_path)

    return pipegen_time


def run_compile_perf_comparison_on_yaml(
    pipegen_yaml: str,
    pipegen2_master_times: list[float],
    pipegen2_times: list[float],
    out_dir: str,
    arch: str,
):
    pipegen2_master_min_time = None
    pipegen2_min_time = None
    for _ in range(NUM_RUNS_PER_YAML):
        pipegen2_master_time = measure_pipegen_time(
            pipegen_yaml, out_dir, arch, PIPEGEN_MASTER_BIN_NAME
        )
        if pipegen2_master_time < 0:
            return
        pipegen2_min_time = (
            pipegen2_master_time
            if pipegen2_min_time is None
            else min(pipegen2_master_time, pipegen2_master_min_time)
        )

        pipegen2_time = measure_pipegen_time(
            pipegen_yaml, out_dir, arch, PIPEGEN_BIN_NAME
        )
        if pipegen2_time < 0:
            return
        pipegen2_min_time = (
            pipegen2_time
            if pipegen2_min_time is None
            else min(pipegen2_time, pipegen2_min_time)
        )

    pipegen2_master_times.append(pipegen2_master_min_time)
    pipegen2_times.append(pipegen2_min_time)

    logger.info(
        f"Pipegen2_master took {pipegen2_master_min_time} seconds on {pipegen_yaml}"
    )
    logger.info(f"Pipegen2 took {pipegen2_min_time} seconds on {pipegen_yaml}")


def run_compile_perf_comparison(pipegen_yamls: list[str], results_dir: str, arch: str):
    pipegen2_master_times = []
    pipegen2_times = []
    out_dir = f"{results_dir}/out"
    os.makedirs(out_dir)

    for pipegen_yaml in pipegen_yamls:
        run_compile_perf_comparison_on_yaml(
            pipegen_yaml, pipegen2_master_times, pipegen2_times, out_dir, arch
        )

    os.system(f"rm -rf {out_dir}")

    logger.info(f"Finished running on {len(pipegen2_master_times)} pipegen yamls")
    logger.info(f"Pipegen2_master took total of {sum(pipegen2_master_times)} seconds")
    logger.info(f"Pipegen2 took total of {sum(pipegen2_times)} seconds")
    logger.info(
        f"Pipegen2_master took {sum(pipegen2_master_times) / len(pipegen2_master_times)} seconds on average"
    )
    logger.info(
        f"Pipegen2 took {sum(pipegen2_times) / len(pipegen2_times)} seconds on average"
    )


def compare_compile_perf(net2pipe_out_dir: str, out_dir: str, arch: str):
    timestamp = datetime.now().strftime("%m_%d_%Y_%H_%M_%S")
    results_dir = f"{out_dir}/{timestamp}"
    os.makedirs(results_dir)

    log_file_path = f"{results_dir}/{PERF_LOG_FILE_NAME}"
    setup_logger(log_file_path=log_file_path)

    pipegen_yamls = find_pipegen_yamls(net2pipe_out_dir, arch)

    run_compile_perf_comparison(pipegen_yamls, results_dir, arch)
    logger.info(f"Log file path: {log_file_path}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--command", type=str, required=True, help="Commands [measure|compare]"
    )
    parser.add_argument(
        "--net2pipe_out_dir",
        type=str,
        required=True,
        help="Folder where net2pipe outputs are stored",
    )
    parser.add_argument(
        "--out_dir",
        type=str,
        required=True,
        help="Folder where output data are stored.",
    )
    parser.add_argument(
        "--arch",
        type=str,
        required=False,
        default="grayskull",
        help="Commands [grayskull|wormhole_b0]",
    )
    args = parser.parse_args()

    assert DeviceArchs.is_valid_arch(args.arch)
    assert os.path.exists(args.net2pipe_out_dir)
    assert os.path.exists(args.out_dir)

    start_time = time.ctime()

    if args.command == "measure":
        # TODO: finish
        pass
    elif args.command == "compare":
        compare_compile_perf(args.net2pipe_out_dir, args.out_dir, args.arch)

    logger.info(f"Started  at {start_time}")
    logger.info(f"Finished at {time.ctime()}")
