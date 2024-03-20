# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from z3 import *
from util import *
from typing import Dict
from enum import Enum
from feeder_drainer_utils import *
from constraint_utils import *

template_yaml = f'{TEMPLATE_DIR_PATH}/test_feeder_drainer_unary.template.yaml'

def get_all_active_tm_types():
    all_tms_type_vals = [e.value for e in TMType]
    all_tms_type_vals.remove(1)
    return all_tms_type_vals

solver_var_names = [
    'target_device',
    'input_count',
    'input_buf_size_mb',
    'output_buf_size_mb',
    'input0_tensor_r',
    'input0_tensor_c',
    'input0_t',
    'input0_grid_size_r',
    'input0_grid_size_c',
    'input0_mb_r',
    'input0_mb_c',
    'input0_ub_r',
    'input0_ub_c',
    'output_tensor_r',
    'output_tensor_c',
    'output_t',
    'output_grid_size_r',
    'output_grid_size_c',
    'output_mb_r',
    'output_mb_c',
    'output_ub_r',
    'output_ub_c',
    'grid_loc_r',
    'grid_loc_c',
    'feeder0_grid_loc_r',
    'feeder0_grid_loc_c',
    'drainer_grid_loc_r',
    'drainer_grid_loc_c',
    'unary_type',
    'factor',
    'tm_type',
]

perf_fixed_vars = {
    'target_device': None,
    'input_count': None,
    'input_buf_size_mb': None,
    'output_buf_size_mb': None,
    'input0_tensor_r': None,
    'input0_tensor_c': None,
    'input0_mb_r': None,
    'input0_mb_c': None,
    'input0_ub_r': None,
    'input0_ub_c': None,
    'output_tensor_r': None,
    'output_tensor_c': None,
    'output_t': None,
    'grid_loc_r': None,
    'grid_loc_c': 0,
    'feeder0_grid_loc_r': 0,
    'feeder0_grid_loc_c': 0,
    'drainer_grid_loc_r': None,
    'drainer_grid_loc_c': None,
    'input0_grid_size_r': None,
    'input0_grid_size_c': None,
    'unary_type': 8,
}

perf_sweep_vars = {
    'input0_t': [2],

    'output_mb_r': [2, 4, 8, 16],
    'output_mb_c': [2, 4, 8, 16],
    'output_ub_r': [1, 2, 4, 8],
    'output_ub_c': [1, 2, 4, 8],

    'output_grid_size_r': [1],
    'output_grid_size_c': [1],

    'factor': [2],
    'tm_type': get_all_active_tm_types(),
}


def constraint_model(solver, svars, arch="grayskull"):
    """
    solver definition and setup
    """
    solver.add(svars['target_device'] == 0)
    solver.add(svars['input_count'] == 128)
    solver.add(svars['factor'] >= 1)

    max_mm_ub_dim = 8
    min_mm_ub_dim = 1
    solver.add(svars['output_ub_r'] * svars['output_ub_c'] <= 8) # Half dest
    solver.add(svars['output_ub_r'] >= min_mm_ub_dim)
    solver.add(svars['output_ub_r'] <= max_mm_ub_dim)
    solver.add(svars['output_ub_c'] >= min_mm_ub_dim)
    solver.add(svars['output_ub_c'] <= max_mm_ub_dim)

    max_ub_dim = 8
    min_ub_dim = 1

    max_tensor_r = 128
    min_tensor_r = 1
    max_tensor_c = 128
    min_tensor_c = 1

    # grid sizing for ops
    max_grid_size_r = 10
    max_grid_size_c = 12
    min_grid_size_r = 1
    min_grid_size_c = 1

    # t constraints
    max_input_t = 8
    solver.add(max_input_t >= svars['input0_t'])
    solver.add(svars['input0_t'] > 0)
    solver.add(svars['output_t'] > 0)
    solver.add(svars['input_buf_size_mb'] == 2 * svars['input0_t'])
    solver.add(svars['output_buf_size_mb'] == 2 * svars['output_t'])
    # solver.add(svars['buf_size_mb'] == 2)
    constrain_tms(
        solver=solver,
        tm_type=svars['tm_type'],
        tm_factor=svars['factor'],
        input_t=svars['input0_t'],
        output_t=svars['output_t'],
        input_tensor=[svars['input0_tensor_r'], svars['input0_tensor_c']],
        output_tensor=[svars['output_tensor_r'], svars['output_tensor_c']],
    )

    constrain_feeder_drainer_grid_loc(
        solver,
        [max_grid_size_r, max_grid_size_c],
        [[svars['feeder0_grid_loc_r'], svars['feeder0_grid_loc_c']]],
        [svars['grid_loc_r'], svars['grid_loc_c']],
        [svars['drainer_grid_loc_r'], svars['drainer_grid_loc_c']],
        [[svars['input0_grid_size_r'], svars['input0_grid_size_c']]],
        [svars['output_grid_size_r'], svars['output_grid_size_c']],
        [svars['output_grid_size_r'], svars['output_grid_size_c']],
    )

    # L1 Memory Usage
    constrain_output_l1(
        solver=solver,
        max_usage_bytes = L1_MAX_USAGE_BYTES,
        tile_size_bytes = BFP8_TILE_SIZE,
        mb_r = svars['output_mb_r'],
        ub_r = svars['output_ub_r'],
        mb_c = svars['output_mb_c'],
        ub_c = svars['output_ub_c'],
        buf_size_mb = svars['output_buf_size_mb']
    )
    # L1 Memory Usage
    constrain_output_l1(
        solver=solver,
        max_usage_bytes = L1_MAX_USAGE_BYTES,
        tile_size_bytes = BFP8_TILE_SIZE,
        mb_r = svars['input0_mb_r'],
        ub_r = svars['input0_ub_r'],
        mb_c = svars['input0_mb_c'],
        ub_c = svars['input0_ub_c'],
        buf_size_mb = svars['input_buf_size_mb']
    )
    #input0 side
    solver.add(max_tensor_r * svars['output_grid_size_r'] >= svars['input0_tensor_r'])
    solver.add(min_tensor_r <= svars['input0_tensor_r'])
    solver.add(max_tensor_c * svars['output_grid_size_c'] >= svars['input0_tensor_c'])
    solver.add(min_tensor_c <= svars['input0_tensor_c'])

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['input0_tensor_r'],
        grid_dim_var=svars['input0_grid_size_r'],
        mb_dim_var=svars['input0_mb_r'],
        ub_dim_var=svars['input0_ub_r'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_r, max_grid_size_r),
    )

    solver.add(
        If (
            svars["tm_type"] != TMType.no_tms.value,
            svars['input0_mb_r'] == svars['input0_mb_c'],
            True
        )
    )
    # solver.add(svars['input0_mb_r'] == svars['input0_mb_c'])
    solver.add(svars['input0_ub_r'] == svars['output_ub_r']) # Force no reblocking
    solver.add(svars['input0_ub_c'] == svars['output_ub_c']) # Force no reblocking

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['input0_tensor_c'],
        grid_dim_var=svars['input0_grid_size_c'],
        mb_dim_var=svars['input0_mb_c'],
        ub_dim_var=svars['input0_ub_c'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_c, max_grid_size_c),
    )

    #output side
    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['output_tensor_r'],
        grid_dim_var=svars['output_grid_size_r'],
        mb_dim_var=svars['output_mb_r'],
        ub_dim_var=svars['output_ub_r'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_r, max_grid_size_r),
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['output_tensor_c'],
        grid_dim_var=svars['output_grid_size_c'],
        mb_dim_var=svars['output_mb_c'],
        ub_dim_var=svars['output_ub_c'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_c, max_grid_size_c),
    )

    all_unary_type_vals = [e.value for e in UnaryType]
    solver.add(svars['unary_type'] <= max(all_unary_type_vals))
    solver.add(svars['unary_type'] >= min(all_unary_type_vals))
    all_tms_type_vals = [e.value for e in TMType]
    # Not running broadcast tms for now
    solver.add(svars['tm_type'] <= max(all_unary_type_vals))
    solver.add(svars['tm_type'] >= min(all_tms_type_vals))
    return solver

