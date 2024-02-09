#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Runs net2pipe and pipegen commands.
"""
from __future__ import annotations

import os
import subprocess
from subprocess import TimeoutExpired
from typing import Optional
from enum import Enum

from pipegen_tests_utils import *

logger = get_logger(__name__)

DEFAULT_TOP_LEVEL_BUILD_DIR = "./build"
DEFAULT_BIN_DIR = f"{DEFAULT_TOP_LEVEL_BUILD_DIR}/bin"
DEFAULT_SUBPROCESS_TIMEOUT = 180
TIMEOUT_RETCODE = 1001
NET2PIPE_BIN_NAME = "net2pipe"
PIPEGEN_BIN_NAME = "pipegen2"
PIPEGEN_MASTER_BIN_NAME = "pipegen2_master"
DEFAULT_WORMHOLE_B0_SOC = './soc_descriptors/wormhole_b0_8x10.yaml'
DEFAULT_HARVESTED_WORMHOLE_B0_SOC = './soc_descriptors/wormhole_b0_80_harvested.yaml'
DEFAULT_GRAYSKULL_SOC = './soc_descriptors/grayskull_10x12.yaml'

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

class DeviceArchs:
    GRAYSKULL = "grayskull"
    WORMHOLE_B0 = "wormhole_b0"

    @staticmethod
    def get_all_archs() -> list[str]:
        return [DeviceArchs.GRAYSKULL, DeviceArchs.WORMHOLE_B0]

    @staticmethod
    def is_valid_arch(arch: str) -> bool:
        return arch in DeviceArchs.get_all_archs()

class PerfDumpLevel(Enum):
    LEVEL_0 = 0
    LEVEL_1 = 1
    LEVEL_2 = 2

class PerfDumpMode(Enum):
    DRAM_SPILL_PERF_TRACE = 1
    L1_PERF_TRACE = 2
    HOST_SPILL_PERF_TRACE = 3

DEFAULT_PERF_DUMP_LEVEL = PerfDumpLevel.LEVEL_0.value
DEFAULT_PERF_DUMP_MODE = PerfDumpMode.L1_PERF_TRACE.value

def run_net2pipe(netlist_path: str, out_dir: str, arch: str, bin_dir: str = DEFAULT_BIN_DIR) -> int:
    """Runs net2pipe on given netlist. Tries multiple cluster descriptors until it runs successfully.

    Parameters
    ----------
    netlist_path: str
        Netlist path.
    out_dir: str
        Output directory.
    bin_dir: str
        Directory with built binaries.
    arch: str
        Device architecture.

    Returns
    -------
    int, str
        Return code and net2pipe command.
    """
    cluster_descriptors = __get_cluster_descriptors(arch)
    ret_code = 1
    net2pipe_cmd = "<no command ran>"
    for cluster_descriptor in cluster_descriptors:
        if cluster_descriptor and not os.path.exists(cluster_descriptor):
            continue
        net2pipe_cmd = __get_net2pipe_cmd(netlist_path, out_dir, bin_dir, arch, cluster_descriptor)
        ret_code = __run_cmd(net2pipe_cmd, arch)
        if ret_code == 0:
            break
        else:
            os.system(f"rm -rf {out_dir}/*")
    return ret_code, net2pipe_cmd

def run_pipegen(pipegen_yaml_path: str,
                blob_yaml_path: str,
                arch: str,
                epoch_id: str,
                perf_dump_mode: int = DEFAULT_PERF_DUMP_MODE,
                perf_dump_level: int = DEFAULT_PERF_DUMP_LEVEL,
                soc_descriptor: str = None,
                bin_dir: str = DEFAULT_BIN_DIR,
                pipegen_bin_name: str = PIPEGEN_BIN_NAME,
                timeout: int = DEFAULT_SUBPROCESS_TIMEOUT):
    """Runs pipegen with given arguments.

    Parameters
    ----------
    pipegen_yaml_path: str
        Pipegen yaml path.
    blob_yaml_path: str
        Output blob yaml path.
    epoch_id: str
        Epoch id.
    arch: str
        Device architecture.
    perf_dump_mode: int
        Performance dump mode to use.
    perf_dump_level: int
        Level of detail of performance dump.
    soc_descriptor: str
        SoC descriptor to use. If not provided,
        default SoC descr for arch will be used.
    bin_dir: str
        Directory with built binaries.
    pipegen_bin_name: str
        Name of the pipegen binary to run.
    timeout: int
        Execution timeout.

    Returns
    -------
    int, str
        Return code and pipegen command.
    """
    pipegen_cmd = __get_pipegen_cmd(pipegen_yaml_path, blob_yaml_path, arch, epoch_id,
                                    perf_dump_mode, perf_dump_level, soc_descriptor, bin_dir,
                                    pipegen_bin_name)
    return __run_cmd(pipegen_cmd, arch, timeout), pipegen_cmd

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
    return __run_cmd(diff_cmd, ""), diff_cmd

def __get_net2pipe_cmd(netlist_path: str, out_dir: str, bin_dir: str, arch: str,
                       cluster_descriptor: str):
    soc_file = __get_soc_file(arch)
    epoch_start = 0
    return f"{bin_dir}/{NET2PIPE_BIN_NAME} {netlist_path} {out_dir} {epoch_start} {soc_file} " \
           f"{cluster_descriptor}"

def __get_pipegen_cmd(pipegen_yaml_path: str, blob_yaml_path: str, arch: str, epoch_id: str,
                      perf_dump_mode: int, perf_dump_level: int, soc_descriptor: Optional[str],
                      bin_dir: str, pipegen_bin_name: str):
    soc_file = soc_descriptor or __get_soc_file(arch)
    perf_dump_info = hex(perf_dump_mode << 8 | perf_dump_level)
    return f"{bin_dir}/{pipegen_bin_name} {pipegen_yaml_path} {soc_file} {blob_yaml_path} " \
           f"{epoch_id} {perf_dump_info}"

def __get_soc_file(arch: str) -> str:
    if arch == DeviceArchs.GRAYSKULL:
        return DEFAULT_GRAYSKULL_SOC
    elif arch == DeviceArchs.WORMHOLE_B0:
        return DEFAULT_WORMHOLE_B0_SOC
    else:
        assert False,\
            f"Unsupported arch in __get_soc_file(): {arch}"

def __get_cluster_descriptors(arch: str) -> list[str]:
    if arch == DeviceArchs.GRAYSKULL:
        return grayskull_cluster_descriptors
    elif arch == DeviceArchs.WORMHOLE_B0:
        return wormhole_b0_cluster_descriptors
    else:
        assert False,\
            f"Unsupported arch in __get_cluster_descriptors(): {arch}"

def __get_diff_cmd(folder1: str, folder2: str):
    return f"diff -rq {folder1} {folder2} --no-dereference"

def __run_cmd(command: str, arch: str, timeout=DEFAULT_SUBPROCESS_TIMEOUT):
    env = {}
    if arch:
        env['ARCH_NAME'] = arch
    cmd = command.split()
    try:
        result = subprocess.run(cmd, capture_output=True, env=env, timeout=timeout)
    except TimeoutExpired:
        logger.error(f"{cmd} timed out")
        return TIMEOUT_RETCODE
    return result.returncode
