#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Script used to compare many files in two locations.
It can be used to compare hex blobs, yaml files, etc.
You can use run_backend_compile_many_files.py to generate the files to compare.

Example vscode launch config:
{
    "name": "Python hex comparator",
    "type": "python",
    "request": "launch",
    "python": "${workspaceFolder}/verif/pipegen_tests/env/bin/python",
    "program": "verif/pipegen_tests/run_compare_many_files.py",
    "console": "integratedTerminal",
    "args": [
        "--log_out_root",
        "out/",
        "--original_dir",
        "out/output_blobgen",
        "--new_dir",
        "out/new/output_blobgen",
        "--filename_filter",
        "*.hex",
        // "--num_samples",
        // "500",
    ]
}
Example command lines:
python3 verif/pipegen_tests/run_compare_many_files.py --log_out_root out/  --original_dir out/output_blobgen \
    --new_dir out/new/output_blobgen --filename_filter "*.hex"
python3 verif/pipegen_tests/run_compare_many_files.py --log_out_root out/  --original_dir out/output_pipegen \
    --new_dir out/new/output_pipegen --filename_filter "*.yaml"
"""
from __future__ import annotations

import argparse
import os
from datetime import datetime

from compare_blobs import BlobComparatorCaller

from verif.common.runner_compare import CompareFilesRunner
from verif.common.test_utils import get_logger, setup_logger

logger = get_logger(__name__)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--log_out_root",
        type=str,
        required=True,
        default=None,
        help=f"Log output directory",
    )
    parser.add_argument(
        "--original_dir",
        type=str,
        required=True,
        default=None,
        help="Folder where original files are located.",
    )
    parser.add_argument(
        "--new_dir",
        type=str,
        required=True,
        default=None,
        help=f"Folder where new files are located.",
    )
    parser.add_argument(
        "--filename_filter",
        type=str,
        required=True,
        default=None,
        help=f"Filename filter to be used to find files of interest.",
    )
    parser.add_argument(
        "--num_samples",
        type=int,
        required=False,
        default=-1,
        help="Number of samples to compare. DEFAULT: all existing files will be used.",
    )
    parser.add_argument(
        "--sg_comparison_strategy",
        type=str,
        required=False,
        default=None,
        help=(
            "How to compare stream graphs in blob.yamls. "
            "Note that this only makes sense in case of comparing blob.yamls. "
            "If this is not defined, a default byte-to-byte comparison is used. "
            "If it is defined, blob_comparator will be invoked. "
            "DEFAULT: None"
        ),
    )

    args = parser.parse_args()

    timestamp = datetime.now().strftime("%m_%d_%Y_%H_%M_%S")
    setup_logger(os.path.join(args.log_out_root, f"run_compare_{timestamp}.log"))

    comparison_function = (
        BlobComparatorCaller(args.sg_comparison_strategy)
        if args.sg_comparison_strategy
        else CompareFilesRunner.compare_file_worker
    )

    CompareFilesRunner.compare_all_outputs(
        args.original_dir,
        args.new_dir,
        args.filename_filter,
        args.num_samples,
        comparison_function=comparison_function,
    )
