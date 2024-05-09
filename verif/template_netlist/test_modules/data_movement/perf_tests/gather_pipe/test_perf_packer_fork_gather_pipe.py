# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
from typing import List

from z3 import Solver

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.common.sweep import SweepVarsGroup
from test_modules.data_movement.data_movement_common_constraints import *
from test_modules.data_movement.perf_tests.data_movement_perf_constants import (
    MAX_TENSOR_SIZE_IN_TILES,
    MAX_GATHER_SOURCE_BUFFERS,
)
from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestBase,
)
from util import TestType, get_git_root


TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

template_yaml = os.path.join(
    get_git_root(),
    f"verif/template_netlist/templates/data_movement/perf_tests/gather_pipe/test_perf_packer_fork_gather_pipe.template.yaml",
)


class PackerForkGatherPipePerfTest(DataMovementPerfTestBase):

    # @override
    def additional_constraints(self) -> None:
        feeder_op = self.nodes["feeder_op"]
        target_op = self.nodes["target_op"]
        drainer_op = self.nodes["drainer_op"]

        self.solver.add(feeder_op.t_dim == 1)
        self.solver.add(target_op.t_dim == 1)

        self.solver.add(feeder_op.grid_loc_y == 0, feeder_op.grid_loc_x == 0)

        self.solver.add(target_op.grid_loc_y == 0)
        self.solver.add(target_op.grid_loc_x == feeder_op.grid_loc_x + feeder_op.grid_size_x)

        self.solver.add(drainer_op.grid_loc_y == 0)
        self.solver.add(drainer_op.grid_loc_x == target_op.grid_loc_x + target_op.grid_size_x)

        constrain_no_reblocking_on_connection(self.solver, target_op, drainer_op)

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        feeder_op = self.nodes["feeder_op"]
        target_op = self.nodes["target_op"]

        def valid_combination_callback(sweep_comb_dict: dict) -> bool:
            def is_ublock_valid(ub_r_key, ub_c_key):
                ub_r = sweep_comb_dict[ub_r_key]
                ub_c = sweep_comb_dict[ub_c_key]
                return ub_r * ub_c <= self.arch.max_tiles_in_dest / 2

            # Check if the dimensions are divisible. THis will make sure solver can find mblock for target op.
            def divisible_dim(M_key, U_key, X_key, grid_factor_feeder=1, grid_factor_target=1):
                M = sweep_comb_dict[M_key]
                U = sweep_comb_dict[U_key]
                X = sweep_comb_dict[X_key]
                return (grid_factor_feeder * M * U) % (X * grid_factor_target) == 0

            feeder_ub_r_key = feeder_op.ub_r.sexpr()
            feeder_ub_c_key = feeder_op.ub_c.sexpr()
            feeder_mb_m_key = feeder_op.mb_m.sexpr()
            feeder_mb_n_key = feeder_op.mb_n.sexpr()

            target_ub_r_key = target_op.ub_r.sexpr()
            target_ub_c_key = target_op.ub_c.sexpr()

            feeder_op_height = sweep_comb_dict[feeder_op.grid_size_y.sexpr()]
            target_op_height = sweep_comb_dict[target_op.grid_size_y.sexpr()]
            if feeder_op_height < target_op_height:
                # If feeder op is smaller, it boils down to fork pipe, so we can skip this.
                return False

            if not (
                divisible_dim(
                    feeder_mb_m_key,
                    feeder_ub_r_key,
                    target_ub_r_key,
                    grid_factor_feeder=feeder_op_height,
                    grid_factor_target=target_op_height,
                )
                and divisible_dim(feeder_mb_n_key, feeder_ub_c_key, target_ub_c_key)
            ):
                return False

            if not is_ublock_valid(feeder_ub_r_key, feeder_ub_c_key):
                return False

            if not is_ublock_valid(target_ub_r_key, target_ub_c_key):
                return False

            return True

        ublock_side_dim = list(range(1, 9))

        return [
            SweepVarsGroup(
                var_names_range_dict={
                    self.data_format.sexpr(): [DataFormat.Float16.value, DataFormat.Bfp8_b.value],
                    feeder_op.mb_m.sexpr(): [1, 2],
                    feeder_op.mb_n.sexpr(): [1, 2, 3],
                    feeder_op.ub_r.sexpr(): ublock_side_dim,
                    feeder_op.ub_c.sexpr(): ublock_side_dim,
                    target_op.ub_r.sexpr(): ublock_side_dim,
                    target_op.ub_c.sexpr(): ublock_side_dim,
                    feeder_op.grid_size_x.sexpr(): [1],
                    feeder_op.grid_size_y.sexpr(): list(range(2, 9)),  # 8 is max height
                    target_op.grid_size_x.sexpr(): [1],
                    target_op.grid_size_y.sexpr(): list(range(2, 9)),  # 8 is max height
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
pipe_perf_test: PackerForkGatherPipePerfTest = None


def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    global device_architecture, pipe_perf_test
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    pipe_perf_test = PackerForkGatherPipePerfTest(
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
