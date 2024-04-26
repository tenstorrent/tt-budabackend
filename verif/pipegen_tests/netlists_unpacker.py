#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Finds all netlist zips starting from a given folder recursively,
and unzips them to specified output folder.
"""
from __future__ import annotations

import argparse
import os
import uuid

root_dirs_to_skip = ["build", "ci", "dbd", "docs", "test_pipegen", "umd"]


def find_netlist_zips_in_dir(dir_path: str) -> list[str]:
    """Finds netlist zips recursively starting from the given directory.

    Parameters
    ----------
    dir_path: str
        Directory to search for netlists.

    Returns
    -------
    list[str]
        List of found netlist zips paths.
    """
    netlist_zips = []

    for entry_path in os.listdir(dir_path):
        full_entry_path = os.path.join(dir_path, entry_path)
        if os.path.isdir(full_entry_path):
            netlist_zips.extend(find_netlist_zips(full_entry_path))
        elif full_entry_path.endswith(".zip"):
            netlist_zips.append(full_entry_path)

    return netlist_zips


def find_netlist_zips(root_dir: str) -> list[str]:
    """Finds netlist zips recursively starting from the given root repo folder and excluding some
    specific folders.

    Parameters
    ----------
    root_dir: str
        Root repo folder to search for netlists.

    Returns
    -------
    list[str]
        List of found netlist zips paths.
    """
    netlist_zips = []

    for entry_path in os.listdir(root_dir):
        full_entry_path = os.path.join(root_dir, entry_path)
        if os.path.isdir(full_entry_path):
            # Skipping some repo dirs which don't contain netlist zips in order to speed things up.
            if entry_path in root_dirs_to_skip:
                continue
            netlist_zips.extend(find_netlist_zips_in_dir(full_entry_path))
        elif full_entry_path.endswith(".zip"):
            netlist_zips.append(full_entry_path)

    return netlist_zips


def unzip_netlists(netlist_zips: list[str]):
    """Unzips netlist zips each into separate folder with unique name.

    Parameters
    ----------
    netlist_zips: list[str]
        List of netlist zips paths.
    """
    for netlist_zip in netlist_zips:
        print(f"    {netlist_zip}")
        out_subdir = f"{os.path.basename(netlist_zip[:-4])}_{uuid.uuid4().hex}"
        os.makedirs(out_subdir)
        os.chdir(out_subdir)
        os.system(f"unzip -qq {netlist_zip}")
        os.chdir("..")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out-dir",
        type=str,
        required=True,
        help="Folder where to store the found netlists",
    )
    args = parser.parse_args()

    root_dir = os.getcwd()
    out_dir = args.out_dir

    assert {out_dir != root_dir}

    print("Collecting netlist zips")
    netlist_zips = find_netlist_zips(root_dir)

    os.system(f"rm -rf {out_dir}")
    os.makedirs(out_dir)
    os.chdir(out_dir)

    print("Unzipping netlist zips:")
    unzip_netlists(netlist_zips)

    os.chdir(root_dir)
    print("Finished.")
