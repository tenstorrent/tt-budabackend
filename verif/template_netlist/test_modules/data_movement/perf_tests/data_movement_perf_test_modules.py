# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestType,
)


__perf_tests_dir = "test_modules.data_movement.perf_tests"
__perf_test_type_to_module_name = {
    # Direct pipes.
    DataMovementPerfTestType.PackerUnicast: f"{__perf_tests_dir}.direct_pipe.test_perf_packer_unicast_pipe",
    DataMovementPerfTestType.PackerScatterUnicast: f"{__perf_tests_dir}.direct_pipe.test_perf_packer_scatter_unicast_pipe",
    DataMovementPerfTestType.DramUnicast: f"{__perf_tests_dir}.direct_pipe.test_perf_dram_unicast_pipe",
    DataMovementPerfTestType.UnicastToDram: f"{__perf_tests_dir}.direct_pipe.test_perf_unicast_to_dram_pipe",
    # Gather pipes.
    DataMovementPerfTestType.DramGather: f"{__perf_tests_dir}.gather_pipe.test_perf_dram_gather_pipe",
    DataMovementPerfTestType.PackerGather: f"{__perf_tests_dir}.gather_pipe.test_perf_packer_gather_pipe",
    DataMovementPerfTestType.PackerForkGather: f"{__perf_tests_dir}.gather_pipe.test_perf_packer_fork_gather_pipe",
    DataMovementPerfTestType.GatherToDram: f"{__perf_tests_dir}.gather_pipe.test_perf_gather_to_dram_pipe",
    # Fork pipes.
    DataMovementPerfTestType.DramSingleFork: f"{__perf_tests_dir}.fork_pipe.test_perf_dram_single_fork_pipe",
    DataMovementPerfTestType.PackerSingleFork: f"{__perf_tests_dir}.fork_pipe.test_perf_packer_single_fork_pipe",
    DataMovementPerfTestType.DramMultiFork: f"{__perf_tests_dir}.fork_pipe.test_perf_dram_multi_fork_pipe",
    DataMovementPerfTestType.PackerMultiFork: f"{__perf_tests_dir}.fork_pipe.test_perf_packer_multi_fork_pipe",
    # Multicast pipes.
    DataMovementPerfTestType.DramMulticast: f"{__perf_tests_dir}.multicast_pipe.test_perf_dram_multicast_pipe",
    DataMovementPerfTestType.DramGatherMulticast: f"{__perf_tests_dir}.multicast_pipe.test_perf_dram_gather_multicast_pipe",
    DataMovementPerfTestType.PackerMulticast: f"{__perf_tests_dir}.multicast_pipe.test_perf_packer_multicast_pipe",
    DataMovementPerfTestType.PackerGatherMulticast: f"{__perf_tests_dir}.multicast_pipe.test_perf_packer_gather_multicast_pipe",
    # Pipe scatter pipes.
    DataMovementPerfTestType.PipeScatter: f"{__perf_tests_dir}.pipe_scatter.test_perf_pipe_scatter",
    DataMovementPerfTestType.GatherPipeScatter: f"{__perf_tests_dir}.pipe_scatter.test_perf_gather_pipe_scatter",
    DataMovementPerfTestType.PipeScatterMulticast: f"{__perf_tests_dir}.pipe_scatter.test_perf_pipe_scatter_multicast",
    DataMovementPerfTestType.GatherPipeScatterMulticast: f"{__perf_tests_dir}.pipe_scatter.test_perf_gather_pipe_scatter_multicast",
}


def get_perf_test_module_name(perf_test_type: DataMovementPerfTestType) -> str:
    assert (
        perf_test_type in __perf_test_type_to_module_name
    ), f"Tests for {perf_test_type} are not yet suported"

    return __perf_test_type_to_module_name[perf_test_type]
