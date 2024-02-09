#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import os
import argparse
import yaml
import importlib
import argparse
import sys
import textwrap

from util import *
from z3_config_generator import generate_z3_configs
from test_modules.common.solution_search import SolutionSearchConfig, SearchType


def generate_all_configs(
    module_name,
    output_dir,
    arch="grayskull",
    random_seed=0,
    max_num_configs=None,
    search_type="default",
    verbose=False,
    log_to_file=False,
    clamp_num_configs=False,
    dump_config_yaml=False,
    perf_config=None,
    timeout=0,
    num_retries_on_timeout=0,
    enable_strategy=False,
    harvested_rows=0,
    run_compiler=False,
):
    search_type = SearchType.create_from_string(search_type)
    search_config = SolutionSearchConfig.create_from_search_type(
        search_type=search_type,
        test_module_name=module_name,
        random_seed=random_seed,
        max_num_configs=max_num_configs,
        arch=arch,
        verbose=verbose,
        log_to_file=log_to_file,
        clamp_to_max_num_configs=clamp_num_configs,
        timeout=timeout,
        num_retries_on_timeout=num_retries_on_timeout,
        enable_strategy=enable_strategy,
        harvested_rows=harvested_rows,
        run_compiler=run_compiler,
    )

    # Generate configs
    config_list = generate_z3_configs(search_config=search_config, perf_config=perf_config)

    # Possibly dump the generated configs to a yaml file
    if dump_config_yaml:
        os.makedirs(output_dir, exist_ok=True)
        with open(f"{output_dir}/test_configs.yaml", "w") as f:
            yaml.dump(config_list, f)

    return config_list


def full_test_driver(
    module_name,
    output_dir,
    arch,
    random_seed,
    max_num_configs,
    search_type,
    verbose,
    log_to_file,
    clamp_num_configs,
    dump_config_yaml,
    dry_run,
    timeout,
    num_retries_on_timeout,
    enable_strategy,
    use_hash,
    harvested_rows,
    run_compiler,
):
    config_list = generate_all_configs(
        module_name=module_name,
        output_dir=output_dir,
        arch=arch,
        random_seed=random_seed,
        max_num_configs=max_num_configs,
        search_type=search_type,
        verbose=verbose,
        log_to_file=log_to_file,
        clamp_num_configs=clamp_num_configs,
        dump_config_yaml=dump_config_yaml,
        timeout=timeout,
        num_retries_on_timeout=num_retries_on_timeout,
        enable_strategy=enable_strategy,
        harvested_rows=harvested_rows,
        run_compiler=run_compiler,
    )

    # Import test module that contains solver constraints and test specific config
    test_module = importlib.import_module(module_name)
    # Generate netlists and test dirs
    tests = create_netlists(
        template_yaml=test_module.template_yaml,
        output_dir=f"{get_git_root()}/{output_dir}",
        configs=config_list,
        use_hash=use_hash,
    )

    # Run and grep errors in tests
    if not dry_run:
        for odir, net_yaml in tests:
            cmdline, status = run_tm_test(netlist_yaml=net_yaml, outdir=odir, dry_run=dry_run)
            print(f"Test: {cmdline}")
            print(f"Status: {'Pass' if status else 'Fail'}")
            print(grep_tm_test_error_summary(outdir=odir))

    print(f"Done. Generated {len(config_list)} configs.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=textwrap.dedent(
            """
            Example1: invoke free search (default) for solutions for softmax grayskull
                python generate_tests.py --module-name test_modules.graph_tests.test_softmax --output-dir configs_dir --max-num-configs 100 --dry-run

            Example2: invoke serial sweep for mha wormhole, print info along the way, store test_configs.yaml
                python generate_tests.py --module-name test_modules.graph_tests.test_mha --arch wormhole --output-dir configs_dir --max-num-configs 100 --search-type serial-sweep --dump-config-yaml --verbose --dry-run

            Example2: invoke parallel sweep for mha wormhole, print info along the way
                python generate_tests.py --module-name test_modules.graph_tests.test_mha --arch wormhole --output-dir configs_dir --max-num-configs 100 --search-type parallel-sweep --verbose --dry-run
            """
        ),
    )
    parser.add_argument("--module-name", required=True, help="Test Module Name")
    parser.add_argument(
        "--output-dir", required=True, help="Test output directory relative to root"
    )
    parser.add_argument(
        "--arch",
        default="grayskull",
        type=str,
        help="Name of device architecture, one of: ['grayskull', 'wormhole', 'wormhole_b0']",
    )
    parser.add_argument("--random-seed", default=0, type=int, help="Random seed for solver")
    parser.add_argument(
        "--max-num-configs", default=None, type=int, help="Number of test configs to generate"
    )
    parser.add_argument(
        "--search-type",
        type=str,
        default="default",
        help="One of: ['default', 'serial-sweep', 'parallel-sweep']",
    )
    parser.add_argument(
        "--clamp-num-configs",
        action="store_true",
        default=False,
        help="Clamp sweep combinations to max_num_configs",
    )
    parser.add_argument("--verbose", action="store_true", default=False, help="Print out progress")
    parser.add_argument("--log-to-file", action="store_true", default=False, help="Log to file")
    parser.add_argument(
        "--dry-run", action="store_true", default=False, help="Don't actually run tests"
    )
    parser.add_argument(
        "--dump-config-yaml",
        action="store_true",
        default=False,
        help="Write generated configs to a yaml",
    )
    parser.add_argument(
        "--timeout", default=0, type=int, help="Timeout in seconds for z3 solver, zero==inf"
    )
    parser.add_argument(
        "--num-retries-on-timeout",
        default=0,
        type=int,
        help="Enables retry with different seed if z3 solver timed out",
    )
    parser.add_argument(
        "--enable-strategy",
        action="store_true",
        default=False,
        help="Instantiate z3 solver with predefined strategy (as it can improve z3 solver performance)",
    )
    parser.add_argument(
        "--harvested-rows", default=0, type=int, help="Number of harvested rows on the architecture"
    )
    parser.add_argument(
        "--run-compiler",
        action="store_true",
        default=False,
        help="Does the compiler run as part of test generation",
    )

    args = parser.parse_args(sys.argv[1:])

    if args.arch not in ["grayskull", "wormhole", "wormhole_b0"]:
        raise argparse.ArgumentError(
            None, "--arch must be one of: ['grayskull', 'wormhole', 'wormhole_b0']"
        )

    if args.search_type not in ["default", "serial-sweep", "parallel-sweep"]:
        raise argparse.ArgumentError(
            None, "--search-config must be one of: ['default', 'serial-sweep', 'parallel-sweep']"
        )

    full_test_driver(
        module_name=args.module_name,
        output_dir=args.output_dir,
        arch=args.arch,
        random_seed=args.random_seed,
        max_num_configs=args.max_num_configs,
        search_type=args.search_type,
        verbose=args.verbose,
        log_to_file=args.log_to_file,
        clamp_num_configs=args.clamp_num_configs,
        dry_run=args.dry_run,
        dump_config_yaml=args.dump_config_yaml,
        timeout=args.timeout,
        num_retries_on_timeout=args.num_retries_on_timeout,
        enable_strategy=args.enable_strategy,
        use_hash=1,
        harvested_rows=args.harvested_rows,
        run_compiler=args.run_compiler,
    )
