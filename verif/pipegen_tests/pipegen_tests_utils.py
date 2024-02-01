#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Utility methods.
"""
from __future__ import annotations

import logging
import os
import threading
import yaml

LOGGER_NAME = "pipegen_tests_logger"
logger = logging.getLogger(LOGGER_NAME)

def setup_logger(log_file_path: str):
    if logger.hasHandlers():
        logger.handlers.clear()

    logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter('%(asctime)s - %(filename)s:%(lineno)s - %(levelname)s - %(process)d - %(message)s')

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
    configured diffrently, to log data to different log files.
    """
    process_shared_logger = get_logger(name)
    # Disable error/critical/warning log propagation to stdout.
    process_shared_logger.propagate = False

    logger_handlers = process_shared_logger.handlers
    for handler in logger_handlers:
        process_shared_logger.removeHandler(handler)

    process_shared_logger.addHandler(logging.NullHandler())

    process_shared_logger.setLevel(logging.DEBUG)
    formatter = logging.Formatter('%(asctime)s - %(filename)s:%(lineno)s - %(levelname)s - %(process)d - %(message)s')

    # Set up file logging for DEBUG level.
    fh = logging.FileHandler(log_file_path)
    fh.setLevel(logging.DEBUG)
    fh.setFormatter(formatter)
    process_shared_logger.addHandler(fh)

    return process_shared_logger

def get_logger(name: str):
    return logging.getLogger(f"{LOGGER_NAME}.{name}")

class ConsoleColors:
    OK_GREEN = '\033[92m'
    WARNING = '\033[93m'
    FAIL = '\033[91m'
    END = '\033[0m'

def print_success(message: str):
    print(f"{ConsoleColors.OK_GREEN}{message}{ConsoleColors.END}")

def print_warning(message: str):
    print(f"{ConsoleColors.WARNING}{message}{ConsoleColors.END}")

def print_fail(message: str):
    print(f"{ConsoleColors.FAIL}{message}{ConsoleColors.END}")

def get_netlist_name(netlist_path: str) -> str:
    return os.path.basename(netlist_path)[:-5]

def get_epoch_dir(results_dir: str, epoch_id: str) -> str:
    return f"{results_dir}/temporal_epoch_{epoch_id}/overlay"

def create_dir_if_not_exist(dir_path: str):
    if not os.path.exists(dir_path):
        os.makedirs(dir_path)

def create_or_clean_dir(dir_path: str):
    if os.path.exists(dir_path):
        os.system(f"rm -rf {dir_path}/*")
    else:
        create_dir_if_not_exist(dir_path)

def print_thread_info(thread_idx: int, lock: threading.Lock, curr_idx: int, start_idx: int,
                      end_idx: int):
    num_processed = curr_idx - start_idx + 1
    num_total = end_idx - start_idx
    if num_processed % 100 == 0:
        lock.acquire()
        print(f"Thread {thread_idx} processed {num_processed}/{num_total} samples")
        lock.release()

def get_netlist_arch(netlist_path: str) -> list[str]:
    """Parses architecture names from given netlist."""
    try:
        with open(netlist_path, 'r') as input_yaml_file:
            yaml_dict = yaml.safe_load(input_yaml_file.read())
            devices_key = 'devices'
            arch_key = 'arch'
            if (yaml_dict is None or
                devices_key not in yaml_dict or
                arch_key not in yaml_dict[devices_key]):
                return None
            arch = yaml_dict[devices_key][arch_key]
            if not type(arch) is list:
                arch = [arch]

            arch = [x.lower() for x in arch]

            return arch
    except:
        return None
