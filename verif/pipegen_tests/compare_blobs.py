# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
import os

from blob_comparator import StreamGraphComparisonStrategy
from pipegen_refactor_test import DEFAULT_SG_COMPARISON_STRATEGY, compare_blob_yamls

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--original-blob", type=str, required=True)
    parser.add_argument("--generated-blob", type=str, required=True)
    parser.add_argument("--log-path", type=str, required=True)
    parser.add_argument(
        "--sg-comparison-strategy",
        type=str,
        required=False,
        default=DEFAULT_SG_COMPARISON_STRATEGY,
        help=f"How to compare stream graphs, default is {DEFAULT_SG_COMPARISON_STRATEGY}",
    )
    args = parser.parse_args()

    if os.path.exists(args.log_path):
        os.remove(args.log_path)

    print(
        f"Blob comparator is using: '{args.sg_comparison_strategy}' as the comparison strategy"
    )
    match = compare_blob_yamls(
        args.original_blob,
        args.generated_blob,
        args.log_path,
        StreamGraphComparisonStrategy[args.sg_comparison_strategy],
    )

    if match:
        print(f"{args.original_blob} and {args.generated_blob} match")
    else:
        print(
            f"{args.original_blob} and {args.generated_blob} don't match, "
            f"take a look at {args.log_path}"
        )
