#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from util import load_yaml_test_configs, create_netlists, generate_test_commands
import argparse


def generate_netlists(
    template_yaml, test_configs_yaml, output_dir, determinism_tests=False, use_hash=1, num_loops=1
):
    configs = load_yaml_test_configs(test_configs_yaml)
    netlist_test_info = create_netlists(
        template_yaml=template_yaml,
        output_dir=output_dir,
        configs=configs,
        use_shared_dir=False,
        use_hash=use_hash,
    )
    generate_test_commands(
        netlist_test_info, determinism_tests=determinism_tests, num_loops=num_loops
    )
    return netlist_test_info


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--template-yaml",
        required=True,
        type=str,
        help="Path to the template .yaml from root budabackend repo",
    )
    parser.add_argument(
        "--test-configs-yaml",
        required=True,
        type=str,
        help="Directory of the generated configs, from the root budabackend repo",
    )
    parser.add_argument("--output-dir", required=True, type=str, help="Path to output directory")
    parser.add_argument(
        "--determinism-tests",
        default=False,
        action="store_true",
        help="Generating determinism tests",
    )
    parser.add_argument("--use-hash", default=1, type=int, help="Use hash in test name")
    parser.add_argument(
        "--num-loops", default=1, type=int, help="Number of loops for determinism tests"
    )
    args = parser.parse_args()
    generate_netlists(
        args.template_yaml,
        args.test_configs_yaml,
        args.output_dir,
        args.determinism_tests,
        args.use_hash,
        args.num_loops,
    )


if __name__ == "__main__":
    main()
