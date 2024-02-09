# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import os
from logging import Logger
from z3 import *

from test_modules.common.device_architecture import *
from test_modules.common.node import Node
from test_modules.common.data_formats import DataFormat
from test_modules.common.enums import BufferLoc, Untilize
from test_modules.common.sweep import SweepVarsGroup
from test_modules.common.limits import GeneralLimits
from test_modules.common.constants import ARCH_VAR_NAME, TEMPLATE_DIR_PATH
from test_modules.graph_tests.graph_test_base import GraphTestBase
# TODO get rid of util dependecy
from util import TestType

template_yaml = f"{TEMPLATE_DIR_PATH}/test_softmax.template.yaml"

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv('TEST_TYPE', TestType.GraphTest)

softmax_test: GraphTestBase = None
sweep_var_names = []

MAX_INPUT_SIZE_TILES_R = 48
MIN_INPUT_SIZE_TILES_R = 1
MAX_INPUT_SIZE_TILES_C = 48
MIN_INPUT_SIZE_TILES_C = 1
# Size from which to use Float16_b+ data formats
MEDIUM_PRECISION_INPUT_SIZE_TILES_R = 4
MEDIUM_PRECISION_INPUT_SIZE_TILES_C = 4
# Size from which to use Float16+ data formats
HIGH_PRECISION_INPUT_SIZE_TILES_R = 8
HIGH_PRECISION_INPUT_SIZE_TILES_C = 8

class SoftmaxTest(GraphTestBase):
    """Defines constraints for softmax graph tests."""

    # @override
    def additional_constraints(self):
        """Adds constraints specific for softmax graph."""
        medium_precision_input_size_r = (
            MEDIUM_PRECISION_INPUT_SIZE_TILES_R * self.arch.tile_r
        )
        medium_precision_input_size_c = (
            MEDIUM_PRECISION_INPUT_SIZE_TILES_C * self.arch.tile_c
        )
        high_precision_input_size_r = (
            HIGH_PRECISION_INPUT_SIZE_TILES_R * self.arch.tile_r
        )
        high_precision_input_size_c = (
            HIGH_PRECISION_INPUT_SIZE_TILES_C * self.arch.tile_c
        )

        input_queue: Node = self.get_input_queues()[0]

        self.solver.add(Implies(
            input_queue.get_size_r_var() >= medium_precision_input_size_r,
            self.data_format >= DataFormat.Float16_b.value)
        )
        self.solver.add(Implies(
            input_queue.get_size_c_var() >= medium_precision_input_size_c,
            self.data_format >= DataFormat.Float16_b.value)
        )
        self.solver.add(Implies(
            input_queue.get_size_r_var() >= high_precision_input_size_r,
            self.data_format >= DataFormat.Float16.value)
        )
        self.solver.add(Implies(
            input_queue.get_size_c_var() >= high_precision_input_size_c,
            self.data_format >= DataFormat.Float16.value)
        )

    # @override
    def constrain_parameter_queue(self, param_queue: Node):
        """Override. Nothing to do."""
        pass

    def export_sweep_vars(self) -> list[SweepVarsGroup]:
        """Returns list of sweep vars combinations."""
        input_queue: Node = self.get_input_queues()[0]
        output_queue: Node = self.get_output_queues()[0]
        output_op: Node = self.nodes[output_queue.input]

        return [
            SweepVarsGroup(
                {
                    input_queue.get_size_c_var().sexpr() : list(
                        range(self.general_limits.min_input_size_c,
                              self.general_limits.max_input_size_c + 1,
                              self.arch.tile_c)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    input_queue.get_size_r_var().sexpr() : list(
                        range(self.general_limits.min_input_size_r,
                              self.general_limits.max_input_size_r + 1,
                              self.arch.tile_r)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    self.minibatch_count.sexpr() : list(
                        range(self.general_limits.min_minibatch_count,
                              self.general_limits.max_minibatch_count + 1)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    self.microbatch_count.sexpr() : list(
                        range(self.general_limits.min_microbatch_count,
                              self.general_limits.max_microbatch_count + 1)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    self.input_count.sexpr() : list(
                        range(self.general_limits.min_input_count,
                              self.general_limits.max_input_count + 1)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    self.global_t_dim.sexpr() : list(
                        range(self.general_limits.min_t_dim_size,
                              self.general_limits.max_t_dim_size + 1)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    self.data_format.sexpr() : list(
                        range(DataFormat.Bfp2.value,
                              DataFormat.Float32.value + 1)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    output_op.untilize.sexpr() : [
                        Untilize.false.value, Untilize.true.value
                    ]
                },
                1
            ),
            SweepVarsGroup(
                {
                    output_queue.loc.sexpr() : [
                        BufferLoc.dram.value, BufferLoc.host.value
                    ]
                },
                1
            )
        ]

def _create_general_limits(device_arch: DeviceArchitecture) -> GeneralLimits:
    """
    Creates general limits for softmax graph test constraints.

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
    """Adds constraints for softmax graph tests.

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
    global softmax_test, template_yaml

    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    softmax_test = SoftmaxTest(
        solver,
        svars,
        _create_general_limits(device_architecture),
        template_yaml,
        device_architecture
    )

    softmax_test.constraint_model()
    sweep_var_names.extend(softmax_test.export_sweep_vars())

    return solver

def extra_config_callback(model_vars: dict) -> dict:
    """Processes solver generated variable values and converts some of them into
    appropriate format.

    Parameters
    ----------
    model_vars: dict
        Dictionary of z3 variables and their respective values.
    """
    global softmax_test
    assert softmax_test is not None

    softmax_test.postprocess(model_vars)
    model_vars[ARCH_VAR_NAME] = str(softmax_test.arch)

    return model_vars

def valid_config_callback(model_vars: dict, logger: Logger) -> bool:
    """Checks whether generated config given by `model_vars` is valid.

    Parameters
    ----------
    model_vars: dict
        Dictionary of z3 variables and their respective values.
    """
    global softmax_test
    assert softmax_test is not None
    return softmax_test.valid_config_callback(model_vars, logger)