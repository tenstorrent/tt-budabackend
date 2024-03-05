#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Functions for running net2pipe commands.
"""
import os

from verif.common.runner_utils import (
    DEFAULT_BIN_DIR,
    get_cluster_descriptors,
    get_soc_file,
    run_cmd,
)

NET2PIPE_BIN_NAME = "net2pipe"


def run_net2pipe(
    netlist_path: str,
    out_dir: str,
    arch: str,
    bin_dir: str = DEFAULT_BIN_DIR,
    throw_if_error: bool = False,
) -> int:
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
    throw_if_error: bool
        If True, raises RuntimeError if net2pipe fails.

    Returns
    -------
    int, str
        Return code and net2pipe command.
    """
    cluster_descriptors = get_cluster_descriptors(arch)
    ret_code = 1
    net2pipe_cmd = "<no command ran>"
    for cluster_descriptor in cluster_descriptors:
        if cluster_descriptor and not os.path.exists(cluster_descriptor):
            continue
        net2pipe_cmd = __get_net2pipe_cmd(
            netlist_path, out_dir, bin_dir, arch, cluster_descriptor
        )
        result = run_cmd(net2pipe_cmd, arch)
        ret_code = result.returncode
        if ret_code == 0:
            break
        else:
            os.system(f"rm -rf {out_dir}/*")
    if ret_code != 0 and throw_if_error:
        raise RuntimeError(f"Running net2pipe on {netlist_path} failed: {result}")
    return ret_code, net2pipe_cmd


def __get_net2pipe_cmd(
    netlist_path: str, out_dir: str, bin_dir: str, arch: str, cluster_descriptor: str
):
    """Returns net2pipe command.

    Example command
    ---------------
    build_archs/wormhole_b0/bin/net2pipe
        ../out/output_netlist_collector/./netlist_softmax_single_tile.yaml
        out/a/output_net2pipe/wormhole_b0/./netlist_softmax_single_tile
        0
        ./soc_descriptors/wormhole_b0_8x10.yaml
        ./verif/multichip_tests/wh_multichip/large_cluster/32chip_wh_cluster_desc.yaml
    """
    soc_file = get_soc_file(arch)
    epoch_start = 0
    return (
        f"{bin_dir}/{NET2PIPE_BIN_NAME} {netlist_path} {out_dir} {epoch_start} {soc_file} "
        f"{cluster_descriptor}"
    )
