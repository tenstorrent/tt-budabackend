# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
import random
from typing import List

from z3 import Solver

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.common.sweep import SweepVarsGroup
from test_modules.data_movement.data_movement_common_constraints import *
from test_modules.data_movement.perf_tests.data_movement_perf_constants import (
    MAX_TENSOR_SIZE_IN_TILES,
    MAX_GATHER_SOURCE_BUFFERS,
)
from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestBase,
)
from util import TestType, get_git_root


TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

template_yaml = os.path.join(
    get_git_root(),
    f"verif/template_netlist/templates/data_movement/perf_tests/gather_pipe/test_perf_dram_gather_pipe.template.yaml",
)


class DramGatherPipePerfTest(DataMovementPerfTestBase):

    # @override
    def additional_constraints(self) -> None:
        dram_gather_input = self.nodes["input0_dram"]
        self.tensor_size = self.add_var("tensor_size")
        self.solver.add(self.tensor_size == dram_gather_input.t_dim * dram_gather_input.get_horizontal_size_var() * dram_gather_input.get_vertical_size_var())

        self.solver.add(self.data_format == DataFormat.Float16.value)


        for op_node in self.ops:
            self.solver.add(
                op_node.grid_size_x == 1, op_node.grid_size_y == 1
            )

        target_op = self.nodes["target_op0"]
        drainer0 = self.nodes["drainer0"]

        self.solver.add(target_op.grid_loc_y == 0, target_op.grid_loc_x == 0)
        self.solver.add(drainer0.grid_loc_y == 0, drainer0.grid_loc_x == 1)

        constrain_no_reblocking_on_connection(self.solver, target_op, drainer0)

    # @override
    def get_available_dram_channels_for_queue(self, queue: Node) -> List[int]:
        # Assuming wormhole B0 architecture here.
        if queue.name == "input0_dram":
            return [0, 1, 2, 3]
        else:
            return [4]

    # @override
    def pick_dram_channel_for_queue(self, queue: Node, available_dram_channels: List[int]) -> int:
        assert available_dram_channels, f"Out of available dram channels for queue: `{queue.name}`"

        return random.choice(available_dram_channels)

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        target_op = self.nodes["target_op0"]

        def valid_combination_callback(sweep_comb_dict: dict) -> bool:
            ub_r = sweep_comb_dict[target_op.ub_r.sexpr()]
            ub_c = sweep_comb_dict[target_op.ub_c.sexpr()]

            if ub_r * ub_c > 8:
                return False

            size_tiles = sweep_comb_dict[self.tensor_size.sexpr()]
            if size_tiles % ub_r != 0 or size_tiles % ub_c != 0:
                return False

            return True

        return [
            SweepVarsGroup(
                var_names_range_dict={
                    self.tensor_size.sexpr(): list(range(10, 600, 8)) + list(range(600, 3000, 32)),
                    self.data_format.sexpr(): [DataFormat.Float16.value],
                    target_op.ub_r.sexpr(): list(range(1, 9)),
                    target_op.ub_c.sexpr(): list(range(1, 9))
                },
                max_num_configs_per_combination=1,
                valid_combination_callback=valid_combination_callback,
            ),
        ]


# Definitions of callback functions required by the z3 config generator.
solver_var_names = []
sweep_var_names = []
coverage_var_names = []
device_architecture: DeviceArchitecture = None
pipe_perf_test: DramGatherPipePerfTest = None


def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    global device_architecture, pipe_perf_test
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    pipe_perf_test = DramGatherPipePerfTest(
        solver=solver,
        svars=svars,
        template_path=template_yaml,
        device_architecture=device_architecture,
    )
    svars = pipe_perf_test.constraint_model()
    sweep_var_names.extend(pipe_perf_test.export_sweep_vars())
    coverage_var_names.extend(pipe_perf_test.export_coverage_vars())
    return solver


def extra_config_callback(model_vars):
    assert pipe_perf_test, "Pipe perf test object has not been instantiated."
    pipe_perf_test.postprocess(model_vars)
    model_vars["math_fidelity"] = "HiFi4"
    return model_vars


def valid_config_callback(model_vars, logger):
    assert pipe_perf_test, "Pipe perf test object has not been instantiated."
    return pipe_perf_test.valid_config_callback(model_vars, logger)
