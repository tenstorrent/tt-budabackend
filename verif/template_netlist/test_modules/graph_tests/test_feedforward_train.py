# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import os
from logging import Logger
from z3 import *

from test_modules.common.device_architecture import *
from test_modules.common.node import Node
from test_modules.common.data_formats import DataFormat
from test_modules.common.enums import NodeType, Prologue
from test_modules.common.sweep import SweepVarsGroup
from test_modules.common.limits import GeneralLimits
from test_modules.common.constants import ARCH_VAR_NAME, TEMPLATE_DIR_PATH
from test_modules.graph_tests.graph_test_base import GraphTestBase
# TODO get rid of util dependecy
from util import TestType

template_yaml = f'{TEMPLATE_DIR_PATH}/test_feedforward_training.template.yaml'

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv('TEST_TYPE', TestType.GraphTest)

feedforward_train_test: GraphTestBase = None
sweep_var_names = []

MIN_INPUT_SIZE_TILES_R = 1
MAX_INPUT_SIZE_TILES_R = 16
MIN_INPUT_SIZE_TILES_C = 4
MAX_INPUT_SIZE_TILES_C = 50
# Size from which to use Float16_b+ data formats
MEDIUM_PRECISION_INPUT_SIZE_TILES = 8
# Size from which to use Float16+ data formats
HIGH_PRECISION_INPUT_SIZE_TILES = 16

class FeedforwardTrainTest(GraphTestBase):
    """Defines constraints for feedforward train graph tests."""

    # @override
    def additional_constraints(self):
        # Calculating gradients for tensors with t > 1 is not supported, hence
        # generate only netlists with t == 1.
        self.solver.add(self.global_t_dim == 1)

        # Using more precise data formats on large inputs to avoid mismatches.
        medium_precision_input_size = (
            MEDIUM_PRECISION_INPUT_SIZE_TILES * self.arch.tile_r
        )
        high_precision_input_size = (
            HIGH_PRECISION_INPUT_SIZE_TILES * self.arch.tile_r
        )

        input_queue: Node = self.nodes['encoder_input']

        self.solver.add(Implies(
            input_queue.get_size_r_var() >= medium_precision_input_size,
            self.data_format >= DataFormat.Float16_b.value)
        )
        self.solver.add(Implies(
            input_queue.get_size_c_var() >= medium_precision_input_size,
            self.data_format >= DataFormat.Float16_b.value)
        )
        self.solver.add(Implies(
            input_queue.get_size_c_var() >= high_precision_input_size,
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
        if "intermediate.dense.weight" in param_queue.name:
            self._constrain_weights_queue(param_queue, 1, 4)
        elif "output.dense.weight" in param_queue.name:
            self._constrain_weights_queue(param_queue, 4, 1)

    # @override
    def constrain_input_buf_min_size_tiles(self, op: Node, input_index: int) -> None:
        """
        Override function which sets all input_buf_min_size_tiles attributes to
        0, since feedforward train is a large graph and solver takes a long time
        to find solutions.
        """
        input_buf_min_size_tiles_var = op.get_input_buf_min_size_tiles_var(input_index)

        if input_buf_min_size_tiles_var == 0:
            # Nothing to constrain.
            return

        self.solver.add(
            input_buf_min_size_tiles_var == 0
        )

    def _constrain_weights_queue(self,
                                weights_q: Node,
                                m_dim_scaling_factor: int,
                                n_dim_scaling_factor: int):
        """Constrains weights queue.

        Parameters
        ----------
        weights_q: Node
            Weights queue node.
        m_dim_scaling_factor: int
            Factor with which to scale m dimension of weights queue.
        m_dim_scaling_factor: int
            Factor with which to scale n dimension of weights queue.
        """
        input_q = self.nodes['encoder_input']
        self.solver.add(
            weights_q.grid_size_y * weights_q.mb_m * weights_q.ub_r ==
            m_dim_scaling_factor * input_q.grid_size_x * input_q.mb_n * input_q.ub_c
        )
        self.solver.add(
            weights_q.grid_size_x * weights_q.mb_n * weights_q.ub_c ==
            n_dim_scaling_factor * input_q.grid_size_x * input_q.mb_n * input_q.ub_c
        )

    # @override
    def constrain_queue_prologue(self, q_node: Node) -> None:
        """
        For large graph such as FF train, set all prologues that we can to false
        to make it easier on solver.
        """
        feeder_op = self.nodes.get(q_node.input, None)
        grad_queue = feeder_op.gradient_op if feeder_op else False

        for graph_name in self.graphs:
            prologue = q_node.get_prologue_var(graph_name)
            if prologue is None:
                continue

            if (q_node.type == NodeType.ConstantQueue or
                grad_queue and graph_name == feeder_op.graph):
                # Gradient queue has to have prologue and epilogue set to
                # true in the graph of gradient op that is feeding the
                # queue.
                self.solver.add(prologue == Prologue.true.value)
            else:
                # For other queues prologue is set to false to make it easier on
                # solver.
                assert q_node.type != NodeType.OutputQueue
                self.solver.add(prologue == Prologue.false.value)

    def export_sweep_vars(self) -> list[SweepVarsGroup]:
        """Returns sweep variables list."""
        input_q = self.nodes['encoder_input']

        return [
            SweepVarsGroup(
                {
                    input_q.get_size_c_var().sexpr() : list(
                        range(self.general_limits.min_input_size_c,
                              self.general_limits.max_input_size_c + 1,
                              self.arch.tile_c)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    input_q.get_size_r_var().sexpr() : list(
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
    Creates general limits for feedforward graph test constraints.

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
    limits.max_minibatch_count = 1
    limits.max_microbatch_count = 1
    limits.max_t_dim_size = 1
    limits.max_graph_execution_iterations = 32
    return limits

def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0) -> Solver:
    """Adds constraints for feedforward training graph tests.

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
    global feedforward_train_test, template_yaml

    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    feedforward_train_test = FeedforwardTrainTest(
        solver,
        svars,
        _create_general_limits(device_architecture),
        template_yaml,
        device_architecture
    )

    feedforward_train_test.constraint_model()
    sweep_var_names.extend(feedforward_train_test.export_sweep_vars())

    return solver

def extra_config_callback(model_vars: dict):
    """Processes some Z3 variables after solver generated a
    solution to convert them from Int to appropriate form.

    Parameters
    ----------
    model_vars: dict
        Dictionary of variable names and their generated values.
    """
    global feedforward_train_test
    assert feedforward_train_test is not None

    feedforward_train_test.postprocess(model_vars)

    model_vars[ARCH_VAR_NAME] = str(feedforward_train_test.arch)

    return model_vars

def valid_config_callback(model_vars: dict, logger: Logger) -> bool:
    """Checks whether generated config given by `model_vars` is valid.

    Parameters
    ----------
    model_vars: dict
        Dictionary of z3 variables and their respective values.
    """
    global feedforward_train_test
    assert feedforward_train_test is not None
    return feedforward_train_test.valid_config_callback(model_vars, logger)
