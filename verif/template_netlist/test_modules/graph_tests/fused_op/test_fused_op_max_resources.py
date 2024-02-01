# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import os

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import *
from test_modules.common.limits import GeneralLimits
from test_modules.common.node import Node
from test_modules.common.sweep import SweepVarsGroup
from test_modules.graph_tests.fused_op.fused_op_test_base import \
    FusedOpTestBase
from test_modules.graph_tests.graph_test_base import GraphTestBase
from util import TestType, get_git_root
from z3 import *

MIN_INPUT_SIZE_TILES_R = 1
MAX_INPUT_SIZE_TILES_R = 32
MIN_INPUT_SIZE_TILES_C = 1
MAX_INPUT_SIZE_TILES_C = 32

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

fused_op_test: GraphTestBase = None
sweep_var_names = []
template_yaml = os.path.join(
    get_git_root(),
    "verif/template_netlist/templates/fused_op/test_fused_op_max_resources.template.yaml",
)


class FusedOpMaxResourcesTest(FusedOpTestBase):
    def additional_constraints(self) -> None:
        """Add solver constraints specific to this test."""
        super().additional_constraints()

        # These tests don't focus on testing different micro/mini batch configurations
        # or multi-core operations. The following constraints help us reduce the 
        # execution time of the tests on the CI.
        self.solver.add(self.microbatch_count <= 2)
        self.solver.add(self.microbatch_size <= 2)
        self.solver.add(self.minibatch_count <= 2)

        for op in self.ops:
            self.solver.add(
                op.grid_size_x == 1,
                op.grid_size_y == 1
            )

    def export_sweep_vars(self) -> list[SweepVarsGroup]:
        """Export sweep var groups specific to this test."""
        output_queue: Node = self.get_output_queues()[0]
        output_op: Node = self.nodes[output_queue.input]

        # We chose the following sweep variables to generate configurations:
        #   - row length of the fused op output in tiles
        #   - column length of the fused op output in tiles
        #   - fused op output data formats.
        #
        # This sweep config will give us 1 netlist configuration for each value combination
        # for each sweep variable group. In this case, we will have 1 netlist configuration
        # for each row length, for each column length and for each data format.
        # Other template variables in each configuration are randomized by the solver, so we should
        # have good coverage of different grid sizes, ublock and mblock dimensions and data formats.
        sweep_vars = [
            SweepVarsGroup(
                {
                    output_queue.get_size_r_var().sexpr(): list(
                        range(
                            self.general_limits.min_input_size_r,
                            self.general_limits.max_input_size_r + 1,
                            self.arch.tile_r,
                        )
                    )
                },
                1,
            ),
            SweepVarsGroup(
                {
                    output_queue.get_size_c_var().sexpr(): list(
                        range(
                            self.general_limits.min_input_size_c,
                            self.general_limits.max_input_size_c + 1,
                            self.arch.tile_c,
                        )
                    )
                },
                1,
            ),
            SweepVarsGroup(
                {
                    output_op.out_df.sexpr(): list(
                        range(
                            DataFormat.Bfp8_b.value,
                            (
                                DataFormat.Float32.value
                                if self.arch.supports_float32_accumulation
                                else DataFormat.Float16.value
                            )
                            + 1,
                        )
                    )
                },
                1,
            ),
        ]

        return sweep_vars


def _create_general_limits(device_arch: DeviceArchitecture) -> GeneralLimits:
    """Create general limits for fused op max resources graph test constraints.

    Parameters
    ----------
    device_arch: DeviceArchitecture
        Device architecture configuration.
    """
    limits = GeneralLimits(device_arch)
    limits.max_input_size_r = MAX_INPUT_SIZE_TILES_R * device_arch.tile_r
    limits.min_input_size_r = MIN_INPUT_SIZE_TILES_R * device_arch.tile_r
    limits.max_input_size_c = MAX_INPUT_SIZE_TILES_C * device_arch.tile_c
    limits.min_input_size_c = MIN_INPUT_SIZE_TILES_C * device_arch.tile_c
    return limits


def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0) -> Solver:
    """Add constraints for fused op max resources graph tests.

    Parameters
    ----------
    solver: Solver
        z3 solver.
    svars: dict
        z3 variables dictionary.
    arch_name: str
        Name of device architecture.
    harvested_rows: int
        Number of harvested rows.

    Returns
    -------
    Solver
        Returns solver with all the needed constraints.
    """
    global fused_op_test, template_yaml

    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    fused_op_test = FusedOpMaxResourcesTest(
        solver,
        svars,
        _create_general_limits(device_architecture),
        template_yaml,
        device_architecture,
    )

    fused_op_test.constraint_model()
    sweep_var_names.extend(fused_op_test.export_sweep_vars())

    return solver


def extra_config_callback(model_vars: dict) -> None:
    """Process some Z3 variables after solver generated a
    solution to convert them from Int to appropriate form.

    Parameters
    ----------
    model_vars: dict
        Dictionary of variable names and their generated values.
    """
    assert fused_op_test is not None

    fused_op_test.postprocess(model_vars)

    return model_vars


def valid_config_callback(model_vars: dict, verbose: bool = False) -> bool:
    """Check whether generated config given by `model_vars` is valid.

    Parameters
    ----------
    model_vars: dict
        Dictionary of z3 variables and their respective values.
    """
    assert fused_op_test is not None
    return fused_op_test.valid_config_callback(model_vars, verbose)
