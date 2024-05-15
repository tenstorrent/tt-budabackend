# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import argparse
import os
from typing import Dict, List, Tuple

from blob_comparator import BlobComparator
from ruamel.yaml import YAML

from verif.common.runner_net2pipe import Net2PipeRunner
from verif.common.runner_pipegen import (
    PIPEGEN_MASTER_BIN_NAME,
    PerfDumpLevel,
    PerfDumpMode,
    PipegenRunner,
)
from verif.common.runner_utils import (
    DEFAULT_BIN_DIR,
    DEFAULT_GRAYSKULL_SOC,
    DEFAULT_HARVESTED_WORMHOLE_B0_SOC,
    DEFAULT_WORMHOLE_B0_SOC,
    DeviceArchs,
)
from verif.common.test_utils import (
    create_or_clean_dir,
    get_epoch_dir,
    get_logger,
    get_netlist_arch,
    setup_logger,
)

# This netlists supports both GS and WH_B0 architectures and will produce only one epoch.
DEFAULT_NETLIST_PATH = "verif/graph_tests/netlists/netlist_softmax_single_tile.yaml"
DEFAULT_OUTPUT_DIR = "perf_info_comparison"
EPOCH_ID = 0

logger = get_logger(__name__)


def get_all_soc_descriptors(arch: str) -> List[str]:
    """Returns all SoC descriptors architecture supports."""
    if arch == DeviceArchs.GRAYSKULL:
        return [DEFAULT_GRAYSKULL_SOC]
    elif arch == DeviceArchs.WORMHOLE_B0:
        return [DEFAULT_WORMHOLE_B0_SOC, DEFAULT_HARVESTED_WORMHOLE_B0_SOC]


def get_soc_descr_name(soc_descr_path: str) -> str:
    """Returns SoC descriptor name from its path."""
    return soc_descr_path.split("/")[-1][0:-5]


def get_run_id(
    soc_descriptor: str, perf_mode: PerfDumpMode, perf_level: PerfDumpLevel
) -> Tuple[Dict, str]:
    """
    Returns dict and str which uniquely represent run with chosen parameters. Used for logging and naming blobs.
    """
    return (
        f"SoC descriptor: {get_soc_descr_name(soc_descriptor)}, "
        f"PerfDumpMode: {perf_mode.name}, "
        f"PerfDumpLevel: {perf_level.name}"
    ), f"{get_soc_descr_name(soc_descriptor)}_{perf_mode.name}_{perf_level.name}"


def compare_blob_yamls_perf_info_section(
    original_blob_yaml_path: str,
    new_blob_yaml_path: str,
    comparison_log_path: str,
    comparison_id: Dict,
) -> None:
    """Convenience wrapper around BlobComparator.compare_dram_perf_info_section()."""
    logger.info(f"Comparing {comparison_id}")

    match = False

    try:
        yaml = YAML(typ="safe")
        orig_blob_yaml = yaml.load(open(original_blob_yaml_path))
        new_blob_yaml = yaml.load(open(new_blob_yaml_path))
        blob_comparator = BlobComparator(
            orig_blob_yaml, new_blob_yaml, comparison_log_path
        )
        match = blob_comparator.compare_dram_perf_info_section()
    except Exception as e:
        print(
            f"Exception occurred during comparison of ({original_blob_yaml_path} vs "
            f"{new_blob_yaml_path}) \n {e}"
        )
        match = False

    if match:
        logger.info(
            f"Successfully compared blobs ({original_blob_yaml_path} vs {new_blob_yaml_path}). "
            f"Blobs match.\n"
        )
    else:
        logger.error(
            f"Failed comparing blobs ({original_blob_yaml_path} vs {new_blob_yaml_path}). "
            f"Log located in {comparison_log_path}.\n"
        )


def validate_arch_and_netlist(arch: str, netlist: str) -> str:
    """Checks if netlist exists, if arch is valid and if netlists supports it."""
    if not os.path.exists(netlist):
        raise RuntimeError(f"Netlist {netlist} not found!")

    if not DeviceArchs.is_valid_arch(arch):
        raise RuntimeError(f"Invalid architecture {arch}!")

    if arch not in get_netlist_arch(netlist):
        raise RuntimeError(
            f"Netlist {netlist} does not support chosen architecture {arch}!"
        )


def setup_logging(out_dir: str) -> str:
    """Setup logger and return log path."""
    comparison_log_path = os.path.join(out_dir, "blob_comparison.log")
    setup_logger(comparison_log_path)
    return comparison_log_path


def main(arch: str, out_dir: str = None, netlist: str = None) -> None:
    netlist = netlist if not netlist == None else DEFAULT_NETLIST_PATH
    validate_arch_and_netlist(arch, netlist)

    out_dir = out_dir if not out_dir == None else DEFAULT_OUTPUT_DIR
    create_or_clean_dir(out_dir)

    log_path = setup_logging(out_dir)

    Net2PipeRunner.run_net2pipe(netlist, out_dir, arch, throw_if_error=True)

    pipegen_yaml_path = os.path.join(get_epoch_dir(out_dir, EPOCH_ID), "pipegen.yaml")

    # Iterate over SoC descriptors for chosen arch and over different PerfDumpModes and PerfDumpLevels to make sure
    # pipegens output the same blobs for these combinations.
    for soc_descr in get_all_soc_descriptors(arch):
        for perf_mode in PerfDumpMode:
            for perf_level in PerfDumpLevel:
                # Create unique ID of this run to log nice results.
                comparison_id, blob_prefix = get_run_id(
                    soc_descr, perf_mode, perf_level
                )

                # Generated blobs are uniquely IDd, while everything is logged to one log file.
                blob1_path = os.path.join(out_dir, f"{blob_prefix}_blob1.yaml")
                blob2_path = os.path.join(out_dir, f"{blob_prefix}_blob2.yaml")

                PipegenRunner.run_pipegen(
                    pipegen_yaml_path,
                    blob1_path,
                    arch,
                    EPOCH_ID,
                    perf_mode.value,
                    perf_level.value,
                    soc_descr,
                    pipegen_path=os.path.join(DEFAULT_BIN_DIR, PIPEGEN_MASTER_BIN_NAME),
                    throw_if_error=True,
                )
                PipegenRunner.run_pipegen(
                    pipegen_yaml_path,
                    blob2_path,
                    arch,
                    EPOCH_ID,
                    perf_mode.value,
                    perf_level.value,
                    soc_descr,
                    throw_if_error=True,
                )

                compare_blob_yamls_perf_info_section(
                    blob1_path, blob2_path, log_path, comparison_id
                )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=(
            "Make sure to call for arch for which net2pipe and pipegens were built.\n"
            "Default netlist has already been hardcoded which supports all archs if another is not passed.\n"
            "Default output path has already been hardcoded if another is not passed.\n"
            "For each soc descr + perf mode + perf level combination uniquely IDd blobs are generated for debugging "
            "purposes.\n"
            "Example: python verif/pipegen_tests/compare_blobs_perf_info_sections.py --arch grayskull"
        ),
    )
    parser.add_argument("--arch", type=str, required=True)
    parser.add_argument("--out-dir", type=str, required=False)
    parser.add_argument("--netlist", type=str, required=False)
    args = parser.parse_args()

    main(args.arch, args.out_dir, args.netlist)
