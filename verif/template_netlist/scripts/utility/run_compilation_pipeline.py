# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
import os
from test_modules.common.compilation_pipeline_runner import CompilationPipelineRunner


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--test-dir", type=str, required=True, help="Dir containing test netlists.")
    parser.add_argument(
        "--out-dir",
        type=str,
        required=False,
        default=None,
        help="Folder where output data are stored",
    )
    parser.add_argument(
        "--arch",
        type=str,
        required=False,
        default="grayskull",
        help="Commands [wormhole|grayskull]",
    )
    parser.add_argument(
        "--save-output",
        default=False,
        action="store_true",
        help="Keep net2pipe and pipegen output.",
    )
    parser.add_argument(
        "--run-blobgen",
        action="store_true",
        default=False,
        help="Run blobgen as part of the compilation pipeline",
    )
    args = parser.parse_args()

    out_dir = args.out_dir or os.path.join(args.test_dir, "net2pipe_output")

    CompilationPipelineRunner.run_compilation(
        test_dir=args.test_dir,
        out_dir=out_dir,
        arch=args.arch,
        clear_output=not args.save_output,
        run_blobgen=args.run_blobgen,
    )


if __name__ == "__main__":
    main()
