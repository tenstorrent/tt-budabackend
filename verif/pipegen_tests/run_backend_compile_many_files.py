#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Script used to massively produce output blob hexes from netlists.

Use verif/pipegen_tests/build_all_archs.sh to build the required structure of binaries,
which is passed to this script in --builds_dir argument.

Example vscode launch config:
{
    "name": "Python hex generator",
    "type": "python",
    "request": "launch",
    "python": "${workspaceFolder}/verif/pipegen_tests/env/bin/python",
    "program": "verif/pipegen_tests/run_backend_compile_many_files.py",
    "console": "integratedTerminal",
    "args": [
        "--builds_dir",
        "build_archs",
        "--log_out_root",
        "out/",
        "--netlists",
        "out/output_netlist_collector",
        "--out_net2pipe",
        "out/output_net2pipe",
        "--out_pipegen_filter",
        "out/output_filter",
        "--out_pipegen",
        "out/output_pipegen",
        "--out_blobgen",
        "out/output_blobgen",
        "--blobgen_path",
        "./src/overlay/blob_gen.rb",
        // "--overwrite",
        // "--num_netlists",
        // "20"
    ]
},
Example command line:
    python3 verif/pipegen_tests/run_backend_compile_many_files.py --builds_dir build_archs --log_out_root out/ \
    --netlists out/output_netlist_collector --out_net2pipe out/output_net2pipe --out_pipegen_filter out/output_filter \
    --out_pipegen out/output_pipegen --out_blobgen out/output_blobgen --blobgen_path ./src/overlay/blob_gen.rb

Note: You can safely omit --out_pipegen_filter argument if you don't want to filter the pipegen yamls. The pipegen part
of the script will still work with the net2pipe output.
    
"""
import argparse
import os
from datetime import datetime

from verif.common.pipegen_yaml_filter import FilterType
from verif.common.runner_blobgen import BlobgenRunner
from verif.common.runner_net2pipe import Net2PipeRunner
from verif.common.runner_pipegen import PipegenRunner
from verif.common.runner_pipegen_filter import PipegenFilterRunner
from verif.common.runner_utils import DEFAULT_TOP_LEVEL_BUILD_DIR
from verif.common.test_utils import setup_logger

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)

    parser.add_argument(
        "--netlists",
        type=str,
        required=False,
        default=None,
        help="Folder where netlists are stored",
    )
    parser.add_argument(
        "--builds_dir",
        type=str,
        required=True,
        default=DEFAULT_TOP_LEVEL_BUILD_DIR,
        help=f"Top level 'build' folder which contains subfolders for different architectures "
        f"(f.e. 'grayskull/bin' or 'wormhole_b0/bin'). DEFAULT: {DEFAULT_TOP_LEVEL_BUILD_DIR}",
    )
    parser.add_argument(
        "--log_out_root",
        type=str,
        required=True,
        default=None,
        help=f"Log output directory",
    )
    parser.add_argument(
        "--out_net2pipe",
        type=str,
        required=False,
        default=None,
        help="Folder where net2pipe output data are stored.",
    )
    parser.add_argument(
        "--out_pipegen_filter",
        type=str,
        required=False,
        default=None,
        help="Folder where pipegen filter output data are stored.",
    )
    parser.add_argument(
        "--out_pipegen",
        type=str,
        required=False,
        default=None,
        help="Folder where pipegen output data are stored.",
    )
    parser.add_argument(
        "--out_blobgen",
        type=str,
        required=False,
        default=None,
        help="Folder where blobgen output data are stored.",
    )
    parser.add_argument(
        "--num_netlists",
        type=int,
        required=False,
        default=-1,
        help="Number of netlists to process.",
    )
    parser.add_argument(
        "--blobgen_path",
        type=str,
        required=False,
        default=None,
        help="Path to blobgen script to use.",
    )

    parser.add_argument("--overwrite", default=False, action="store_true")

    args = parser.parse_args()

    timestamp = datetime.now().strftime("%m_%d_%Y_%H_%M_%S")
    setup_logger(
        os.path.join(args.log_out_root, f"run_backend_compile_{timestamp}.log")
    )

    pipegen_filters = [FilterType.Everything]

    if args.netlists and args.out_net2pipe:
        Net2PipeRunner.generate_net2pipe_outputs(
            args.netlists,
            args.out_net2pipe,
            args.builds_dir,
            args.num_netlists,
            overwrite=args.overwrite,
        )

    if args.out_net2pipe and args.out_pipegen_filter:
        for filter in pipegen_filters:
            PipegenFilterRunner.filter_pipegen_yamls(
                args.out_net2pipe,
                args.out_pipegen_filter,
                filter,
                overwrite=args.overwrite,
            )

    # Note that generate_pipegen_outputs works both with net2pipe output and pipegen filter output.
    if args.out_pipegen_filter and args.out_pipegen:
        PipegenRunner.generate_pipegen_outputs(
            args.out_pipegen_filter,
            args.out_pipegen,
            args.builds_dir,
            overwrite=args.overwrite,
        )
    elif args.out_net2pipe and args.out_pipegen:
        PipegenRunner.generate_pipegen_outputs(
            args.out_net2pipe,
            args.out_pipegen,
            args.builds_dir,
            overwrite=args.overwrite,
        )

    if args.out_pipegen and args.out_blobgen:
        BlobgenRunner.generate_blobgen_outputs(
            args.out_pipegen,
            args.out_blobgen,
            blobgen_rb_path=args.blobgen_path,
            overwrite=args.overwrite,
        )
