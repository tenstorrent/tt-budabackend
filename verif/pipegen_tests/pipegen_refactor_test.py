#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Testing between pipegen2 from master and pipegen2 with your changes.

Example commands (from repo root):
    mkdir -p out/netlists
    cp verif/graph_tests/netlists/netlist_softmax_single_tile.yaml out/netlists/
    verif/pipegen_tests/build_all_archs.sh
    cp build_archs/wormhole_b0/bin/pipegen2 build_archs/wormhole_b0/bin/pipegen2_master
    verif/pipegen_tests/create_env.sh
    source verif/pipegen_tests/env/bin/activate
    python3 verif/pipegen_tests/pipegen_refactor_test.py --command filter \
        --out out --netlists out/netlists \
        --arch wormhole_b0 --builds-dir build_archs
    python3 verif/pipegen_tests/pipegen_refactor_test.py --command test \
        --yamls out/filtered_yamls/filtered_yamls_Nothing --out out/pipegen_refactor_test_output \
        --arch wormhole_b0 --builds-dir build_archs --pipegens-bin-dir build_archs/wormhole_b0/bin
"""
from __future__ import annotations

import argparse
import os
import shutil
import time
from dataclasses import dataclass
from datetime import datetime

from blob_comparator import BlobComparator, StreamGraphComparisonStrategy
from ruamel.yaml import YAML

from verif.common.pipegen_yaml_filter import FilterType
from verif.common.runner_net2pipe import generate_net2pipe_outputs
from verif.common.runner_pipegen import (
    PIPEGEN_BIN_NAME,
    PIPEGEN_MASTER_BIN_NAME,
    PipegenWorkerConfig,
    run_pipegen_worker,
)
from verif.common.runner_pipegen_filter import filter_pipegen_yamls
from verif.common.runner_utils import (
    DEFAULT_BIN_DIR,
    DEFAULT_TOP_LEVEL_BUILD_DIR,
    execute_in_parallel,
)
from verif.common.test_utils import (
    DeviceArchs,
    find_all_files_in_dir,
    get_epoch_from_filename,
    get_logger,
    setup_logger,
)

logger = get_logger(__name__)

NET2PIPE_OUT_DIR_NAME = "net2pipe_out"
FILTERED_YAMLS_OUT_DIR_NAME = "filtered_yamls"
DEFAULT_SG_COMPARISON_STRATEGY = StreamGraphComparisonStrategy.edges.name


def find_pipegen_yamls(
    pipegen_yamls_dir: str,
    results_dir: str,
    num_tests: int,
    pipegen_yaml_paths: list[str],
    original_blob_yaml_paths: list[str],
    new_blob_yaml_paths: list[str],
    epoch_ids: list[str],
):
    for pipegen_relpath in find_all_files_in_dir(pipegen_yamls_dir, "*/pipegen*.yaml"):
        pipegen_yaml_paths.append(os.path.join(pipegen_yamls_dir, pipegen_relpath))
        epoch_id = get_epoch_from_filename(pipegen_relpath)
        epoch_ids.append(epoch_id)
        original_blob_yaml_paths.append(f"{results_dir}/blob_{epoch_id}_original.yaml")
        new_blob_yaml_paths.append(f"{results_dir}/blob_{epoch_id}.yaml")
        if num_tests > 0 and len(pipegen_yaml_paths) == num_tests:
            return


def run_pipegens(
    pipegen_yaml_paths: list[str],
    original_blob_yaml_paths: list[str],
    new_blob_yaml_paths: list[str],
    epoch_ids: list[str],
    arch: str,
    bins_dir: str,
    results_dir: str,
):
    worker_configs = [
        PipegenWorkerConfig(
            pipegen_yaml_path=pipegen_yaml_path,
            blob_yaml_path=original_blob_yaml_path,
            arch=arch,
            epoch_id=epoch_id,
            pipegen_path=os.path.join(bins_dir, PIPEGEN_BIN_NAME),
        )
        for pipegen_yaml_path, original_blob_yaml_path, epoch_id in zip(
            pipegen_yaml_paths, original_blob_yaml_paths, epoch_ids
        )
    ] + [
        PipegenWorkerConfig(
            pipegen_yaml_path=pipegen_yaml_path,
            blob_yaml_path=new_blob_yaml_path,
            arch=arch,
            epoch_id=epoch_id,
            pipegen_path=os.path.join(bins_dir, PIPEGEN_MASTER_BIN_NAME),
        )
        for pipegen_yaml_path, new_blob_yaml_path, epoch_id in zip(
            pipegen_yaml_paths, new_blob_yaml_paths, epoch_ids
        )
    ]

    execute_in_parallel(
        run_pipegen_worker,
        worker_configs,
        log_file=os.path.join(results_dir, "pipegen_commands.log"),
    )


def filter_yamls(
    netlists_dir: str,
    filter_type: FilterType,
    out_dir: str,
    filtered_yamls_dir_name: str,
    builds_dir: str,
    num_samples: int,
):
    net2pipe_out_dir = os.path.join(out_dir, NET2PIPE_OUT_DIR_NAME)
    generate_net2pipe_outputs(netlists_dir, net2pipe_out_dir, builds_dir)

    filter_pipegen_yamls(
        net2pipe_out_dir,
        os.path.join(out_dir, filtered_yamls_dir_name),
        filter_type,
        num_samples,
    )


def compare_blob_yamls(
    original_blob_yaml_path: str,
    new_blob_yaml_path: str,
    comparison_log_path: str,
    sg_comparison_strategy: StreamGraphComparisonStrategy,
):
    """Returns True if original and new blobs fully match each other."""
    try:
        yaml = YAML(typ="safe")
        orig_blob_yaml = yaml.load(open(original_blob_yaml_path))
        new_blob_yaml = yaml.load(open(new_blob_yaml_path))
        return BlobComparator(
            orig_blob_yaml, new_blob_yaml, comparison_log_path
        ).compare(sg_comparison_strategy)
    except Exception as e:
        logger.info(
            f"Exception occurred during comparison of ({original_blob_yaml_path} vs "
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


@dataclass
class BlobComparatorWorker:
    pipegen_yaml_path: str
    original_blob_yaml_path: str
    new_blob_yaml_path: str
    sg_comparison_strategy: StreamGraphComparisonStrategy


def run_blob_comparator_worker(worker_config: BlobComparatorWorker) -> list:
    failed_pipegen_yamls = []
    comparison_log_path = (
        f"{os.path.splitext(worker_config.new_blob_yaml_path)[0]}_comparison.log"
    )

    original_blob_exists = os.path.exists(worker_config.original_blob_yaml_path)
    new_blob_exists = os.path.exists(worker_config.new_blob_yaml_path)

    if not original_blob_exists:
        remove_comparison_log_file(comparison_log_path)
        return failed_pipegen_yamls

    comparison_successful = False
    if new_blob_exists:
        match = compare_blob_yamls(
            worker_config.original_blob_yaml_path,
            worker_config.new_blob_yaml_path,
            comparison_log_path,
            worker_config.sg_comparison_strategy,
        )
        file_message = read_comparison_log_file(comparison_log_path)
        if match:
            logger.info(
                f"Successfully compared blobs ({worker_config.original_blob_yaml_path} vs {worker_config.new_blob_yaml_path})"
                f"{file_message}"
            )
            comparison_successful = True
        else:
            logger.error(
                f"Failed comparing blobs ({worker_config.original_blob_yaml_path} vs {worker_config.new_blob_yaml_path})"
                f"{file_message}"
            )

    if not comparison_successful:
        failed_pipegen_yamls.append(worker_config.pipegen_yaml_path)
        shutil.copy(
            worker_config.pipegen_yaml_path,
            os.path.dirname(worker_config.original_blob_yaml_path),
        )

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
        pipegen_yaml_paths,
        original_blob_yaml_paths,
        new_blob_yaml_paths,
        epoch_ids,
        arch,
        bins_dir,
        results_dir,
    )

    blob_comparator_worker_configs = [
        BlobComparatorWorker(
            pipegen_yaml_path,
            original_blob_yaml_path,
            new_blob_yaml_path,
            sg_comparison_strategy,
        )
        for pipegen_yaml_path, original_blob_yaml_path, new_blob_yaml_path in zip(
            pipegen_yaml_paths, original_blob_yaml_paths, new_blob_yaml_paths
        )
    ]
    failed_pipegen_yamls = execute_in_parallel(
        run_blob_comparator_worker, blob_comparator_worker_configs
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

    logger.info(
        f"Blob comparator is using: '{sg_comparison_strategy.name}' as the comparison strategy"
    )
    result = run_comparison(
        yamls, results_dir, arch, bins_dir, num_samples, sg_comparison_strategy
    )
    logger.info(f"Log file path: {log_file_path}")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--command",
        type=str,
        required=True,
        default=None,
        help="Commands [filter | test]",
    )
    parser.add_argument(
        "--out",
        type=str,
        required=True,
        default=None,
        help="Folder where output data are stored.",
    )
    parser.add_argument(
        "--netlists",
        type=str,
        required=False,
        default=None,
        help="Folder where netlists are stored",
    )
    parser.add_argument(
        "--type",
        type=str,
        required=False,
        default=FilterType.Nothing.name,
        help="Filter type",
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

    # Set up logger.
    os.makedirs(args.out, exist_ok=True)
    log_file_path = f"{args.out}/pipegen_{args.command}.log"
    setup_logger(log_file_path=log_file_path)

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
