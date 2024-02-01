# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from enum import Enum
from dataclasses import dataclass
import subprocess
from typing import Optional
import os
import shutil

import argparse

NET2PIPE_BIN_PATH = "./build/bin/net2pipe"
PIPEGEN_BIN_PATH = "./build/bin/pipegen"

RED = "\033[0;31m"
GRN = "\033[0;32m"
YLW = "\033[0;33m"
INFO = "\033[1;33m"
ENDL = "\033[0m"  # No color
PASS = f"{GRN}<PASSED>{ENDL}"
FAIL = f"{RED}<FAILED>{ENDL}"


class DeviceArchitecture(Enum):
    grayskull = "grayskull"
    wormhole = "wormhole"
    wormhole_b0 = "wormhole_b0"


@dataclass
class TestResult:
    passed: bool
    message: str = ""

    @staticmethod
    def passed():
        return TestResult(passed=True)

    @staticmethod
    def failed(message: str = ""):
        return TestResult(passed=False, message=message)


__default_soc_path = {
    DeviceArchitecture.grayskull: "./soc_descriptors/grayskull_10x12.yaml",
    DeviceArchitecture.wormhole: "./soc_descriptors/wormhole_8x10.yaml",
    DeviceArchitecture.wormhole_b0: "./soc_descriptors/wormhole_b0_8x10.yaml",
}
__default_cluster_desc = {
    DeviceArchitecture.grayskull: "",
    DeviceArchitecture.wormhole: "./wormhole_2chip_cluster.yaml",
    DeviceArchitecture.wormhole_b0: "",
}


def run_net2pipe(
    netlist_path: str,
    out_dir: str,
    arch: str,
    global_epoch_start: Optional[int] = 0,
    soc_path: Optional[str] = None,
    cluster_desc: Optional[str] = None,
) -> subprocess.CompletedProcess:
    device_architecture = DeviceArchitecture[arch]
    soc_path = soc_path or __default_soc_path[device_architecture]
    cluster_desc = cluster_desc or __default_cluster_desc[device_architecture]

    command = [
        NET2PIPE_BIN_PATH,
        netlist_path,
        out_dir,
        str(global_epoch_start),
        soc_path,
        cluster_desc,
    ]
    result = subprocess.run(command, capture_output=True, env={"ARCH_NAME": arch})
    return result


def run_pipegen(
    pipegen_yaml_path: str,
    output_blob_yaml_path: str,
    arch: str,
    soc_path: Optional[str] = None,
    epoch: int = 0,
) -> subprocess.CompletedProcess:
    device_architecture = DeviceArchitecture[arch]
    soc_path = soc_path or __default_soc_path[device_architecture]

    command = [
        PIPEGEN_BIN_PATH,
        pipegen_yaml_path,
        soc_path,
        output_blob_yaml_path,
        str(epoch),
        "0",
    ]
    result = subprocess.run(command, capture_output=True, env={"ARCH_NAME": arch})
    return result


def run_single_test(test_dir: str, netlist_dir: str, out_dir: str, arch: str) -> TestResult:
    netlist_id = netlist_dir.split("_")[1]
    netlist_path = os.path.join(test_dir, netlist_dir, f"netlist_{netlist_id}.yaml")
    netlist_out_dir = os.path.join(out_dir, f"netlist_{netlist_id}")
    os.makedirs(netlist_out_dir, exist_ok=True)

    # Run net2pipe.
    result = run_net2pipe(netlist_path=netlist_path, out_dir=netlist_out_dir, arch=arch)
    if result.returncode:
        return TestResult.failed("net2pipe failed: \n" + result.stderr.decode("utf-8"))

    # Run pipegen.
    result = run_pipegen(
        pipegen_yaml_path=os.path.join(
            netlist_out_dir, "temporal_epoch_0", "overlay", "pipegen.yaml"
        ),
        output_blob_yaml_path=os.path.join(
            netlist_out_dir, "temporal_epoch_0", "overlay", "blob.yaml"
        ),
        arch=arch,
    )
    shutil.copy2(
        netlist_path, os.path.join(netlist_out_dir, "temporal_epoch_0", "overlay", "netlist.yaml")
    )
    if result.returncode:
        return TestResult.failed("pipegen failed: \n" + result.stderr.decode("utf-8"))

    return TestResult.passed()


def run_tests(test_dir: str, out_dir: str, arch: str, clear_output: bool) -> None:
    os.makedirs(out_dir, exist_ok=True)
    total_fail = 0
    total_pass = 0

    for netlist_dir in os.listdir(test_dir):
        # Skip everything which is not a directory.
        if not netlist_dir.startswith("test_") or not os.path.isdir(
            os.path.join(test_dir, netlist_dir)
        ):
            continue

        result = run_single_test(test_dir, netlist_dir, out_dir, arch)
        print(f"{netlist_dir}: {PASS if result.passed else FAIL}")
        if not result.passed:
            # If the test failed, then we want to know why it failed.
            print(result.message)
            total_fail += 1
        else:
            total_pass += 1

    if clear_output:
        shutil.rmtree(out_dir)

    print("\n\nSummary:")
    print(f"{GRN}\tPassed: {total_pass} / {total_fail + total_pass}{ENDL}")
    print(f"{RED}\tFailed: {total_fail} / {total_fail + total_pass}{ENDL}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--test-dir", type=str, required=True, help="Dir containing test netlists.")
    parser.add_argument(
        "--out-dir",
        type=str,
        required=False,
        default=None,
        help="Folder where output data are stored",
    )
    parser.add_argument(
        "--arch",
        type=str,
        required=False,
        default="grayskull",
        help="Commands [wormhole|grayskull]",
    )
    parser.add_argument(
        "--save-output",
        default=False,
        action="store_true",
        help="Keep net2pipe and pipegen output.",
    )
    args = parser.parse_args()

    out_dir = args.out_dir or os.path.join(args.test_dir, "net2pipe_output")

    run_tests(
        test_dir=args.test_dir, out_dir=out_dir, arch=args.arch, clear_output=not args.save_output
    )


if __name__ == "__main__":
    main()
