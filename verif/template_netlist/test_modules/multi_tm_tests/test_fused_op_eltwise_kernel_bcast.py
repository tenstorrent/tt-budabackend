# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys
import os

from z3 import Solver

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.multi_tm_tests.multi_tm_test_base import MultiTMFusedOpKernelBroadcastTestBase
# TODO get rid of util dependecy
from util import TestType, get_git_root

# Reference modules in a directory unrelated to the current test module
sys.path.insert(0, 'verif/template_netlist/test_modules')

# Directory of the current test module
dirname = os.path.dirname(__file__)

# Path to template netlist, relative to the current test module
template_yaml = os.path.join(
    get_git_root(),
    'verif/template_netlist/templates/test_fused_op_eltwise_kernel_bcast.template.yaml'
)

###############################################################################################
# Test module configuration
###############################################################################################
# Number of inputs of the OP with multiple TMs on input(s)
NUM_INPUTS = 5

# Number of TMs on a dram->op connection
NUM_TMS_PER_CONN = [ int(os.getenv('NUM_TMS_PER_CONN%s' % i, 2)) for i in range(NUM_INPUTS) ]

# Number of configs to generate for each combination of TMs
NUM_CONFIGS_PER_TMS_COMBINATION = int(os.getenv('NUM_CONFIGS_PER_TMS_COMBINATION', 10))

# Test type to be used for running the generated tests in CI
# Valid only when multi_test_netlists is not set to True in test list
TEST_TYPE = os.getenv('TEST_TYPE', TestType.GraphTest)

# Maximum size of tensors in tiles
MAX_INPUT_TENSOR_IN_TILES = os.getenv('MAX_INPUT_TENSOR_IN_TILES', 16*16*64)
MAX_OUTPUT_TENSOR_IN_TILES = os.getenv('MAX_OUTPUT_TENSOR_IN_TILES', 16*16*64)


class FusedOpEltwiseKernelBroadcastTest(MultiTMFusedOpKernelBroadcastTestBase):

    def additional_constraints(self) -> None:
        # Constrain input tensor sizes.
        self.constrain_tensor_size(self.nodes['output_dram'], MAX_OUTPUT_TENSOR_IN_TILES)
        for input_dram_idx in range(NUM_INPUTS):
            input_dram_q = self.nodes[f'input{input_dram_idx}_dram']
            self.constrain_tensor_size(input_dram_q, MAX_INPUT_TENSOR_IN_TILES)

        # Constrain feeder ops to have the same dimensions as input queues, since we don't need
        # any reblocking between the two of them.
        for feeder_op in self.ops:
            if not feeder_op.name.startswith('feeder'):
                continue
            self.constrain_producer_consumer_same_size(
                producer=self.nodes[feeder_op.inputs[0]],
                consumer=feeder_op
            )

        self.solver.add(self.data_format == DataFormat.Float16.value)

# Global objects
solver_var_names = []
sweep_var_names = []
device_architecture: DeviceArchitecture = None
multi_tm_test: FusedOpEltwiseKernelBroadcastTest = None

###############################################################################################
# Define all constraints to be solved in z3_generate_configs.py
###############################################################################################
def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    """
    solver definition and setup
    """

    global device_architecture, multi_tm_test
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    multi_tm_test = FusedOpEltwiseKernelBroadcastTest(
        solver=solver,
        svars=svars,
        template_path=template_yaml,
        num_tms_per_conn=NUM_TMS_PER_CONN,
        num_configs_per_tms=NUM_CONFIGS_PER_TMS_COMBINATION,
        arch=device_architecture
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
    model_vars['math_fidelity'] = "HiFi3"
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
