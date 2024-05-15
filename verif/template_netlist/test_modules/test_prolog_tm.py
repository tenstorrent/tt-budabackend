# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import sys

def info(type, value, tb):
    if hasattr(sys, 'ps1') or not sys.stderr.isatty():
        # we are in interactive mode or we don't have a tty-like
        # device, so we call the default hook
        sys.__excepthook__(type, value, tb)
    else:
        import traceback, pdb
        # we are NOT in interactive mode, print the exception...
        traceback.print_exception(type, value, tb)
        print
        # ...then start the debugger in post-mortem mode.
        pdb.post_mortem(tb)

sys.excepthook = info

from z3 import *
from util import *
from test_modules.constraint_utils import *
from test_modules.common.constants import TEMPLATE_DIR_PATH
import random

template_yaml = f'{TEMPLATE_DIR_PATH}/test_prolog_tm_wandb.template.yaml'

solver_var_names = [
    'input_count',
    #'input0_entries',
    #'input1_entries',
    #'output_entries',
    'entries',
    #'input0_buf_size_mb',
    #'input1_buf_size_mb',
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
    'input1_tensor_r',
    'input1_tensor_c',
    'input1_t',
    'input1_grid_size_r',
    'input1_grid_size_c',
    'input1_mb_r',
    'input1_mb_c',
    'input1_ub_r',
    'input1_ub_c',
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
    'mb_inner',
    'ub_inner',
    'mm_input0_tensor_r',
    'mm_input0_tensor_c',
    'mm_input1_tensor_r',
    'mm_input1_tensor_c',
    'mm_t',
    #'queue_wrap_size0',
    #'queue_wrap_size1',

    'tensor_t',

    'bias_tensor_r',
    'bias_tensor_c',

    'bias_queue_tensor_r',
    'bias_queue_tensor_c',

    'bias_queue_grid_size_r',
    'bias_queue_grid_size_c',
    'bias_queue_mb_r',
    'bias_queue_mb_c',
    'bias_queue_ub_r',
    'bias_queue_ub_c',

    'bias_grid_loc_r',
    'bias_grid_loc_c',
    'bias_grid_size_r',
    'bias_grid_size_c',
    'bias_mb_r',
    'bias_mb_c',
    'bias_ub_r',
    'bias_ub_c',

    'bias_col_bcast_factor',
]
"""
solver_var_names = [
    'input_count',
    'entries',
    'input0_t',
    'input0_tensor_r',
    'input0_tensor_c',
    'act_queue_grid_size_y',
    'act_queue_grid_size_x',
    'act_queue_mblock_r',
    'act_queue_mblock_c',
    'act_queue_ublock_r',
    'act_queue_ublock_c',
    'input1_t',
    'input1_tensor_r',
    'input1_tensor_c',
    'weight_queue_grid_size_y',
    'weight_queue_grid_size_x',
    'weight_queue_mblock_r',
    'weight_queue_mblock_c',
    'weight_queue_ublock_r',
    'weight_queue_ublock_c',
    'matmul_grid_loc_r',
    'matmul_grid_loc_c',
    'matmul_grid_size_y',
    'matmul_grid_size_x',
    'matmul_mblock_r',
    'matmul_mblock_c',
    'matmul_ublock_r',
    'matmul_ublock_c',
    'mb_inner',
    'ub_inner',
    'bias_tensor_r',
    'bias_tensor_c',
    'bias_t',
    'bias_grid_loc_r',
    'bias_grid_loc_c',
    'bias_grid_size_y',
    'bias_grid_size_x',
    'output_tensor_r',
    'output_tensor_c',
    'output_mb_r',
    'output_mb_c',
    'output_ub_r',
    'output_ub_c',

    'output_buf_size_mb',
    'output_t',

    'mm_input0_tensor_r',
    'mm_input0_tensor_c',
    'mm_input1_tensor_r',
    'mm_input1_tensor_c',
    'input0_grid_size_r',
    'input0_grid_size_c',
    'input1_grid_size_r',
    'input1_grid_size_c',
    'mm_t',
    'queue_wrap_size0',
    'queue_wrap_size1'
]
"""
def constraint_model(solver,svars):
    """
    solver definition and setup
    """

    max_ub_dim = 8
    min_ub_dim = 1

    max_tensor_r = 16
    min_tensor_r = 1
    max_tensor_c = 16
    min_tensor_c = 1

    max_input_t = 8

    max_grid_size_r = 10
    max_grid_size_c = 12
    min_grid_size_r = 1
    min_grid_size_c = 1

    # broadcast factor
    solver.add(svars['bias_col_bcast_factor'] > 1)
    solver.add(svars['bias_queue_tensor_c'] == 1)

    # input / output relationship
    solver.add(svars['bias_tensor_r'] == svars['output_tensor_r'])
    solver.add(svars['bias_tensor_c'] == svars['output_tensor_c'])
    solver.add(svars['bias_queue_tensor_c'] * svars['bias_col_bcast_factor'] == svars['bias_tensor_c'])
    solver.add(svars['bias_queue_tensor_r'] == svars['bias_tensor_r'])

    solver.add(svars['mm_t'] == svars['tensor_t'])

    # Graph and queue constraints

    #solver.add(svars['queue_wrap_size0'] == 2*svars['input0_entries'])
    #solver.add(svars['queue_wrap_size1'] == 2*svars['input1_entries'])

    constrain_input_count_and_entries(
        solver,
        svars['input_count'],
        svars['entries']
    )

    # placement
    op_placement_constraints(
        solver=solver,
        op_list=[
            (svars["bias_grid_loc_c"], svars["bias_grid_loc_r"], svars["bias_grid_loc_c"] + svars["bias_grid_size_c"], svars["bias_grid_loc_r"] + svars["bias_grid_size_r"]),
            (svars["grid_loc_c"], svars["grid_loc_r"], svars["grid_loc_c"] + svars["output_grid_size_c"], svars["grid_loc_r"] + svars["output_grid_size_r"])
        ],
        max_grid_size_c=max_grid_size_c, max_grid_size_r=max_grid_size_r)

    #constrain_rectangle(
    #    solver,
    #    max_grid_size_r,
    #    max_grid_size_c,
    #    svars['matmul_grid_loc_c'],
    #    svars['matmul_grid_loc_r'],
    #    svars['bias_grid_loc_c'],
    #    svars['bias_grid_loc_r']
    #)

    # MM dims assignment
    #solver.add(svars['input0_grid_size_r'] == svars['act_queue_grid_size_y'])
    #solver.add(svars['input0_grid_size_c'] == svars['act_queue_grid_size_x'])
    #solver.add(svars['input1_grid_size_r'] == svars['weight_queue_grid_size_y'])
    #solver.add(svars['input1_grid_size_c'] == svars['weight_queue_grid_size_x'])

    solver.add(svars['mm_input0_tensor_r'] == svars['input0_tensor_r'])
    solver.add(svars['mm_input0_tensor_c'] == svars['input0_tensor_c'])
    solver.add(svars['mm_input1_tensor_r'] == svars['input1_tensor_r'])
    solver.add(svars['mm_input1_tensor_c'] == svars['input1_tensor_c'])
    solver.add(svars['mm_t'] == svars['input0_t'])
    solver.add(svars['mm_t'] == svars['input1_t'])
    solver.add(svars['mm_t'] == svars['output_t'])


    solver.add(svars['input0_grid_size_r'] * svars['input0_grid_size_c'] <= 32)


    # constrain factor from dram so that we don't exceed the limit of 40 dram queues per core
    # operand 0
    constrain_forking_factor(
        solver=solver,
        producer_grid_r_var=svars['input0_grid_size_r'],
        producer_grid_c_var=svars['input0_grid_size_c'],
        consumer_grid_r_var=svars['output_grid_size_r'],
        consumer_grid_c_var=svars['output_grid_size_c'],
    )

    # operand 1
    constrain_forking_factor(
        solver=solver,
        producer_grid_r_var=svars['input1_grid_size_r'],
        producer_grid_c_var=svars['input1_grid_size_c'],
        consumer_grid_r_var=svars['output_grid_size_r'],
        consumer_grid_c_var=svars['output_grid_size_c'],
    )

    # bias operand 0
    constrain_forking_factor(
        solver=solver,
        producer_grid_r_var=svars['output_grid_size_r'],
        producer_grid_c_var=svars['output_grid_size_c'],
        consumer_grid_r_var=svars['bias_grid_size_r'],
        consumer_grid_c_var=svars['bias_grid_size_c'],
    )

    # operand 1
    constrain_forking_factor(
        solver=solver,
        producer_grid_r_var=svars['bias_queue_grid_size_r'],
        producer_grid_c_var=svars['bias_queue_grid_size_c'],
        consumer_grid_r_var=svars['bias_grid_size_r'],
        consumer_grid_c_var=svars['bias_grid_size_c'],
    )
    # prolog grid constraints
    solver.add(svars['bias_queue_grid_size_r'] <= svars['bias_grid_size_r'])
    solver.add(svars['bias_queue_grid_size_c'] <= svars['bias_grid_size_c'])

    default_matmul_constraints(
        solver=solver,
        svars=svars,
        max_tensor_r=max_tensor_r,
        max_tensor_c=max_tensor_c,
        min_tensor_r=min_tensor_r,
        min_tensor_c=min_tensor_c,
        max_input_t=max_input_t,
        max_grid_size_r=max_grid_size_r,
        max_grid_size_c=max_grid_size_c,
        min_grid_size_r=min_grid_size_r,
        min_grid_size_c=min_grid_size_c,
        max_ub_dim=max_ub_dim,
        min_ub_dim=min_ub_dim
    )

    constrain_ublock_size(
        solver=solver,
        ub_r_var=svars['bias_queue_ub_r'],
        ub_c_var=svars['bias_queue_ub_c'],
        max_size=8, # half dest
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['bias_tensor_r'],
        grid_dim_var=svars['bias_grid_size_r'],
        mb_dim_var=svars['bias_mb_r'],
        ub_dim_var=svars['bias_ub_r'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_r, max_grid_size_r),
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['bias_tensor_c'],
        grid_dim_var=svars['bias_grid_size_c'],
        mb_dim_var=svars['bias_mb_c'],
        ub_dim_var=svars['bias_ub_c'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_c, max_grid_size_c),
    )

    constrain_ublock_size(
        solver=solver,
        ub_r_var=svars['bias_ub_r'],
        ub_c_var=svars['bias_ub_c'],
        max_size=8, # half dest
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['bias_queue_tensor_r'],
        grid_dim_var=svars['bias_queue_grid_size_r'],
        mb_dim_var=svars['bias_queue_mb_r'],
        ub_dim_var=svars['bias_queue_ub_r'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_r, max_grid_size_r),
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['bias_queue_tensor_c'],
        grid_dim_var=svars['bias_queue_grid_size_c'],
        mb_dim_var=svars['bias_queue_mb_c'],
        ub_dim_var=svars['bias_queue_ub_c'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_c, max_grid_size_c),
    )

    constrain_ublock_size(
        solver=solver,
        ub_r_var=svars['bias_queue_ub_r'],
        ub_c_var=svars['bias_queue_ub_c'],
        max_size=8, # half dest
    )

    return solver

