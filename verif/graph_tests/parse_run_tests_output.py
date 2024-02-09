# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
# Wrapper script to run run_tests.sh and parse the output. The script will copy the netlists to the output directory
# based on the test results.
# netlists will be copied to:
#   <output_dir>/passing
#   <output_dir>/failing/fatal_pipegen
#   <output_dir>/failing/fatal_other
#   <output_dir>/failing/hang
#
# Usage:
#   python parse_run_tests_output.py --dir <dir> --arch <arch> --chip-id <chip_id> --timeout <timeout> --output-dir <output_dir>
#
# <dir> is the directory where netlists are generated
# <arch> is the architecture name
# <chip_id> is the chip id
# <timeout> is the timeout value in seconds per netlist
# <output_dir> is the parent output directory where netlists will be copied to based on the test results


import re
import os
import shutil
import subprocess
import argparse

MAX_PASSING_NETLISTS_TO_STORE = 10

def parse_test_result(result_line):
    pattern = r"(.+\.yaml):\s+.*<(.+?)>.*"
    match = re.match(pattern, result_line)

    if match:
        return {"netlist_path": match.group(1), "status": match.group(2)}
    else:
        return None


def copy_netlist(logs_path, netlist_path, status, parent_dir):
    netlist_name = os.path.basename(netlist_path)

    if status == "FAILED":
        dest_dir = os.path.join(parent_dir, "failing")
        dest_dir_fatal_pipegen = os.path.join(dest_dir, "fatal_pipegen")
        dest_dir_fatal_other = os.path.join(dest_dir, "fatal_other")
        dest_dir_hang = os.path.join(dest_dir, "hang")

        # strip .yaml extension and append .log
        log_file = os.path.join(logs_path, netlist_name[:-5] + ".log")
        with open(log_file, "r") as f:
            # grep line that contains "FATAL"
            found_fatal = False
            for line in f:
                if "Running pipegen command failed" in line:
                    os.makedirs(dest_dir_fatal_pipegen, exist_ok=True)
                    dest_path = os.path.join(dest_dir_fatal_pipegen, netlist_name)
                    found_fatal = True
                    break
                elif "FATAL" in line:
                    os.makedirs(dest_dir_fatal_other, exist_ok=True)
                    dest_path = os.path.join(dest_dir_fatal_other, netlist_name)
                    found_fatal = True
                    break

            if not found_fatal:
                os.makedirs(dest_dir_hang, exist_ok=True)
                dest_path = os.path.join(dest_dir_hang, netlist_name)
    else:
        # limit the number of passing netlists to MAX_PASSING_NETLISTS_TO_STORE
        passing_dir = os.path.join(parent_dir, "passing")
        os.makedirs(passing_dir, exist_ok=True)
        if len(os.listdir(passing_dir)) >= MAX_PASSING_NETLISTS_TO_STORE:
            return
        dest_path = os.path.join(passing_dir, netlist_name)

    shutil.copy(netlist_path, dest_path)
    print(f"Netlist '{netlist_name}' copied to '{dest_path}'")


def run_tests_script(args):
    command = [
        "./verif/graph_tests/run_tests.sh",
        "--dir=" + args.dir,
        "--arch=" + args.arch,
        "--chip-id=" + str(args.chip_id),
        "--timeout=" + str(args.timeout),
    ]

    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    # Read and print the output line by line
    stdout = []
    while True:
        output_line = process.stdout.readline()
        if output_line == "" and process.poll() is not None:
            break
        if output_line:
            print(output_line.strip())  # Print the line without newline
            stdout.append(output_line)

    process.wait()  # Wait for the process to complete

    return stdout


def main():
    parser = argparse.ArgumentParser(description="Test Result Parser")
    parser.add_argument("--dir", required=True, help="Directory argument for run_tests.sh")
    parser.add_argument("--arch", required=True, help="ARCH_NAME argument for run_tests.sh")
    parser.add_argument("--chip-id", type=int, required=True, help="CHIP_ID argument for run_tests.sh")
    parser.add_argument("--timeout", type=int, required=True, help="Timeout argument for run_tests.sh")
    parser.add_argument("--output-dir", required=True, help="Path to the parent output directory")
    args = parser.parse_args()

    # delete output directory if it exists
    if os.path.exists(args.output_dir):
        shutil.rmtree(args.output_dir, ignore_errors=True)

    test_output = run_tests_script(args)

    # remove all lines before "Details:\n"
    for i, line in enumerate(test_output):
        if line.startswith("Details:"):
            test_output = test_output[i + 1 :]
            break

    for result_line in test_output:
        result = parse_test_result(result_line)
        if result:
            netlist_path = result["netlist_path"]
            print("Netlist Path:", netlist_path)
            copy_netlist(os.path.join(args.dir, "logs"), netlist_path, result["status"], args.output_dir)
            print("--------------")


if __name__ == "__main__":
    main()
