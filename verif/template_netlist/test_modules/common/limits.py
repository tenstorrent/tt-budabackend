# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from test_modules.common.device_architecture import DeviceArchitecture

class GeneralLimits:
    """
    Defines limits for constraining some general graph parameters.
    """

    def __init__(self, device_architecture: DeviceArchitecture):
        self.max_input_size_r: int = 16 * device_architecture.tile_r
        self.min_input_size_r: int = 1 * device_architecture.tile_r
        self.max_input_size_c: int = 50 * device_architecture.tile_c
        self.min_input_size_c: int = 4 * device_architecture.tile_c
        self.max_minibatch_count: int = 8
        self.min_minibatch_count: int = 1
        self.max_microbatch_count: int = 8
        self.min_microbatch_count: int = 1
        self.max_input_count: int = 8
        self.min_input_count: int = 1
        self.max_t_dim_size: int = 8
        self.min_t_dim_size: int = 1
        self.max_graph_execution_iterations: int = 64
        self.min_graph_execution_iterations: int = 1