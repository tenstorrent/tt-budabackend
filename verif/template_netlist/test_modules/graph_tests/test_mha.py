# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import math
from typing import Any
import os
from logging import Logger
from z3 import *

from test_modules.common.device_architecture import *
from test_modules.common.node import Node
from test_modules.common.data_formats import DataFormat
from test_modules.common.enums import BufferLoc, Untilize, NodeType, Prologue
from test_modules.common.sweep import SweepVarsGroup
from test_modules.common.limits import GeneralLimits
from test_modules.common.constants import ARCH_VAR_NAME, TEMPLATE_DIR_PATH
from test_modules.graph_tests.graph_test_base import GraphTestBase
# TODO get rid of util dependecy
from util import TestType

template_yaml = f"{TEMPLATE_DIR_PATH}/test_mha.template.yaml"

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv('TEST_TYPE', TestType.GraphTest)

mha_test: GraphTestBase = None
sweep_var_names = []

MAX_INPUT_SIZE_TILES_R = 16
MIN_INPUT_SIZE_TILES_R = 1
MAX_INPUT_SIZE_TILES_C = 32
MIN_INPUT_SIZE_TILES_C = 1
MAX_GRAPH_EXECUTION_ITERATIONS = 32
MAX_NUMBER_OF_HEADS = 16
# Size from which to use Float16_b+ data formats.
MEDIUM_PRECISION_INPUT_SIZE_TILES_R = 16
MEDIUM_PRECISION_INPUT_SIZE_TILES_C = 16
# Size from which to use Float16+ data formats.
HIGH_PRECISION_INPUT_SIZE_TILES_C = 32

NUM_HEADS_KEY = "num_heads"
RECIPROCAL_SQRT_HEAD_SIZE_KEY = "reciprocal_of_sqrt_head_size"

