#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Blackbox testing of net2pipe and pipegen component.
"""
from __future__ import annotations

import argparse
import hashlib
import multiprocessing as mp
import os
import shutil
import subprocess
import sys
import time
import uuid
import zipfile
from enum import IntEnum
from itertools import repeat

import yaml

from verif.common.runner_net2pipe import run_net2pipe
from verif.common.runner_pipegen import run_pipegen
from verif.common.test_utils import (
    DeviceArchs,
    create_dir_if_not_exist,
    create_or_clean_dir,
    get_epoch_dir,
    print_fail,
    print_success,
    print_warning,
)

CI_TEST_LISTS_DIR = "ci/test-lists"
NETLISTS_DIR_NAME = "netlists"
BASELINE_RESULTS_FILE_NAME = "baseline_results.txt"
BASELINE_FILE_NAME = "baseline.zip"
TEST_LOG_FILE_NAME = "run_log.txt"

BLACKBOX_CI_TEST_LISTS = {
    "pipegen_grayskull_model_nightly.yaml",
    "pipegen_grayskull_model_push.yaml",
    "pipegen_wormhole_b0_model_nightly.yaml",
    "pipegen_wormhole_b0_model_push.yaml",
}


class CITestExitCode(IntEnum):
    Success = 0
    DifferentOverlayOutput = 1
    OverlayFailed = 2
    NetlistMissing = 5
    InvalidBaseline = 10


def update_baseline_data(baseline_file_path: str, out_dir: str, num_cores: int):
    """
    Collects all netlists in CI test lists for given baseline file, filters out those who fail
    net2pipe, generates hashes of overlay results for them, and packs everything anew into the
    baseline file.

    To be used sporadically to get new netlists from repo into the baseline datasets.

    Parameters
    ----------
    baseline_file_path: str
        Path to the baseline file to update.
    out_dir: str
        Directory to be used for storing temporary outputs during update.
    num_cores: int
        Number of CPU cores to utilize.
    """
    create_or_clean_dir(out_dir)

    print("\nSearching CI test lists for netlists:")
    arch_name = os.path.basename(os.path.dirname(os.path.dirname(baseline_file_path)))
    test_tag = os.path.basename(os.path.dirname(baseline_file_path))
    netlist_paths = find_ci_netlists(arch_name, test_tag)
    print_success(f"Done. Found {len(netlist_paths)} netlists.")

    print(f"\nGenerating overlay results:")
    overlay_results = generate_overlay_results(
        netlist_paths, out_dir, arch_name, num_cores
    )
    print_success(f"Done. {len(overlay_results)} netlists passed overlay.")

    print(f"\nPacking baseline zip...")
    pack_new_baseline_zip(overlay_results, baseline_file_path, out_dir)
    print_success(f"Done. Packed {len(overlay_results)} netlists into baseline zip.")

    print_warning(
        "\nPS: You might want to clean your build/test folder now since that is where CI"
        " test lists unpacked their netlists."
    )


def find_ci_netlists(arch_name: str, test_tag: str) -> list[str]:
    """
    Reads all CI test lists for the given architecture and tag (push/nightly) and parses unique
    netlist paths from them.

    Parameters
    ----------
    arch_name: str
        Target device architecture.
    test_tag: str
        CI tag (push/nightly).

    Returns
    -------
    list[str]
        List of unique netlist paths found.
    """
    netlist_paths = set()
    for root, _, files in os.walk(CI_TEST_LISTS_DIR):
        for test_list_name in files:
            if (
                not test_list_name.endswith(".yaml") and not test_list_name.endswith(".yml")
            ) or test_list_name in BLACKBOX_CI_TEST_LISTS:
                continue
            test_list_path = os.path.join(root, test_list_name)
            print(f"  {test_list_path}")
            with open(test_list_path) as yaml_file_handler:
                test_list_dict = yaml.safe_load(yaml_file_handler.read())
            add_netlists_from_test_list(test_list_dict, arch_name, test_tag, netlist_paths)
    return list(netlist_paths)


def add_netlists_from_test_list(
    test_list_dict: dict, arch_name: str, test_tag: str, netlist_paths: set[str]
):
    """
    Finds test groups in the test list dictionary with the given architecture and tag
    (push/nightly), runs build step for each group to generate netlists, and returns list of
    netlist paths from those test groups.

    Parameters
    ----------
    test_list_dict: dict
        Test list dictionary loaded from test list yaml.
    arch_name: str
        Target device architecture.
    test_tag: str
        CI tag (push/nightly).
    netlist_paths: set[str]
        Netlist paths set where to add the found netlist paths.
    """
    for test_group_name, test_group in test_list_dict.items():
        if not (test_group["tag"] == test_tag and test_group["arch_name"] == arch_name):
            continue
        netlists_cnt = len(netlist_paths)
        print(f"    {test_group_name}")
        run_build_for_test_group(test_group)
        for test_dict in test_group.values():
            if not isinstance(test_dict, dict):
                continue
            add_netlists_from_test(test_dict, netlist_paths)
        netlists_cnt = len(netlist_paths) - netlists_cnt
        print(f"      Added {netlists_cnt} netlists")


def run_build_for_test_group(test_group: dict):
    """
    Runs build step for the given test group in order to generate netlists.

    Parameters
    ----------
    test_group: dict
        Test group dictionary loaded from test list yaml.
    """
    if "build_cmd" not in test_group:
        return
    for build_cmd in test_group["build_cmd"]:
        if build_cmd.startswith("make "):
            continue
        print(f'      Running build command: "{build_cmd}"')
        subprocess.run(build_cmd.split(), capture_output=True)


def add_netlists_from_test(test_dict: dict, netlist_paths: set[str]):
    """
    Adds netlists listed for the given test config, if there is any. It could be listed as a single
    test netlist, or multi test dir. In case of multi test dir all netlist paths in a listed dir
    are added.

    Parameters
    ----------
    test_dict: dict
        Test config dictionary loaded from test list yaml.
    netlist_paths: set[str]
        Netlist paths set where to add the found netlist paths.
    """
    if "multi_test_dir" in test_dict:
        netlist_dir = test_dict["multi_test_dir"]
        if not os.path.exists(netlist_dir):
            return
        for dir_path, _, file_names in os.walk(netlist_dir):
            for netlist_name in file_names:
                if not netlist_name.endswith(".yaml"):
                    continue
                netlist_paths.add(os.path.join(dir_path, netlist_name))
    elif "command" in test_dict:
        for cmd_part in test_dict["command"]:
            for cmd_subpart in cmd_part.split():
                if cmd_subpart.endswith(".yaml") and os.path.exists(cmd_subpart):
                    netlist_paths.add(cmd_subpart)


def init_done_counter(counter: mp.Value):
    """
    Used to initialize shared counter of processed data in each process.

    Parameters
    ----------
    counter: mp.Value
        Shared counter of processed data.
    """
    global done_counter
    done_counter = counter


def generate_overlay_results(
    netlist_paths: list[str], out_dir: str, arch_name: str, num_cores: int
) -> dict[str, str]:
    """
    Generates overlay results for the given netlists.

    Parameters
    ----------
    netlist_paths: list[str]
        List of netlist paths.
    out_dir: str
        Directory to be used for storing temporary overlay outputs.
    arch_name: str
        Device architecture for which results are generated.
    num_cores: int
        Number of CPU cores to utilize.

    Returns
    -------
    dict[str, str]
        Dictionary of generated overlay results {netlist_name: overlay_hash}.
    """
    shared_done_counter = mp.Value("i", 0)
    num_netlists = len(netlist_paths)
    with mp.Pool(
        processes=num_cores,
        initializer=init_done_counter,
        initargs=(shared_done_counter,),
    ) as pool:
        results_list = pool.starmap(
            generate_overlay_result,
            zip(
                netlist_paths, repeat(num_netlists), repeat(out_dir), repeat(arch_name)
            ),
        )

    return {k: v for k, v in results_list if v}


def generate_overlay_result(
    netlist_path: str,
    num_netlists: int,
    out_dir: str,
    arch_name: str,
) -> tuple[str, str]:
    """
    Generates overlay result for the given netlist.

    Parameters
    ----------
    netlist_path: str
        Netlist path.
    num_netlists: int
        Total number of netlists for which overlay results are generated.
    out_dir: str
        Directory to be used for storing temporary overlay outputs.
    arch_name: str
        Device architecture for which results are generated.

    Returns
    -------
    tuple[str, str]
        Netlist path and overlay result hash tuple.

    """
    temp_dir = os.path.join(out_dir, f"temp_{mp.current_process().pid}")
    os.makedirs(temp_dir)

    try:
        overlay_hash = get_overlay_output_hash(netlist_path, temp_dir, arch_name)
    except:
        overlay_hash = None

    os.system(f"rm -rf {temp_dir}")

    with done_counter.get_lock():
        done_counter.value += 1
        if done_counter.value % 100 == 0 or done_counter.value == num_netlists:
            print(f"  Processed {done_counter.value}/{num_netlists} netlists")

    return netlist_path, overlay_hash


def get_overlay_output_hash(netlist_path: str, temp_dir: str, arch_name: str) -> str:
    """
    Runs overlay and calculates overlay result hash for the given netlist.

    Parameters
    ----------
    netlist_path: str
        Netlist path.
    temp_dir: str
        Directory to be used for storing temporary overlay outputs.
    arch_name: str
        Device architecture for which results are generated.

    Returns
    -------
    str
        Netlist overlay result hash.
    """
    # Running net2pipe
    run_net2pipe(netlist_path, temp_dir, arch_name, throw_if_error=True)
    # Running pipegen
    epoch_id = 0
    epoch_dir = get_epoch_dir(temp_dir, epoch_id)
    while os.path.isdir(epoch_dir):
        pipegen_yaml_path = f"{epoch_dir}/pipegen.yaml"
        blob_yaml_path = f"{epoch_dir}/blob.yaml"
        run_pipegen(
            pipegen_yaml_path, blob_yaml_path, arch_name, epoch_id, throw_if_error=True
        )
        epoch_id = epoch_id + 1
        epoch_dir = get_epoch_dir(temp_dir, epoch_id)
    # Hashing results
    results_hash = get_hash(temp_dir)
    return results_hash


def get_hash(dir_path) -> str:
    """
    Calculates hash for the given directory by zipping it deterministically and calculating md5
    hash of the zip file.

    Parameters
    ----------
    dir_path: str
        Target directory path.

    Returns
    -------
    str
        Calculated hash for the given directory.
    """
    zip_path = os.path.join(dir_path, "temp.zip")
    zip_dir(dir_path, zip_path)
    results_hash = hashlib.md5(open(zip_path, "rb").read()).hexdigest()
    os.remove(zip_path)
    return results_hash


def zip_dir(dir_path, zip_path: str):
    """
    Deterministically zips the given directory by making sure directory files are added in a sorted
    order and overriding timestamps in files zip metadata.

    Parameters
    ----------
    dir_path: str
        Target directory path.
    zip_path: str
        Path to the output zip file.
    """
    file_paths = []
    for root, _, files in os.walk(dir_path):
        for file in files:
            file_paths.append(os.path.join(root, file))
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as zipf:
        # Adding files in sorted order to ensure deterministic zips.
        for file_path in sorted(file_paths):
            info = zipfile.ZipInfo(
                filename=os.path.relpath(file_path, dir_path),
                # Overriding timestamp to ensure deterministic zips.
                date_time=(1980, 1, 1, 0, 0, 0),
            )
            with open(file_path) as file_data:
                zipf.writestr(info, file_data.read())


def pack_new_baseline_zip(
    overlay_results: dict[str, str], baseline_file_path: str, out_dir: str
):
    """
    Packs the new baseline zip with the given overlay results and netlists. Netlist names are
    prepended with a random salt to make sure their names are unique.

    Parameters
    ----------
    overlay_results: dict[str, str]
        Dictionary of overlay results {netlist_name: overlay_hash}.
    baseline_file_path: str
        Path to the baseline file to pack.
    out_dir: str
        Directory to be used for storing contents to pack.
    """
    netlists_dir = os.path.join(out_dir, NETLISTS_DIR_NAME)
    results_path = os.path.join(out_dir, BASELINE_RESULTS_FILE_NAME)
    os.makedirs(netlists_dir)
    with open(results_path, "w") as results_writer:
        for netlist_path, overlay_hash in overlay_results.items():
            # Adding random uuid because there can be different netlists with the same name.
            netlist_baseline_name = (
                f"{uuid.uuid4().hex}_{os.path.basename(netlist_path)}"
            )
            shutil.copyfile(
                netlist_path, os.path.join(netlists_dir, netlist_baseline_name)
            )
            results_writer.write(f"{netlist_baseline_name} {overlay_hash}\n")
    os.remove(baseline_file_path)
    zip_dir(out_dir, baseline_file_path)


def update_baseline_results(
    baseline_file_path: str, out_dir: str, force_update: bool, num_cores: int
):
    """
    Updates overlay results hashes in the given baseline file.

    To be used when overlay functionality is changed and output is legitimately different, to
    update the results in baseline datasets. If the output dir is not empty then the
    results will be updated only for files that failed the test, in order to save on time.

    Parameters
    ----------
    baseline_file_path: str
        Path to the baseline file to update.
    out_dir: str
        Directory to be used for storing temporary outputs during update.
    force_update: bool
        Should the update be done even if some netlists are failing overlay. In that case those
        netlists will be removed from the baseline.
    num_cores: int
        Number of CPU cores to utilize.
    """
    create_or_clean_dir(out_dir)

    unpack_baseline_data(baseline_file_path, out_dir)

    print(f"\nGenerating overlay results:")
    arch_name = os.path.basename(os.path.dirname(os.path.dirname(baseline_file_path)))
    netlists_dir = os.path.join(out_dir, NETLISTS_DIR_NAME)
    netlist_paths = [
        os.path.join(netlists_dir, netlist_name)
        for netlist_name in os.listdir(netlists_dir)
    ]
    overlay_results = generate_overlay_results(
        netlist_paths, out_dir, arch_name, num_cores
    )
    print_success("Done.")

    print(f"\nUpdating baseline zip...")
    if pack_updated_baseline_zip(
        overlay_results, baseline_file_path, out_dir, force_update
    ):
        print_success(f"Done.")


def pack_updated_baseline_zip(
    overlay_results: dict[str, str],
    baseline_file_path: str,
    out_dir: str,
    force_update: bool,
) -> bool:
    """
    Packs the updated baseline zip with the given overlay results and netlists which are in the
    output dir. If some netlist is in the output dir but its overlay results couldn't be generated,
    failure is reported or the netlist is simply not packed, depending on the `force_update` flag.

    Parameters
    ----------
    overlay_results: dict[str, str]
        Dictionary of overlay results {netlist_name: overlay_hash}.
    baseline_file_path: str
        Path to the baseline file to update.
    out_dir: str
        Directory to be used for storing contents to pack.
    force_update: str
        Controls whether to skip packing netlists which failed in overlay or report failure.

    Returns
    -------
    bool
        True if no netlists failed in overlay and new baseline zip is successfully packed, false
        otherwise.
    """
    netlists_dir = os.path.join(out_dir, NETLISTS_DIR_NAME)
    results_path = os.path.join(out_dir, BASELINE_RESULTS_FILE_NAME)
    failed_netlists = []
    with open(results_path, "w") as results_writer:
        for netlist_name in os.listdir(netlists_dir):
            netlist_path = os.path.join(netlists_dir, netlist_name)
            if netlist_path not in overlay_results:
                failed_netlists.append(netlist_name)
                if force_update:
                    os.remove(netlist_path)
                continue
            overlay_hash = overlay_results[netlist_path]
            results_writer.write(f"{netlist_name} {overlay_hash}\n")

    if failed_netlists and not force_update:
        print_fail(
            f"{len(failed_netlists)} netlists failed in overlay! Please inspect them to make sure"
            + " they are no longer valid netlists, and if you are sure rerun the script using"
            + " --force-update override. Failed netlists are:"
        )
        for netlist_name in failed_netlists:
            print(f"    {netlist_name}")
        return False
    else:
        os.remove(baseline_file_path)
        zip_dir(out_dir, baseline_file_path)
        return True


def unpack_baseline_data(baseline_file_path: str, out_dir: str):
    """
    Unpacks baseline zip file into the output dir, with folder structure like:
        - netlists/
            - netlist_001.yaml
            ...
        - baseline_results.yaml

    Parameters
    ----------
    baseline_file_path: str
        Path to the baseline file to unpack.
    out_dir: str
        Output directory where to unpack.
    """
    create_or_clean_dir(out_dir)
    print("\nUnpacking baseline data...")
    with zipfile.ZipFile(baseline_file_path, "r") as baseline_zip:
        baseline_zip.extractall(out_dir)
    print_success(f"Done.")


def test_all(baseline_file_path: str, out_dir: str, num_cores: int):
    """
    Runs blackbox test on a whole baseline dataset.

    Parameters
    ----------
    baseline_file_path: str
        Path to the baseline file to test on.
    out_dir: str
        Directory to be used for storing temporary outputs during test.
    num_cores: int
        Number of CPU cores to utilize.
    """
    create_dir_if_not_exist(out_dir)

    baseline_results_path = os.path.join(out_dir, BASELINE_RESULTS_FILE_NAME)
    if not os.path.exists(baseline_results_path):
        unpack_baseline_data(baseline_file_path, out_dir)

    print(f"\nReading baseline overlay results...")
    try:
        baseline_overlay_results = read_overlay_results(baseline_results_path)
    except Exception as exc:
        print_fail("Couldn't read baseline overlay results:")
        print(exc)
        return
    print_success("Done.")

    print(f"\nGenerating new overlay results:")
    arch_name = os.path.basename(os.path.dirname(os.path.dirname(baseline_file_path)))
    netlists_dir = os.path.join(out_dir, NETLISTS_DIR_NAME)
    netlist_paths = [
        os.path.join(netlists_dir, netlist_name)
        for netlist_name in os.listdir(netlists_dir)
    ]
    overlay_results = generate_overlay_results(
        netlist_paths, out_dir, arch_name, num_cores
    )
    print_success("Done.")

    failed_overlay = []
    different_results = []
    for netlist_name, overlay_hash in baseline_overlay_results.items():
        netlist_path = os.path.join(netlists_dir, netlist_name)
        if netlist_path not in overlay_results:
            failed_overlay.append(netlist_path)
        elif overlay_hash != overlay_results[netlist_path]:
            different_results.append(netlist_path)

    failed_cnt = len(failed_overlay) + len(different_results)
    total_cnt = len(baseline_overlay_results)
    if not failed_overlay and not different_results:
        print_success(f"\nTest passed. Ran on {total_cnt} netlists.")
    else:
        print_fail(f"\nTest failed on {failed_cnt}/{total_cnt} netlists!")
        if failed_overlay:
            print("\nFailed in overlay:")
            for netlist_path in failed_overlay:
                print(f"  {netlist_path}")
        if different_results:
            print("\nProduced different overlay output:")
            for netlist_path in different_results:
                print(f"  {netlist_path}")


def read_overlay_results(results_path: str) -> dict[str, str]:
    """
    Parses dictionary of overlay results {netlist_name: overlay_hash} from the given path.

    Parameters
    ----------
    results_path: str
        Overlay results file path.

    Returns
    -------
    dict[str, str]
        Dictionary of overlay results {netlist_name: overlay_hash}.
    """
    overlay_results = {}
    with open(results_path, "r") as results_reader:
        while True:
            result_line = results_reader.readline()
            if not result_line:
                break
            netlist_result = result_line.split()
            overlay_results[netlist_result[0]] = netlist_result[1]
    return overlay_results


def test_single(arch_name: str, base_dir: str, out_dir: str, netlist_path: str):
    """
    Runs blackbox test on a single netlist. Used in CI.

    Parameters
    ----------
    arch_name: str
        Device architecture name.
    base_dir: str
        Directory with baseline test data.
    out_dir: str
        Directory to be used for storing temporary outputs during test.
    netlist_path: str
        Path of the netlist to test.
    """
    assert os.path.exists(base_dir), "Folder with baseline test data doesn't exist!"
    create_or_clean_dir(out_dir)

    netlist_name = os.path.basename(netlist_path)
    baseline_results_path = os.path.join(base_dir, BASELINE_RESULTS_FILE_NAME)

    try:
        baseline_overlay_results = read_overlay_results(baseline_results_path)
    except Exception as exc:
        print("Couldn't read baseline overlay results:")
        print(exc)
        exit_with_code(CITestExitCode.InvalidBaseline)

    exit_code = CITestExitCode.Success
    try:
        overlay_hash = get_overlay_output_hash(netlist_path, out_dir, arch_name)
    except Exception as exc:
        exit_code = CITestExitCode.OverlayFailed
        print(exc)
        print(f"Couldn't generate overlay output for the netlist: {netlist_name}")
    else:
        if netlist_name in baseline_overlay_results:
            if overlay_hash == baseline_overlay_results[netlist_name]:
                print(f"Netlist passed: {netlist_name}")
            else:
                exit_code = CITestExitCode.DifferentOverlayOutput
                print(f"Netlist failed: {netlist_name}")
                print(f"Baseline hash: {baseline_overlay_results[netlist_name]}")
                print(f"Generated hash: {overlay_hash}")
        else:
            exit_code = CITestExitCode.NetlistMissing
            print(f"Netlist not found in the baseline results: {netlist_name}")

    exit_with_code(exit_code)


def exit_with_code(exit_code: CITestExitCode):
    """
    Exits process with give exit code.

    Parameters
    ----------
    exit_code: CITestExitCode
        Exit code.
    """
    sys.exit(int(exit_code))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--command",
        type=str,
        required=True,
        default=None,
        help="Commands [update|update-results|unpack|test|test-single]",
    )
    parser.add_argument(
        "--baseline-file",
        type=str,
        required=False,
        default=None,
        help="Path to the baseline zip file to test or update",
    )
    parser.add_argument(
        "--out-dir",
        type=str,
        required=True,
        default=None,
        help="Folder where temporary test output data are stored",
    )
    parser.add_argument(
        "--force-update",
        action="store_true",
        default=False,
        help="Forces update of the baseline file even when some netlists fail in overlay",
    )
    parser.add_argument(
        "--arch",
        type=str,
        required=False,
        default="grayskull",
        help="Commands [wormhole_b0|grayskull]",
    )
    parser.add_argument(
        "--netlist",
        type=str,
        required=False,
        default=None,
        help="Path of the netlist to run a single test on",
    )
    parser.add_argument(
        "--base-dir",
        type=str,
        required=False,
        default=None,
        help="Folder where netlists and baseline results are unpacked for single tests",
    )
    parser.add_argument(
        "--num-cores",
        type=int,
        required=False,
        default=os.cpu_count(),
        help="Number of CPU cores to utilize",
    )
    args = parser.parse_args()

    assert args.arch == DeviceArchs.GRAYSKULL or args.arch == DeviceArchs.WORMHOLE_B0
    assert args.num_cores > 0

    start_time = time.ctime()

    if args.command == "update":
        update_baseline_data(args.baseline_file, args.out_dir, args.num_cores)
    elif args.command == "update-results":
        update_baseline_results(
            args.baseline_file, args.out_dir, args.force_update, args.num_cores
        )
    elif args.command == "unpack":
        unpack_baseline_data(args.baseline_file, args.out_dir)
    elif args.command == "test":
        test_all(args.baseline_file, args.out_dir, args.num_cores)
    elif args.command == "test-single":
        test_single(args.arch, args.base_dir, args.out_dir, args.netlist)

    print(f"\nStarted  at {start_time}")
    print(f"Finished at {time.ctime()}")
