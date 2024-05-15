#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Functions for running comparison of many files.
"""

import os
from dataclasses import dataclass
from filecmp import cmp
from typing import Callable, List

from verif.common.runner_utils import WorkerResult, execute_in_parallel
from verif.common.test_utils import find_all_files_in_dir, get_logger

logger = get_logger(__name__)


@dataclass
class CompareFilesWorkerConfig:
    original_file: str
    new_file: str


class CompareFilesRunner:

    def compare_file_worker(worker_config: CompareFilesWorkerConfig):
        files_same = cmp(
            worker_config.original_file, worker_config.new_file, shallow=False
        )
        return WorkerResult(
            f"cmp {worker_config.original_file} {worker_config.new_file}",
            0 if files_same else 1,
        )

    def compare_all_outputs(
        original_dir: str,
        new_dir: str,
        filename_filter: str,
        num_samples: int = -1,
        allow_different_number_of_files: bool = False,
        comparison_function: Callable[
            [CompareFilesWorkerConfig], WorkerResult
        ] = compare_file_worker,
    ) -> bool:
        """Compares files found in original_dir and new_dir, on all levels, matching filename_filter.
        Parameters
        ----------
        original_dir : str
            Directory with original files.
        new_dir : str
            Directory with new files.
        filename_filter : str
            Filter to be used with fnmatch module for filenames to compare.
        num_samples : int
            Number of file pairs to compare. -1 will compare all files.
        allow_different_number_of_files: bool
            If True, allow different number of files between original and new file list. Compare only existing files.
        comparison_function: function
            Function to be used for comparison. Defaults to simple byte comparison.

        Returns
        -------
        bool
            True if all compared files are the same, False otherwise.
        """
        all_original_files = find_all_files_in_dir(original_dir, filename_filter)

        logger.info(f"Found {len(all_original_files)} files in {original_dir}.")
        original_file_list = []
        new_file_list = []
        for original_file_name in all_original_files:
            new_file = os.path.join(new_dir, original_file_name)
            if os.path.exists(new_file):
                original_file_list.append(
                    os.path.join(original_dir, original_file_name)
                )
                new_file_list.append(new_file)
            else:
                if allow_different_number_of_files:
                    continue
                else:
                    logger.fatal(
                        f"File {new_file} does not exist, but it's pair in {original_dir} does."
                    )
                    return False

        return CompareFilesRunner.compare_all_outputs_lists(
            original_file_list, new_file_list, new_dir, num_samples, comparison_function
        )

    def compare_all_outputs_lists(
        original_file_list: List[str],
        new_file_list: List[str],
        log_out_dir: str,
        num_samples: int = -1,
        comparison_function: Callable[
            [CompareFilesWorkerConfig], WorkerResult
        ] = compare_file_worker,
    ) -> bool:
        """Compares files provided in original_file_list and new_file_list, using provided comparison_function.
        Parameters
        ----------
        original_file_list : List[str]
            Directory with original files.
        new_file_list : List[str]
            Directory with new files.
        log_out_dir : str
            Output directory for logs.
        num_samples : int
            Number of file pairs to compare. -1 will compare all files.
        comparison_function: function
            Function to be used for comparison. Defaults to simple byte comparison.

        Returns
        -------
        bool
            True if all compared files are the same, False otherwise.
        """
        if len(original_file_list) != len(new_file_list):
            logger.fatal(
                f"Number of files are different! There are {len(original_file_list)}"
                f" in original list and {len(new_file_list)} in new list."
            )
            return False

        all_worker_args = []
        for idx in range(len(original_file_list)):
            original_file = original_file_list[idx]
            new_file = new_file_list[idx]
            all_worker_args.append(CompareFilesWorkerConfig(original_file, new_file))

        if num_samples >= 0:
            all_worker_args = all_worker_args[:num_samples]
            logger.info(f"Selected first {num_samples} files to compare.")

        results = execute_in_parallel(
            comparison_function,
            all_worker_args,
            log_file=os.path.join(log_out_dir, "runner_compare.log"),
        )

        return all(result.error_code == 0 for result in results)
