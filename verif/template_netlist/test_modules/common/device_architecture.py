#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from dataclasses import dataclass

@dataclass(frozen=True)
class DeviceArchitecture:
    """
    Defines architecture parameters for supported devices.
    """

    # Buda tile size
    tile_r: int
    tile_c: int

    # Tensix grid size
    grid_size_x: int
    grid_size_y: int

    # Tensix L1 memory
    l1_mem_size: int
    fw_l1_mem_size: int
    pipegen_l1_mem_size: int
    max_input_buf_size_tiles: int

    # Tensix DEST register
    max_tiles_in_dest: int

    # NOC
    noc_data_word_width_bytes: int

    # Multichip properties
    max_ethernet_links: int

    # Dram memory
    num_dram_channels: int
    max_num_queue_buffers_accessed: int
    dram_buffer_start_addr: int
    dram_buffer_end_addr: int
    dram_queue_header_size: int

    # Host memory
    host_buffer_start_addr: int
    host_buffer_end_addr: int

    # Pipegen limits
    max_fork_streams_used_per_core: int
    max_extra_streams_used_per_core: int
    max_gather_stream_user_per_core: int
    max_op_queues_fanout: int
    max_stream_phases_per_core: int
    max_queues_op_pipe_inputs: int
    max_gather_relay_streams: int

    # Data format limits
    supports_float32_accumulation: bool

    def get_max_l1_mem_buffer_size(self) -> int:
        """Calculates maximum available space in core L1 memory after
        space for firmware and pipegen is allocated."""
        return self.l1_mem_size - self.fw_l1_mem_size - self.pipegen_l1_mem_size

    def get_dram_buffer_size(self) -> int:
        """Calculates available DRAM buffer size."""
        return self.dram_buffer_end_addr - self.dram_buffer_start_addr + 1

    @staticmethod
    def create_from_string(arch_name: str, harvested_rows: int = 0) -> DeviceArchitecture:
        """Factory method for creating specific device architecture
        instance based on device architecture name.

        Parameters
        ----------
        arch_name: str
            Device architecture name.

        Returns
        -------
        DeviceArchitecture
            Created device architecture instance.
        """

        if arch_name == "grayskull":
            return GrayskullArchitecture(harvested_rows)
        elif arch_name == "wormhole":
            return WormholeArchitecture(harvested_rows)
        elif arch_name == "wormhole_b0":
            return WormholeB0Architecture(harvested_rows)
        else:
            assert False, f"Found an unsupported device architecture: {arch_name}"

    @staticmethod
    def get_supported_arch_names():
        return ["grayskull", "wormhole", "wormhole_b0"]

@dataclass(frozen=True, init=False)
class GrayskullArchitecture(DeviceArchitecture):
    """
    Defines architecture parameters for Grayskull.
    """

    def __init__(self, harvested_rows: int = 0):
        super().__init__(
            tile_r = 32,
            tile_c = 32,
            grid_size_x = 12,
            grid_size_y = 10 - harvested_rows,
            l1_mem_size = 1024 * 1024,
            fw_l1_mem_size = 170 * 1024,
            pipegen_l1_mem_size = 256 * 1024,
            max_input_buf_size_tiles = 500,
            max_tiles_in_dest = 16,
            noc_data_word_width_bytes = 32,
            max_ethernet_links = 0,
            num_dram_channels = 8,
            max_num_queue_buffers_accessed = 40,
            dram_buffer_start_addr = 256 * 1024 * 1024,
            dram_buffer_end_addr = 1024 * 1024 * 1024 - 1,
            dram_queue_header_size = 32,
            host_buffer_start_addr = 0 * 1024 * 1024,
            host_buffer_end_addr = 256 * 1024 * 1024 - 1,
            max_fork_streams_used_per_core = 16,
            max_extra_streams_used_per_core = 24,
            max_gather_stream_user_per_core = 3,
            max_op_queues_fanout = 8,
            max_stream_phases_per_core = 2048,
            max_queues_op_pipe_inputs = 5000,
            max_gather_relay_streams = 3,
            supports_float32_accumulation=False
        )

    def __str__(self):
        return "grayskull"

@dataclass(frozen=True, init=False)
class WormholeArchitecture(DeviceArchitecture):
    """
    Defines architecture parameters for Wormhole.
    """

    def __init__(self, harvested_rows: int = 0):
        super().__init__(
            tile_r = 32,
            tile_c = 32,
            grid_size_x = 8,
            grid_size_y = 10 - harvested_rows,
            l1_mem_size = 1464 * 1024,
            fw_l1_mem_size = 204 * 1024,
            pipegen_l1_mem_size = 256 * 1024,
            max_input_buf_size_tiles = 500,
            max_tiles_in_dest = 64,
            noc_data_word_width_bytes = 32,
            max_ethernet_links = 14,
            num_dram_channels = 6,
            max_num_queue_buffers_accessed = 40,
            dram_buffer_start_addr = 256 * 1024 * 1024,
            dram_buffer_end_addr = 1024 * 1024 * 1024 - 1,
            dram_queue_header_size = 32,
            host_buffer_start_addr = 0 * 1024 * 1024,
            host_buffer_end_addr = 256 * 1024 * 1024 - 1,
            max_fork_streams_used_per_core = 16,
            max_extra_streams_used_per_core = 24,
            max_gather_stream_user_per_core = 4,
            max_op_queues_fanout = 8,
            max_stream_phases_per_core = 2048,
            max_queues_op_pipe_inputs = 2000,
            max_gather_relay_streams = 3,
            supports_float32_accumulation=True
        )

    def __str__(self):
        return "wormhole"

@dataclass(frozen=True, init=False)
class WormholeB0Architecture(DeviceArchitecture):
    """
    Defines architecture parameters for Wormhole B0.
    """

    def __init__(self, harvested_rows: int = 0):
        super().__init__(
            tile_r = 32,
            tile_c = 32,
            grid_size_x = 8,
            grid_size_y = 10 - harvested_rows,
            l1_mem_size = 1464 * 1024,
            fw_l1_mem_size = 204 * 1024,
            pipegen_l1_mem_size = 256 * 1024,
            max_input_buf_size_tiles = 500,
            max_tiles_in_dest = 16,
            noc_data_word_width_bytes = 32,
            max_ethernet_links = 14,
            num_dram_channels = 6,
            max_num_queue_buffers_accessed = 40,
            dram_buffer_start_addr = 256 * 1024 * 1024,
            dram_buffer_end_addr = 1024 * 1024 * 1024 - 1,
            dram_queue_header_size = 32,
            host_buffer_start_addr = 0 * 1024 * 1024,
            host_buffer_end_addr = 256 * 1024 * 1024 - 1,
            max_fork_streams_used_per_core = 16,
            max_extra_streams_used_per_core = 24,
            max_gather_stream_user_per_core = 4,
            max_op_queues_fanout = 8,
            max_stream_phases_per_core = 1800,
            max_queues_op_pipe_inputs = 2000,
            max_gather_relay_streams = 3,
            supports_float32_accumulation=True
        )

    def __str__(self):
        return "wormhole_b0"

# TODO: add other architectures
