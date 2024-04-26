# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
from typing import List

from z3 import Solver

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.sweep import SweepVarsGroup

from test_modules.data_movement.data_movement_common_constraints import *
from test_modules.data_movement.pipe_tests.data_movement_pipe_test_base import DataMovementPipeTestBase
from util import TestType, get_git_root


TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

template_yaml = os.path.join(
    get_git_root(),
    f"verif/template_netlist/templates/data_movement/pipe_tests/direct_pipe/test_packer_unicast_pipe.template.yaml",
)


class PackerUnicastPipeTest(DataMovementPipeTestBase):
    # @override
    def additional_constraints(self) -> None:
        self.solver.add(self.data_format == DataFormat.Float16.value)

        for op in self.ops:
            self.solver.add(op.buf_size_mb == 2)
            self.solver.add(op.grid_size_x == 1, op.grid_size_y == 1)

        feeder0 = self.nodes["feeder0"]
        target_op = self.nodes["target_op0"]

        self.solver.add(feeder0.grid_loc_y == 0, feeder0.grid_loc_x == 0)
        self.solver.add(target_op.grid_loc_y == 0, target_op.grid_loc_x == 1)

        constrain_no_reblocking_on_connection_except_mb_mn(self.solver, self.nodes["input0_dram"], feeder0)

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        mblock_range = list(range(48, 256, 4))

        input_queue = self.nodes["input0_dram"]
        return [
            SweepVarsGroup(
                var_names_range_dict={
                    input_queue.mb_m.sexpr(): mblock_range,
                    input_queue.mb_n.sexpr(): mblock_range,
                },
                max_num_configs_per_combination=1,
            ),
        ]


# Definitions of callback functions required by the z3 config generator.
solver_var_names = []
sweep_var_names = []
coverage_var_names = []
device_architecture: DeviceArchitecture = None
pipe_test: PackerUnicastPipeTest = None


def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    global device_architecture, pipe_test
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    pipe_test = PackerUnicastPipeTest(
        solver=solver,
        svars=svars,
        general_limits=DataMovementPipeTestBase._create_general_limits(device_architecture),
        template_path=template_yaml,
        device_architecture=device_architecture,
    )
    svars = pipe_test.constraint_model()
    sweep_var_names.extend(pipe_test.export_sweep_vars())
    coverage_var_names.extend(pipe_test.export_coverage_vars())
    return solver


def extra_config_callback(model_vars):
    assert pipe_test, "Pipe test object has not been instantiated."
    pipe_test.postprocess(model_vars)
    model_vars["math_fidelity"] = "HiFi4"
    return model_vars


def valid_config_callback(model_vars, logger):
    assert pipe_test, "Pipe test object has not been instantiated."
    return pipe_test.valid_config_callback(model_vars, logger)
