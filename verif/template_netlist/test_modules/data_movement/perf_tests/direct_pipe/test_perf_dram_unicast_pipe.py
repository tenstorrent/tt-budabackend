# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
from typing import List

from z3 import Solver

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.common.sweep import SweepVarsGroup
from test_modules.data_movement.data_movement_common_constraints import *
from test_modules.data_movement.perf_tests.data_movement_perf_constants import (
    MAX_TENSOR_SIZE_IN_TILES,
)
from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestBase,
)
from util import TestType, get_git_root


TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

template_yaml = os.path.join(
    get_git_root(),
    f"verif/template_netlist/templates/data_movement/perf_tests/direct_pipe/test_perf_dram_unicast_pipe.template.yaml",
)


class DramUnicastPipePerfTest(DataMovementPerfTestBase):

    # @override
    def additional_constraints(self) -> None:
        input_q = self.nodes["input0_dram"]
        target_op = self.nodes["target_op0"]
        drainer0 = self.nodes["drainer0"]

        self.y_tiles = self.add_var("y_tiles")
        self.x_tiles = self.add_var("x_tiles")

        self.solver.add(self.y_tiles == input_q.get_vertical_size_var())
        self.solver.add(self.x_tiles == input_q.get_horizontal_size_var())

        self.solver.add(self.data_format == DataFormat.Float16.value)

        for node in self.nodes.values():
            self.solver.add(node.grid_size_x == 1, node.grid_size_y == 1)

        self.solver.add(target_op.grid_loc_y == 0, target_op.grid_loc_x == 0)
        self.solver.add(drainer0.grid_loc_y == 0, drainer0.grid_loc_x == 1)

        # constrain_no_reblocking_on_connection(self.solver, input_q, target_op)
        constrain_no_reblocking_on_connection(self.solver, target_op, drainer0)

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        input_q = self.nodes["input0_dram"]
        target_op = self.nodes["target_op0"]

        return [
            SweepVarsGroup(
                var_names_range_dict={
                    self.data_format.sexpr(): [DataFormat.Float16.value, DataFormat.Bfp8_b.value],

                    input_q.mb_m.sexpr(): [1, 2, 3, 4, 5],
                    input_q.mb_n.sexpr(): [1, 2, 3, 4, 5],
                    input_q.ub_r.sexpr(): list(range(1, 9)),
                    input_q.ub_c.sexpr(): list(range(1, 9)),

                    target_op.ub_r.sexpr(): list(range(1, 9)),
                    target_op.ub_c.sexpr(): list(range(1, 9)),
                },
                max_num_configs_per_combination=1,
            ),
        ]


# Definitions of callback functions required by the z3 config generator.
solver_var_names = []
sweep_var_names = []
coverage_var_names = []
device_architecture: DeviceArchitecture = None
pipe_perf_test: DramUnicastPipePerfTest = None


def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    global device_architecture, pipe_perf_test
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    pipe_perf_test = DramUnicastPipePerfTest(
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
