# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
from generate_tests import full_test_driver

from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestType,
)

__perf_tests_dir = "test_modules.data_movement.perf_tests"
__perf_test_type_to_module_name = {
    # Direct pipes.
    DataMovementPerfTestType.PackerUnicast: f"{__perf_tests_dir}.direct_pipe.test_perf_packer_unicast_pipe",
    DataMovementPerfTestType.DramUnicast: f"{__perf_tests_dir}.direct_pipe.test_perf_dram_unicast_pipe",
    DataMovementPerfTestType.UnicastToDram: f"{__perf_tests_dir}.direct_pipe.test_perf_unicast_to_dram_pipe",
    # Gather pipes.
    DataMovementPerfTestType.DramGather: f"{__perf_tests_dir}.gather_pipe.test_perf_dram_gather_pipe",
    DataMovementPerfTestType.PackerGather: f"{__perf_tests_dir}.gather_pipe.test_perf_packer_gather_pipe",
    DataMovementPerfTestType.GatherToDram: f"{__perf_tests_dir}.gather_pipe.test_perf_gather_to_dram_pipe",
    # Fork pipes.
    DataMovementPerfTestType.DramSingleFork: f"{__perf_tests_dir}.fork_pipe.test_perf_dram_single_fork_pipe",
    DataMovementPerfTestType.PackerSingleFork: f"{__perf_tests_dir}.fork_pipe.test_perf_packer_single_fork_pipe",
    DataMovementPerfTestType.DramMultiFork: f"{__perf_tests_dir}.fork_pipe.test_perf_dram_multi_fork_pipe",
    DataMovementPerfTestType.PackerMultiFork: f"{__perf_tests_dir}.fork_pipe.test_perf_packer_multi_fork_pipe",
    # Multicast pipes.
    DataMovementPerfTestType.DramMulticast: f"{__perf_tests_dir}.multicast_pipe.test_perf_dram_multicast_pipe",
    DataMovementPerfTestType.DramGatherMulticast: f"{__perf_tests_dir}.multicast_pipe.test_perf_dram_gather_multicast_pipe",
    DataMovementPerfTestType.PackerMulticast: f"{__perf_tests_dir}.multicast_pipe.test_perf_packer_multicast_pipe",
    DataMovementPerfTestType.PackerGatherMulticast: f"{__perf_tests_dir}.multicast_pipe.test_perf_packer_gather_multicast_pipe",
    # Pipe scatter pipes.
    DataMovementPerfTestType.PipeScatter: f"{__perf_tests_dir}.pipe_scatter.test_perf_pipe_scatter",
    DataMovementPerfTestType.GatherPipeScatter: f"{__perf_tests_dir}.pipe_scatter.test_perf_gather_pipe_scatter",
    DataMovementPerfTestType.PipeScatterMulticast: f"{__perf_tests_dir}.pipe_scatter.test_perf_pipe_scatter_multicast",
    DataMovementPerfTestType.GatherPipeScatterMulticast: f"{__perf_tests_dir}.pipe_scatter.test_perf_gather_pipe_scatter_multicast",
}


def generate_data_movement_perf_tests(
    pipe_type: DataMovementPerfTestType, output_dir: str, arch: str, harvested_rows: int
) -> None:
    assert pipe_type in __perf_test_type_to_module_name, f"Tests for {pipe_type} are not yet suported"
    module_name = __perf_test_type_to_module_name[pipe_type]
    full_test_driver(
        module_name=module_name,
        output_dir=output_dir,
        arch=arch,
        random_seed=0,
        max_num_configs=9999,
        search_type="parallel-sweep",
        verbose=True,
        log_to_file=False,
        clamp_num_configs=False,
        dry_run=True,
        dump_config_yaml=True,
        timeout=0,
        num_retries_on_timeout=0,
        enable_strategy=False,
        use_hash=0,
        harvested_rows=harvested_rows,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pipe-type", required=True, help="Pipe perf test type")
    parser.add_argument("--output-dir", required=True, help="Test output directory relative to root")
    parser.add_argument(
        "--arch",
        default="grayskull",
        type=str,
        help="Name of device architecture, one of: ['grayskull', 'wormhole_b0']",
    )
    parser.add_argument(
        "--harvested-rows",
        default=0,
        type=int,
        help="Number of harvested rows",
    )
    args = parser.parse_args()

    generate_data_movement_perf_tests(
        pipe_type=DataMovementPerfTestType[args.pipe_type],
        output_dir=args.output_dir,
        arch=args.arch,
        harvested_rows=args.harvested_rows,
    )
