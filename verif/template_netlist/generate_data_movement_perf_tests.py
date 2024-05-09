# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
from generate_tests import full_test_driver

from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestType,
)
from test_modules.data_movement.perf_tests.data_movement_perf_test_modules import (
    get_perf_test_module_name
)

def generate_data_movement_perf_tests(
    pipe_type: DataMovementPerfTestType, output_dir: str, arch: str, harvested_rows: int
) -> None:
    full_test_driver(
        module_name=get_perf_test_module_name(pipe_type),
        output_dir=output_dir,
        arch=arch,
        random_seed=0,
        max_num_configs=99999,
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
        run_compiler=False
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--pipe-type", required=True, help="Pipe perf test type")
    parser.add_argument("--output-dir", required=True, help="Test output directory relative to root")
    parser.add_argument(
        "--arch",
        default="wormhole_b0",
        type=str,
        help="Name of device architecture, one of: ['grayskull', 'wormhole_b0']",
    )
    parser.add_argument(
        "--harvested-rows",
        default=2,
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
