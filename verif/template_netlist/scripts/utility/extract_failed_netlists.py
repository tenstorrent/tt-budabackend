# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
import os
import os.path
import shutil
from typing import Optional


def extract_failed_netlists(test_dir: str, destination: Optional[str] = None) -> None:
    assert os.path.exists(test_dir), f"Test dir '{test_dir}' not found."
    if destination:
        os.makedirs(destination, exist_ok=True)

    failed_cnt = 0
    
    summary_log_path = os.path.join(test_dir, "logs", "summary.log")
    with open(summary_log_path, "r") as summary_file:
        for log_line in summary_file:
            
            # Skip empty lines.
            log_line = log_line.strip()
            if not log_line:
                continue
            
            # Skip netlists which passed.
            netlist_path, netlist_status = log_line.split(":")
            if "FAIL" not in netlist_status:
                continue

            # Extract test name.
            test_name = os.path.basename(os.path.dirname(netlist_path))
            netlist_test_dir_path = os.path.join(test_dir, test_name)
            
            # Move to destination folder.
            if destination:
                destination_path = os.path.join(destination, test_name)
                shutil.move(netlist_test_dir_path, destination_path, copy_function=shutil.copytree)

            failed_cnt += 1

    print(f"Extracted {failed_cnt} failed tests.")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--test-dir", required=True, type=str)
    parser.add_argument("--destination", default=None, type=str)
    args = parser.parse_args()

    extract_failed_netlists(
        test_dir=args.test_dir,
        destination=args.destination
    )


if __name__ == "__main__":
    main()
