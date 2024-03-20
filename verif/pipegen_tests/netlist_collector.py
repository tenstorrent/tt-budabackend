#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Collects netlists in a given folder recursively, that pass net2pipe successfully.
"""
from __future__ import annotations

import argparse
import math
import os
import random
import threading
import time

from pipegen_runner import DeviceArchs, run_net2pipe
from pipegen_tests_utils import *

MAX_NUM_THREADS = 8

non_netlist_names = ["netlist_queues", "blob", "pipegen", "queue_to_consumer"]

invalid_netlists_names = [
    # This netlist has overlapping DRAM queues.
    "default_netlist"
]


def validate_netlist(
    netlist_path: str, tmp_output_dir: str, builds_dir: str, arch: str
) -> bool:
    """Checks if given netlist passes net2pipe."""
    netlist_name = get_netlist_name(netlist_path)
    out_dir = f"{tmp_output_dir}/{netlist_name}/{arch}"
    assert not os.path.exists(out_dir)
    os.makedirs(out_dir)
    # Expecting to find subfolders for different architectures inside builds_dir.
    bin_dir = f"{builds_dir}/{arch}/bin"
    assert os.path.exists(bin_dir)
    net2pipe_retcode, _ = run_net2pipe(netlist_path, out_dir, arch, bin_dir)
    os.system(f"rm -rf {out_dir}/*")
    return net2pipe_retcode == 0


def validate_netlists(
    netlist_paths: list[str],
    tmp_output_dir: str,
    builds_dir: str,
    start_idx: int,
    end_idx: int,
    netlist_valid: list[bool],
    netlist_failing: list[bool],
    thread_idx: int,
    lock: threading.Lock,
):
    """Checks which netlists in a subset of netlist paths pass net2pipe."""
    for idx in range(start_idx, end_idx):
        netlist_path = netlist_paths[idx]
        archs = get_netlist_arch(netlist_path)
        has_valid_archs = False
        if archs is not None:
            for arch in archs:
                if not DeviceArchs.is_valid_arch(arch):
                    continue
                has_valid_archs = True
                if not validate_netlist(netlist_path, tmp_output_dir, builds_dir, arch):
                    netlist_valid[idx] = False
                    netlist_failing[idx] = True
        netlist_valid[idx] = netlist_valid[idx] and has_valid_archs
        print_thread_info(thread_idx, lock, idx, start_idx, end_idx)


def find_netlists(root_dir: str, existing_names: set[str]) -> list[str]:
    """Finds netlists recursively starting from the given root folder.

    Parameters
    ----------
    root_dir: str
        Root folder to search for netlists.
    existing_names: set[str]
        Set of names of already found netlists.

    Returns
    -------
    list[str]
        List of found netlist paths.
    """
    netlist_paths = []

    for entry_path in os.listdir(root_dir):
        # Skip dbd/dbd/dbd... symbolic link infinite loop.
        if entry_path == "dbd":
            continue

        full_entry_path = os.path.join(root_dir, entry_path)
        if os.path.isdir(full_entry_path):
            netlist_paths.extend(find_netlists(full_entry_path, existing_names))
        elif full_entry_path.endswith(".yaml"):
            netlist_name = get_netlist_name(entry_path)
            if (
                netlist_name in existing_names
                or netlist_name in non_netlist_names
                or netlist_name in invalid_netlists_names
            ):
                continue
            netlist_paths.append(full_entry_path)
            existing_names.add(netlist_name)

    return netlist_paths


def filter_valid_netlists(
    netlist_paths: list[str],
    out_dir: str,
    builds_dir: str,
    num_threads: int,
    valid_netlist_paths: list[str],
    failing_netlist_paths: list[str],
):
    """Runs net2pipe on given netlists, and returns list of those who pass successfully.

    Parameters
    ----------
    netlist_paths: list[str]
        List of netlist paths.
    out_dir: str
        Folder where to store temporary net2pipe outputs.
    builds_dir: str
        Folder with net2pipe builds for different architectures.
    num_threads: int
        Number of threads to utilize for net2pipe run.

    Returns
    -------
    list[str]
    """
    tmp_output_dir = f"{out_dir}/net2pipe_outputs"
    if not os.path.exists(tmp_output_dir):
        os.makedirs(tmp_output_dir)

    netlist_valid = [True for _ in netlist_paths]
    netlist_failing = [False for _ in netlist_paths]

    num_netlists_per_thread = math.ceil(len(netlist_paths) / num_threads)
    lock = threading.Lock()
    threads = []
    for thread_idx in range(num_threads):
        start_idx = thread_idx * num_netlists_per_thread
        end_idx = min(start_idx + num_netlists_per_thread, len(netlist_paths))
        threads.append(
            threading.Thread(
                target=validate_netlists,
                args=(
                    netlist_paths,
                    tmp_output_dir,
                    builds_dir,
                    start_idx,
                    end_idx,
                    netlist_valid,
                    netlist_failing,
                    thread_idx,
                    lock,
                ),
            )
        )

    for thread in threads:
        thread.start()

    for thread in threads:
        thread.join()

    os.system(f"rm -rf {tmp_output_dir}")

    for idx, valid in enumerate(netlist_valid):
        if valid:
            valid_netlist_paths.append(netlist_paths[idx])
        if netlist_failing[idx]:
            failing_netlist_paths.append(netlist_paths[idx])


def log_failing_netlists(failing_netlist_paths: list[str], log_path: str):
    """Logs paths of netlists that failed net2pipe."""
    with open(log_path, "w") as log_file:
        for netlist_path in failing_netlist_paths:
            log_file.write(f"{netlist_path}\n")


def copy_netlists(netlist_paths: list[str], out_dir: str):
    """Copies found netlists to output folder.

    Parameters
    ----------
    netlist_paths: str
        List of found valid netlist paths.
    out_dir: str
        Folder where to store the found netlists.
    """
    if not os.path.exists(out_dir):
        os.makedirs(out_dir)
    for netlist_path in netlist_paths:
        os.system(f"cp {netlist_path} {out_dir}/{os.path.basename(netlist_path)}")


def collect_netlists(root_dir: str, out_dir: str, builds_dir: str, num_threads: int):
    """Collects netlists in a given folder recursively, that pass net2pipe successfully.

    Parameters
    ----------
    root_dir: str
        Root folder to search for netlists.
    out_dir: str
        Folder where to store the found netlists.
    builds_dir: str
        Folder with net2pipe builds for different architectures.
    num_threads: int
        Number of threads to utilize.
    """
    print("\nSearching for netlists...")
    netlist_paths = find_netlists(root_dir, set())
    if len(netlist_paths) == 0:
        print_fail("No netlists found in a given folder!")
        return
    print_success(f"Found {len(netlist_paths)} netlists")

    # Shuffling the list to ensure no thread is stuck on the same folder
    # which might contain non-netlist yamls.
    random.shuffle(netlist_paths)

    print("\nValidating netlists...")
    valid_netlist_paths = []
    failing_netlist_paths = []
    filter_valid_netlists(
        netlist_paths,
        out_dir,
        builds_dir,
        num_threads,
        valid_netlist_paths,
        failing_netlist_paths,
    )
    if len(valid_netlist_paths) == 0:
        print_fail("No netlists that pass net2pipe found in a given folder!")
        return
    print_success(f"{len(valid_netlist_paths)} netlists were valid and passed net2pipe")

    if len(failing_netlist_paths) > 0:
        print_warning(f"{len(failing_netlist_paths)} netlists failed net2pipe")
        log_failing_netlists(failing_netlist_paths, f"{root_dir}/invalid_netlists.log")

    print("\nCopying netlists...")
    copy_netlists(valid_netlist_paths, out_dir)
    print_success(f"Copied {len(valid_netlist_paths)} netlists to output folder.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out-dir",
        type=str,
        required=True,
        help="Folder where to store the found netlists",
    )
    parser.add_argument(
        "--builds-dir",
        type=str,
        required=True,
        help="Folder with net2pipe builds for different architectures",
    )
    parser.add_argument(
        "--num-threads",
        type=int,
        required=False,
        default=8,
        help="Number of threads to utilize",
    )
    args = parser.parse_args()

    assert args.num_threads > 0 and args.num_threads <= MAX_NUM_THREADS

    start_time = time.ctime()

    collect_netlists(os.getcwd(), args.out_dir, args.builds_dir, args.num_threads)

    print(f"\nStarted  at {start_time}")
    print(f"Finished at {time.ctime()}")
