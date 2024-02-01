# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import os
import random

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import *
from test_modules.common.enums import TMS
from test_modules.common.limits import GeneralLimits
from test_modules.common.node import Node
from test_modules.common.sweep import SweepVarsGroup
from test_modules.graph_tests.fused_op.fused_op_test_base import FusedOpTestBase
from test_modules.graph_tests.graph_test_base import GraphTestBase
from util import TestType, get_git_root
from z3 import *

# Number of different configurations generated for one value combination of one sweep
# variable group - in this case sweep variables in the group are broadcast factors.
CONFIGS_PER_COMBINATION = 1

MIN_INPUT_SIZE_TILES_R = 1
MAX_INPUT_SIZE_TILES_R = 32
MIN_INPUT_SIZE_TILES_C = 1
MAX_INPUT_SIZE_TILES_C = 32
MIN_BROADCAST_FACTOR = 2
MAX_BROADCAST_FACTOR = 12

# Number of factor values we will generate for the broadcast factor sweep variable.
BROADCAST_SAMPLE = 5

# Test type to be used for running the generated tests in CI.
# Valid only when multi_test_netlists is not set to True in test list.
TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

fused_op_test: GraphTestBase = None
sweep_var_names = []
template_yaml = os.path.join(
    get_git_root(),
    "verif/template_netlist/templates/fused_op/test_fused_op_matmul_reduce_r_bcast.template.yaml",
)


class FusedOpMatmulReduceRBcastTest(FusedOpTestBase):
    def additional_constraints(self) -> None:
        """Add solver constraints specific to this test."""
        super().additional_constraints()

        # These tests don't focus on testing different micro/mini batch configurations
        # or multi-core operations.
        self.solver.add(self.microbatch_count <= 2)
        self.solver.add(self.microbatch_size <= 2)
        self.solver.add(self.minibatch_count <= 2)

        # R bcast doesn't work with grid size r != 1 and
        # c broadcast doesn't work with grid size c != 1.
        output_queue: Node = self.get_output_queues()[0]
        output_op: Node = self.nodes[output_queue.input]
        self.solver.add(output_op.grid_size_x == 1, output_op.grid_size_y == 1)

    def export_sweep_vars(self) -> list[SweepVarsGroup]:
        """Export sweep var groups specific to this test."""
        output_queue: Node = self.get_output_queues()[0]
        output_op: Node = self.nodes[output_queue.input]

        # We generate preset number of configs per each combination of values per
        # each var group. In this case, we generate preset number of configs for
        # each broadcast factor from the sample for each broadcast in the netlist, 
        # total: 3 bcasts * BROADCAST_SAMPLE * CONFIGS_PER_COMBINATION.
        sweep_vars = [
            SweepVarsGroup(
                {
                    tm.tm_arg.sexpr(): list(
                        random.sample(
                            range(MIN_BROADCAST_FACTOR, MAX_BROADCAST_FACTOR + 1, 1),
                            BROADCAST_SAMPLE,
                        )
                    )
                },
                CONFIGS_PER_COMBINATION,
            )
            for scheduled_op in output_op.scheduled_ops
            for input_name in scheduled_op.inputs
            for tm in self.op_input_tm_map[scheduled_op.name][input_name].get_tms()
            if tm.tm_type in [TMS.r_broadcast.value, TMS.c_broadcast.value]
        ]

        return sweep_vars


def _create_general_limits(device_arch: DeviceArchitecture) -> GeneralLimits:
    """Create general limits for fused op matmul reduce broadcast graph test constraints.

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
    """Add constraints for fused op matmul reduce broadcast graph tests.

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

    fused_op_test = FusedOpMatmulReduceRBcastTest(
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
