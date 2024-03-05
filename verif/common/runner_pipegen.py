#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Functions for running pipegen commands.
"""
from __future__ import annotations

import os
from enum import Enum
from typing import Optional

from verif.common.runner_utils import (
    DEFAULT_BIN_DIR,
    DEFAULT_SUBPROCESS_TIMEOUT,
    get_soc_file,
    run_cmd,
)

PIPEGEN_BIN_NAME = "pipegen2"
PIPEGEN_MASTER_BIN_NAME = "pipegen2_master"


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


def run_pipegen(
    pipegen_yaml_path: str,
    blob_yaml_path: str,
    arch: str,
    epoch_id: str,
    perf_dump_mode: int = DEFAULT_PERF_DUMP_MODE,
    perf_dump_level: int = DEFAULT_PERF_DUMP_LEVEL,
    soc_descriptor: str = None,
    bin_dir: str = DEFAULT_BIN_DIR,
    pipegen_bin_name: str = PIPEGEN_BIN_NAME,
    timeout: int = DEFAULT_SUBPROCESS_TIMEOUT,
    throw_if_error: bool = False,
):
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
    throw_if_error: bool
        If True, raises RuntimeError if net2pipe fails.

    Returns
    -------
    int, str
        Return code and pipegen command.
    """
    pipegen_cmd = __get_pipegen_cmd(
        pipegen_yaml_path,
        blob_yaml_path,
        arch,
        epoch_id,
        perf_dump_mode,
        perf_dump_level,
        soc_descriptor,
        bin_dir,
        pipegen_bin_name,
    )
    result = run_cmd(pipegen_cmd, arch, timeout)
    if result.returncode != 0:
        if os.path.exists(blob_yaml_path):
            os.remove(blob_yaml_path)
        if throw_if_error:
            raise RuntimeError(
                f"Running pipegen on {pipegen_yaml_path} failed: {result}"
            )
    return result.returncode, pipegen_cmd


def __get_pipegen_cmd(
    pipegen_yaml_path: str,
    blob_yaml_path: str,
    arch: str,
    epoch_id: str,
    perf_dump_mode: int,
    perf_dump_level: int,
    soc_descriptor: Optional[str],
    bin_dir: str,
    pipegen_bin_name: str,
):
    """Returns pipegen command.

    Example command
    ---------------
    build_archs/wormhole_b0/bin/pipegen2
        out/a/output_filter/filtered_yamls_Nothing/wormhole_b0/netlist_softmax_single_tile/pipegen_0.yaml
        ./soc_descriptors/wormhole_b0_8x10.yaml
        out/a/output_pipegen/filtered_yamls_Nothing/wormhole_b0/netlist_softmax_single_tile/blob_0.yaml
        0
        0x200
    """
    soc_file = soc_descriptor or get_soc_file(arch)
    perf_dump_info = hex(perf_dump_mode << 8 | perf_dump_level)
    return (
        f"{bin_dir}/{pipegen_bin_name} {pipegen_yaml_path} {soc_file} {blob_yaml_path} "
        f"{epoch_id} {perf_dump_info}"
    )
