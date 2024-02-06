#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
import argparse
import sys
import textwrap

from util import *
from z3_config_generator import generate_z3_configs
from test_modules.common.solution_search import ParallelSweepSearchConfig


def generate_all_configs(
    module_names_list,
    arch_list,
    random_seed_list,
    max_num_configs,
    verbose,
    log_to_file,
    clamp_num_configs
):
    search_configs = [
        ParallelSweepSearchConfig(
            module_name,
            random_seed,
            max_num_configs,
            arch,
            verbose,
            log_to_file,
            clamp_num_configs
        )
        for module_name in module_names_list
        for arch in arch_list
        for random_seed in random_seed_list
    ]

    # Generate configs
    config_list = generate_z3_configs(search_config=search_configs)

    print(f"Done. Generated {len(config_list)} configs.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=textwrap.dedent("""
        Example:
            python generate_multi_module_multi_arch_tests.py \
                --module-names-list test_modules.graph_tests.test_softmax test_modules.graph_tests.test_feedforward \
                --arch-list grayskull wormhole 
                --random-seed-list 0 1 2
                --max-num-configs 100 
                --clamp-num-configs --verbose --log-to-file
        """
    ))
    parser.add_argument('--module-names-list', nargs='+', required=True, help="List of test module names")
    parser.add_argument("--arch-list", nargs='+', required=True, help="List of device architectures: ['grayskull', 'wormhole', 'wormhole_b0']")
    parser.add_argument('--random-seed-list', nargs="+", required=True, help="List of random seeds for solver")
    parser.add_argument('--max-num-configs', required=True, type=int, help="Number of test configs to generate")
    parser.add_argument("--clamp-num-configs", action="store_true", default=False, help="Clamp sweep combinations to max_num_configs")
    parser.add_argument("--verbose", action="store_true", default=False, help="Print out progress")
    parser.add_argument("--log-to-file", action="store_true", default=False, help="Log output to file")

    args = parser.parse_args(sys.argv[1:])

    generate_all_configs(
        module_names_list=args.module_names_list,
        arch_list=args.arch_list,
        random_seed_list=args.random_seed_list,
        max_num_configs=args.max_num_configs,
        verbose=args.verbose,
        log_to_file=args.log_to_file,
        clamp_num_configs=args.clamp_num_configs
    )
