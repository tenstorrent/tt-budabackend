# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys
import os

from z3 import Solver, Or, And

from test_modules.common.enums import Prologue, UblockOrder
from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.multi_tm_tests.multi_tm_test_base import MultiTmTestBase, MultiTMTestParameters

# TODO get rid of util dependecy
from util import TestType, get_git_root

# Reference modules in a directory unrelated to the current test module
sys.path.insert(0, "verif/template_netlist/test_modules")

# Directory of the current test module
dirname = os.path.dirname(__file__)


# Path to template netlist, relative to the current test module
def compile_template_path():
    test_type_extension = "_prologue" if MultiTMTestParameters.get_is_prologue_test() else ""
    return os.path.join(
        get_git_root(),
        f"verif/template_netlist/templates/test_dram_input_datacopy_multiple_tms_and_reblock{test_type_extension}.template.yaml",
    )


###############################################################################################
# Test module configuration
###############################################################################################
# Number of inputs of the OP with multiple TMs on input(s)
NUM_INPUTS = 1

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)


class DramInputDatacopyMultiTmTestWithout40DramBuffersLimitation(MultiTmTestBase):
    def additional_constraints(self) -> None:
        self.solver.add(self.data_format == DataFormat.Float16.value)
        
        self.solver.add(self.nodes["datacopy0"].get_grid_size_var() > 1)
        # Make solver generate cases with more than 40 buffers on input and output for every op core.
        self.solver.add(
            self.nodes["input0_dram"].get_grid_size_var() + self.nodes["output_dram"].get_grid_size_var()
            > self.arch.max_num_queue_buffers_accessed * self.nodes["datacopy0"].get_grid_size_var()
        )
        
    # @override
    def constrain_output_queue_grid_size(self, q_node: Node) -> None:
        self.x_fork_factor = self.add_var("op_dram_x_fork_factor")
        self.y_fork_factor = self.add_var("op_dram_y_fork_factor")

        q_input_op = self.nodes[q_node.input]

        self.solver.add(self.x_fork_factor * self.y_fork_factor <= self.arch.max_op_queues_fanout)
        self.solver.add(self.x_fork_factor * self.y_fork_factor > 1)

        self.solver.add(
            And(
                q_node.grid_size_y == self.y_fork_factor * q_input_op.grid_size_y,
                q_node.grid_size_x == self.x_fork_factor * q_input_op.grid_size_x,
            )
        )

        # T dimensions has to match.
        self.solver.add(q_input_op.t_dim == q_node.t_dim)

        # For now, force that op and queue have the same scan order.
        self.solver.add(q_node.ublock_order == q_input_op.ublock_order)

    def constrain_parameter_queue(self, param_queue: Node):
        super().constrain_parameter_queue(param_queue)
        if MultiTMTestParameters.get_is_prologue_test():
            for graph_name in self.graphs:
                self.solver.add(param_queue.get_prologue_var(graph_name) == Prologue.true.value)
                self.solver.add(param_queue.ublock_order == UblockOrder.r.value)


# Global objects
solver_var_names = []
sweep_var_names = []
coverage_var_names = []
device_architecture: DeviceArchitecture = None
multi_tm_test: DramInputDatacopyMultiTmTestWithout40DramBuffersLimitation = None


###############################################################################################
# Define all constraints to be solved in z3_generate_configs.py
###############################################################################################
def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    """
    solver definition and setup
    """

    global device_architecture, multi_tm_test, sweep_var_names, coverage_var_names
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)
    NUM_TMS_PER_CONN = MultiTMTestParameters.get_num_tms_per_conn(NUM_INPUTS)
    NUM_CONFIGS_PER_TMS_COMBINATION = MultiTMTestParameters.get_num_configs_per_tms_combination()
    multi_tm_test = DramInputDatacopyMultiTmTestWithout40DramBuffersLimitation(
        solver=solver,
        svars=svars,
        template_path=compile_template_path(),
        num_tms_per_conn=NUM_TMS_PER_CONN,
        num_configs_per_tms=NUM_CONFIGS_PER_TMS_COMBINATION,
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
