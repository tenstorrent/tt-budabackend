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
    f"verif/template_netlist/templates/data_movement/perf_tests/direct_pipe/test_perf_packer_unicast_pipe.template.yaml",
)


class PackerUnicastPipePerfTest(DataMovementPerfTestBase):

    # @override
    def additional_constraints(self) -> None:

        feeder0 = self.nodes["feeder0"]
        target_op = self.nodes["target_op0"]
        drainer0 = self.nodes["drainer0"]

        self.solver.add(feeder0.t_dim == 1)
        self.solver.add(feeder0.grid_loc_y == 0, feeder0.grid_loc_x == 0)
        self.solver.add(target_op.grid_loc_y == 0, target_op.grid_loc_x == 1)
        self.solver.add(drainer0.grid_loc_y == 0, drainer0.grid_loc_x == 2)

        constrain_no_reblocking_on_connection(self.solver, target_op, drainer0)

        constrain_no_reblocking_on_connection(self.solver, feeder0, target_op)

        self.input_buf_multiplier = self.add_var("input_buf_multiplier")
        input_buf_min_size_tiles_var = target_op.get_input_buf_min_size_tiles_var(0)
        self.solver.add(
            input_buf_min_size_tiles_var
            == self.input_buf_multiplier * self.get_op_input_buffer_size_tiles(target_op, 0)
        )

        self.buf_size_mb_multiplier = self.add_var("buf_size_mb_multiplier")
        self.solver.add(
            If(
                self.buf_size_mb_multiplier > 0,
                feeder0.buf_size_mb == self.buf_size_mb_multiplier * feeder0.t_dim,
                feeder0.buf_size_mb == 2,
            )
        )

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        feeder_op = self.nodes["feeder0"]

        def valid_combination_callback(sweep_comb_dict: dict) -> bool:
            # Purpose of this validation is to limit the number of configurations generated.

            ub_r_key = feeder_op.ub_r.sexpr()
            ub_c_key = feeder_op.ub_c.sexpr()

            ublock_size = sweep_comb_dict[ub_r_key] * sweep_comb_dict[ub_c_key]
            if ublock_size > self.arch.max_tiles_in_dest / 2:
                return False

            mb_m_key = feeder_op.mb_m.sexpr()
            mb_n_key = feeder_op.mb_n.sexpr()

            # If the product of mb_m and mb_n is greater than 10, then the ublock size should be at least 6
            # limits many tests with small ublocks
            if (sweep_comb_dict[mb_m_key] * sweep_comb_dict[mb_n_key] > 10) and ublock_size < 6:
                return False

            input_buf_multiplier_key = self.input_buf_multiplier.sexpr()
            if not input_buf_multiplier_key in sweep_comb_dict:
                return True

            tile_size = get_tile_size(DataFormat(sweep_comb_dict[self.data_format.sexpr()]))
            ublock_size_bytes = ublock_size * tile_size
            input_buf_size_tiles = sweep_comb_dict[self.input_buf_multiplier.sexpr()] * ublock_size
            input_buf_size_bytes = input_buf_size_tiles * tile_size

            _32_kb_in_bytes = 32 * 1024

            # Small ublocks should be a multiple of 4.
            if ublock_size <= 4 and sweep_comb_dict[self.buf_size_mb_multiplier.sexpr()] % 4 != 0:
                return False

            # If we get to 3 ublocks above 32KB, we should not have any more ublocks.
            if input_buf_size_bytes - 3 * ublock_size_bytes > _32_kb_in_bytes:
                return False

            return True

        return [
            SweepVarsGroup(
                var_names_range_dict={
                    self.data_format.sexpr(): [DataFormat.Float16.value, DataFormat.Bfp8_b.value],
                    self.input_buf_multiplier.sexpr(): list(range(2, 24, 2)),
                    self.buf_size_mb_multiplier.sexpr(): list(range(0, 5, 2)),
                    feeder_op.grid_size_x.sexpr(): [1],
                    feeder_op.grid_size_y.sexpr(): [1],
                    feeder_op.mb_m.sexpr(): [1, 2, 4, 5],
                    feeder_op.mb_n.sexpr(): [1, 2, 4, 5],
                    feeder_op.ub_r.sexpr(): list(range(1, 9)),
                    feeder_op.ub_c.sexpr(): list(range(1, 9)),
                },
                max_num_configs_per_combination=1,
                valid_combination_callback=valid_combination_callback,
            ),

            # second sweep group to try grid size > 1 tensix
            SweepVarsGroup(
                var_names_range_dict={
                    self.data_format.sexpr(): [DataFormat.Float16.value],
                    feeder_op.grid_size_x.sexpr(): [1],
                    feeder_op.grid_size_y.sexpr(): [2, 3],
                    feeder_op.mb_m.sexpr(): [1, 2],
                    feeder_op.mb_n.sexpr(): [1, 4],
                    feeder_op.ub_r.sexpr(): [2, 4],
                    feeder_op.ub_c.sexpr(): [2, 4],
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
