#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Testing between pipegen2 from master and pipegen2 with your changes.

Example commands (from repo root):
    mkdir -p out/netlists
    cp verif/graph_tests/netlists/*.yaml out/netlists/
    verif/pipegen_tests/build_all_archs.sh
    cp build_archs/wormhole_b0/bin/pipegen2 build_archs/wormhole_b0/bin/pipegen2_master
    verif/pipegen_tests/create_env.sh
    source verif/pipegen_tests/env/bin/activate
    python3 verif/pipegen_tests/pipegen_refactor_test.py --command filter \
        --out out --netlists out/netlists \
        --arch wormhole_b0 --builds-dir build_archs
    python3 verif/pipegen_tests/pipegen_refactor_test.py --command test \
        --yamls out/filtered_yamls/filtered_yamls_Everything --out out/pipegen_refactor_test_output \
        --arch wormhole_b0 --builds-dir build_archs --pipegens-bin-dir build_archs/wormhole_b0/bin
"""
from __future__ import annotations

import argparse
import os
import time
from datetime import datetime

from blob_comparator import StreamGraphComparisonStrategy
from compare_blobs import DEFAULT_SG_COMPARISON_STRATEGY, BlobComparatorCaller

from verif.common.pipegen_yaml_filter import FilterType
from verif.common.runner_compare import CompareFilesRunner
from verif.common.runner_net2pipe import Net2PipeRunner
from verif.common.runner_pipegen import (
    PIPEGEN_BIN_NAME,
    PIPEGEN_MASTER_BIN_NAME,
    PipegenRunner,
    PipegenWorkerConfig,
)
from verif.common.runner_pipegen_filter import PipegenFilterRunner
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

        netlist_name = pipegen_relpath.split(os.path.sep)[0]
        blob_output_path = os.path.join(results_dir, netlist_name)
        original_blob_yaml_paths.append(
            os.path.join(blob_output_path, f"blob_{epoch_id}_original.yaml")
        )
        new_blob_yaml_paths.append(
            os.path.join(blob_output_path, f"blob_{epoch_id}.yaml")
        )
        os.makedirs(blob_output_path, exist_ok=True)

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
            pipegen_path=os.path.join(bins_dir, PIPEGEN_MASTER_BIN_NAME),
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
            pipegen_path=os.path.join(bins_dir, PIPEGEN_BIN_NAME),
        )
        for pipegen_yaml_path, new_blob_yaml_path, epoch_id in zip(
            pipegen_yaml_paths, new_blob_yaml_paths, epoch_ids
        )
    ]

    execute_in_parallel(
        PipegenRunner.run_pipegen_worker,
        worker_configs,
        log_file=os.path.join(results_dir, "pipegen_commands.log"),
    )


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

    return CompareFilesRunner.compare_all_outputs_lists(
        original_blob_yaml_paths,
        new_blob_yaml_paths,
        log_out_dir=results_dir,
        comparison_function=BlobComparatorCaller(sg_comparison_strategy),
    )


def compare_pipegens_on_yamls(
    yamls: str,
    out: str,
    arch: str,
    bins_dir: str,
    num_samples: int,
    sg_comparison_strategy: StreamGraphComparisonStrategy,
) -> bool:
    timestamp = datetime.now().strftime("%m_%d_%Y_%H_%M_%S")
    results_dir = os.path.join(out, timestamp)
    os.makedirs(results_dir)

    logger.info(
        f"Blob comparator is using: '{sg_comparison_strategy.name}' as the comparison strategy"
    )
    result = run_comparison(
        yamls, results_dir, arch, bins_dir, num_samples, sg_comparison_strategy
    )
    return result
