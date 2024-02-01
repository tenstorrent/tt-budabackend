#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Testing between original and v2 versions of pipegen.
"""
from __future__ import annotations

from datetime import datetime
from dataclasses import dataclass
from itertools import repeat
from typing import Optional
from ruamel.yaml import YAML

import argparse
import multiprocessing as mp
import os
import shutil
import time

from blob_comparator import BlobComparator, StreamGraphComparisonStrategy
from pipegen_runner import (
    DeviceArchs,
    run_net2pipe,
    run_pipegen1,
    run_pipegen2,
    DEFAULT_TOP_LEVEL_BUILD_DIR,
    DEFAULT_BIN_DIR,
)
from pipegen_yaml_filter import *
from pipegen_tests_utils import *

logger = get_logger(__name__)

MAX_NUM_THREADS = 8
NET2PIPE_OUT_DIR_NAME = "net2pipe_out"
FILTERED_YAMLS_OUT_DIR_NAME = "filtered_yamls"
PIPEGEN_LOG_FILE_NAME = "pipegen.log"
DEFAULT_SG_COMPARISON_STRATEGY = StreamGraphComparisonStrategy.edges.name


@dataclass
class PipegenYamlFilterWorkerConfig:
    netlist_dir: str
    filtered_netlist_dir: str
    filter_type: FilterType
    num_samples: int
    total_netlist_count: int
    arch_net2pipe_out_dir: str
    arch: DeviceArchs


@dataclass
class PipegenRunResult:
    pipegen1_command: str
    pipegen1_retcode: int
    pipegen2_command: str
    pipegen2_retcode: int


filtered_netlists_count_sync: Optional[dict[DeviceArchs, mp.Value]] = None
filtered_pipegen_yamls_count_sync: Optional[mp.Value] = None
pipegen_run_count: Optional[mp.Value] = None
net2pipe_run_count: Optional[mp.Value] = None


def init_run_pipegens_sync_var(pipegen_count: mp.Value):
    global pipegen_run_count
    pipegen_run_count = pipegen_count


def init_run_net2pipes_sync_var(net2pipe_count: mp.Value):
    global net2pipe_run_count
    net2pipe_run_count = net2pipe_count


def init_global_sync_vars(
    netlist_count: dict[DeviceArchs, mp.Value], pipegen_yamls_value: mp.Value
):
    global filtered_netlists_count_sync, filtered_pipegen_yamls_count_sync
    filtered_netlists_count_sync = netlist_count
    filtered_pipegen_yamls_count_sync = pipegen_yamls_value


def write_cmd_logs(log_path: str, commands: list[str], retcodes: list[int]):
    with open(log_path, "a+") as log_file:
        for idx, command in enumerate(commands):
            log_file.write(f"{command}\n")
            retcode = retcodes[idx]
            if retcode:
                log_file.write(f"Command failed with error code: {retcode}\n")


# TODO remove the old func when filtering switches to logger too.
def write_cmd_logs_v2(pipegen_run_results: list[PipegenRunResult]):
    for pipegen_run_result in pipegen_run_results:
        if pipegen_run_result.pipegen1_retcode and pipegen_run_result.pipegen2_retcode:
            # If both pipegens are failing on some yaml we consider such yaml invalid for the
            # purpose of these tests, which is to compare pipegen2 to pipegen1 output,
            # not to test if pipegen1 is working correctly.
            continue
        elif pipegen_run_result.pipegen1_retcode:
            logger.error(
                f"Command {pipegen_run_result.pipegen1_command} failed with"
                f" error code: {pipegen_run_result.pipegen1_retcode}"
            )
        if pipegen_run_result.pipegen2_retcode:
            logger.error(
                f"Command {pipegen_run_result.pipegen2_command} failed with error"
                f" code: {pipegen_run_result.pipegen2_retcode}"
            )


def _run_net2pipe_worker(
    netlist_path: str, net2pipe_out_dir: str, builds_dir: str, total_netlist_count: int
) -> Tuple[int, str]:
    netlist_path = netlist_path
    archs = get_netlist_arch(netlist_path)

    for arch in archs:
        if not DeviceArchs.is_valid_arch(arch):
            continue

        # Expecting to find subfolders for different architectures inside builds_dir.
        bin_dir = f"{builds_dir}/{arch}/bin"
        assert os.path.exists(bin_dir)

        netlist_out_dir = f"{net2pipe_out_dir}/{arch}/{get_netlist_name(netlist_path)}"

        retcode, net2pipe_cmd = run_net2pipe(netlist_path, netlist_out_dir, arch, bin_dir)

    with net2pipe_run_count.get_lock():
        net2pipe_run_count.value += 1
        local_net2pipe_run_count = net2pipe_run_count.value

    if local_net2pipe_run_count > 0 and local_net2pipe_run_count % 100 == 0:
        print(
            f"Finished running net2pipe on {local_net2pipe_run_count} / {total_netlist_count} netlists"
        )

    if retcode != 0:
        os.system(f"rm -rf {netlist_out_dir}")

    return retcode, net2pipe_cmd


def generate_net2pipe_outputs(netlists_dir: str, net2pipe_out_dir: str, builds_dir: str):
    netlist_paths = [f"{netlists_dir}/{netlist_name}" for netlist_name in os.listdir(netlists_dir)]
    total_netlist_count = len(netlist_paths)
    net2pipe_run_sync_var = mp.Value("i", 0)

    with mp.Pool(
        initializer=init_run_net2pipes_sync_var, initargs=(net2pipe_run_sync_var,)
    ) as pool:
        net2pipe_run_results = pool.starmap(
            _run_net2pipe_worker,
            zip(
                netlist_paths,
                repeat(net2pipe_out_dir),
                repeat(builds_dir),
                repeat(total_netlist_count),
            ),
        )

    netlist_retcodes = [net2pipe_run_res[0] for net2pipe_run_res in net2pipe_run_results]
    netlist_commands = [net2pipe_run_res[1] for net2pipe_run_res in net2pipe_run_results]

    if any(retcode > 0 for retcode in netlist_retcodes):
        print_warning("Some of the netlists failed on net2pipe, see log!")

    write_cmd_logs(f"{net2pipe_out_dir}/net2pipe.log", netlist_commands, netlist_retcodes)


def filter_netlist_yamls(
    netlist_dir: str, filtered_netlist_dir: str, filter_type: FilterType, num_samples: int
) -> int:
    with filtered_pipegen_yamls_count_sync.get_lock():
        if num_samples > 0 and filtered_pipegen_yamls_count_sync.value >= num_samples:
            return 0

    os.mkdir(filtered_netlist_dir)

    epoch_id = 0
    epoch_dir = get_epoch_dir(netlist_dir, epoch_id)
    num_processed = 0
    while os.path.isdir(epoch_dir):
        filtered = True
        pipegen_yaml_path = os.path.join(epoch_dir, "pipegen.yaml")

        # Check if pipegen.yaml exists, if not, skip this epoch.
        if os.path.isfile(pipegen_yaml_path):
            if filter_type == FilterType.Nothing:
                os.system(f"cp {pipegen_yaml_path} {filtered_netlist_dir}/pipegen_{epoch_id}.yaml")
            else:
                filtered = filter_pipegen_yaml(
                    f"{pipegen_yaml_path}",
                    f"{filtered_netlist_dir}/pipegen_{epoch_id}.yaml",
                    filter_type,
                )
        else:
            filtered = False

        epoch_id = epoch_id + 1
        epoch_dir = get_epoch_dir(netlist_dir, epoch_id)
        if filtered:
            num_processed += 1
            with filtered_pipegen_yamls_count_sync.get_lock():
                filtered_pipegen_yamls_count_sync.value += 1
                if num_samples > 0 and filtered_pipegen_yamls_count_sync.value >= num_samples:
                    break

    if not os.listdir(filtered_netlist_dir):
        os.system(f"rm -rf {filtered_netlist_dir}")

    return num_processed


def find_pipegen_yamls(
    pipegen_yamls_dir: str,
    results_dir: str,
    num_tests: int,
    pipegen_yaml_paths: list[str],
    original_blob_yaml_paths: list[str],
    new_blob_yaml_paths: list[str],
    epoch_ids: list[str],
):
    for netlist_name in os.listdir(pipegen_yamls_dir):
        netlist_dir = f"{pipegen_yamls_dir}/{netlist_name}"
        out_dir = f"{results_dir}/{netlist_name}"
        os.makedirs(out_dir)
        for pipegen_yaml_name in os.listdir(netlist_dir):
            pipegen_yaml_paths.append(f"{netlist_dir}/{pipegen_yaml_name}")
            epoch_id = pipegen_yaml_name[:-5].split("_")[1]
            epoch_ids.append(epoch_id)
            original_blob_yaml_paths.append(f"{out_dir}/blob_{epoch_id}_original.yaml")
            new_blob_yaml_paths.append(f"{out_dir}/blob_{epoch_id}.yaml")
            if num_tests > 0 and len(pipegen_yaml_paths) == num_tests:
                return


def _run_pipegens_worker(
    pipegen_yaml_path: str,
    original_blob_yaml_path: str,
    new_blob_yaml_path: str,
    epoch_id: str,
    arch: str,
    bins_dir: str,
    total_pipegen_yaml_count: int,
) -> PipegenRunResult:
    retcode1, pipegen1_cmd = run_pipegen1(
        pipegen_yaml_path, original_blob_yaml_path, arch, epoch_id, bin_dir=bins_dir
    )
    retcode2, pipegen2_cmd = run_pipegen2(
        pipegen_yaml_path, new_blob_yaml_path, arch, epoch_id, bin_dir=bins_dir
    )

    with pipegen_run_count.get_lock():
        pipegen_run_count.value += 1
        local_pipegen_run_count = pipegen_run_count.value

    if local_pipegen_run_count > 0 and local_pipegen_run_count % 100 == 0:
        print(
            f"Finished processing {local_pipegen_run_count} / {total_pipegen_yaml_count} pipegen "
            f"yamls"
        )

    return PipegenRunResult(
        pipegen1_command=pipegen1_cmd,
        pipegen1_retcode=retcode1,
        pipegen2_command=pipegen2_cmd,
        pipegen2_retcode=retcode2,
    )


def run_pipegens(
    pipegen_yaml_paths: list[str],
    original_blob_yaml_paths: list[str],
    new_blob_yaml_paths: list[str],
    epoch_ids: list[str],
    arch: str,
    bins_dir: str,
):
    total_pipegen_count = len(pipegen_yaml_paths)
    pipegen_run_sync_var = mp.Value("i", 0)
    with mp.Pool(initializer=init_run_pipegens_sync_var, initargs=(pipegen_run_sync_var,)) as pool:
        pipegen_run_results = pool.starmap(
            _run_pipegens_worker,
            zip(
                pipegen_yaml_paths,
                original_blob_yaml_paths,
                new_blob_yaml_paths,
                epoch_ids,
                repeat(arch),
                repeat(bins_dir),
                repeat(total_pipegen_count),
            ),
        )

    write_cmd_logs_v2(pipegen_run_results)


def run_filter_netlist_yamls_worker(worker_config: PipegenYamlFilterWorkerConfig) -> int:
    num_processed = filter_netlist_yamls(
        netlist_dir=worker_config.netlist_dir,
        filtered_netlist_dir=worker_config.filtered_netlist_dir,
        filter_type=worker_config.filter_type,
        num_samples=worker_config.num_samples,
    )

    with filtered_netlists_count_sync[worker_config.arch].get_lock():
        filtered_netlists_count_sync[worker_config.arch].value += 1
        num_filtered_netlists = filtered_netlists_count_sync[worker_config.arch].value

    if num_filtered_netlists > 0 and num_filtered_netlists % 100 == 0:
        print(
            f"Filtered {num_filtered_netlists}/{worker_config.total_netlist_count} netlists in "
            f"{worker_config.arch_net2pipe_out_dir}"
        )

    return num_processed


def filter_yamls(
    netlists_dir: str,
    filter_type: FilterType,
    out_dir: str,
    filtered_yamls_dir_name: str,
    builds_dir: str,
    num_samples: int,
):
    net2pipe_out_dir = os.path.join(out_dir, NET2PIPE_OUT_DIR_NAME)
    if not os.path.exists(net2pipe_out_dir):
        # Optimization to reuse generated net2pipe outputs.
        # Delete the folder manually if you want net2pipe outputs regenerated.

        # Create subdir for each supported arch.
        for arch in DeviceArchs.get_all_archs():
            os.makedirs(f"{net2pipe_out_dir}/{arch}")

        generate_net2pipe_outputs(netlists_dir, net2pipe_out_dir, builds_dir)
        print(f"Net2pipe outputs generated.\n")

    filtered_yamls_dir_per_arch = {}
    for arch in DeviceArchs.get_all_archs():
        filtered_yamls_dir = f"{out_dir}/{filtered_yamls_dir_name}/{arch}"
        filtered_yamls_dir_per_arch[arch] = filtered_yamls_dir
        create_or_clean_dir(filtered_yamls_dir)

    worker_configs = []

    for arch in DeviceArchs.get_all_archs():
        net2pipe_arch_out_dir = os.path.join(net2pipe_out_dir, arch)
        netlist_names = os.listdir(net2pipe_arch_out_dir)

        for netlist_name in netlist_names:
            netlist_dir = os.path.join(net2pipe_arch_out_dir, netlist_name)
            if not os.path.isdir(netlist_dir):
                continue
            filtered_netlist_dir = f"{filtered_yamls_dir_per_arch[arch]}/{netlist_name}"
            worker_configs.append(
                PipegenYamlFilterWorkerConfig(
                    netlist_dir=netlist_dir,
                    filtered_netlist_dir=filtered_netlist_dir,
                    filter_type=filter_type,
                    num_samples=num_samples,
                    total_netlist_count=len(netlist_names),
                    arch_net2pipe_out_dir=net2pipe_arch_out_dir,
                    arch=arch,
                )
            )

    processed_netlist_sync = {arch: mp.Value("i", 0) for arch in DeviceArchs.get_all_archs()}
    filtered_pipegen_yamls_sync = mp.Value("i", 0)
    with mp.Pool(
        initializer=init_global_sync_vars,
        initargs=(processed_netlist_sync, filtered_pipegen_yamls_sync),
    ) as pool:
        num_processed = pool.map(run_filter_netlist_yamls_worker, worker_configs)

    print()

    num_processed = sum(num_processed)
    if num_processed > 0:
        print(f"Found {num_processed} pipegen yamls")
    else:
        print("No pipegen yamls found")
    print()


def compare_blob_yamls(
    original_blob_yaml_path: str,
    new_blob_yaml_path: str,
    comparison_log_path: str,
    sg_comparison_strategy: StreamGraphComparisonStrategy,
):
    """Returns True if original and new blobs fully match each other."""
    try:
        yaml = YAML(typ="safe")
        # Pipegen1 has a bug and can produce multiple NCRISC configs in dram blob with the same key.
        yaml.allow_duplicate_keys = True
        orig_blob_yaml = yaml.load(open(original_blob_yaml_path))
        yaml.allow_duplicate_keys = False
        new_blob_yaml = yaml.load(open(new_blob_yaml_path))
        return BlobComparator(orig_blob_yaml, new_blob_yaml, comparison_log_path).compare(
            sg_comparison_strategy
        )
    except Exception as e:
        logger.info(
            f"Exception occured during comparison of ({original_blob_yaml_path} vs "
            f"{new_blob_yaml_path}) \n {e}"
        )
        return False


def read_comparison_log_file(log_file_path: str) -> str:
    if not os.path.exists(log_file_path):
        return ""
    with open(log_file_path, "r") as log_file:
        message = log_file.read().strip()
    if message:
        message = f"\n{message}"
    return message


def remove_comparison_log_file(log_file_path: str):
    if not os.path.exists(log_file_path):
        return
    os.remove(log_file_path)


def run_blob_comparator_worker(
    pipegen_yaml_path: str,
    original_blob_yaml_path: str,
    new_blob_yaml_path: str,
    sg_comparison_strategy: StreamGraphComparisonStrategy,
) -> list:
    failed_pipegen_yamls = []
    comparison_log_path = f"{os.path.splitext(new_blob_yaml_path)[0]}_comparison.log"

    original_blob_exists = os.path.exists(original_blob_yaml_path)
    new_blob_exists = os.path.exists(new_blob_yaml_path)

    if not original_blob_exists:
        remove_comparison_log_file(comparison_log_path)
        return failed_pipegen_yamls

    comparison_successfull = False
    if new_blob_exists:
        match = compare_blob_yamls(
            original_blob_yaml_path, new_blob_yaml_path, comparison_log_path, sg_comparison_strategy
        )
        file_message = read_comparison_log_file(comparison_log_path)
        if match:
            logger.info(
                f"Successfully compared blobs ({original_blob_yaml_path} vs {new_blob_yaml_path})"
                f"{file_message}"
            )
            comparison_successfull = True
        else:
            logger.error(
                f"Failed comparing blobs ({original_blob_yaml_path} vs {new_blob_yaml_path})"
                f"{file_message}"
            )

    if not comparison_successfull:
        failed_pipegen_yamls.append(pipegen_yaml_path)
        shutil.copy(pipegen_yaml_path, os.path.dirname(original_blob_yaml_path))

    remove_comparison_log_file(comparison_log_path)

    return failed_pipegen_yamls


def run_comparison(
    pipegen_yamls_dir: str,
    results_dir: str,
    arch: str,
    bins_dir: str,
    num_samples: int,
    sg_comparison_strategy: StreamGraphComparisonStrategy,
) -> bool:
    pipegen_yamls_dir = os.path.join(pipegen_yamls_dir, arch)
    if not os.path.exists(pipegen_yamls_dir):
        logger.error(f"Failed to find expected arch subdir {pipegen_yamls_dir}")
        return False
    pipegen_yaml_paths = []
    original_blob_yaml_paths = []
    new_blob_yaml_paths = []
    epoch_ids = []
    find_pipegen_yamls(
        pipegen_yamls_dir,
        results_dir,
        num_samples,
        pipegen_yaml_paths,
        original_blob_yaml_paths,
        new_blob_yaml_paths,
        epoch_ids,
    )

    run_pipegens(
        pipegen_yaml_paths, original_blob_yaml_paths, new_blob_yaml_paths, epoch_ids, arch, bins_dir
    )

    logger.info("Running pipegens done")

    with mp.Pool() as pool:
        failed_pipegen_yamls = pool.starmap(
            run_blob_comparator_worker,
            zip(
                pipegen_yaml_paths,
                original_blob_yaml_paths,
                new_blob_yaml_paths,
                repeat(sg_comparison_strategy),
            ),
        )
    failed_pipegen_yamls = sum(failed_pipegen_yamls, [])

    logger.info(f"Finished running on {len(pipegen_yaml_paths)} test cases")
    logger.info("Summary:")
    if failed_pipegen_yamls:
        logger.error(f"Test failed on {len(failed_pipegen_yamls)} pipegen yamls:")
        for pipegen_yaml in failed_pipegen_yamls:
            logger.error(f"{pipegen_yaml}")
        return False
    else:
        logger.info(f"Test passed")
        return True


def compare_pipegens_on_yamls(
    yamls: str,
    out: str,
    arch: str,
    bins_dir: str,
    num_samples: int,
    sg_comparison_strategy: StreamGraphComparisonStrategy,
) -> bool:
    timestamp = datetime.now().strftime("%m_%d_%Y_%H_%M_%S")
    results_dir = f"{out}/{timestamp}"
    os.makedirs(results_dir)

    # Set up logger.
    log_file_path = f"{results_dir}/{PIPEGEN_LOG_FILE_NAME}"
    setup_logger(log_file_path=log_file_path)

    logger.info(
        f"Blob comparator is using: '{sg_comparison_strategy.name}' as the comparison strategy"
    )
    result = run_comparison(yamls, results_dir, arch, bins_dir, num_samples, sg_comparison_strategy)
    logger.info(f"Log file path: {log_file_path}")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--command", type=str, required=True, default=None, help="Commands [filter | test]"
    )
    parser.add_argument(
        "--out", type=str, required=True, default=None, help="Folder where output data are stored."
    )
    parser.add_argument(
        "--netlists",
        type=str,
        required=False,
        default=None,
        help="Folder where netlists are stored",
    )
    parser.add_argument(
        "--type", type=str, required=False, default=FilterType.Nothing.name, help="Filter type"
    )
    parser.add_argument(
        "--yamls",
        type=str,
        required=False,
        default=None,
        help="Folder where input pipegen yamls are stored",
    )
    parser.add_argument(
        "--arch",
        type=str,
        required=False,
        default="grayskull",
        help="Device architecture [wormhole_b0 | grayskull]. DEFAULT: grayskull",
    )
    parser.add_argument(
        "--builds-dir",
        type=str,
        required=False,
        default=DEFAULT_TOP_LEVEL_BUILD_DIR,
        help=f"Top level 'build' folder which contains subfolders for different architectures "
             f"(for example'grayskull/bin' or 'wormhole_b0/bin'). DEFAULT: {DEFAULT_TOP_LEVEL_BUILD_DIR}",
    )
    parser.add_argument(
        "--pipegens-bin-dir",
        type=str,
        required=False,
        default=DEFAULT_BIN_DIR,
        help=f"Folder containing pipegen binaries (for example 'build/bin' or 'build/grayskull/bin'). "
             f"DEFAULT: {DEFAULT_BIN_DIR}",
    )
    parser.add_argument(
        "--num-samples",
        type=int,
        required=False,
        default=-1,
        help="Number of samples to filter or test. DEFAULT: all existing samples will be used.",
    )
    parser.add_argument(
        "--filtered-yamls-dir-name",
        type=str,
        required=False,
        default=FILTERED_YAMLS_OUT_DIR_NAME,
        help=f"Name of the dir with filtered yamls. DEFAULT: {FILTERED_YAMLS_OUT_DIR_NAME}",
    )
    parser.add_argument(
        "--sg-comparison-strategy",
        type=str,
        required=False,
        default=DEFAULT_SG_COMPARISON_STRATEGY,
        help=f"How to compare stream graphs. DEFAULT: {DEFAULT_SG_COMPARISON_STRATEGY}",
    )
    args = parser.parse_args()

    assert DeviceArchs.is_valid_arch(args.arch)
    assert args.num_samples > 0 or args.num_samples == -1

    start_time = time.ctime()

    # TODO switch prints to logger for filtering mode too.
    if args.command == "filter":
        filter_yamls(
            args.netlists,
            FilterType[args.type],
            args.out,
            args.filtered_yamls_dir_name,
            args.builds_dir,
            args.num_samples,
        )
    elif args.command == "test":
        compare_pipegens_on_yamls(
            args.yamls,
            args.out,
            args.arch,
            args.pipegens_bin_dir,
            args.num_samples,
            StreamGraphComparisonStrategy[args.sg_comparison_strategy],
        )

    print(f"Started  at {start_time}")
    print(f"Finished at {time.ctime()}")
