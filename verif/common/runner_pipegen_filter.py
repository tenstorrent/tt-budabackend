#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Runs pipegen filter commands.
"""
from __future__ import annotations

import multiprocessing as mp
import os
from dataclasses import dataclass

from verif.common.pipegen_yaml_filter import *
from verif.common.runner_utils import execute_in_parallel
from verif.common.test_utils import find_all_files_in_dir, get_epoch_dir, get_logger

logger = get_logger(__name__)

FILTERED_YAMLS_OUT_DIR_NAME_PREFIX = "filtered_yamls"


@dataclass
class PipegenYamlFilterWorkerConfig:
    netlist_dir: str
    filtered_netlist_dir: str
    filter_type: FilterType
    num_samples: int


class PipegenFilterRunner:
    def filter_pipegen_yamls(
        net2pipe_out_dir: str,
        out_dir: str,
        filter_type: FilterType,
        num_samples: int = -1,
        overwrite: bool = False,
    ):
        """Runs pipegen filter in parallel on net2pipe outputs in net2pipe_out_dir.

        Parameters
        ----------
        net2pipe_out_dir: str
            Directory with pipegen.yamls to run pipegen filter on.
        out_dir: str
            Output directory.
        filter_type: FilterType
            Type of pipe filter to use for filtering.
        num_samples: int
            Number of pipegens yamls to produce per each filter.
            -1 will process all.
        netlists_to_skip: list
            List of netlist names to skip explicitly.
        overwrite: bool
            Overwrite existing net2pipe outputs, even if the output folder exists.

        Example of expected input and directory structures
        --------------------------------------------------
        .
        |-- net2pipe_out_dir
        |   |-- grayskull
        |   |   |-- 1xlink
        |   |   |   |-- netlist_queues.yaml
        |   |   |   |-- padding_table.yaml
        |   |   |   |-- reports
        |   |   |   |   `-- op_to_pipe_map_temporal_epoch_0.yaml
        |   |   |   `-- temporal_epoch_0
        |   |   |       `-- overlay
        |   |   |           |-- pipegen.yaml
        |   |   |           |-- queue_to_consumer.yaml
        |   |   |           `-- queue_to_producer.yaml
        |   |   `-- netlist_programs1_nested_parallel_loops_single_epoch
        |   |       |-- netlist_queues.yaml
        |   |       |-- padding_table.yaml
        |   |       |-- reports
        |   |       |   |-- op_to_pipe_map_temporal_epoch_0.yaml
        |   |       |   `-- op_to_pipe_map_temporal_epoch_1.yaml
        |   |       |-- temporal_epoch_0
        |   |       |   `-- overlay
        |   |       |       |-- pipegen.yaml
        |   |       |       |-- queue_to_consumer.yaml
        |   |       |       `-- queue_to_producer.yaml
        |   |       `-- temporal_epoch_1
        |   |           `-- overlay
        |   |               |-- pipegen.yaml
        |   |               |-- queue_to_consumer.yaml
        |   |               `-- queue_to_producer.yaml
        |   |-- net2pipe.log
        |   `-- wormhole_b0
        |       `-- netlist_programs1_nested_parallel_loops_single_epoch
        |           |-- netlist_queues.yaml
        |           |-- padding_table.yaml
        |           |-- reports
        |           |   |-- op_to_pipe_map_temporal_epoch_0.yaml
        |           |   `-- op_to_pipe_map_temporal_epoch_1.yaml
        |           |-- temporal_epoch_0
        |           |   `-- overlay
        |           |       |-- pipegen.yaml
        |           |       |-- queue_to_consumer.yaml
        |           |       `-- queue_to_producer.yaml
        |           `-- temporal_epoch_1
        |               `-- overlay
        |                   |-- pipegen.yaml
        |                   |-- queue_to_consumer.yaml
        |                   `-- queue_to_producer.yaml
        `-- out_dir
            |-- filtered_yamls_Everything
            |   |-- grayskull
            |   |   |-- 1xlink
            |   |   |   `-- pipegen_0.yaml
            |   |   `-- netlist_programs1_nested_parallel_loops_single_epoch
            |   |       |-- pipegen_0.yaml
            |   |       `-- pipegen_1.yaml
            |   `-- wormhole_b0
            |       `-- netlist_programs1_nested_parallel_loops_single_epoch
            |           |-- pipegen_0.yaml
            |           `-- pipegen_1.yaml
            `-- filtered_yamls_UnicastPipe
                |-- grayskull
                |   `-- 1xlink
                |       `-- pipegen_0.yaml
                `-- wormhole_b0
        """
        # This runner adds another level of subdirectories to pipegen_filter, related to filter name.
        filtered_yamls_dir_name = (
            f"{FILTERED_YAMLS_OUT_DIR_NAME_PREFIX}_{filter_type.name}"
        )
        out_dir_with_filter_name = os.path.join(out_dir, filtered_yamls_dir_name)
        if os.path.exists(out_dir_with_filter_name) and not overwrite:
            # Optimization to reuse generated filter outputs.
            logger.warning(
                f"Skipping pipegen filter, outputs already exist at {out_dir_with_filter_name}"
            )
            return

        logger.info(f"Running filter {filter_type.name}")

        worker_configs = []
        # Search only for epoch 0 pipegen.yaml files, since we are executing pipegen filter per netlist and not per epoch.
        all_pipegen_yamls = find_all_files_in_dir(
            net2pipe_out_dir, "*temporal_epoch_0/overlay/pipegen.yaml"
        )
        for pipegen_yaml_relpath in all_pipegen_yamls:
            # To get the netlist folder, we need third parent of pipegen.yaml file.
            netlist_relpath = os.path.dirname(
                os.path.dirname(os.path.dirname(pipegen_yaml_relpath))
            )
            output_dir = os.path.join(out_dir_with_filter_name, netlist_relpath)
            os.makedirs(output_dir, exist_ok=True)
            worker_configs.append(
                PipegenYamlFilterWorkerConfig(
                    netlist_dir=os.path.join(net2pipe_out_dir, netlist_relpath),
                    filtered_netlist_dir=output_dir,
                    filter_type=filter_type,
                    num_samples=num_samples,
                )
            )

        pipegen_yamls_value = mp.Value("i", 0)
        num_processed = execute_in_parallel(
            PipegenFilterRunner.filter_netlist_yamls_worker,
            worker_configs,
            PipegenFilterRunner.init_global_pipegen_filter_sync_vars,
            pipegen_yamls_value,
        )

        logger.info(f"Filtered {sum(num_processed)} pipegen yamls.\n")

    def init_global_pipegen_filter_sync_vars(pipegen_yamls_value: mp.Value):
        global filtered_pipegen_yamls_count_sync
        filtered_pipegen_yamls_count_sync = pipegen_yamls_value

    def filter_netlist_yamls_worker(
        worker_config: PipegenYamlFilterWorkerConfig,
    ) -> int:
        netlist_dir = worker_config.netlist_dir
        filtered_netlist_dir = worker_config.filtered_netlist_dir
        filter_type = worker_config.filter_type
        num_samples = worker_config.num_samples

        with filtered_pipegen_yamls_count_sync.get_lock():
            if (
                num_samples > 0
                and filtered_pipegen_yamls_count_sync.value >= num_samples
            ):
                return 0

        epoch_id = 0
        epoch_dir = get_epoch_dir(netlist_dir, epoch_id)
        num_processed = 0
        while os.path.isdir(epoch_dir):
            filtered = True
            pipegen_yaml_path = os.path.join(epoch_dir, "pipegen.yaml")

            # Check if pipegen.yaml exists, if not, skip this epoch.
            if os.path.isfile(pipegen_yaml_path):
                if filter_type == FilterType.Everything:
                    os.system(
                        f"cp {pipegen_yaml_path} {filtered_netlist_dir}/pipegen_{epoch_id}.yaml"
                    )
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
                if num_samples > 0:
                    with filtered_pipegen_yamls_count_sync.get_lock():
                        filtered_pipegen_yamls_count_sync.value += 1
                        if filtered_pipegen_yamls_count_sync.value >= num_samples:
                            break

        if not os.listdir(filtered_netlist_dir):
            os.system(f"rm -rf {filtered_netlist_dir}")

        return num_processed
