# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from z3 import *
from util import *
from test_modules.constraint_utils import *
from test_modules.common.constants import TEMPLATE_DIR_PATH

template_yaml = f'{TEMPLATE_DIR_PATH}/test_dram_input_fork_datacopy.template.yaml'

solver_var_names = [
    'input_tensor_r',
    'input_tensor_c',
    'input_t',
    'input_grid_size_r',
    'input_grid_size_c',
    'input_mb_r',
    'input_mb_c',
    'input_ub_r',
    'input_ub_c',
    'output0_tensor_r',
    'output0_tensor_c',
    'output0_t',
    'output0_grid_size_r',
    'output0_grid_size_c',
    'output0_mb_r',
    'output0_mb_c',
    'output0_ub_r',
    'output0_ub_c',
    'op0_loc_r',
    'op0_loc_c',
    'output1_tensor_r',
    'output1_tensor_c',
    'output1_t',
    'output1_grid_size_r',
    'output1_grid_size_c',
    'output1_mb_r',
    'output1_mb_c',
    'output1_ub_r',
    'output1_ub_c',
    'op1_loc_r',
    'op1_loc_c',
]

def constraint_model(solver,svars):
    """
    solver definition and setup
    """

    max_ub_dim = 8
    min_ub_dim = 1
    max_tensor_dim = 16
    min_tensor_dim = 1

    max_grid_size_r = 10
    max_grid_size_c = 12
    min_grid_size_r = 1
    min_grid_size_c = 1

    max_input_t = 8

    # input / output relationship
    solver.add(svars['input_tensor_r'] == svars['output0_tensor_r'])
    solver.add(svars['input_tensor_c'] == svars['output0_tensor_c'])
    solver.add(svars['input_t'] == svars['output0_t'])

    solver.add(svars['input_tensor_r'] == svars['output1_tensor_r'])
    solver.add(svars['input_tensor_c'] == svars['output1_tensor_c'])
    solver.add(svars['input_t'] == svars['output1_t'])

    # placement - diagonal
    solver.add(svars['op0_loc_r'] >= 0)
    solver.add(svars['op0_loc_c'] >= 0)
    solver.add(svars['op0_loc_r'] + svars['output0_grid_size_r'] <= svars['op1_loc_r'])
    solver.add(svars['op0_loc_c'] + svars['output0_grid_size_c'] <= svars['op1_loc_c'])

    solver.add(svars['op1_loc_r'] >= 0)
    solver.add(svars['op1_loc_c'] >= 0)
    solver.add(svars['op1_loc_r'] + svars['output1_grid_size_r'] <= GRAYSKULL_GRID_R)
    solver.add(svars['op1_loc_c'] + svars['output1_grid_size_c'] <= GRAYSKULL_GRID_C)

    # L1 Memory Usage
    constrain_output_l1(
        solver=solver,
        max_usage_bytes = L1_MAX_USAGE_BYTES,
        tile_size_bytes = FP16_TILE_SIZE,
        mb_r = svars['output0_mb_r'],
        ub_r = svars['output0_ub_r'],
        mb_c = svars['output0_mb_c'],
        ub_c = svars['output0_ub_c']
    )

    constrain_output_l1(
        solver=solver,
        max_usage_bytes = L1_MAX_USAGE_BYTES,
        tile_size_bytes = FP16_TILE_SIZE,
        mb_r = svars['output1_mb_r'],
        ub_r = svars['output1_ub_r'],
        mb_c = svars['output1_mb_c'],
        ub_c = svars['output1_ub_c']
    )

    #input side
    solver.add(max_tensor_dim >= svars['input_tensor_r'])
    solver.add(min_tensor_dim <= svars['input_tensor_r'])
    solver.add(max_tensor_dim >= svars['input_tensor_c'])
    solver.add(min_tensor_dim <= svars['input_tensor_c'])
    solver.add(max_input_t >= svars['input_t'])
    solver.add(0 < svars['input_t'])

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['input_tensor_r'],
        grid_dim_var=svars['input_grid_size_r'],
        mb_dim_var=svars['input_mb_r'],
        ub_dim_var=svars['input_ub_r'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_r, max_grid_size_r),
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['input_tensor_c'],
        grid_dim_var=svars['input_grid_size_c'],
        mb_dim_var=svars['input_mb_c'],
        ub_dim_var=svars['input_ub_c'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_c, max_grid_size_c),
    )

    #output side
    constrain_ublock_size(
        solver=solver,
        ub_r_var=svars['output0_ub_r'],
        ub_c_var=svars['output0_ub_c'],
        max_size=8, # half dest
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['output0_tensor_r'],
        grid_dim_var=svars['output0_grid_size_r'],
        mb_dim_var=svars['output0_mb_r'],
        ub_dim_var=svars['output0_ub_r'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_r, max_grid_size_r),
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['output0_tensor_c'],
        grid_dim_var=svars['output0_grid_size_c'],
        mb_dim_var=svars['output0_mb_c'],
        ub_dim_var=svars['output0_ub_c'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_c, max_grid_size_c),
    )

    constrain_ublock_size(
        solver=solver,
        ub_r_var=svars['output1_ub_r'],
        ub_c_var=svars['output1_ub_c'],
        max_size=8, # half dest
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['output1_tensor_r'],
        grid_dim_var=svars['output1_grid_size_r'],
        mb_dim_var=svars['output1_mb_r'],
        ub_dim_var=svars['output1_ub_r'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_r, max_grid_size_r),
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['output1_tensor_c'],
        grid_dim_var=svars['output1_grid_size_c'],
        mb_dim_var=svars['output1_mb_c'],
        ub_dim_var=svars['output1_ub_c'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_c, max_grid_size_c),
    )

    return solver

def create_dram_buffer_strings(model_vars):

    input_num_buffers, input_buffer_size = calculate_num_buffers_and_size(model_vars['input_grid_size_r'], model_vars['input_grid_size_c'], model_vars['input_entries'], model_vars['input_mb_r'], model_vars['input_mb_c'], model_vars['input_ub_r'], model_vars['input_ub_c'], model_vars['input_t'])
    output0_num_buffers, output0_buffer_size = calculate_num_buffers_and_size(model_vars['output0_grid_size_r'], model_vars['output0_grid_size_c'], model_vars['output0_entries'], model_vars['output0_mb_r'], model_vars['output0_mb_c'], model_vars['output0_ub_r'], model_vars['output0_ub_c'], model_vars['output0_t'])
    output1_num_buffers, output1_buffer_size = calculate_num_buffers_and_size(model_vars['output1_grid_size_r'], model_vars['output1_grid_size_c'], model_vars['output1_entries'], model_vars['output1_mb_r'], model_vars['output1_mb_c'], model_vars['output1_ub_r'], model_vars['output1_ub_c'], model_vars['output1_t'])
    model_vars['input_dram_buffers'] = default_dram_string(channel=0, num_buffers=input_num_buffers, min_buffer_size=input_buffer_size)
    model_vars['output0_dram_buffers'] = default_dram_string(channel=1, num_buffers=output0_num_buffers, min_buffer_size=output0_buffer_size)
    model_vars['output1_dram_buffers'] = default_dram_string(channel=2, num_buffers=output1_num_buffers, min_buffer_size=output1_buffer_size)
    return model_vars

def extra_config_callback(model_vars):
    model_vars = create_dram_buffer_strings(model_vars)
    model_vars['tms'] = '' # No additional TMs
    return model_vars