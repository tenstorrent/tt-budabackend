# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
from typing import List

from z3 import Solver

from test_modules.common.data_formats import DataFormat, get_tile_size
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.common.sweep import SweepVarsGroup
from test_modules.data_movement.data_movement_common_constraints import *
from test_modules.data_movement.perf_tests.data_movement_perf_constants import (
    MAX_TENSOR_SIZE_IN_TILES,
)
from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestBase,
)
from util import TestType, get_git_root


TEST_TYPE = os.getenv("TEST_TYPE", TestType.GraphTest)

template_yaml = os.path.join(
    get_git_root(),
    f"verif/template_netlist/templates/data_movement/perf_tests/direct_pipe/test_perf_packer_scatter_unicast_pipe.template.yaml",
)


class PackerUnicastPipePerfTest(DataMovementPerfTestBase):

    # @override
    def additional_constraints(self) -> None:

        feeder_op = self.nodes["feeder_op"]
        target_op = self.nodes["target_op"]
        drainer_op = self.nodes["drainer_op"]

        # base class already constrains no reblocking on input queue to feeder op
        input_queue = self.nodes["input_dram"]

        self.solver.add(feeder_op.t_dim == 1)
        self.solver.add(target_op.t_dim == 1)
        self.solver.add(feeder_op.grid_loc_y == 0, feeder_op.grid_loc_x == 0)
        self.solver.add(target_op.grid_loc_y == 0, target_op.grid_loc_x == 1)
        self.solver.add(drainer_op.grid_loc_y == 0, drainer_op.grid_loc_x == 2)

        self.solver.add(feeder_op.grid_size_x == 1, feeder_op.grid_size_y == 1)
        self.solver.add(target_op.grid_size_x == 1, target_op.grid_size_y == 1)
        self.solver.add(drainer_op.grid_size_x == 1, drainer_op.grid_size_y == 1)

        # reblocking between feeder and target is constrained by sweep vars

        # no reblocking between target and drainer
        constrain_no_reblocking_on_connection(self.solver, target_op, drainer_op)

        self.input_buf_multiplier = self.add_var("input_buf_multiplier")
        input_buf_min_size_tiles_var = target_op.get_input_buf_min_size_tiles_var(0)
        self.solver.add(
            input_buf_min_size_tiles_var
            == self.input_buf_multiplier * self.get_op_input_buffer_size_tiles(target_op, 0)
        )

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        feeder_op = self.nodes["feeder_op"]
        target_op = self.nodes["target_op"]

        def valid_combination_callback(sweep_comb_dict: dict) -> bool:
            def is_ublock_valid(ub_r_key, ub_c_key):
                ub_r = sweep_comb_dict[ub_r_key]
                ub_c = sweep_comb_dict[ub_c_key]
                return ub_r * ub_c <= self.arch.max_tiles_in_dest / 2

            def get_tensor_size(mb_m_key, mb_n_key, ub_r_key, ub_c_key):
                return (
                    sweep_comb_dict[mb_m_key]
                    * sweep_comb_dict[mb_n_key]
                    * sweep_comb_dict[ub_r_key]
                    * sweep_comb_dict[ub_c_key]
                )

            feeder_ub_r_key = feeder_op.ub_r.sexpr()
            feeder_ub_c_key = feeder_op.ub_c.sexpr()

            target_ub_r_key = target_op.ub_r.sexpr()
            target_ub_c_key = target_op.ub_c.sexpr()

            if not is_ublock_valid(feeder_ub_r_key, feeder_ub_c_key):
                return False

            if not is_ublock_valid(target_ub_r_key, target_ub_c_key):
                return False

            feeder_mb_m_key = feeder_op.mb_m.sexpr()
            feeder_mb_n_key = feeder_op.mb_n.sexpr()

            # Check if target ublock is out of bounds.
            if (
                get_tensor_size(feeder_mb_m_key, feeder_mb_n_key, feeder_ub_r_key, feeder_ub_c_key)
                < sweep_comb_dict[target_ub_r_key] * sweep_comb_dict[target_ub_c_key]
            ):
                return False

            # Check if the dimensions are divisible. THis will make sure solver can find mblock for target op.
            def divisible_dim(M_key, U_key, X_key):
                M = sweep_comb_dict[M_key]
                U = sweep_comb_dict[U_key]
                X = sweep_comb_dict[X_key]
                return M * U % X == 0

            if not (
                divisible_dim(feeder_mb_m_key, feeder_ub_r_key, target_ub_r_key)
                and divisible_dim(feeder_mb_n_key, feeder_ub_c_key, target_ub_c_key)
            ):
                return False

            return True

        return [
            SweepVarsGroup(
                var_names_range_dict={
                    self.data_format.sexpr(): [DataFormat.Float16.value, DataFormat.Bfp8_b.value],
                    self.input_buf_multiplier.sexpr(): [2],

                    feeder_op.mb_m.sexpr(): [1, 2, 3, 4, 5],
                    feeder_op.mb_n.sexpr(): [1, 2, 3, 4, 5],
                    feeder_op.ub_r.sexpr(): list(range(1, 9)),
                    feeder_op.ub_c.sexpr(): list(range(1, 9)),

                    target_op.ub_r.sexpr(): list(range(1, 9)),
                    target_op.ub_c.sexpr(): list(range(1, 9)),
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
pipe_perf_test: PackerUnicastPipePerfTest = None


def constraint_model(solver: Solver, svars: dict, arch_name: str, harvested_rows: int = 0):
    global device_architecture, pipe_perf_test
    device_architecture = DeviceArchitecture.create_from_string(arch_name, harvested_rows)

    pipe_perf_test = PackerUnicastPipePerfTest(
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