class MultiHeadAttentionTest(GraphTestBase):
    """Defines constraints for add & norm graph tests."""

    # @override
    def additional_constraints(self):
        """Adds constraints specific for add & norm graph."""
        # MultiHeadAttention network always has t dimension equal to 1 on the
        # input to the network. So, there is no reason to randomize it. However,
        # since it expands t dimension through slice tms some operations will
        # have t greater than 1.
        self.solver.add(self.global_t_dim == 1)

        # hstack factor of `mha_0_output` op corresponds to number of heads of
        # the attention module.
        mha_output_op = self.nodes["mha_0_output"]
        mha_output_op_in0_tms = self.op_input_tm_map[mha_output_op.name][mha_output_op.inputs[0]]
        hstack_factor = mha_output_op_in0_tms.hstack_factor
        self.solver.add(
            hstack_factor >= 1,
            hstack_factor <= MAX_NUMBER_OF_HEADS,
            hstack_factor % 2 == 0
        )

        # Save num_heads as a variable in svars dictionary for later use.
        num_heads_var = Int(NUM_HEADS_KEY)
        self.solver.add(num_heads_var == hstack_factor)
        self.svars.update({num_heads_var.sexpr(): num_heads_var})

        # Constrain input size to bi divisible by number of heads.
        self.solver.add(
            self.nodes["encoder_input"].get_horizontal_size_var() % hstack_factor == 0
        )

        # Using more precise data formats on large inputs to avoid mismatches.
        # Size from which to use Float16_b+ data formats:
        medium_precision_input_size_r = (
            MEDIUM_PRECISION_INPUT_SIZE_TILES_R * self.arch.tile_r
        )
        medium_precision_input_size_c = (
            MEDIUM_PRECISION_INPUT_SIZE_TILES_C * self.arch.tile_c
        )
        # Size from which to use Float16+ data formats:
        high_precision_input_size_c = (
            HIGH_PRECISION_INPUT_SIZE_TILES_C * self.arch.tile_c
        )

        encoder_input: Node = self.nodes["encoder_input"]

        self.solver.add(Implies(
            encoder_input.get_size_r_var() >= medium_precision_input_size_r,
            self.data_format >= DataFormat.Float16_b.value)
        )
        self.solver.add(Implies(
            encoder_input.get_size_c_var() >= medium_precision_input_size_c,
            self.data_format >= DataFormat.Float16_b.value)
        )
        self.solver.add(Implies(
            encoder_input.get_size_c_var() >= high_precision_input_size_c,
            self.data_format >= DataFormat.Float16.value)
        )

    # @override
    def constrain_input_queue_size(self, input_queue: Node):
        """Overriden method that constrains `attention_mask` queue size with
        respect to the size of `encoder_input`."""
        if input_queue.name == "attention_mask":
            enc_input = self.nodes["encoder_input"]
            self.solver.add(
                input_queue.get_vertical_size_var() == enc_input.get_vertical_size_var()
            )
            self.solver.add(input_queue.get_horizontal_size_var() == 1)
        else:
            super().constrain_input_queue_size(input_queue)

    # @override
    def constrain_output_queue_size(self, output_queue: Node):
        """Overriden method that constrains output queue size to match
        `encoder_input` queue size."""
        enc_input = self.nodes["encoder_input"]
        self.solver.add(
            enc_input.get_vertical_size_var() == output_queue.get_vertical_size_var()
        )
        self.solver.add(
            enc_input.get_horizontal_size_var() == output_queue.get_horizontal_size_var()
        )

    # @override
    def constrain_parameter_queue(self, param_queue: Node):
        """Overriden method that constrains sizes of parameter queues."""
        embedding_size = self.nodes["encoder_input"].get_horizontal_size_var()

        if param_queue.name == "mha.bert.encoder.layer.0.attention.output.dense.bias":
            self._constrain_parameter_queue(param_queue, 1, embedding_size)
        elif param_queue.name == "mha.reciprocal_of_sqrt_of_head_size_0":
            self._constrain_parameter_queue(param_queue, 1, 1)
        elif param_queue.name == "mha.bert.encoder.layer.0.attention.self.key.bias":
            self._constrain_parameter_queue(param_queue, 1, embedding_size)
        elif param_queue.name == "mha.bert.encoder.layer.0.attention.self.key.weight":
            self._constrain_parameter_queue(param_queue, embedding_size, embedding_size)
        elif param_queue.name == "mha.bert.encoder.layer.0.attention.self.query.bias":
            self._constrain_parameter_queue(param_queue, 1, embedding_size)
        elif param_queue.name == "mha.bert.encoder.layer.0.attention.self.query.weight":
            self._constrain_parameter_queue(param_queue, embedding_size, embedding_size)
        elif param_queue.name == "mha.bert.encoder.layer.0.attention.self.value.bias":
            self._constrain_parameter_queue(param_queue, 1, embedding_size)
        elif param_queue.name == "mha.bert.encoder.layer.0.attention.self.value.weight":
            self._constrain_parameter_queue(param_queue, embedding_size, embedding_size)
        elif param_queue.name == "mha.bert.encoder.layer.0.attention.output.dense.weight":
            self._constrain_parameter_queue(param_queue, embedding_size, embedding_size)

    def _constrain_parameter_queue(self, param_queue: Node, r_size: Any, c_size: Any):
        """Helper function to constrain a parameter queue size.

        Parameters
        ----------
        param_queue: Node
            Parameter queue node.
        r_size: Any
            Variable representing `param_queue`'s vertical size.
        c_size: Any
            Variable representing `param_queue`'s horizontal size.
        """
        self.solver.add(param_queue.get_horizontal_size_var() == c_size)
        self.solver.add(param_queue.get_vertical_size_var() == r_size)

    # @override
    def constrain_input_buf_min_size_tiles(self, op: Node, input_index: int) -> None:
        """
        Override function which sets all input_buf_min_size_tiles attributes to
        0, since MHA is a large graph and solver takes a long time to find
        solutions.
        """
        input_buf_min_size_tiles_var = op.get_input_buf_min_size_tiles_var(input_index)

        if input_buf_min_size_tiles_var == 0:
            # Nothing to constrain.
            return

        self.solver.add(
            input_buf_min_size_tiles_var == 0
        )

    # @override
    def constrain_queue_prologue(self, q_node: Node) -> None:
        """
        For large graph such as MHA, set all prologues that we can to false
        to make it easier on solver
        ."""
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
        """Returns list of sweep vars combinations."""
        enc_input = self.nodes["encoder_input"]
        output_queue: Node = self.get_output_queues()[0]
        output_op: Node = self.nodes[output_queue.input]

        return [
            SweepVarsGroup(
                {
                    enc_input.get_size_c_var().sexpr() : list(
                        range(self.general_limits.min_input_size_c,
                              self.general_limits.max_input_size_c + 1,
                              self.arch.tile_c)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    enc_input.get_size_r_var().sexpr() : list(
                        range(self.general_limits.min_input_size_r,
                              self.general_limits.max_input_size_r + 1,
                              self.arch.tile_r)
                    )
                },
                1
            ),
            SweepVarsGroup(
                {
                    NUM_HEADS_KEY : list(range(1, MAX_NUMBER_OF_HEADS + 1))
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
    Creates general limits for MHA graph test constraints.

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
    limits.max_graph_execution_iterations = MAX_GRAPH_EXECUTION_ITERATIONS
    return limits

def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0) -> Solver:
    """Adds constraints for add & norm graph tests.

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
    global mha_test, template_yaml

    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    mha_test = MultiHeadAttentionTest(
        solver,
        svars,
        _create_general_limits(device_architecture),
        template_yaml,
        device_architecture
    )

    mha_test.constraint_model()
    sweep_var_names.extend(mha_test.export_sweep_vars())

    return solver

def extra_config_callback(model_vars: dict) -> dict:
    """Processes solver generated variable values and converts some of them into
    appropriate format.

    Parameters
    ----------
    model_vars: dict
        Dictionary of z3 variables and their respective values.
    """
    global mha_test
    assert mha_test is not None

    mha_test.postprocess(model_vars)

    encoder_input = mha_test.nodes["encoder_input"]
    embedding_size = model_vars[encoder_input.size_c.sexpr()]
    num_heads = model_vars[NUM_HEADS_KEY]

    model_vars[RECIPROCAL_SQRT_HEAD_SIZE_KEY] = (
        1.0 / math.sqrt(embedding_size / num_heads)
    )
    model_vars[ARCH_VAR_NAME] = str(mha_test.arch)

    return model_vars

def valid_config_callback(model_vars: dict, logger: Logger) -> bool:
    """Checks whether generated config given by `model_vars` is valid.

    Parameters
    ----------
    model_vars: dict
        Dictionary of z3 variables and their respective values.
    """
    global mha_test
    assert mha_test is not None
    return mha_test.valid_config_callback(model_vars, logger)