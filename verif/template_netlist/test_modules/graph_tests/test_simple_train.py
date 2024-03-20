# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import os
from logging import Logger
from z3 import *

from test_modules.common.device_architecture import *
from test_modules.common.node import Node
from test_modules.common.data_formats import DataFormat
from test_modules.common.sweep import SweepVarsGroup
from test_modules.common.limits import GeneralLimits
from test_modules.common.constants import ARCH_VAR_NAME, TEMPLATE_DIR_PATH
from test_modules.graph_tests.graph_test_base import GraphTestBase
# TODO get rid of util dependecy
from util import TestType

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv('TEST_TYPE', TestType.GraphTest)

template_yaml = f'{TEMPLATE_DIR_PATH}/test_simple_training.template.yaml'

simple_train_test: GraphTestBase = None
sweep_var_names = []

MAX_INPUT_SIZE_TILES_C = 32
MAX_INPUT_SIZE_TILES_R = 32
MIN_INPUT_SIZE_TILES_C = 1
MIN_INPUT_SIZE_TILES_R = 1
# Size from which to use Float16_b+ data formats
MEDIUM_PRECISION_INPUT_SIZE_TILES_R = 6
# Size from which to use Float16+ data formats
HIGH_PRECISION_INPUT_SIZE_TILES_C = 16

class SimpleTrainTest(GraphTestBase):
    """Defines constraints for simple train graph tests."""

    # @override
    def additional_constraints(self):
        # Calculating gradients for tensors with t > 1 is not supported, hence
        # generate only netlists with t == 1.
        self.solver.add(self.global_t_dim == 1)

        # Using more precise data formats on large inputs to avoid mismatches.
        medium_precision_input_size_r = (
            MEDIUM_PRECISION_INPUT_SIZE_TILES_R * self.arch.tile_r
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
            input_queue.get_size_c_var() >= high_precision_input_size_c,
            self.data_format >= DataFormat.Float16.value)
        )

    # @override
    def constrain_parameter_queue(self, param_queue: Node):
        """Constrains parameter queue.

        Parameters
        ----------
        param_queue: Node
            Parameter queue node.
        """
        input_q = self.nodes["act1"]
        self.solver.add(
            param_queue.get_horizontal_size_var() == input_q.get_horizontal_size_var()
        )
        self.solver.add(
            param_queue.get_vertical_size_var() == input_q.get_vertical_size_var()
        )

    # @override
    def constrain_input_buf_min_size_tiles(self, op: Node, input_index: int) -> None:
        """
        Override function which sets all input_buf_min_size_tiles attributes to
        0, since simple train is a large graph and solver takes a long time to find
        solutions.
        """
        input_buf_min_size_tiles_var = op.get_input_buf_min_size_tiles_var(input_index)

        if input_buf_min_size_tiles_var == 0:
            # Nothing to constrain.
            return

        self.solver.add(
            input_buf_min_size_tiles_var == 0
        )

    def export_sweep_vars(self) -> list[SweepVarsGroup]:
        """Returns list of sweep vars combinations."""

        input_queue: Node = self.get_input_queues()[0]

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
                    self.data_format.sexpr() : list(
                        range(DataFormat.Bfp2.value,
                              DataFormat.Float32.value + 1)
                    )
                },
                1
            )
        ]

def _create_general_limits(device_arch: DeviceArchitecture) -> GeneralLimits:
    """
    Creates general limits for MHA graph test constraints.

    Parameters
    ----------
    device_arch: DeviceArchitecture
        Device architecture configuration.
    """
    limits = GeneralLimits(device_arch)
    limits.max_input_size_c = MAX_INPUT_SIZE_TILES_C * device_arch.tile_c
    limits.max_input_size_r = MAX_INPUT_SIZE_TILES_R * device_arch.tile_r
    limits.min_input_size_c = MIN_INPUT_SIZE_TILES_C * device_arch.tile_c
    limits.min_input_size_r = MIN_INPUT_SIZE_TILES_R * device_arch.tile_r
    limits.max_graph_execution_iterations = 32
    limits.max_minibatch_count = 4
    return limits

def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0) -> Solver:
    """Adds constraints for simple train graph tests.

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
    global simple_train_test, template_yaml

    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    simple_train_test = SimpleTrainTest(
        solver,
        svars,
        _create_general_limits(device_architecture),
        template_yaml,
        device_architecture
    )

    simple_train_test.constraint_model()
    sweep_var_names.extend(simple_train_test.export_sweep_vars())
    return solver

def extra_config_callback(model_vars: dict):
    """Processes solver generated variable values and converts some of them into
    appropriate format.

    Parameters
    ----------
    model_vars: dict
        Dictionary of z3 variables and their respective values.
    """
    global simple_train_test
    assert simple_train_test is not None

    simple_train_test.postprocess(model_vars)
    model_vars[ARCH_VAR_NAME] = str(simple_train_test.arch)
    return model_vars

def valid_config_callback(model_vars: dict, logger: Logger) -> bool:
    """Checks whether generated config given by `model_vars` is valid.

    Parameters
    ----------
    model_vars: dict
        Dictionary of z3 variables and their respective values.
    """
    global simple_train_test
    assert simple_train_test is not None
    return simple_train_test.valid_config_callback(model_vars, logger)

