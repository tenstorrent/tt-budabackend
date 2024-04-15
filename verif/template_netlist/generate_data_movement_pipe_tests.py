# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
from generate_tests import full_test_driver

from test_modules.data_movement.pipe_tests.data_movement_pipe_test_base import (
    DataMovementPipeTestType,
)

__pipe_tests_dir = "test_modules.data_movement.pipe_tests"
__pipe_test_type_to_module_name = {
    # Direct pipes.
    DataMovementPipeTestType.PackerUnicast: f"{__pipe_tests_dir}.direct_pipe.test_unicast_pipe",
    DataMovementPipeTestType.DramUnicast: f"{__pipe_tests_dir}.direct_pipe.test_dram_unicast_pipe",
    DataMovementPipeTestType.DramPrefetchPostTM: f"{__pipe_tests_dir}.direct_pipe.test_dram_prefetch_post_tm_pipe",
    DataMovementPipeTestType.DramPrefetchPreTM: f"{__pipe_tests_dir}.direct_pipe.test_dram_prefetch_pre_tm_pipe",
    DataMovementPipeTestType.UnicastToDram: f"{__pipe_tests_dir}.direct_pipe.test_unicast_to_dram_pipe",

    DataMovementPipeTestType.PackerGather: f"{__pipe_tests_dir}.gather_pipe.test_packer_gather_pipe",
    DataMovementPipeTestType.DramGather: f"{__pipe_tests_dir}.gather_pipe.test_dram_gather_pipe",
    DataMovementPipeTestType.GatherToDram: f"{__pipe_tests_dir}.gather_pipe.test_gather_to_dram_pipe",

    DataMovementPipeTestType.DramMulticast: f"{__pipe_tests_dir}.multicast_pipe.test_dram_multicast_pipe",
    DataMovementPipeTestType.DramGatherMulticast: f"{__pipe_tests_dir}.multicast_pipe.test_dram_gather_multicast_pipe",
    DataMovementPipeTestType.PackerMulticast: f"{__pipe_tests_dir}.multicast_pipe.test_packer_multicast_pipe",
    DataMovementPipeTestType.PackerGatherMulticast: f"{__pipe_tests_dir}.multicast_pipe.test_packer_gather_multicast_pipe",

    DataMovementPipeTestType.PackerParallelFork: f"{__pipe_tests_dir}.fork_pipe.test_packer_parallel_fork_pipe",
    DataMovementPipeTestType.DramParallelFork: f"{__pipe_tests_dir}.fork_pipe.test_dram_parallel_fork_pipe",
    DataMovementPipeTestType.PackerSerialFork: f"{__pipe_tests_dir}.fork_pipe.test_packer_serial_fork_pipe",

}


def generate_data_movement_pipe_tests(
    pipe_type: DataMovementPipeTestType,
    max_num_configs: int,
    output_dir: str,
    arch: str,
    harvested_rows: int,
    run_compiler: bool
) -> None:

    assert (
        pipe_type in __pipe_test_type_to_module_name
    ), f"Tests for {str(pipe_type)} are not yet suported"

    module_name = __pipe_test_type_to_module_name[pipe_type]
    full_test_driver(
        module_name=module_name,
        output_dir=output_dir,
        arch=arch,
        random_seed=0,
        max_num_configs=max_num_configs,
        search_type="parallel-sweep",
        verbose=True,
        log_to_file=False,
        clamp_num_configs=True,
        dump_config_yaml=True,
        dry_run=True,
        timeout=0,
        num_retries_on_timeout=0,
        enable_strategy=False,
        use_hash=0,
        harvested_rows=harvested_rows,
        run_compiler=run_compiler
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pipe-type", required=True, help="Pipe test type")
    parser.add_argument(
        "--output-dir", required=True, help="Test output directory relative to root"
    )
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
    parser.add_argument(
        "--max-num-configs",
        default=20,
        type=int,
        help="Maximum number of configs to generate"
    )
    parser.add_argument(
        "--run-compiler",
        action="store_true",
        default=False,
        help="Run compiler to verify tests as part of test generation",
    )

    args = parser.parse_args()

    assert args.pipe_type in DataMovementPipeTestType.__members__, f"Invalid pipe type: {args.pipe_type}"

    generate_data_movement_pipe_tests(
        pipe_type=DataMovementPipeTestType[args.pipe_type],
        max_num_configs=args.max_num_configs,
        output_dir=args.output_dir,
        arch=args.arch,
        harvested_rows=args.harvested_rows,
        run_compiler=args.run_compiler
    )
