# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys
import os

from z3 import Solver

from test_modules.common.enums import Prologue, UblockOrder
from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.multi_tm_tests.multi_tm_test_base import MultiTMKernelBroadcastTestBase

# TODO get rid of util dependecy
from util import TestType, get_git_root

# Reference modules in a directory unrelated to the current test module
sys.path.insert(0, "verif/template_netlist/test_modules")

# Directory of the current test module
dirname = os.path.dirname(__file__)

# Path to template netlist, relative to the current test module
IS_PROLOGUE_TEST = int(os.getenv("PROLOGUE_TEST", 0))
IS_MATMUL_BIAS_TEST = int(os.getenv("MATMUL_BIAS", 0))
prologue_extension = "_prologue" if IS_PROLOGUE_TEST else ""
bias_extension = "_with_bias" if IS_MATMUL_BIAS_TEST else ""
template_yaml = os.path.join(
    get_git_root(),
    f"verif/template_netlist/templates/test_dram_input_matmul{bias_extension}_kernel_bcast{prologue_extension}.template.yaml",
)

###############################################################################################
# Test module configuration
###############################################################################################
# Number of inputs of the OP with multiple TMs on input(s)
NUM_INPUTS = 2 if not IS_MATMUL_BIAS_TEST else 3

# Number of TMs on a dram->op connection
NUM_TMS_PER_CONN = [int(os.getenv("NUM_TMS_PER_CONN%s" % i, 2)) for i in range(NUM_INPUTS)]

# Number of configs to generate for each combination of TMs
NUM_CONFIGS_PER_TMS_COMBINATION = int(os.getenv("NUM_CONFIGS_PER_TMS_COMBINATION", 10))

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

# Maximum size of tensors in tiles
MAX_OUTPUT_TENSOR_IN_TILES = os.getenv("MAX_OUTPUT_TENSOR_IN_TILES", 16 * 16 * 64)


class DramInputMatmulSingleTileBroadcastTest(MultiTMKernelBroadcastTestBase):
    def additional_constraints(self) -> None:
        super().additional_constraints()

        self.constrain_tensor_size(self.nodes["output_dram"], MAX_OUTPUT_TENSOR_IN_TILES)

        self.solver.add(self.data_format == DataFormat.Float16.value)

    def constrain_parameter_queue(self, param_queue: Node):
        super().constrain_parameter_queue(param_queue)

        if IS_PROLOGUE_TEST:
            for graph_name in self.graphs:
                self.solver.add(param_queue.get_prologue_var(graph_name) == Prologue.true.value)
                self.solver.add(param_queue.ublock_order == UblockOrder.r.value)


# Global objects
solver_var_names = []
sweep_var_names = []
device_architecture: DeviceArchitecture = None
multi_tm_test: DramInputMatmulSingleTileBroadcastTest = None

###############################################################################################
# Define all constraints to be solved in z3_generate_configs.py
###############################################################################################
def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    """
    solver definition and setup
    """

    global device_architecture, multi_tm_test
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    multi_tm_test = DramInputMatmulSingleTileBroadcastTest(
        solver=solver,
        svars=svars,
        template_path=template_yaml,
        num_tms_per_conn=NUM_TMS_PER_CONN,
        num_configs_per_tms=NUM_CONFIGS_PER_TMS_COMBINATION,
        arch=device_architecture,
    )
    svars = multi_tm_test.constraint_model()
    sweep_var_names.extend(multi_tm_test.export_sweep_vars())
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


def push_sweep_context(context):
    assert multi_tm_test, "Multi tm test object has not been instantiated."
    multi_tm_test.push_sweep_context(context)


def pop_sweep_context():
    assert multi_tm_test, "Multi tm test object has not been instantiated."
    multi_tm_test.pop_sweep_context()
