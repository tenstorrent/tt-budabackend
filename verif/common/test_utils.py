# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Utility methods.
"""
from __future__ import annotations

import fnmatch
import logging
import os
import threading

import yaml

LOGGER_NAME = "common_tests_logger"
logger = logging.getLogger(LOGGER_NAME)


class ConsoleColors:
    OK_GREEN = "\033[92m"
    WARNING = "\033[95m"
    FAIL = "\033[91m"
    END = "\033[0m"


def print_success(message: str):
    print(f"{ConsoleColors.OK_GREEN}{message}{ConsoleColors.END}")


def print_warning(message: str):
    print(f"{ConsoleColors.WARNING}{message}{ConsoleColors.END}")


def print_fail(message: str):
    print(f"{ConsoleColors.FAIL}{message}{ConsoleColors.END}")


class DeviceArchs:
    GRAYSKULL = "grayskull"
    WORMHOLE_B0 = "wormhole_b0"

    @staticmethod
    def get_all_archs() -> list[str]:
        return [DeviceArchs.GRAYSKULL, DeviceArchs.WORMHOLE_B0]

    @staticmethod
    def is_valid_arch(arch: str) -> bool:
        return arch in DeviceArchs.get_all_archs()


def setup_logger(log_file_path: str):
    if logger.hasHandlers():
        logger.handlers.clear()

    logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter(
        "%(asctime)s - %(filename)s:%(lineno)s - %(levelname)s - %(process)d - %(message)s"
    )

    os.makedirs(os.path.dirname(log_file_path), exist_ok=True)

    # Set up file logging for DEBUG level.
    fh = logging.FileHandler(log_file_path)
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(formatter)
    logger.addHandler(fh)

    # Set up console logging for INFO level.
    console = logging.StreamHandler()
    console.setLevel(logging.INFO)
    console.setFormatter(formatter)
    logger.addHandler(console)


def get_process_shared_logger(name: str, log_file_path: str):
    """
    Returns a logger instance which is used by a single process inside of process pool. During it's lifetime it can be
    configured differently, to log data to different log files.
    """
    process_shared_logger = get_logger(name)
    # Disable error/critical/warning log propagation to stdout.
    process_shared_logger.propagate = False

    logger_handlers = process_shared_logger.handlers
    for handler in logger_handlers:
        process_shared_logger.removeHandler(handler)

    process_shared_logger.addHandler(logging.NullHandler())

    process_shared_logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter(
        "%(asctime)s - %(filename)s:%(lineno)s - %(levelname)s - %(process)d - %(message)s"
    )

    # Set up file logging for DEBUG level.
    fh = logging.FileHandler(log_file_path)
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(formatter)
    process_shared_logger.addHandler(fh)

    return process_shared_logger


def get_logger(name: str):
    return logging.getLogger(f"{LOGGER_NAME}.{name}")


def get_netlist_name(netlist_path: str) -> str:
    return os.path.basename(netlist_path)[:-5]


def get_epoch_dir(results_dir: str, epoch_id: str) -> str:
    return f"{results_dir}/temporal_epoch_{epoch_id}/overlay"


def get_epoch_from_filename(filename: str) -> int:
    """Extracts epoch number from given filename. Works when the epoch is the last thing in the filename.
    Pipegen filter and pipegen output files like this.
    Examples:
        pipegen_0.yaml
        example_dir/blob_25.yaml
        wormhole_b0/1xlink/temporal_epoch_0

    """
    if filename.endswith(".yaml"):
        filename = filename[:-5]
    return int(filename.split("_")[-1])


def create_dir_if_not_exist(dir_path: str):
    if not os.path.exists(dir_path):
        os.makedirs(dir_path)


def create_or_clean_dir(dir_path: str):
    if os.path.exists(dir_path):
        os.system(f"rm -rf {dir_path}/*")
    else:
        create_dir_if_not_exist(dir_path)


def print_thread_info(
    thread_idx: int, lock: threading.Lock, curr_idx: int, start_idx: int, end_idx: int
):
    num_processed = curr_idx - start_idx + 1
    if num_processed % 100 == 0 or curr_idx + 1 == end_idx:
        num_total = end_idx - start_idx
        message = f"Thread {thread_idx} processed {num_processed}/{num_total} samples"
        lock.acquire()
        print(message)
        lock.release()


def get_netlist_arch(netlist_path: str) -> list[str]:
    """Parses architecture names from given netlist."""
    try:
        with open(netlist_path, "r") as input_yaml_file:
            yaml_dict = yaml.safe_load(input_yaml_file.read())
            devices_key = "devices"
            arch_key = "arch"
            if (
                yaml_dict is None
                or devices_key not in yaml_dict
                or arch_key not in yaml_dict[devices_key]
            ):
                return []
            arch = yaml_dict[devices_key][arch_key]
            if not type(arch) is list:
                arch = [arch]

            arch = [x.lower() for x in arch]

            return arch
    except:
        return []


def extract_chip_ids_from_blob_yaml(blob_yaml_path: str) -> list[int]:
    chip_ids = set()
    with open(blob_yaml_path, "r") as file:
        for line in file:
            if line.startswith("  chip_"):
                chip_ids.add(line.split("_")[1])
    return list(chip_ids)


def find_all_files_in_dir(root_path: str, file_filter: str) -> list[str]:
    """
    Traversed all files in given directory and its subdirectories and returns list of files which match given filter.

    Parameters
    ----------
    root_path: str
        Root path to start searching for files.
    file_filter: str
        File filter to match files using fnmatch module.

    Return
    ------
    list[str]
        Files passing the filter.
        The files are returned with relative path from the root_path passed.
    """
    matches = []
    for path, _, filenames in os.walk(root_path):
        relative_path = os.path.relpath(path, root_path)
        relative_filenames = [
            os.path.join(relative_path, filename) for filename in filenames
        ]
        matches.extend(fnmatch.filter(relative_filenames, file_filter))
    return matches


def extract_arch_from_path(path: str) -> str:
    for arch in DeviceArchs.get_all_archs():
        if arch + os.path.sep in path:
            return arch
