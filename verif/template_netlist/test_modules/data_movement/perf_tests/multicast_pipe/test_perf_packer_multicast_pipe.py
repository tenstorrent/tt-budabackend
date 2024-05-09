# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
from typing import List

from z3 import *

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.common.sweep import SweepVarsGroup
from test_modules.data_movement.data_movement_common_constraints import *
from test_modules.data_movement.perf_tests.data_movement_perf_constants import (
    MAX_TENSOR_SIZE_IN_TILES,
    MAX_MULTICAST_DESTINATION_BUFFERS,
)
from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestBase,
)
from util import TestType, get_git_root


TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

template_yaml = os.path.join(
    get_git_root(),
    f"verif/template_netlist/templates/data_movement/perf_tests/multicast_pipe/test_perf_packer_multicast_pipe.template.yaml",
)


class PackerMulticastPipePerfTest(DataMovementPerfTestBase):

    # @override
    def additional_constraints(self) -> None:

        feeder_op = self.nodes["feeder_op"]
        target_op = self.nodes["target_op"]
        drainer_op = self.nodes["drainer_op"]

        self.solver.add(feeder_op.t_dim == 1)
        self.solver.add(target_op.t_dim == 1)

        # Lets do gather+mcast from feeder N,1 cores to 1,M cores.
        self.solver.add(target_op.grid_size_y == 1)

        # Place target core at the bottom of the feeder core because NOC0 goes right and down.
        self.solver.add(target_op.grid_loc_y == feeder_op.grid_size_y - 1)
        self.solver.add(
            target_op.grid_loc_x == feeder_op.grid_loc_x + feeder_op.grid_size_x
        )

        # This is a simple case where the drainer is directly below the target op
        # But this does not test what happens after target has drainer to the right, we should test that as well.
        self.solver.add(
            drainer_op.grid_loc_y == target_op.grid_loc_y + 1,
            drainer_op.grid_loc_x == target_op.grid_loc_x,
        )

        constrain_no_reblocking_on_connection(self.solver, target_op, drainer_op)

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        feeder_op = self.nodes["feeder_op"]
        target_op = self.nodes["target_op"]
        input1 = self.nodes["input1_dram"]

        def valid_combination_callback(sweep_comb_dict: dict) -> bool:
            def is_ublock_valid(ub_r_key, ub_c_key):
                ub_r = sweep_comb_dict[ub_r_key]
                ub_c = sweep_comb_dict[ub_c_key]
                return ub_r * ub_c <= self.arch.max_tiles_in_dest / 2

            # Check if the dimensions are divisible. THis will make sure solver can find mblock for target op.
            def divisible_dim(M_key, U_key, X_key, grid_factor=1):
                M = sweep_comb_dict[M_key]
                U = sweep_comb_dict[U_key]
                X = sweep_comb_dict[X_key]
                return grid_factor * M * U % X == 0

            feeder_ub_r_key = feeder_op.ub_r.sexpr()
            feeder_ub_c_key = feeder_op.ub_c.sexpr()
            feeder_mb_m_key = feeder_op.mb_m.sexpr()
            feeder_mb_n_key = feeder_op.mb_n.sexpr()

            target_ub_r_key = target_op.ub_r.sexpr()

            if not (divisible_dim(feeder_mb_m_key, feeder_ub_r_key, target_ub_r_key)):
                return False

            if not is_ublock_valid(feeder_ub_r_key, feeder_ub_c_key):
                return False

            target_op_ukt = sweep_comb_dict[target_op.u_kt.sexpr()]
            inner_dim_tiles = (
                sweep_comb_dict[feeder_mb_n_key] * sweep_comb_dict[feeder_ub_c_key]
            )
            if inner_dim_tiles % target_op_ukt != 0:
                return False

            return True

        ublock_side_dim = list(range(1, 9))

        return [
            SweepVarsGroup(
                var_names_range_dict={
                    self.data_format.sexpr(): [
                        DataFormat.Float16.value,
                        DataFormat.Bfp8_b.value,
                    ],
                    feeder_op.mb_m.sexpr(): [1, 2],
                    feeder_op.mb_n.sexpr(): [1, 2, 3],
                    feeder_op.ub_r.sexpr(): ublock_side_dim,
                    feeder_op.ub_c.sexpr(): ublock_side_dim,
                    target_op.ub_r.sexpr(): ublock_side_dim,
                    target_op.u_kt.sexpr(): list(range(1, 3 * 8 + 1)),
                    feeder_op.grid_size_x.sexpr(): [1],
                    feeder_op.grid_size_y.sexpr(): [1],
                    target_op.grid_size_x.sexpr(): [1, 2],
                    target_op.grid_size_y.sexpr(): [1],
                },
                max_num_configs_per_combination=1,
                valid_combination_callback=valid_combination_callback,
            ),
        ]


# Definitions of callback functions required by the z3 config generator.
solver_var_names = []
sweep_var_names = []
coverage_var_names = []
device_architecture: DeviceArchitecture = None
pipe_perf_test: PackerMulticastPipePerfTest = None


def constraint_model(
    solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0
):
    global device_architecture, pipe_perf_test
    device_architecture = DeviceArchitecture.create_from_string(
        arch_name, harvested_rows
    )

    pipe_perf_test = PackerMulticastPipePerfTest(
        solver=solver,
        svars=svars,
        template_path=template_yaml,
        device_architecture=device_architecture,
    )
    svars = pipe_perf_test.constraint_model()
    sweep_var_names.extend(pipe_perf_test.export_sweep_vars())
    coverage_var_names.extend(pipe_perf_test.export_coverage_vars())
    return solver


def extra_config_callback(model_vars):
    assert pipe_perf_test, "Pipe perf test object has not been instantiated."
    pipe_perf_test.postprocess(model_vars)
    model_vars["math_fidelity"] = "HiFi4"
    return model_vars


def valid_config_callback(model_vars, logger):
    assert pipe_perf_test, "Pipe perf test object has not been instantiated."
    return pipe_perf_test.valid_config_callback(model_vars, logger)