def create_dram_buffer_strings(model_vars):
    model_vars['input0_dram_buffers'] = default_dram_string(channel=0, num_buffers=model_vars['input0_grid_size_r'] * model_vars['input0_grid_size_c'], min_buffer_size=0x800000, generate_random_addr=False)
    model_vars['output_dram_buffers'] = default_dram_string(channel=1, num_buffers=model_vars['output_grid_size_r'] * model_vars['output_grid_size_c'], min_buffer_size=0x800000, generate_random_addr=False)
    return model_vars

def extra_config_callback(model_vars):
    model_vars = create_dram_buffer_strings(model_vars)
    model_vars['dram_input0_prologue'] = "false"
    model_vars['input0_df'] = "Bfp8_b"
    model_vars['output_df'] = "Bfp8_b"

    model_vars['untilize_output'] = "false"
    model_vars['device'] = "grayskull"

    if 'unary_type' in model_vars:
        model_vars['unary_type'] = UnaryType(model_vars['unary_type']).name
    else:
        model_vars['unary_type'] = 'nop'

    if 'tm_type' in model_vars:
        model_vars['tm_type'] = TMType(model_vars['tm_type']).name
    else:
        model_vars['tm_type'] = 'no_tms'
    #Op
    model_vars['dest_accumulate_df'] = "Bfp8_b"
    model_vars['out_df'] = "Bfp8_b"
    model_vars['math_fidelity'] = "LoFi"
    if model_vars['factor'] == 1:
        model_vars['input0_tms'] = ""
        # raise ValueError("In TM tests factor must be greater than 1")
    else:
        if model_vars["tm_type"] == TMType.broadcast_r.name:
            model_vars['input0_tms'] = f"broadcast: {{r: {model_vars['output_tensor_r']}}}"
        elif model_vars["tm_type"] == TMType.broadcast_c.name:
            model_vars['input0_tms'] = f"broadcast: {{c: {model_vars['output_tensor_c']}}}"
        elif model_vars["tm_type"] == TMType.broadcast_z.name:
            model_vars['input0_tms'] = f"broadcast: {{z: {model_vars['output_t']}}}"
        else:
            model_vars['input0_tms'] = f"{TMType[model_vars['tm_type']].name}: {{{model_vars['factor']}}}"

    model_vars['target_op_name'] = "target_op"
    return model_vars

def perf_sweep_constraint_model(solver, svars, fixed_vars: Dict[str, tuple], sweep_vars: Dict[str, any], arch):
    solver = constraint_model(solver, svars, arch)
    solver = constraint_perf_sweep_model(solver, svars, fixed_vars, sweep_vars)
    return solver