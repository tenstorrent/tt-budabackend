# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys
import os
from typing import List

from z3 import *

from test_modules.common.enums import Prologue, UblockOrder
from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.common.sweep import SweepVarsGroup
from test_modules.multi_tm_tests.multi_tm_test_base import MultiTmTestBase, MultiTMTestParameters

# TODO get rid of util dependecy
from util import TestType, get_git_root

# Reference modules in a directory unrelated to the current test module
sys.path.insert(0, "verif/template_netlist/test_modules")

# Directory of the current test module
dirname = os.path.dirname(__file__)


# Path to template netlist, relative to the current test module
def compile_template_path():
    return os.path.join(
        get_git_root(),
        f"verif/template_netlist/templates/test_op_dram_multiple_tms_and_reblock.template.yaml",
    )


###############################################################################################
# Test module configuration
###############################################################################################
# Number of inputs of the OP with multiple TMs on input(s)
NUM_INPUTS = 1

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)


class OpDramMultiTmTest(MultiTmTestBase):
    def additional_constraints(self) -> None:
        self.constrain_tensor_size(
            self.nodes["input0_dram"], MultiTMTestParameters.get_max_input_tensor_in_tiles(0)
        )
        self.constrain_tensor_size(
            self.nodes["output_dram"], MultiTMTestParameters.get_max_output_tensor_in_tiles()
        )

        self.solver.add(self.data_format == DataFormat.Float16.value)

        in_q = self.nodes["input0_dram"]
        op = self.nodes["datacopy0"]

        self.solver.add(
            in_q.grid_size_x == op.grid_size_x,
            in_q.grid_size_y == op.grid_size_y,
            in_q.mb_m == op.mb_m,
            in_q.mb_n == op.mb_n,
            in_q.ub_r == op.ub_r,
            in_q.ub_c == op.ub_c,
        )

    # @override
    def constrain_output_queue_grid_size(self, q_node: Node) -> None:
        self.x_fork_factor = self.add_var("op_dram_x_fork_factor")
        self.y_fork_factor = self.add_var("op_dram_y_fork_factor")

        q_input_op = self.nodes[q_node.input]

        self.solver.add(self.x_fork_factor * self.y_fork_factor <= self.arch.max_op_queues_fanout)

        self.solver.add(
            And(
                q_node.grid_size_y == self.y_fork_factor * q_input_op.grid_size_y,
                q_node.grid_size_x == self.x_fork_factor * q_input_op.grid_size_x,
            )
        )

        # T dimensions has to match.
        self.solver.add(q_input_op.t_dim == q_node.t_dim)

        self.solver.add(
            Or(
                q_node.ublock_order == UblockOrder.r.value,
                q_node.ublock_order == UblockOrder.c.value,
            )
        )

        # For now, force that op and queue have the same scan order.
        self.solver.add(q_node.ublock_order == q_input_op.ublock_order)

        # Uncomment if you want to force different scan order between the producer op and the consumer queue.
        # self.solver.add(q_node.ublock_order != q_input_op.ublock_order)

    # @override
    def export_coverage_vars(self) -> List[str]:
        output_q = self.nodes["output_dram"]

        return [output_q.ub_r.sexpr(), output_q.ub_c.sexpr()]

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        fork_factor_values = list(range(1, 10))

        return [
            SweepVarsGroup(
                var_names_range_dict={
                    self.x_fork_factor.sexpr(): fork_factor_values,
                    self.y_fork_factor.sexpr(): fork_factor_values,
                },
                max_num_configs_per_combination=self.num_configs_per_tms,
            )
        ]


# Global objects
solver_var_names = []
sweep_var_names = []
coverage_var_names = []
device_architecture: DeviceArchitecture = None
multi_tm_test: OpDramMultiTmTest = None


###############################################################################################
# Define all constraints to be solved in z3_generate_configs.py
###############################################################################################
def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    """
    solver definition and setup
    """

    global device_architecture, multi_tm_test, sweep_var_names, coverage_var_names
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    multi_tm_test = OpDramMultiTmTest(
        solver=solver,
        svars=svars,
        template_path=compile_template_path(),
        num_tms_per_conn=MultiTMTestParameters.get_num_tms_per_conn(NUM_INPUTS),
        num_configs_per_tms=MultiTMTestParameters.get_num_configs_per_tms_combination(),
        arch=device_architecture,
    )
    svars = multi_tm_test.constraint_model()
    sweep_var_names = multi_tm_test.export_sweep_vars()
    coverage_var_names = multi_tm_test.export_coverage_vars()
    return solver


###############################################################################################
# Callback function called from the z3_config_generator.py functions
###############################################################################################
def extra_config_callback(model_vars):
    assert multi_tm_test, "Multi tm test object has not been instantiated."
    multi_tm_test.postprocess(model_vars)
    model_vars["math_fidelity"] = "HiFi3"
    return model_vars


def valid_config_callback(model_vars, logger):
    assert multi_tm_test, "Multi tm test object has not been instantiated."
    return multi_tm_test.valid_config_callback(model_vars, logger)
