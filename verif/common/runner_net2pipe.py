#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Functions for running net2pipe commands.
"""
import os
import random
from dataclasses import dataclass
from typing import Dict

from verif.common.runner_utils import (
    DEFAULT_BIN_DIR,
    WorkerResult,
    execute_in_parallel,
    fetch_custom_env_configs,
    get_arch_bin_dir,
    get_cluster_descriptors,
    get_soc_file,
    run_cmd,
    verify_builds_dir,
)
from verif.common.test_utils import (
    DeviceArchs,
    find_all_files_in_dir,
    get_logger,
    get_netlist_arch,
    get_netlist_name,
)

logger = get_logger(__name__)

NET2PIPE_BIN_NAME = "net2pipe"


@dataclass
class Net2PipeWorkerConfig:
    netlist_path: str
    net2pipe_out_dir: str
    arch: DeviceArchs
    builds_dir: str


class Net2PipeRunner:
    def generate_net2pipe_outputs(
        netlists_dir: str,
        net2pipe_out_dir: str,
        builds_all_archs_dir: str,
        num_samples: int = -1,
        netlists_to_skip: list = [],
        overwrite: bool = False,
    ) -> bool:
        """Runs net2pipe in parallel on all netlists in netlists_dir.

        Parameters
        ----------
        netlists_dir: str
            Directory with netlists to run net2pipe on.
        net2pipe_out_dir: str
            Output directory.
        builds_all_archs_dir: str
            Directory with built binaries per arch.
        num_samples: int
            Number of netlists to process.
            -1 will process all netlist.
            The sample is randomized, so for a given num_samples, the netlists chosen will be different.
        netlists_to_skip: list
            List of netlist names to skip explicitly.
        overwrite: bool
            Overwrite existing net2pipe outputs, even if the output folder exists.

        Returns
        -------
        bool
            True if all the workers succeeded, False otherwise.

        Example of expected input and directory structures
        --------------------------------------------------
        .
        |-- netlists_dir
        |   |-- 1xlink.yaml
        |   `-- netlist_programs1_nested_parallel_loops_single_epoch.yaml
        `-- net2pipe_out_dir
            |-- grayskull
            |   |-- 1xlink
            |   |   |-- netlist_queues.yaml
            |   |   |-- padding_table.yaml
            |   |   |-- reports
            |   |   |   `-- op_to_pipe_map_temporal_epoch_0.yaml
            |   |   `-- temporal_epoch_0
            |   |       `-- overlay
            |   |           |-- pipegen.yaml
            |   |           |-- queue_to_consumer.yaml
            |   |           `-- queue_to_producer.yaml
            |   `-- netlist_programs1_nested_parallel_loops_single_epoch
            |       |-- netlist_queues.yaml
            |       |-- padding_table.yaml
            |       |-- reports
            |       |   |-- op_to_pipe_map_temporal_epoch_0.yaml
            |       |   `-- op_to_pipe_map_temporal_epoch_1.yaml
            |       |-- temporal_epoch_0
            |       |   `-- overlay
            |       |       |-- pipegen.yaml
            |       |       |-- queue_to_consumer.yaml
            |       |       `-- queue_to_producer.yaml
            |       `-- temporal_epoch_1
            |           `-- overlay
            |               |-- pipegen.yaml
            |               |-- queue_to_consumer.yaml
            |               `-- queue_to_producer.yaml
            |-- net2pipe.log
            `-- wormhole_b0
                `-- netlist_programs1_nested_parallel_loops_single_epoch
                    |-- netlist_queues.yaml
                    |-- padding_table.yaml
                    |-- reports
                    |   |-- op_to_pipe_map_temporal_epoch_0.yaml
                    |   `-- op_to_pipe_map_temporal_epoch_1.yaml
                    |-- temporal_epoch_0
                    |   `-- overlay
                    |       |-- pipegen.yaml
                    |       |-- queue_to_consumer.yaml
                    |       `-- queue_to_producer.yaml
                    `-- temporal_epoch_1
                        `-- overlay
                            |-- pipegen.yaml
                            |-- queue_to_consumer.yaml
                            `-- queue_to_producer.yaml
        """

        if os.path.exists(net2pipe_out_dir) and not overwrite:
            # Optimization to reuse generated net2pipe outputs.
            logger.warning(
                f"Skipping net2pipe workers, outputs already exist at {net2pipe_out_dir}"
            )
            return True
        logger.info("Running net2pipe workers")

        verify_builds_dir(builds_all_archs_dir)

        all_worker_args = []

        all_files = find_all_files_in_dir(netlists_dir, "*.yaml")
        for netlist_path in all_files:
            if get_netlist_name(netlist_path) in netlists_to_skip:
                continue
            # This runner adds another level of subdirectories to net2pipe_out_dir, related to arch.
            for arch in DeviceArchs.get_all_archs():
                netlist_relative_base_dir = os.path.dirname(netlist_path)
                arch_dir = os.path.join(net2pipe_out_dir, arch)
                os.makedirs(arch_dir, exist_ok=True)
                output_dir = os.path.join(
                    arch_dir,
                    netlist_relative_base_dir,
                    get_netlist_name(netlist_path),
                )

                all_worker_args.append(
                    Net2PipeWorkerConfig(
                        netlist_path=os.path.join(netlists_dir, netlist_path),
                        net2pipe_out_dir=output_dir,
                        arch=arch,
                        builds_dir=get_arch_bin_dir(builds_all_archs_dir, arch),
                    )
                )

        if num_samples >= 0:
            random.shuffle(all_worker_args)
            all_worker_args = all_worker_args[:num_samples]
            logger.info(
                f"Choosing randomized {num_samples} netlists to run net2pipe on."
            )

        results = execute_in_parallel(
            Net2PipeRunner.run_net2pipe_worker,
            all_worker_args,
            log_file=f"{net2pipe_out_dir}/net2pipe.log",
            heavy_workload=True,
        )

        return all(result.error_code == 0 for result in results)

    def run_net2pipe_worker(worker_config: Net2PipeWorkerConfig):
        archs = get_netlist_arch(worker_config.netlist_path)
        if worker_config.arch not in archs:
            return WorkerResult(
                f"Skipped {worker_config.netlist_path} as not compatible with {worker_config.arch}.",
                0,
            )

        err, cmd = Net2PipeRunner.run_net2pipe(
            worker_config.netlist_path,
            worker_config.net2pipe_out_dir,
            worker_config.arch,
            worker_config.builds_dir,
        )
        return WorkerResult(cmd, err)

    def run_net2pipe(
        netlist_path: str,
        out_dir: str,
        arch: str,
        bin_dir: str = DEFAULT_BIN_DIR,
        throw_if_error: bool = False,
    ) -> int:
        """Runs net2pipe on given netlist. Tries multiple cluster descriptors until it runs successfully.

        Parameters
        ----------
        netlist_path: str
            Netlist path.
        out_dir: str
            Output directory.
        bin_dir: str
            Directory with built binaries.
        arch: str
            Device architecture.
        throw_if_error: bool
            If True, raises RuntimeError if net2pipe fails.

        Returns
        -------
        int, str
            Return code and net2pipe command.
        """
        cluster_descriptors = get_cluster_descriptors(arch)
        custom_env_configs = fetch_custom_env_configs().get(
            get_netlist_name(netlist_path), {}
        )
        ret_code = 1
        net2pipe_cmd = "<no command ran>"
        for cluster_descriptor in cluster_descriptors:
            if cluster_descriptor and not os.path.exists(cluster_descriptor):
                continue
            net2pipe_cmd = Net2PipeRunner.__get_net2pipe_cmd(
                netlist_path, out_dir, bin_dir, arch, cluster_descriptor
            )
            result = run_cmd(net2pipe_cmd, arch, custom_env_configs)
            ret_code = result.returncode
            if ret_code == 0:
                break
            else:
                os.system(f"rm -rf {out_dir}/*")
        if ret_code != 0 and throw_if_error:
            raise RuntimeError(f"Running net2pipe on {netlist_path} failed: {result}")
        return ret_code, net2pipe_cmd

    def __get_net2pipe_cmd(
        netlist_path: str,
        out_dir: str,
        bin_dir: str,
        arch: str,
        cluster_descriptor: str,
    ):
        """Returns net2pipe command.

        Example command
        ---------------
        build_archs/wormhole_b0/bin/net2pipe
            ../out/output_netlist_collector/./netlist_softmax_single_tile.yaml
            out/a/output_net2pipe/wormhole_b0/./netlist_softmax_single_tile
            0
            ./soc_descriptors/wormhole_b0_8x10.yaml
            ./verif/multichip_tests/wh_multichip/large_cluster/32chip_wh_cluster_desc.yaml
        """
        soc_file = get_soc_file(arch)
        epoch_start = 0
        return (
            f"{bin_dir}/{NET2PIPE_BIN_NAME} {netlist_path} {out_dir} {epoch_start} {soc_file} "
            f"{cluster_descriptor}"
        )
