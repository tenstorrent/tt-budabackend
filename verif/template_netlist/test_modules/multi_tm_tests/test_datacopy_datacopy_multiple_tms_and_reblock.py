# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys
import os

from z3 import Solver

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
    test_type_extension = "_multichip" if MultiTMTestParameters.get_is_multichip_test() else ""
    return os.path.join(
        get_git_root(),
        f"verif/template_netlist/templates/test_datacopy_datacopy_multiple_tms_and_reblock{test_type_extension}.template.yaml",
    )


###############################################################################################
# Test module configuration
###############################################################################################
# Number of inputs of the OP with multiple TMs on input(s)
NUM_INPUTS = 1

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)


class DatacopyDatacopyMultiTmTest(MultiTmTestBase):
    def additional_constraints(self) -> None:
        self.constrain_tensor_size(
            self.nodes["input0_dram"], MultiTMTestParameters.get_max_input_tensor_in_tiles(0)
        )
        self.constrain_tensor_size(
            self.nodes["output_dram"], MultiTMTestParameters.get_max_output_tensor_in_tiles()
        )

        self.solver.add(self.data_format == DataFormat.Float16.value)

    # @override
    def constrain_point_to_point_connection(self, producer: Node, consumer: Node) -> None:
        super().constrain_point_to_point_connection(producer, consumer)

        if not self.guiding_options.force_keep_padding and producer.is_queue():
            self.solver.add(
                producer.grid_size_x == consumer.grid_size_x,
                producer.grid_size_y == consumer.grid_size_y,
                producer.mb_m == consumer.mb_m,
                producer.mb_n == consumer.mb_n,
                producer.ub_r == consumer.ub_r,
                producer.ub_c == consumer.ub_c,
            )


# Global objects
solver_var_names = []
sweep_var_names = []
coverage_var_names = []
device_architecture: DeviceArchitecture = None
multi_tm_test: DatacopyDatacopyMultiTmTest = None


###############################################################################################
# Define all constraints to be solved in z3_generate_configs.py
###############################################################################################
def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    """
    solver definition and setup
    """

    global device_architecture, multi_tm_test, sweep_var_names, coverage_var_names
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)
    multi_tm_test = DatacopyDatacopyMultiTmTest(
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
    model_vars["math_fidelity"] = "HiFi4"
    return model_vars


def valid_config_callback(model_vars, logger):
    assert multi_tm_test, "Multi tm test object has not been instantiated."
    return multi_tm_test.valid_config_callback(model_vars, logger)
