# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Testing between pipegen2 from master and pipegen2 with your changes.

Example commands for creating a sample with netlists:
    mkdir -p out/netlists
    cp verif/graph_tests/netlists/*.yaml out/netlists/

Build pipegen on master (remember to use stash if you have any changes):
    git checkout master
    verif/pipegen_tests/build_all_archs.sh build_master
    git checkout -

Build pipegen on your change and copy master_pipegen:
    verif/pipegen_tests/build_all_archs.sh
    cp build_master/wormhole_b0/bin/pipegen2 build_archs/wormhole_b0/bin/pipegen2_master
    cp build_master/grayskull/bin/pipegen2 build_archs/grayskull/bin/pipegen2_master

Create python virtual environment:
    verif/pipegen_tests/create_env.sh
    source verif/pipegen_tests/env/bin/activate

This script run example:
    python3 verif/pipegen_tests/compare_with_master.py --out out --netlists out/netlists \
        --builds-dir build_archs --pipegens-bin-dir build_archs/wormhole_b0/bin \
        --arch wormhole_b0
"""
import argparse
import os

from pipegen_refactor_test import (
    DEFAULT_SG_COMPARISON_STRATEGY,
    FILTERED_YAMLS_OUT_DIR_NAME,
    StreamGraphComparisonStrategy,
    compare_pipegens_on_yamls,
)

from verif.common.pipegen_yaml_filter import FilterType
from verif.common.runner_net2pipe import Net2PipeRunner
from verif.common.runner_pipegen_filter import PipegenFilterRunner
from verif.common.runner_utils import DEFAULT_BIN_DIR, DEFAULT_TOP_LEVEL_BUILD_DIR
from verif.common.test_utils import setup_logger

NET2PIPE_OUT_DIR_NAME = "net2pipe_out"

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--out",
        type=str,
        required=True,
        default=None,
        help="Folder where output data are stored.",
    )
    parser.add_argument(
        "--netlists",
        type=str,
        required=True,
        default=None,
        help="Folder where netlists are stored",
    )
    parser.add_argument(
        "--builds-dir",
        type=str,
        required=False,
        default=DEFAULT_TOP_LEVEL_BUILD_DIR,
        help=f"Top level 'build' folder which contains subfolders for different architectures "
        f"(f.e. 'grayskull/bin' or 'wormhole_b0/bin'). DEFAULT: {DEFAULT_TOP_LEVEL_BUILD_DIR}",
    )
    parser.add_argument(
        "--pipegens-bin-dir",
        type=str,
        required=False,
        default=DEFAULT_BIN_DIR,
        help=f"Folder containing pipegen binaries (for example 'build/bin' or 'build/grayskull/bin'). "
        f"DEFAULT: {DEFAULT_BIN_DIR}",
    )
    parser.add_argument(
        "--arch",
        type=str,
        required=True,
        default="",
        help="Specific architecture for which net2pipe and pipegens were built",
    )
    parser.add_argument(
        "--num-samples",
        type=int,
        required=False,
        default=-1,
        help="Number of samples to filter or test. DEFAULT: all existing samples will be used.",
    )
    parser.add_argument(
        "--sg-comparison-strategy",
        type=str,
        required=False,
        default=DEFAULT_SG_COMPARISON_STRATEGY,
        help=f"How to compare stream graphs. DEFAULT: {DEFAULT_SG_COMPARISON_STRATEGY}",
    )
    parser.add_argument("--force-run-filter", default=False, action="store_true")

    args = parser.parse_args()
    log_path = os.path.join(args.out, f"compare_with_master.log")
    setup_logger(log_path)

    # Pick some or leave as is to run all existing filters starting from UnicastPipe.
    filters = [f for f in FilterType if f.value >= FilterType.UnicastPipe.value]

    # Don't filter anything, all pipes pass this filter.
    # filters = [FilterType.Everything]

    # Run filters.
    for f in filters:
        filtered_yamls_dir_name = f"{FILTERED_YAMLS_OUT_DIR_NAME}_{f.name}"

        print(f"Running filter {f.name}")

        try:
            net2pipe_out_dir = os.path.join(args.out, NET2PIPE_OUT_DIR_NAME)
            Net2PipeRunner.generate_net2pipe_outputs(
                args.netlists,
                net2pipe_out_dir,
                args.builds_dir,
                overwrite=args.force_run_filter,
            )

            PipegenFilterRunner.filter_pipegen_yamls(
                net2pipe_out_dir,
                args.out,
                f,
                args.num_samples,
                overwrite=args.force_run_filter,
            )
        except Exception as e:
            print(f"Exception occurred during filtering {f.name}: {e}")

    # Run comparison.
    failed_filters = []

    for f in filters:
        print(f"Comparing {f.name}")

        filtered_dir = f"{args.out}/{FILTERED_YAMLS_OUT_DIR_NAME}_{f.name}"
        if not os.path.exists(filtered_dir):
            print(f"Directory {filtered_dir} does not exist, nothing to compare.")
            continue

        comparison_dir = f"{args.out}/comparison_with_master_{args.arch}/{f.name}"
        result = False

        try:
            result = compare_pipegens_on_yamls(
                filtered_dir,
                comparison_dir,
                args.arch,
                args.pipegens_bin_dir,
                args.num_samples,
                StreamGraphComparisonStrategy[args.sg_comparison_strategy],
            )
        except Exception as e:
            print(f"Exception occurred during comparison {filtered_dir}: {e}")

        if not result:
            failed_filters.append(f.name)

    if failed_filters:
        print("!ATTENTION! Some filters failed comparison:")

        for filter_name in failed_filters:
            print(f"\t{filter_name}")
    else:
        print("All filters passed comparison.")