def create_dram_buffer_strings(model_vars):
    random_chans = random.sample(range(8), 4)
    input0_num_buffers, input0_buffer_size = calculate_num_buffers_and_size(model_vars['input0_grid_size_r'], model_vars['input0_grid_size_c'], model_vars['input_count'], model_vars['input0_mb_r'], model_vars['input0_mb_c'], model_vars['input0_ub_r'], model_vars['input0_ub_c'], model_vars['input0_t'])
    input1_num_buffers, input1_buffer_size = calculate_num_buffers_and_size(model_vars['input1_grid_size_r'], model_vars['input1_grid_size_c'], model_vars['input_count'], model_vars['input1_mb_r'], model_vars['input1_mb_c'], model_vars['input1_ub_r'], model_vars['input1_ub_c'], model_vars['input1_t'])
    output_num_buffers, output_buffer_size = calculate_num_buffers_and_size(model_vars['bias_grid_size_r'], model_vars['bias_grid_size_c'], model_vars['input_count'], model_vars['bias_mb_r'], model_vars['bias_mb_c'], model_vars['bias_ub_r'], model_vars['bias_ub_c'], model_vars['tensor_t'])
    bias_num_buffers, bias_buffer_size = calculate_num_buffers_and_size(model_vars['bias_queue_grid_size_r'], model_vars['bias_queue_grid_size_c'], model_vars['input_count'], model_vars['bias_queue_mb_r'], model_vars['bias_queue_mb_c'], model_vars['bias_queue_ub_r'], model_vars['bias_queue_ub_c'], model_vars['tensor_t'])
    model_vars['input0_dram_buffers'] = default_dram_string(channel=random_chans[0], num_buffers=input0_num_buffers, min_buffer_size=input0_buffer_size)
    model_vars['input1_dram_buffers'] = default_dram_string(channel=random_chans[1], num_buffers=input1_num_buffers, min_buffer_size=input1_buffer_size)
    model_vars['output_dram_buffers'] = default_dram_string(channel=random_chans[2], num_buffers=output_num_buffers, min_buffer_size=output_buffer_size)
    model_vars['bias_dram_buffers'] = default_dram_string(channel=random_chans[3], num_buffers=bias_num_buffers, min_buffer_size=bias_buffer_size)
    return model_vars

def extra_config_callback(model_vars):
    model_vars = create_dram_buffer_strings(model_vars)
    #Op
    model_vars['dest_accumulate_df'] = "Float16"
    model_vars['out_df'] = "Float16"
    model_vars['math_fidelity'] = "HiFi4"
    model_vars['input0_tms'] = ""
    model_vars['input1_tms'] = ""
    return model_vars