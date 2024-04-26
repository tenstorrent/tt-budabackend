# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
from typing import List

from z3 import *

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.common.sweep import SweepVarsGroup
from test_modules.data_movement.data_movement_common_constraints import *
from test_modules.data_movement.perf_tests.data_movement_perf_constants import (
    MAX_TENSOR_SIZE_IN_TILES,
    MAX_MULTICAST_DESTINATION_BUFFERS,
)
from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestBase,
)
from util import TestType, get_git_root


TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

template_yaml = os.path.join(
    get_git_root(),
    f"verif/template_netlist/templates/data_movement/perf_tests/multicast_pipe/test_perf_dram_multicast_pipe.template.yaml",
)


class DramMulticastPipePerfTest(DataMovementPerfTestBase):

    # @override
    def additional_constraints(self) -> None:
        self.in0_multicast_dest_buffers = self.add_var("in0_multicast_dest_buffers")
        self.in1_multicast_dest_buffers = self.add_var("in1_multicast_dest_buffers")
        self.solver.add(
            self.in0_multicast_dest_buffers >= 1,
            self.in0_multicast_dest_buffers <= MAX_MULTICAST_DESTINATION_BUFFERS,
        )
        self.solver.add(
            self.in1_multicast_dest_buffers >= 1,
            self.in1_multicast_dest_buffers <= MAX_MULTICAST_DESTINATION_BUFFERS,
        )

        for queue in self.queues:
            self.constrain_tensor_size(queue, MAX_TENSOR_SIZE_IN_TILES)

        self.solver.add(self.data_format == DataFormat.Float16.value)

        for node in self.nodes.values():
            self.solver.add(node.t_dim == 1)

        for op in self.ops:
            self.solver.add(op.buf_size_mb == 2)

        dram_input0 = self.nodes["input0_dram"]
        dram_input1 = self.nodes["input1_dram"]
        target_op = self.nodes["target_op0"]
        drainer0 = self.nodes["drainer0"]

        self.solver.add(dram_input0.grid_size_y == 1, dram_input0.grid_size_x == 1)
        self.solver.add(dram_input1.grid_size_x == 1, dram_input1.grid_size_y == 1)
        self.solver.add(
            target_op.grid_size_x == self.in0_multicast_dest_buffers,
            target_op.grid_size_y == self.in1_multicast_dest_buffers,
        )

        self.solver.add(target_op.grid_loc_y == 0, target_op.grid_loc_x == 0)
        self.solver.add(
            If(
                target_op.grid_size_x <= target_op.grid_size_y,
                And(drainer0.grid_loc_y == 1, drainer0.grid_loc_x == target_op.grid_size_x + 1),
                And(drainer0.grid_loc_y == target_op.grid_size_y + 1, drainer0.grid_loc_x == 1),
            )
        )

        constrain_no_reblocking_on_connection(self.solver, target_op, drainer0)

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        in0_mcast_range = list(range(1, MAX_MULTICAST_DESTINATION_BUFFERS + 1))
        in1_mcast_range = list(range(1, MAX_MULTICAST_DESTINATION_BUFFERS + 1))
        return [
            SweepVarsGroup(
                var_names_range_dict={
                    self.in0_multicast_dest_buffers.sexpr(): in0_mcast_range,
                    self.in1_multicast_dest_buffers.sexpr(): in1_mcast_range,
                },
                max_num_configs_per_combination=1,
            ),
        ]


# Definitions of callback functions required by the z3 config generator.
solver_var_names = []
sweep_var_names = []
coverage_var_names = []
device_architecture: DeviceArchitecture = None
pipe_perf_test: DramMulticastPipePerfTest = None


def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    global device_architecture, pipe_perf_test
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    pipe_perf_test = DramMulticastPipePerfTest(
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
