#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Functions for running pipegen commands.
"""
from __future__ import annotations

import os
from dataclasses import dataclass
from enum import Enum
from typing import Optional

from verif.common.runner_utils import (
    DEFAULT_BIN_DIR,
    DEFAULT_SUBPROCESS_TIMEOUT,
    WorkerResult,
    execute_in_parallel,
    get_arch_bin_dir,
    get_soc_file,
    run_cmd,
)
from verif.common.test_utils import (
    DeviceArchs,
    extract_arch_from_path,
    find_all_files_in_dir,
    get_epoch_from_filename,
    get_logger,
)

logger = get_logger(__name__)

PIPEGEN_BIN_NAME = "pipegen2"
PIPEGEN_MASTER_BIN_NAME = "pipegen2_master"


class PerfDumpLevel(Enum):
    LEVEL_0 = 0
    LEVEL_1 = 1
    LEVEL_2 = 2


class PerfDumpMode(Enum):
    DRAM_SPILL_PERF_TRACE = 1
    L1_PERF_TRACE = 2
    HOST_SPILL_PERF_TRACE = 3


DEFAULT_PERF_DUMP_LEVEL = PerfDumpLevel.LEVEL_0.value
DEFAULT_PERF_DUMP_MODE = PerfDumpMode.L1_PERF_TRACE.value


@dataclass
class PipegenWorkerConfig:
    pipegen_yaml_path: str
    blob_yaml_path: str
    arch: DeviceArchs
    epoch_id: str
    pipegen_path: str


class PipegenRunner:
    def generate_pipegen_outputs(
        pipegen_yamls_dir: str,
        pipegen_out_dir: str,
        builds_all_archs_dir: str,
        num_samples: int = -1,
        overwrite: bool = False,
    ) -> bool:
        """Runs pipegen in parallel on all pipegen.yamls found in pipegen_yamls_dir.

        Parameters
        ----------
        pipegen_yamls_dir: str
            Directory with pipegen.yamls to run pipegen on. All subdirectories will be searched.
            Since we extract epoch name from the path of pipegen.yaml, there are two directory structures
            that are supported: The first one is the output of filter_pipegen_yamls, the second one is the
            output of generate_net2pipe_outputs.
        pipegen_out_dir: str
            Output directory.
        builds_all_archs_dir: str
            Directory with built binaries per arch.
        num_samples: int
            Number of pipegen.yamls to process. Samples are taken in order
            -1 will process all yamls.
        overwrite: bool
            Overwrite existing pipegen outputs, even if the output folder exists.

        Returns
        -------
        bool
            True if all the workers succeeded, False otherwise.

        Example of expected input and directory structures v1
        --------------------------------------------------
        .
        |-- pipegen_yamls_dir
        |   |-- filtered_yamls_Nothing
        |   |   |-- grayskull
        |   |   |   |-- 1xlink
        |   |   |   |   `-- pipegen_0.yaml
        |   |   |   `-- netlist_programs1_nested_parallel_loops_single_epoch
        |   |   |       |-- pipegen_0.yaml
        |   |   |       `-- pipegen_1.yaml
        |   |   `-- wormhole_b0
        |   |       `-- netlist_programs1_nested_parallel_loops_single_epoch
        |   |           |-- pipegen_0.yaml
        |   |           `-- pipegen_1.yaml
        |   `-- filtered_yamls_UnicastPipe
        |       |-- grayskull
        |       |   `-- 1xlink
        |       |       `-- pipegen_0.yaml
        |       `-- wormhole_b0
        `-- pipegen_out_dir
            |-- filtered_yamls_Nothing
            |   |-- grayskull
            |   |   |-- 1xlink
            |   |   |   `-- blob_0.yaml
            |   |   `-- netlist_programs1_nested_parallel_loops_single_epoch
            |   |       |-- blob_0.yaml
            |   |       `-- blob_1.yaml
            |   `-- wormhole_b0
            |       `-- netlist_programs1_nested_parallel_loops_single_epoch
            |           |-- blob_0.yaml
            |           `-- blob_1.yaml
            |-- filtered_yamls_UnicastPipe
            |   `-- grayskull
            |       `-- 1xlink
            |           `-- blob_0.yaml
            `-- pipegen.log

        Example of expected input and directory structures v2
        --------------------------------------------------
        |-- pipegen_yamls_dir
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
        `-- pipegen_out_dir
            |-- grayskull
            |   |-- 1xlink
            |   |   `-- blob_0.yaml
            |   `-- netlist_programs1_nested_parallel_loops_single_epoch
            |       |-- blob_0.yaml
            |       `-- blob_1.yaml
            |-- wormhole_b0
            |   `-- netlist_programs1_nested_parallel_loops_single_epoch
            |       |-- blob_0.yaml
            |       `-- blob_1.yaml
            `-- pipegen.log
        """
        if os.path.exists(pipegen_out_dir) and not overwrite:
            # Optimization to reuse generated pipegen outputs.
            logger.warning(
                f"Skipping pipegen, outputs already exist at {pipegen_out_dir}"
            )
            return True
        logger.info("Running pipegen workers")

        worker_configs = []

        all_pipegen_yamls = find_all_files_in_dir(pipegen_yamls_dir, "*/pipegen*.yaml")
        for pipegen_relpath in all_pipegen_yamls:
            # Split into dirname and basename
            pipegen_reldir, pipegen_yaml_name = os.path.split(pipegen_relpath)

            # If there is an epoch number in pipegen file name, than we have the v1 structure input.
            # If not, then the name is exactly pipegen.yaml and we have the v2 structure input.
            if pipegen_yaml_name != "pipegen.yaml":
                # v1
                # For pipegen_relpath filtered_yamls_UnicastPipe/grayskull/1xlink/pipegen_0.yaml
                # Output folder will be pipegen_out_dir/filtered_yamls_UnicastPipe/grayskull/1xlink
                output_folder = os.path.join(pipegen_out_dir, pipegen_reldir)
                epoch_id = get_epoch_from_filename(pipegen_yaml_name)
            else:
                # v2
                # For pipegen_relpath grayskull/1xlink/temporal_epoch_0/overlay/pipegen.yaml
                # Output folder will be pipegen_out_dir/grayskull/1xlink
                temporal_epoch_folder = os.path.dirname(pipegen_reldir)
                netlist_folder = os.path.dirname(temporal_epoch_folder)
                output_folder = os.path.join(pipegen_out_dir, netlist_folder)
                epoch_id = get_epoch_from_filename(temporal_epoch_folder)

            arch = extract_arch_from_path(pipegen_relpath)
            os.makedirs(output_folder, exist_ok=True)
            bin_dir = get_arch_bin_dir(builds_all_archs_dir, arch)

            worker_configs.append(
                PipegenWorkerConfig(
                    pipegen_yaml_path=os.path.join(pipegen_yamls_dir, pipegen_relpath),
                    blob_yaml_path=os.path.join(output_folder, f"blob_{epoch_id}.yaml"),
                    arch=arch,
                    epoch_id=epoch_id,
                    pipegen_path=os.path.join(bin_dir, PIPEGEN_BIN_NAME),
                )
            )

        if num_samples >= 0:
            worker_configs = worker_configs[:num_samples]
            logger.info(
                f"Choosing first {num_samples} pipegen.yamls to run pipegen on."
            )

        results = execute_in_parallel(
            PipegenRunner.run_pipegen_worker,
            worker_configs,
            log_file=f"{pipegen_out_dir}/pipegen.log",
            heavy_workload=True,
        )

        return all(result.error_code == 0 for result in results)

    def run_pipegen_worker(worker_config: PipegenWorkerConfig):
        err, cmd = PipegenRunner.run_pipegen(
            worker_config.pipegen_yaml_path,
            worker_config.blob_yaml_path,
            worker_config.arch,
            worker_config.epoch_id,
            pipegen_path=worker_config.pipegen_path,
        )
        return WorkerResult(cmd, err)

    def run_pipegen(
        pipegen_yaml_path: str,
        blob_yaml_path: str,
        arch: str,
        epoch_id: str,
        perf_dump_mode: int = DEFAULT_PERF_DUMP_MODE,
        perf_dump_level: int = DEFAULT_PERF_DUMP_LEVEL,
        soc_descriptor: str = None,
        pipegen_path: str = os.path.join(DEFAULT_BIN_DIR, PIPEGEN_BIN_NAME),
        timeout: int = DEFAULT_SUBPROCESS_TIMEOUT,
        throw_if_error: bool = False,
    ):
        """Runs pipegen with given arguments.

        Parameters
        ----------
        pipegen_yaml_path: str
            Pipegen yaml path.
        blob_yaml_path: str
            Output blob yaml path.
        epoch_id: str
            Epoch id.
        arch: str
            Device architecture.
        perf_dump_mode: int
            Performance dump mode to use.
        perf_dump_level: int
            Level of detail of performance dump.
        soc_descriptor: str
            SoC descriptor to use. If not provided,
            default SoC descr for arch will be used.
        bin_dir: str
            Directory with built binaries.
        pipegen_bin_name: str
            Name of the pipegen binary to run.
        timeout: int
            Execution timeout.
        throw_if_error: bool
            If True, raises RuntimeError if net2pipe fails.

        Returns
        -------
        int, str
            Return code and pipegen command.
        """
        pipegen_cmd = PipegenRunner.__get_pipegen_cmd(
            pipegen_yaml_path,
            blob_yaml_path,
            arch,
            epoch_id,
            perf_dump_mode,
            perf_dump_level,
            soc_descriptor,
            pipegen_path,
        )
        result = run_cmd(pipegen_cmd, arch, timeout)
        if result.returncode != 0:
            if os.path.exists(blob_yaml_path):
                os.remove(blob_yaml_path)
            if throw_if_error:
                raise RuntimeError(
                    f"Running pipegen on {pipegen_yaml_path} failed: {result}"
                )
        return result.returncode, pipegen_cmd

    def __get_pipegen_cmd(
        pipegen_yaml_path: str,
        blob_yaml_path: str,
        arch: str,
        epoch_id: str,
        perf_dump_mode: int,
        perf_dump_level: int,
        soc_descriptor: Optional[str],
        pipegen_path: str,
    ):
        """Returns pipegen command.

        Example command
        ---------------
        build_archs/wormhole_b0/bin/pipegen2
            out/a/output_filter/filtered_yamls_Nothing/wormhole_b0/netlist_softmax_single_tile/pipegen_0.yaml
            ./soc_descriptors/wormhole_b0_8x10.yaml
            out/a/output_pipegen/filtered_yamls_Nothing/wormhole_b0/netlist_softmax_single_tile/blob_0.yaml
            0
            0x200
        """
        soc_file = soc_descriptor or get_soc_file(arch)
        perf_dump_info = hex(perf_dump_mode << 8 | perf_dump_level)
        return (
            f"{pipegen_path} {pipegen_yaml_path} {soc_file} {blob_yaml_path} "
            f"{epoch_id} {perf_dump_info}"
        )
