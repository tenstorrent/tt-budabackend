#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Functions for running comparison of many files.
"""

import os
from dataclasses import dataclass
from filecmp import cmp

from verif.common.runner_utils import WorkerResult, execute_in_parallel
from verif.common.test_utils import find_all_files_in_dir, get_logger

logger = get_logger(__name__)


@dataclass
class CompareFilesWorkerConfig:
    original_file: str
    new_file: str


class CompareFilesRunner:

    def compare_all_outputs(
        original_dir: str, new_dir: str, filename_filter: str, num_samples: int = -1
    ):
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
        """
        all_original_files = find_all_files_in_dir(original_dir, filename_filter)
        all_new_files = find_all_files_in_dir(new_dir, filename_filter)

        logger.info(
            f"Found {len(all_original_files)} files matching {filename_filter} to compare"
        )
        if len(all_original_files) != len(all_new_files):
            logger.fatal(
                f"Number of files are different! There are {len(all_original_files)}"
                f" in {original_dir} and {len(all_new_files)} in {new_dir}."
            )
            return

        if all_original_files != all_new_files:
            logger.fatal(
                f"List of files in {original_dir} and {new_dir} are different!"
            )
            return

        all_worker_args = []
        for original_file_name in all_original_files:
            original_file = os.path.join(original_dir, original_file_name)
            new_file = os.path.join(new_dir, original_file_name)
            all_worker_args.append(CompareFilesWorkerConfig(original_file, new_file))

        if num_samples >= 0:
            all_worker_args = all_worker_args[:num_samples]
            logger.info(f"Selected first {num_samples} files to compare.")

        execute_in_parallel(
            CompareFilesRunner.__compare_file_worker,
            all_worker_args,
            log_file=os.path.join(new_dir, "runner_compare.log"),
        )

    def __compare_file_worker(worker_config: CompareFilesWorkerConfig):
        files_same = cmp(
            worker_config.original_file, worker_config.new_file, shallow=False
        )
        return WorkerResult(
            f"cmp {worker_config.original_file} {worker_config.new_file}",
            0 if files_same else 1,
        )
