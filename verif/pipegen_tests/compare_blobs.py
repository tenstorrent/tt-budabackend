# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
import os

from blob_comparator import BlobComparator, StreamGraphComparisonStrategy
from ruamel.yaml import YAML

from verif.common.runner_compare import CompareFilesWorkerConfig
from verif.common.runner_utils import WorkerResult
from verif.common.test_utils import get_logger

logger = get_logger(__name__)

DEFAULT_SG_COMPARISON_STRATEGY = StreamGraphComparisonStrategy.edges.name


def compare_blob_yamls(
    original_blob_yaml_path: str,
    new_blob_yaml_path: str,
    comparison_log_path: str,
    sg_comparison_strategy: StreamGraphComparisonStrategy,
):
    """Returns True if original and new blobs fully match each other."""
    try:
        yaml = YAML(typ="safe")
        orig_blob_yaml = yaml.load(open(original_blob_yaml_path))
        new_blob_yaml = yaml.load(open(new_blob_yaml_path))
        return BlobComparator(
            orig_blob_yaml, new_blob_yaml, comparison_log_path
        ).compare(sg_comparison_strategy)
    except Exception as e:
        logger.info(
            f"Exception occurred during comparison of ({original_blob_yaml_path} vs "
            f"{new_blob_yaml_path}) \n {e}"
        )
        return False


def read_comparison_log_file(log_file_path: str) -> str:
    if not os.path.exists(log_file_path):
        return ""
    with open(log_file_path, "r") as log_file:
        message = log_file.read().strip()
    if message:
        message = f"\n{message}"
    return message


def remove_comparison_log_file(log_file_path: str):
    if not os.path.exists(log_file_path):
        return
    os.remove(log_file_path)


class BlobComparatorCaller:
    """
    Objects of this class can be called like functions:

        blob_comparator_caller = BlobComparatorCaller(StreamGraphComparisonStrategy.edges)
        comparison_result = blob_comparator_caller(example_worker_config)

    This class exists so its objects can capture `sg_comparison_strategy`. That way we can use these objects for the
    purposes where callable (a function) is expected which accept only `CompareFilesWorkerConfig`:

        def func(comparison function: Callable[[CompareFilesWorkerConfig], WorkerResult]):
            ...
            compare_result = comparison_function(worker_config)
            ...

        func(blob_comparator_caller)

    This could've been done using a lambda, as such:

        blob_comparator_caller = lambda worker_config: compare_blob_yamls(worker_config, sg_comparison_strategy)
        func(blob_comparator_caller)

    The problem with that is that `func` passes this callable further to multiprocessing library, which requires this
    callable to be in global scope, which is not true for local lambdas, but is true for global classes like this.
    """

    def __init__(self, sg_comparison_strategy: StreamGraphComparisonStrategy):
        self.sg_comparison_strategy = sg_comparison_strategy
        self.__name__ = self.__class__.__name__

    def __call__(self, worker_config: CompareFilesWorkerConfig):
        log_path = f"{os.path.splitext(worker_config.new_file)[0]}_comparison.log"
        match = compare_blob_yamls(
            worker_config.original_file,
            worker_config.new_file,
            log_path,
            self.sg_comparison_strategy,
        )

        file_message = read_comparison_log_file(log_path)
        if match:
            logger.info(
                f"Successfully compared blobs ({worker_config.original_file} vs {worker_config.new_file})"
                f"{file_message}"
            )
        else:
            logger.error(
                f"Failed comparing blobs ({worker_config.original_file} vs {worker_config.new_file})"
                f"{file_message}"
            )

        remove_comparison_log_file(log_path)

        blob_compare_script_command = (
            "verif/pipegen_tests/compare_blobs.py "
            + f"--original-blob {worker_config.original_file} "
            + f"--generated-blob {worker_config.new_file} "
            + "--log-path compare_blob.log "
            + f"--sg-comparison-strategy {self.sg_comparison_strategy}"
        )
        return WorkerResult(blob_compare_script_command, 0 if match else 0)


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
