# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Example on how to run this module:

GUIDING_ENABLED=1 NUM_TMS_PER_CONN0=0 
python3 verif/template_netlist/generate_ci_tests.py 
--test-module=test_modules.multi_tm_tests.test_dram_input_datacopy_multiple_tms_and_reblock_without_40_dram_buffers_limitation 
--output-dir=test_40_dram_buffers/netlists 
--arch=wormhole_b0 
--search-type=serial-sweep 
--skip-sh-files-gen 
--verbose 
--timeout=12 
--harvested-rows 2 
--num-tests 1000
"""
import os
import sys
from typing import List, Dict

from z3 import Solver

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.enums import BufferLoc
from test_modules.common.sweep import SweepVarsGroup
from test_modules.data_movement.data_movement_common_constraints import constrain_no_reblocking_on_connection
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
        f"verif/template_netlist/templates/"
        f"test_dram_input_datacopy_without_40_dram_buffers_limitation.template.yaml"
    )

# Names used in netlist.
DRAM_INPUT = "dram_input"
TARGET_OP = "target_op"
DRAINER_OP = "drainer_op"
DRAM_OUTPUT = "dram_output"

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
        """Tweak this function to set up test scenario."""
        # To get better perf measurements, set these global vars.
        self.solver.add(self.microbatch_count == 1)
        self.solver.add(self.microbatch_size == 128)
        self.solver.add(self.minibatch_count == 1)

        # Get netlist nodes.
        input_queue = self.nodes[DRAM_INPUT]
        target_op = self.nodes[TARGET_OP]
        drainer_op = self.nodes[DRAINER_OP]
        output_queue = self.nodes[DRAM_OUTPUT]

        # Set all data formats to something used frequently.
        self.solver.add(self.data_format == DataFormat.Bfp8_b.value)

        # Sweep this variable in `export_sweep_vars`.
        self.num_queue_buffers_accessed_per_core = self.add_var("num_queue_buffers_accessed_per_core")

        # Fix input queue shape. Later, we will make sure it is placed on DRAM channels 0 or 1.
        self.solver.add(
            input_queue.grid_size_y == 1, 
            input_queue.grid_size_x == self.num_queue_buffers_accessed_per_core,
            input_queue.mb_m == 1,
            input_queue.mb_n == 1,
            input_queue.ub_r == 1,
            input_queue.ub_c == 8,
            input_queue.t_dim == 1
        )
        # NOTE: By ensuring this constrain below instead of setting t_dim=1, we would have tiles_per_input the same for 
        # all unpackers independent of number of DRAM buffers. It is just a huge number, can't be done. Due to this we 
        # will have periodic values of dram_buf_read_chunk_size_tiles.
        # self.solver.add(input_queue.t_dim * input_queue.grid_size_x * input_queue.grid_size_y == math.lcm(*range(1, 81, 1)))

        # Pick target op shape.
        self.solver.add(
            target_op.grid_size_y == 1, 
            target_op.grid_size_x == 1,
            target_op.mb_m == 1, 
            target_op.mb_n == self.num_queue_buffers_accessed_per_core,
            target_op.ub_r == 1, 
            target_op.ub_c == 8,
            target_op.t_dim == input_queue.t_dim,
            target_op.buf_size_mb == 1,
            target_op.ublock_order == input_queue.ublock_order
        )

        # Place target and drainer op in the same row of tensix cores so NOC links don't overlap.
        self.solver.add(
            target_op.grid_loc_y == 0, 
            target_op.grid_loc_x == 0,
            drainer_op.grid_loc_y == 0,
            drainer_op.grid_loc_x == 1
        )

        # Drainer op same as target op.
        constrain_no_reblocking_on_connection(self.solver, target_op, drainer_op)
        self.solver.add(drainer_op.buf_size_mb == 1)

        # Output queue same as drainer op.
        constrain_no_reblocking_on_connection(self.solver, drainer_op, output_queue)

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        """Tweak this function to choose desired values for number of buffers input queue will have."""
        return [
            SweepVarsGroup(
                var_names_range_dict={
                    self.num_queue_buffers_accessed_per_core.sexpr() : range(1, 150, 1),
                },
                max_num_configs_per_combination=1
            )
        ]

    # @override
    def create_dram_buffer_strings(self, model_vars: Dict[str, any]) -> None:
        """Overridden method to make sure queues are not randomly placed on DRAM channels.
        We want input queue placed on DRAM channels 0 or 1, and output queue placed on 3, or 4 to avoid NOC overlap.
        """
        def get_dram_channel_for_queue(q_node_name, use_backup_channel = False) -> int:
            """Backup channel is used if we run out of space on primary channel."""
            if q_node_name == DRAM_INPUT:
                if not use_backup_channel:
                    return 0
                else:
                    return 1
            elif q_node_name == DRAM_OUTPUT:
                if not use_backup_channel:
                    return 3
                else:
                    return 4

        def pick_dram_channel(q_node_name) -> int:
            has_assigned_dram_channel = False
            dram_channel = get_dram_channel_for_queue(q_node_name)
            
            while not has_assigned_dram_channel:
                q_end_address = curr_start_addr[dram_channel] + num_queue_buffers * single_dram_buffer_size
                if q_end_address < self.arch.dram_buffer_end_addr:
                    has_assigned_dram_channel = True
                else:
                    # We filled this DRAM channel. Use backup channel.
                    dram_channel = get_dram_channel_for_queue(q_node_name, True)

            return dram_channel

        curr_start_addr = [self.arch.dram_buffer_start_addr] * self.arch.num_dram_channels
        curr_host_addr = 0

        for q_node in self.queues:
            grid_size_x = q_node.get_attr_from_model(model_vars, "grid_size_x")
            grid_size_y = q_node.get_attr_from_model(model_vars, "grid_size_y")

            num_queue_buffers = grid_size_x * grid_size_y
            # Single DRAM buffer size is guaranteed to be aligned to arch, since queue headers are aligned as well as 
            # tile sizes.
            single_dram_buffer_size = q_node.get_attr_from_model(model_vars, "buffers_size")
            total_queue_size = num_queue_buffers * single_dram_buffer_size

            if model_vars[q_node.loc.sexpr()] == BufferLoc.host.value:
                model_vars[q_node.loc_template_str] = "[" + hex(curr_host_addr) + "]"
                curr_host_addr += total_queue_size

            elif model_vars[q_node.loc.sexpr()] == BufferLoc.dram.value:
                dram_channel = pick_dram_channel(q_node.name)

                model_vars[q_node.loc_template_str] = self.dram_string(
                    channel=dram_channel,
                    start_addr=curr_start_addr[dram_channel],
                    buffer_count=num_queue_buffers,
                    buffer_size=single_dram_buffer_size,
                )

                curr_start_addr[dram_channel] += total_queue_size
            else:
                raise RuntimeError(f"Queue {q_node.name}'s location is not in DRAM nor on HOST!")


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
