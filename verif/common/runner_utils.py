# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Utility functions related to running external processes.
"""
from __future__ import annotations

import subprocess
from subprocess import TimeoutExpired

from verif.common.test_utils import DeviceArchs, get_logger

DEFAULT_TOP_LEVEL_BUILD_DIR = "./build"
DEFAULT_BIN_DIR = f"{DEFAULT_TOP_LEVEL_BUILD_DIR}/bin"
DEFAULT_WORMHOLE_B0_SOC = "./soc_descriptors/wormhole_b0_8x10.yaml"
DEFAULT_HARVESTED_WORMHOLE_B0_SOC = "./soc_descriptors/wormhole_b0_80_harvested.yaml"
DEFAULT_GRAYSKULL_SOC = "./soc_descriptors/grayskull_10x12.yaml"
DEFAULT_SUBPROCESS_TIMEOUT = 180
TIMEOUT_RETCODE = 1001

logger = get_logger(__name__)

wormhole_b0_cluster_descriptors = [
    "./verif/multichip_tests/wh_multichip/large_cluster/32chip_wh_cluster_desc.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/12chip_wh_cluster_desc.yaml",
    "./wormhole_2chip_cluster_both_mmio.yaml",
    "./wormhole_2chip_cluster_trimmed.yaml",
    "./wormhole_2chip_cluster.yaml",
    "./wormhole_2x4_sequential_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_2chip_bidirectional_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_2x2_cluster_1_link_per_pair.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_3x3_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_4chip_linear_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_4chip_square_cluster.yaml",
    "./src/net2pipe/unit_tests/cluster_descriptions/wormhole_weird_intersecting_3x3_ring_cluster.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/12chip_wh_cluster_lab68.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/8chip_wh_cluster_jb11.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/4chip_wh_cluster_jb11.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/32chip_wh_cluster_issue_1559.yaml",
    "./verif/multichip_tests/wh_multichip/large_cluster/32chip_wh_cluster_lab78.yaml",
]
grayskull_cluster_descriptors = [
    # Grayskull doesn't need cluster descriptors, passing empty string as argument.
    ""
]


def run_diff(folder1: str, folder2: str):
    """Runs difference on two given folders.

    Parameters
    ----------
    folder1: str
        First folder path.
    folder2: str
        Second folder path.

    Returns
    -------
    int, str
        Return code and diff command.
    """
    diff_cmd = __get_diff_cmd(folder1, folder2)
    return run_cmd(diff_cmd, ""), diff_cmd


def run_cmd(
    command: str, arch: str, timeout=DEFAULT_SUBPROCESS_TIMEOUT
) -> subprocess.CompletedProcess:
    env = {}
    if arch:
        env["ARCH_NAME"] = arch
    cmd = command.split()
    try:
        result = subprocess.run(cmd, capture_output=True, env=env, timeout=timeout)
    except TimeoutExpired:
        logger.error(f"{cmd} timed out")
        return subprocess.CompletedProcess(
            stdout="",
            stderr=f"Timed out after {timeout} seconds",
            returncode=TIMEOUT_RETCODE,
        )
    return result


def get_soc_file(arch: str) -> str:
    if arch == DeviceArchs.GRAYSKULL:
        return DEFAULT_GRAYSKULL_SOC
    elif arch == DeviceArchs.WORMHOLE_B0:
        return DEFAULT_WORMHOLE_B0_SOC
    else:
        assert False, f"Unsupported arch in __get_soc_file(): {arch}"


def get_cluster_descriptors(arch: str) -> list[str]:
    if arch == DeviceArchs.GRAYSKULL:
        return grayskull_cluster_descriptors
    elif arch == DeviceArchs.WORMHOLE_B0:
        return wormhole_b0_cluster_descriptors
    else:
        assert False, f"Unsupported arch in __get_cluster_descriptors(): {arch}"


def __get_diff_cmd(folder1: str, folder2: str):
    return f"diff -rq {folder1} {folder2} --no-dereference"
