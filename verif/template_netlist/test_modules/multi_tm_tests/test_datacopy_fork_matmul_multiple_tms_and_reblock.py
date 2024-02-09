# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys
import os

from z3 import Solver

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.multi_tm_tests.multi_tm_test_base import (
    MultiTMForkingTestBase,
    MultiTMTestParameters,
    StackingGuidingOptions,
)

# TODO get rid of util dependecy
from util import TestType, get_git_root


# Reference modules in a directory unrelated to the current test module
sys.path.insert(0, "verif/template_netlist/test_modules")

# Directory of the current test module
dirname = os.path.dirname(__file__)

# Path to template netlist, relative to the current test module
# Compile template file path based on the forking factor


def compile_template_path():
    return os.path.join(
        get_git_root(),
        f"verif/template_netlist/templates/test_datacopy_fork{MultiTMTestParameters.get_fork_factor()}_matmul_multiple_tms_and_reblock.template.yaml",
    )


# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)


class DatacopyForkMatmulMultiTmTest(MultiTMForkingTestBase):
    def additional_constraints(self) -> None:
        self.constrain_tensor_size(
            self.nodes["input0_dram"], MultiTMTestParameters.get_max_input_tensor_in_tiles(0)
        )
        self.constrain_tensor_size(
            self.nodes["input1_dram"], MultiTMTestParameters.get_max_input_tensor_in_tiles(1)
        )
        for output_idx in range(MultiTMTestParameters.get_fork_factor()):
            output_q = self.nodes[f"output{output_idx}_dram"]
            self.constrain_tensor_size(
                output_q, MultiTMTestParameters.get_max_output_tensor_in_tiles()
            )

        self.solver.add(self.data_format == DataFormat.Float16.value)


# Global objects
solver_var_names = []
sweep_var_names = []
device_architecture: DeviceArchitecture = None
multi_tm_test: DatacopyForkMatmulMultiTmTest = None


def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    """
    solver definition and setup
    """

    global device_architecture, multi_tm_test, sweep_var_names
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    multi_tm_test = DatacopyForkMatmulMultiTmTest(
        solver=solver,
        svars=svars,
        template_path=compile_template_path(),
        # Multiplied by two, since every matmul has 2 inputs
        num_tms_per_conn=MultiTMTestParameters.get_num_tms_per_conn(
            2 * MultiTMTestParameters.get_fork_factor()
        ),
        num_configs_per_tms=MultiTMTestParameters.get_num_configs_per_tms_combination(),
        arch=device_architecture,
        guiding_options=StackingGuidingOptions.create_from_env(),
    )
    svars = multi_tm_test.constraint_model()
    sweep_var_names = multi_tm_test.export_sweep_vars()
    return solver


def extra_config_callback(model_vars):
    assert multi_tm_test, "Multi tm test object has not been instantiated."
    multi_tm_test.postprocess(model_vars)
    model_vars["math_fidelity"] = "HiFi4"
    return model_vars


def valid_config_callback(model_vars, logger):
    assert multi_tm_test, "Multi tm test object has not been instantiated."
    return multi_tm_test.valid_config_callback(model_vars, logger)
