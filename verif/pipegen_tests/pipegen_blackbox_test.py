#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Blackbox testing of net2pipe and pipegen component.
"""
from __future__ import annotations

import argparse
import math
import os
import sys
import threading
import time

from pipegen_runner import DeviceArchs, run_net2pipe, run_pipegen1, run_diff
from pipegen_tests_utils import *

MAX_NUM_THREADS = 8
RESULTS_DIR_NAME = "results"
NETLISTS_DIR_NAME = "netlists"
BASELINE_FILE_NAME = "baseline.zip"
TEST_LOG_FILE_NAME = "run_log.txt"

class TestDriver:
    def __init__(self, arch: str, base_dir: str, out_dir: str, num_threads: int):
        self.__arch = arch
        self.__base_dir = base_dir
        self.__out_dir = out_dir
        create_dir_if_not_exist(out_dir)
        create_dir_if_not_exist(f"{out_dir}/{RESULTS_DIR_NAME}")
        create_dir_if_not_exist(f"{out_dir}/{NETLISTS_DIR_NAME}")
        self.__logs_per_thread = []
        for thread_idx in range(num_threads):
            self.__logs_per_thread.append(list())

    def get_test_output_folder(self, id: str):
        return f"{self.__out_dir}/{RESULTS_DIR_NAME}/{id}"

    def get_base_folder(self, id: str):
        if not self.__base_dir: return ""
        return f"{self.__base_dir}/{RESULTS_DIR_NAME}/{id}"

    def run(self, netlist_path: str, thread_idx: int):
        netlist_name = get_netlist_name(netlist_path)
        return self.run_test(
            netlist_path,
            self.get_base_folder(netlist_name),
            self.get_test_output_folder(netlist_name),
            thread_idx
        )

    def run_test(self, netlist_path: str, base_dir: str, out_dir: str, thread_idx: int):
        create_or_clean_dir(out_dir)

        # net2pipe
        net2pipe_retcode, net2pipe_cmd = run_net2pipe(netlist_path, out_dir, self.__arch)
        self.__logs_per_thread[thread_idx].append(f"{net2pipe_cmd}\n")
        if net2pipe_retcode:
            self.__logs_per_thread[thread_idx].append(
                f"Net2Pipe failed with {net2pipe_retcode}\n")
            return net2pipe_retcode

        # pipegen
        epoch_id = 0
        epoch_dir = get_epoch_dir(out_dir, epoch_id)
        while os.path.isdir(epoch_dir):
            pipegen_yaml_path = f"{epoch_dir}/pipegen.yaml"
            blob_yaml_path = f"{epoch_dir}/blob.yaml"
            pipegen_retcode, pipegen_cmd = run_pipegen1(
                pipegen_yaml_path, blob_yaml_path, self.__arch, epoch_id
            )
            self.__logs_per_thread[thread_idx].append(f"{pipegen_cmd}\n")
            if pipegen_retcode:
                self.__logs_per_thread[thread_idx].append(
                    f"Pipegen epoch {epoch_id} failed with {pipegen_retcode}\n")
                return pipegen_retcode
            epoch_id = epoch_id + 1
            epoch_dir = get_epoch_dir(out_dir, epoch_id)

        # diff
        if base_dir:
            diff_retcode, diff_cmd = run_diff(out_dir, base_dir)
            self.__logs_per_thread[thread_idx].append(f"{diff_cmd}\n")
            if diff_retcode:
                self.__logs_per_thread[thread_idx].append(
                    f"Diff failed with {diff_retcode}\n")
                return diff_retcode

        return 0

    def write_logs(self, write_to_console: bool = False):
        if write_to_console:
            for thread_logs in self.__logs_per_thread:
                for thread_log in thread_logs:
                    print(thread_log)
        else:
            with open(self.__out_dir + f"/{TEST_LOG_FILE_NAME}", "a+") as log_file:
                for thread_logs in self.__logs_per_thread:
                    log_file.writelines(thread_logs)

def read_lines(filename: str) -> list[str]:
    with open(filename, "r") as f:
        lines= f.readlines()
    return [value.rstrip() for value in lines]

def pack_test_data(base_dir: str):
    cwd = os.getcwd()
    os.chdir(base_dir)
    if (os.path.exists(BASELINE_FILE_NAME)):
        # Zip command is updating the existing zip file,
        # so we have to delete it first.
        os.system(f"rm -rf {BASELINE_FILE_NAME}")
    os.system(f"zip -r {BASELINE_FILE_NAME} {RESULTS_DIR_NAME} {NETLISTS_DIR_NAME}")
    os.chdir(cwd)

def add_netlist_data(netlist_path: str, netlists_dir: str, test_driver: TestDriver) -> bool:
    """
    Runs net2pipe&pipegen on a single netlist and adds it and its
    results into the folder with the baseline data.
    """
    netlist_name = get_netlist_name(netlist_path)
    # TODO: We might need some more robust way to filter out
    #       netlists from other files, but this will do for now.
    if not netlist_path.endswith(".yaml") or netlist_name == "test_configs":
        return False

    print({netlist_name})

    if os.path.exists(test_driver.get_test_output_folder(netlist_name)):
        print(f"Duplicate. Netlist {netlist_name} already exists!")
        return False

    if test_driver.run(netlist_path, 0) != 0:
        print(f"Error. Validation of netlist {netlist_name} failed.")
        os.system(f"rm -rf {test_driver.get_test_output_folder(netlist_name)}")
        return False

    if os.system(f"cp {netlist_path} {netlists_dir}") != 0:
        print(f"Error. Failed to copy {netlist_path} to {netlists_dir}")
        os.system(f"rm -rf {test_driver.get_test_output_folder(netlist_name)}")
        return False

    return True

def add_netlists_from_dir(netlists_dir: str,
                          skip_subdirs: bool,
                          out_netlists_dir: str,
                          test_driver: TestDriver) -> bool:
    """
    Runs net2pipe&pipegen on all netlists found recursively in the
    specified netlist folder and adds them and their results into
    the folder with the baseline data.
    """
    updated = False

    print(netlists_dir + ("/" if not netlists_dir.endswith("/") else ""))

    for netlist_path in os.listdir(netlists_dir):
        full_netlist_path = os.path.join(netlists_dir, netlist_path)
        if os.path.isdir(full_netlist_path) and skip_subdirs:
            continue
        if add_netlists_from_path(full_netlist_path,
                                  out_netlists_dir,
                                  test_driver):
            updated = True

    return updated

def add_netlists_from_path(netlist_path: str,
                           out_netlists_dir: str,
                           test_driver: TestDriver) -> bool:
    """
    Updates baseline data with the netlist or netlists at the given
    path, depending if the path is a single file or a folder.
    """
    skip_subdirs = netlist_path.endswith("/*")
    if (skip_subdirs):
        netlist_path = netlist_path[0:-1]

    if os.path.isdir(netlist_path):
        return add_netlists_from_dir(netlist_path,
                                     skip_subdirs,
                                     out_netlists_dir,
                                     test_driver)
    else:
        return add_netlist_data(netlist_path,
                                out_netlists_dir,
                                test_driver)

def _run_netlists(netlist_paths: list[str], test_driver: TestDriver, start_idx: int, end_idx: int,
                  thread_idx: int, lock: threading.Lock, netlist_failed: list[bool]):
    for idx in range(start_idx, end_idx):
        netlist_path = netlist_paths[idx]
        netlist_failed[idx] = test_driver.run(netlist_path, thread_idx) != 0
        print_thread_info(thread_idx, lock, idx, start_idx, end_idx)

def run_netlists(netlist_paths: list[str], test_driver: TestDriver, netlist_failed: list[bool],
                 num_threads: int):
    num_netlists_per_thread = math.ceil(len(netlist_paths) / num_threads)
    lock = threading.Lock()
    threads = []
    for thread_idx in range(num_threads):
        start_idx = thread_idx * num_netlists_per_thread
        end_idx = min(start_idx + num_netlists_per_thread, len(netlist_paths))
        threads.append(threading.Thread(
            target=_run_netlists,
            args=(netlist_paths, test_driver, start_idx, end_idx, thread_idx, lock,
                  netlist_failed))
        )

    for thread in threads:
        thread.start()

    for thread in threads:
        thread.join()

def update_test_data(
    arch: str, netlists_file: str, base_dir: str):
    """
    Updates baseline zip with the netlists specified in the netlists
    file, if they can be sucessfully run.

    Netlists can be specified as a full paths to the netlists and as a
    full paths to the folders containing netlists (found recursively).

    It is expected that the baseline zip is already unpacked into the
    specified base dir, or that the base dir contains the baseline
    zip file, otherwise it creates new baseline zip only from the
    files specified in the netlists file.
    """
    netlists_dir = f"{base_dir}/{NETLISTS_DIR_NAME}"
    baseline_file = f"{base_dir}/{BASELINE_FILE_NAME}"
    if not os.path.exists(netlists_dir) and os.path.exists(baseline_file):
        unpack_test_data(base_dir, base_dir)

    updated = False
    test_driver = TestDriver(arch, "", base_dir, 1)

    print("Adding netlists:")
    for netlist_path in read_lines(netlists_file):
        if not netlist_path or netlist_path.startswith("#"):
            continue
        elif add_netlists_from_path(netlist_path, netlists_dir, test_driver):
            updated = True

    test_driver.write_logs()

    print("\nPacking baseline file:")
    if updated:
        pack_test_data(base_dir)

def update_test_results(arch: str, base_dir: str, num_threads: int):
    """
    Generates fresh net2pipe&pipegen results for netlists in the
    baseline zip file, if they can be sucessfully run, and updates
    the baseline zip file.

    It is expected that the baseline zip is already unpacked into the
    specified base dir, or that the base dir contains the baseline
    zip file.
    """
    netlists_dir = f"{base_dir}/{NETLISTS_DIR_NAME}"
    baseline_file = f"{base_dir}/{BASELINE_FILE_NAME}"
    if not os.path.exists(netlists_dir):
        assert os.path.exists(baseline_file), \
            "Baseline zip file is missing in the base dir!"
        unpack_test_data(base_dir, base_dir)

    print("Deleting old results...")
    results_dir = f"{base_dir}/{RESULTS_DIR_NAME}"
    os.system(f"rm -rf {results_dir}")

    netlists_cnt = 0
    netlist_paths = []
    for netlist_path in os.listdir(netlists_dir):
        netlists_cnt += 1
        netlist_name = get_netlist_name(netlist_path)
        netlist_paths.append(os.path.join(netlists_dir, netlist_path))

    netlist_failed = [False for netlist_path in netlist_paths]
    num_threads = min(len(netlist_paths), num_threads)

    print("Generating new results:")
    test_driver = TestDriver(arch, "", base_dir, num_threads)
    run_netlists(netlist_paths, test_driver, netlist_failed, num_threads)
    test_driver.write_logs()

    failed_netlists = []
    for idx, failed in enumerate(netlist_failed):
        if failed:
            netlist_path = netlist_paths[idx]
            netlist_name = get_netlist_name(netlist_path)
            print(f"Running netlist {netlist_name} failed!")
            failed_netlists.append(netlist_path)

    if failed_netlists:
        # Moving failed netlists to separate folder for inspection.
        # They will not be packed into updated baseline file.
        failed_netlists_dir = f"{base_dir}/failed_netlists"
        create_or_clean_dir(failed_netlists_dir)
        for netlist_path in failed_netlists:
            netlist_name = get_netlist_name(netlist_path)
            os.system(f"mv {netlist_path} {failed_netlists_dir}")
            os.system(f"rm -rf {test_driver.get_test_output_folder(netlist_name)}")

    print("\nPacking baseline file:")
    pack_test_data(base_dir)

    if failed_netlists:
        print_fail(f"\n{len(failed_netlists)}/{netlists_cnt}" +
                   " netlists failed to be included in the new baseline! Please" +
                   " check them to make sure they are no longer valid netlists:")
        for netlist_path in failed_netlists:
            print(f"    {failed_netlists_dir}/{os.path.basename(netlist_path)}")
        print()
    else:
        print_success(f"\nUpdated baseline created sucessfully.\n")

def unpack_test_data(base_dir: str, out_dir: str):
    """
    Unpacks baseline zip file from the base dir into the out dir,
    with folder structure like:
        - netlists/
            - netlist_001.yaml
            ...
        - results/
            - netlist_001/
                - (pipegen output)
            ...
    """
    if base_dir != out_dir:
        create_or_clean_dir(out_dir)

    baseline_path = os.path.abspath(base_dir)
    cwd = os.getcwd()
    os.chdir(out_dir)
    print("\nUnpacking baseline file...\n")
    os.system(f"unzip -qq {baseline_path}/{BASELINE_FILE_NAME}")
    os.chdir(cwd)

def test_all(arch: str, base_dir: str, out_dir: str, num_threads: int):
    assert os.path.exists(base_dir), \
        "Folder with baseline test data doesn't exist!"

    create_or_clean_dir(out_dir)

    results_dir = f"{base_dir}/{RESULTS_DIR_NAME}"
    netlist_paths = []
    for netlist_name in os.listdir(results_dir):
        netlist_paths.append(os.path.join(base_dir, NETLISTS_DIR_NAME, netlist_name + ".yaml"))

    netlists_cnt = len(netlist_paths)
    num_threads = min(netlists_cnt, num_threads)
    test_driver = TestDriver(arch, base_dir, out_dir, num_threads)
    netlist_failed = [False for netlist_path in netlist_paths]
    run_netlists(netlist_paths, test_driver, netlist_failed, num_threads)
    test_driver.write_logs()

    failed_netlists = []
    for idx, failed in enumerate(netlist_failed):
        if failed:
            failed_netlists.append(netlist_paths[idx])

    if failed_netlists:
        print_fail(f"\nTest failed on {len(failed_netlists)}/{netlists_cnt} netlists:")
        for netlist_path in failed_netlists:
            print(f"    {netlist_path}")
        print()
    else:
        print_success(f"\nTest passed. Ran on {netlists_cnt} netlists.\n")

def test_single(arch: str,
                base_dir: str,
                out_dir: str,
                netlist_filename: str):
    assert os.path.exists(base_dir), \
        "Folder with baseline test data doesn't exist!"

    netlist_path = os.path.join(
        base_dir, NETLISTS_DIR_NAME, os.path.basename(netlist_filename))

    test_driver = TestDriver(arch, base_dir, out_dir, 1)
    retcode = test_driver.run(netlist_path, 0)
    test_driver.write_logs(True)

    print(f"{netlist_path} - retcode {retcode}")
    # Python limitation, if exit code is larger than 255 it exits
    # with 0 which indicates success for CI.
    sys.exit(min(255, retcode))

if __name__=="__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--command',  type=str, required=True, default=None, help='Commands [update|update-results|unpack|test|test-single]')
    parser.add_argument('--base-dir',  type=str, required=True, default=None, help='Folder where baseline test data are stored')
    parser.add_argument('--out-dir',  type=str, required=False, default=None, help='Folder where output data are stored')
    parser.add_argument('--arch',  type=str, required=False, default="grayskull", help='Commands [wormhole_b0|grayskull]')
    parser.add_argument('--netlists-file',  type=str, required=False, default=None, help='File with netlist paths to add in case of update')
    parser.add_argument('--netlist', type=str, required=False, default=None, help='Netlist filename to run a single test on')
    parser.add_argument('--num-threads',  type=int, required=False, default=8, help='Number of threads to utilize')
    args = parser.parse_args()

    assert(args.arch == DeviceArchs.GRAYSKULL or args.arch == DeviceArchs.WORMHOLE_B0)
    assert(args.num_threads > 0 and args.num_threads <= MAX_NUM_THREADS)

    start_time = time.ctime()

    if (args.command == "update"):
        update_test_data(args.arch, args.netlists_file, args.base_dir)
    elif (args.command == "update-results"):
        update_test_results(args.arch, args.base_dir, args.num_threads)
    elif (args.command == "unpack"):
        unpack_test_data(args.base_dir, args.out_dir)
    elif (args.command == "test"):
        test_all(args.arch, args.base_dir, args.out_dir, args.num_threads)
    elif (args.command == "test-single"):
        test_single(args.arch, args.base_dir, args.out_dir, args.netlist)

    print(f"Started  at {start_time}")
    print(f"Finished at {time.ctime()}")
