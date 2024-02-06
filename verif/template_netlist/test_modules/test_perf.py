# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from z3 import *
from util import *
from typing import Dict
from feeder_drainer_utils import *
from constraint_utils import *

templates = {
    PerfTestType.Matmul: {
        PerfTestMode.prologue: 'templates/test_matmul_prologue.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_matmul.template.yaml',
        PerfTestMode.single_op: 'templates/test_matmul_single_op.template.yaml'
    },
    PerfTestType.MatmulSparse: {
        PerfTestMode.prologue: 'templates/test_matmul_sparse_prologue.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_matmul_sparse.template.yaml',
    },
    PerfTestType.MatmulSparseNzCounts: {
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_matmul_sparse_nz_counts.template.yaml',
        PerfTestMode.single_op: 'templates/test_matmul_sparse_nz_counts_single_op.template.yaml'
    },
    PerfTestType.MatmulBias: {
        PerfTestMode.prologue: 'templates/test_matmul_with_bias_prologue.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_matmul_with_bias.template.yaml'
    },
    PerfTestType.Unary: {
        PerfTestMode.prologue: 'templates/test_unary_prologue.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_unary.template.yaml'
    },
    PerfTestType.Binary: {
        PerfTestMode.prologue: 'templates/test_binary_prologue.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_binary.template.yaml',
        PerfTestMode.single_op: 'templates/test_binary_single_op.template.yaml'
    },
    PerfTestType.Dram: {
        # Same netlist for both modes
        PerfTestMode.prologue: 'templates/test_datacopy_dram.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_datacopy_dram.template.yaml',
    },
    PerfTestType.Fused0: {
        PerfTestMode.prologue: 'templates/test_fused_op_0_prologue.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_fused_op_0.template.yaml',
    },
    PerfTestType.Fused2: {
        PerfTestMode.prologue: 'templates/test_fused_op_2_prologue.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_fused_op_2.template.yaml',
    },
    PerfTestType.Pcie: {
        # Same netlist for both modes
        PerfTestMode.prologue: 'templates/test_datacopy_pcie.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_datacopy_pcie.template.yaml',
    },
    PerfTestType.ReduceMax: {
        # Same netlist for both modes
        PerfTestMode.prologue: 'templates/test_reduce_max_prologue.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_reduce_max.template.yaml',
        # PerfTestMode.feeder_drainer: 'templates/test_datacopy_pcie.template.yaml',
    },
    PerfTestType.Splice: {
        # Same netlist for both modes
        PerfTestMode.prologue: 'templates/test_splice_prologue.template.yaml',
        PerfTestMode.feeder_drainer: 'templates/test_feeder_drainer_splice.template.yaml',
    },
    PerfTestType.Tilizer: {
        PerfTestMode.prologue: 'templates/test_tilizer_prologue.template.yaml',
        PerfTestMode.single_op: 'templates/test_tilizer_single_op.template.yaml',
    }
}

solver_operand_var_names = [
    't',
    'grid_size_r',
    'grid_size_c',
    'mb_r',
    'mb_c',
    'ub_r',
    'ub_c',
    'buf_size_mb',
    'ublock_order',
    'df',
]

def VAR(var_name, operand_index):
    if operand_index == -1:
        return "output_" + var_name
    elif operand_index >= 0:
        return f"input{operand_index}_" + var_name
    else:
        raise ValueError("Invalid operand index")

def get_all_vars_for_operand(operand_index):
    return [VAR(i, operand_index) for i in solver_operand_var_names]

solver_base_var_names = [
    "input_count",
    "math_fidelity",
    "ARCH",
    'target_device',
    'intermed_df',
]

op_specific_var_names = {
    PerfTestType.Matmul: [
        'mb_inner',
        'ub_inner',
        'l1_acc',
    ],
    PerfTestType.MatmulBias: [
        'mb_inner',
        'ub_inner',
        'l1_acc',
    ],
    PerfTestType.Binary:    [
        'binary_type'
    ],
    PerfTestType.Unary:     [
        'unary_type',
        'approximate_mode',
        'vector_mode',
    ],
    PerfTestType.TM:        [],
    PerfTestType.Dram:      [],
    PerfTestType.Fused0:    [],
    PerfTestType.Fused2:    [],
    PerfTestType.Pcie:      [],
    PerfTestType.MatmulSparse: [
        'mb_inner',
        'ub_inner',
        'zero_strip_freq',
        'zero_ublock_freq',
        'zero_tile_freq',
        'num_index_tiles',
        'sparse_num_tiles',
        'l1_acc',
    ],
    PerfTestType.MatmulSparseNzCounts: [
        'mb_inner',
        'ub_inner',
        'num_index_tiles',
        'sparse_num_tiles',
        'l1_acc',
    ],
    PerfTestType.ReduceMax: [
        'mb_inner',
        'ub_inner',
        'reduce_dim',
    ],
    PerfTestType.Splice: [
        'input0_index',
        'input0_length',
        'input0_stride',

        'input1_index',
        'input1_length',
        'input1_stride',

        'input2_index',
        'input2_length',
        'input2_stride',
    ],
    PerfTestType.Tilizer: [
        'parallelization_factor_r',
        'parallelization_factor_c',
    ],
}

def get_solver_var_names(perf_config: PerfOpSweepConfig):
    solver_var_names = []
    for operand_idx in range(get_num_input_operands(perf_config.op_type)):
        solver_var_names.extend(get_all_vars_for_operand(operand_idx))
    solver_var_names.extend(get_all_vars_for_operand(-1))
    solver_var_names.extend(solver_base_var_names)
    solver_var_names.extend(op_specific_var_names[perf_config.op_type])
    return solver_var_names


def constraint_model(
    solver,
    svars,
    perf_config: PerfOpSweepConfig,
    arch: str = "grayskull"
):
    if perf_config.reblocking_mode == ReblockTM.Gather:
        assert (perf_config.op_type == PerfTestType.Unary)
        assert (perf_config.perf_test_mode == PerfTestMode.feeder_drainer)
    if perf_config.reblocking_mode == ReblockTM.Hstack or perf_config.reblocking_mode == ReblockTM.Broadcast_r:
        assert (perf_config.op_type == PerfTestType.Unary)
        assert (perf_config.perf_test_mode == PerfTestMode.feeder_drainer)

    # grid sizing for ops
    if arch.lower() == "wormhole":
        solver.add(svars["ARCH"] == ARCH.WH_A0.value)
        max_grid_size_r = 10
        max_grid_size_c = 8
    elif arch.lower() == "wormhole_b0":
        solver.add(svars["ARCH"] == ARCH.WH_B0.value)
        max_grid_size_r = 10
        max_grid_size_c = 8
    elif arch.lower() == "grayskull":
        solver.add(svars["ARCH"] == ARCH.GS.value)
        max_grid_size_r = 10
        max_grid_size_c = 12
    else:
        raise ValueError("Invalid arch type")
    
    if perf_config.skip_constraint_solver:
        return solver

    
    input_tensor_r = []
    input_tensor_c = []
    for i in range(get_num_input_operands(perf_config.op_type)):
        input_tensor_r.append(svars[VAR('grid_size_r', i)] * svars[VAR('mb_r', i)] * svars[VAR('ub_r', i)])
        input_tensor_c.append(svars[VAR('grid_size_c', i)] * svars[VAR('mb_c', i)] * svars[VAR('ub_c', i)])

    output_tensor_r = svars[VAR('grid_size_r', -1)] * svars[VAR('mb_r', -1)] * svars[VAR('ub_r', -1)]
    output_tensor_c = svars[VAR('grid_size_c', -1)] * svars[VAR('mb_c', -1)] * svars[VAR('ub_c', -1)]

    ######################################################################
    ## Base constraints on the common variables between ops
    ######################################################################
    constrain_common_svars(
        solver=solver,
        inputs_mb=[[svars[VAR('mb_r', i)], svars[VAR('mb_c', i)]] for i in range(get_num_input_operands(perf_config.op_type))],
        inputs_ub=[[svars[VAR('ub_r', i)], svars[VAR('ub_c', i)]] for i in range(get_num_input_operands(perf_config.op_type))],
        output_mb=[svars[VAR('mb_r', -1)], svars[VAR('mb_c', -1)]],
        output_ub=[svars[VAR('ub_r', -1)], svars[VAR('ub_c', -1)]],
        input_count=svars['input_count'],
        target_device=svars['target_device'],
        perf_config=perf_config
    )
    ######################################################################
    ## Constraints on t and buf_size_mb
    ######################################################################
    constrain_tensor_t_dim(
        solver=solver,
        input_t=[svars[VAR('t', i)] for i in range(get_num_input_operands(perf_config.op_type))],
        output_t=svars[VAR('t', -1)],
        input_buf_size_mb=[svars[VAR('buf_size_mb', i)] for i in range(get_num_input_operands(perf_config.op_type))],
        output_buf_size_mb=svars[VAR('buf_size_mb', -1)],
        reduce_dim=svars['reduce_dim'] if perf_config.op_type == PerfTestType.ReduceMax else None,
        perf_config=perf_config,
    )
    ######################################################################
    ## Constraints between the operands
    ######################################################################
    constrain_across_operand_tensor_dims(
        solver=solver,
        input_tensors=[[input_tensor_r[i], input_tensor_c[i]] for i in range(get_num_input_operands(perf_config.op_type))],
        output_tensor=[output_tensor_r, output_tensor_c],
        input_mb_dims=[[svars[VAR('mb_r', i)], svars[VAR('mb_c', i)]] for i in range(get_num_input_operands(perf_config.op_type))],
        input_ub_dims=[[svars[VAR('ub_r', i)], svars[VAR('ub_c', i)]] for i in range(get_num_input_operands(perf_config.op_type))],
        output_mb_dims=[svars[VAR('mb_r', -1)], svars[VAR('mb_c', -1)]],
        output_ub_dims=[svars[VAR('ub_r', -1)], svars[VAR('ub_c', -1)]],
        perf_config=perf_config,
        inner_u=svars['ub_inner'] if has_inner_dim(perf_config.op_type) else None,
        inner_m=svars['mb_inner'] if has_inner_dim(perf_config.op_type) else None,
        input_t=[svars[VAR('t', i)] for i in range(get_num_input_operands(perf_config.op_type))],
        output_t=svars[VAR('t', -1)],
        reduce_dim=svars['reduce_dim'] if perf_config.op_type == PerfTestType.ReduceMax else None,
    )
    ######################################################################
    ## Constraints for ublock order
    ######################################################################
    constrain_ublock_order(
        solver=solver,
        input_ub_dims=[[svars[VAR('ub_r', i)], svars[VAR('ub_c', i)]] for i in range(get_num_input_operands(perf_config.op_type))],
        output_ub_dims=[svars[VAR('ub_r', -1)], svars[VAR('ub_c', -1)]],
        feeders_ublock_order=[svars[VAR('ublock_order', i)] for i in range(get_num_input_operands(perf_config.op_type))],
        target_op_ublock_order=svars[VAR('ublock_order', -1)],
        perf_config=perf_config,
    )

    ######################################################################
    ## Constraints for grid size
    ######################################################################
    constrain_grid_size(
        solver=solver,
        max_grid_size=[max_grid_size_r, max_grid_size_c],
        feeders_grid_size=[[svars[VAR('grid_size_r', i)], svars[VAR('grid_size_c', i)]] for i in range(get_num_input_operands(perf_config.op_type))],
        target_op_grid_size=[svars[VAR('grid_size_r', -1)], svars[VAR('grid_size_c', -1)]],
        drainer_grid_size=[svars[VAR('grid_size_r', -1)], svars[VAR('grid_size_c', -1)]],
        reduce_dim=svars['reduce_dim'] if perf_config.op_type == PerfTestType.ReduceMax else None,
        perf_config=perf_config,
    )
    ######################################################################
    ## Constraints on output buffer size
    ######################################################################
    constrain_l1_usage(
        solver=solver,
        mb_dims=[svars[VAR('mb_r', -1)], svars[VAR('mb_c', -1)]],
        ub_dims=[svars[VAR('ub_r', -1)], svars[VAR('ub_c', -1)]],
        data_format=svars[VAR('df', -1)],
        buf_size_mb=svars[VAR('buf_size_mb', -1)],
        arch=arch,
        svars=svars,
        perf_config=perf_config
    )
    ######################################################################
    ## Constraints on math-fidelity and data-format
    ######################################################################
    constrain_math_fidelity_and_data_format(
        solver=solver,
        math_fidelity=svars['math_fidelity'],
        input_data_formats=[svars[VAR("df", i)] for i in range(get_num_input_operands(perf_config.op_type))],
        output_data_format=svars[VAR('df', -1)],
        intermed_data_format=svars['intermed_df'],
        arch=arch,
        l1_acc=svars['l1_acc'] if is_matmul_op(perf_config.op_type) else None,
        perf_config=perf_config,
    )
    ######################################################################
    ## Constraints on op-specific variables
    ######################################################################
    constrain_op_specific_vars(
        solver=solver,
        svars=svars,
        perf_config=perf_config,
        input_tensor_c=input_tensor_c
    )
    return solver

def create_dram_buffer_strings(model_vars, perf_config: PerfOpSweepConfig):
    if model_vars["ARCH"] == ARCH.WH_A0.value:
        max_grid_size_r = 10
        max_grid_size_c = 8
        num_dram_channels = 6
    elif model_vars["ARCH"] == ARCH.WH_B0.value:
        max_grid_size_r = 10
        max_grid_size_c = 8
        num_dram_channels = 6
    elif model_vars["ARCH"] == ARCH.GS.value:
        max_grid_size_r = 10
        max_grid_size_c = 12
        num_dram_channels = 8
    else:
        raise ValueError("Invalid arch type")
    start_addr_each_channel = {i: 0x10000000 for i in range(num_dram_channels)}
    input_buf_sizes = []
    for i in range(get_num_input_operands(perf_config.op_type)):
        input_buf_sizes.append(
            get_queue_buffer_size(
                model_vars[VAR('df', i)],
                model_vars["num_entries"],
                model_vars[VAR('t', i)],
                [model_vars[VAR('mb_r', i)], model_vars[VAR('mb_c', i)]],
                [model_vars[VAR('ub_r', i)], model_vars[VAR('ub_c', i)]]
            )
        )

    output_buf_size = get_queue_buffer_size(
        model_vars[VAR('df', -1)],
        model_vars["input_count"],
        model_vars[VAR('t', -1)],
        [model_vars[VAR('mb_r', -1)], model_vars[VAR('mb_c', -1)]],
        [model_vars[VAR('ub_r', -1)], model_vars[VAR('ub_c', -1)]]
    )
    dram_channels_prologue = [list(range(num_dram_channels)) for i in range(get_num_input_operands(perf_config.op_type))]
    output_dram_channels = list(range(num_dram_channels))
    override_output_dram_channels = perf_config.perf_test_mode == PerfTestMode.single_op
    gs_operands_dram_channels_feeder_drainer = [
        [0, 2], # Input 0
        [5, 7], # Input 1
        [1, 3], # Output
    ]
    gs_operands_dram_channels_prologue = [
        [0, 2], # Input 0
        [4, 6], # Input 1
        [3, 5], # Output
    ]
    wh_operands_dram_channels_feeder_drainer = [
        [0, 2], # Input 0
        [1, 3], # Input 1
        [4, 5], # Output
    ]
    wh_operands_dram_channels_prologue = [
        [0, 2], # Input 0
        [1, 3], # Input 1
        [4, 5], # Output
    ]
    if perf_config.perf_test_mode == PerfTestMode.prologue or perf_config.reblocking_mode == ReblockTM.Gather:
        if model_vars["ARCH"] == ARCH.GS.value:
            dram_channels = gs_operands_dram_channels_prologue
        else:
            dram_channels = wh_operands_dram_channels_prologue
    else:
        if model_vars["ARCH"] == ARCH.GS.value:
            dram_channels = gs_operands_dram_channels_feeder_drainer
        else:
            dram_channels = wh_operands_dram_channels_feeder_drainer

    for i in range(get_num_input_operands(perf_config.op_type)):
        if perf_config.perf_test_mode == PerfTestMode.single_op and perf_config.op_type == PerfTestType.Matmul:
            if i == 0:
                num_buffers = model_vars[VAR('grid_size_r', i)]
            elif i == 1:
                num_buffers = model_vars[VAR('grid_size_c', i)]
            else:
                num_buffers = model_vars[VAR('grid_size_r', i)] * model_vars[VAR('grid_size_c', i)]
        else:
            num_buffers = model_vars[VAR('grid_size_r', i)] * model_vars[VAR('grid_size_c', i)]
        if perf_config.dram_assignment_mode != DramAssignment.Normal:
            print(f"Assigning dram channels for input operand {i}")
        model_vars[VAR('dram_buffers', i)], dram_assignment_valid = default_dram_string(
            channel=dram_channels[i] if perf_config.op_type == PerfTestType.Dram else dram_channels_prologue[i],
            num_buffers=num_buffers,
            min_buffer_size=input_buf_sizes[i],
            generate_random_addr=False,
            start_addr=start_addr_each_channel,
            dram_assignment=perf_config.dram_assignment_mode,
        )
    if perf_config.dram_assignment_mode != DramAssignment.Normal:
        print(f"Assigning dram channels for output operand")
    model_vars[VAR('dram_buffers', -1)], dram_assignment_valid = default_dram_string(
        channel=output_dram_channels if override_output_dram_channels else dram_channels[-1],
        num_buffers=model_vars[VAR('grid_size_r', -1)] * model_vars[VAR('grid_size_c', -1)],
        min_buffer_size=output_buf_size,
        generate_random_addr=False,
        start_addr=start_addr_each_channel,
        dram_assignment=perf_config.dram_assignment_mode
    )
    if perf_config.dram_assignment_mode != DramAssignment.Normal:
            print(f"------------------------------------------")
    
    return model_vars, dram_assignment_valid

def override_input_dim(model_vars, input: int, row_col: str):
    # For feeder drainer, dest can only fit a microblock that has no more than 8 tiles
    MAX_UB_SIZE = 8
    assert row_col == 'r' or row_col == 'c'
    ub_inner_dim_key = f"input{input}_ub_{row_col}"
    ub_outer_dim_key = f"input{input}_ub_{'c' if row_col == 'r' else 'r'}"
    ub_inner_dim_size, ub_outer_dim_size = model_vars[ub_inner_dim_key], model_vars[ub_outer_dim_key]
    ub_size = ub_outer_dim_size * ub_inner_dim_size

    # TODO: Update this since this doesn't allow outer dims larger than 8
    assert model_vars[ub_outer_dim_key] <= MAX_UB_SIZE
    # TODO: Update this, currently downscaling ub inner dim by a factor of 2 until ublock fits in dest
    if ub_size > MAX_UB_SIZE:
        mb_multiplier = 1

        while ub_inner_dim_size > 0 and ub_size > MAX_UB_SIZE:
            assert ub_inner_dim_size % 2 == 0
            ub_inner_dim_size //= 2
            ub_size = ub_outer_dim_size * ub_inner_dim_size
            mb_multiplier *= 2

        assert ub_inner_dim_size > 0

        mb_inner_dim_key = ub_inner_dim_key.replace("ub", "mb")

        # Upsize mb inner dim, downsize ub inner dim
        model_vars[mb_inner_dim_key] *= mb_multiplier
        model_vars[ub_inner_dim_key] = ub_inner_dim_size

    return model_vars

def derive_intermed_acc_df(model_vars, op_type):
    # intermed data format should equal output data format if l1 acc is disabled
    if is_matmul_op(op_type) and model_vars["l1_acc"]:
        model_vars["intermed_df"] = DataFormat(model_vars["intermed_df"]).name
    else:
        model_vars["intermed_df"] = DataFormat(model_vars[VAR("df", -1)]).name
    
    # dest acc df should be fp16 or fp16b based on the A/B of output df
    if is_format_type_a(DataFormat(model_vars[VAR("df", -1)])):
        model_vars['dest_accumulate_df'] = DataFormat(DataFormat.Float16).name
    else:
        model_vars['dest_accumulate_df'] = DataFormat(DataFormat.Float16_b).name
        
    return model_vars


def extra_config_callback(model_vars, perf_config: PerfOpSweepConfig):
    if model_vars["ARCH"] == ARCH.WH_A0.value:
        max_grid_size_r = 10
        max_grid_size_c = 8
        num_dram_channels = 6
    elif model_vars["ARCH"] == ARCH.WH_B0.value:
        max_grid_size_r = 10
        max_grid_size_c = 8
        num_dram_channels = 6
    elif model_vars["ARCH"] == ARCH.GS.value:
        max_grid_size_r = 10
        max_grid_size_c = 12
    else:
        raise ValueError("Invalid arch type")
    input_tensor_r = []
    input_tensor_c = []
    for i in range(get_num_input_operands(perf_config.op_type)):
        operand_data_format_key = VAR('df', i)
        input_tensor_r.append(model_vars[VAR('grid_size_r', i)] * model_vars[VAR('mb_r', i)] * model_vars[VAR('ub_r', i)])
        input_tensor_c.append(model_vars[VAR('grid_size_c', i)] * model_vars[VAR('mb_c', i)] * model_vars[VAR('ub_c', i)])
        if perf_config.op_type in [PerfTestType.MatmulSparse, PerfTestType.MatmulSparseNzCounts]:
            if i == 0:
                # inputs have to be all A formats or all B formats
                if is_format_type_a(DataFormat(model_vars[VAR('df', i)])):
                    model_vars[VAR('df', i)] = "Bfp2"
                else:
                    model_vars[operand_data_format_key] = "Bfp2_b"
            elif i == 2:
                model_vars[operand_data_format_key] = "RawUInt32"
            else:
                model_vars[VAR('df', i)] = DataFormat(model_vars[VAR('df', i)]).name
        else:
            model_vars[VAR('df', i)] = DataFormat(model_vars[VAR('df', i)]).name
    
    output_tensor_r = model_vars[VAR('grid_size_r', -1)] * model_vars[VAR('mb_r', -1)] * model_vars[VAR('ub_r', -1)]
    output_tensor_c = model_vars[VAR('grid_size_c', -1)] * model_vars[VAR('mb_c', -1)] * model_vars[VAR('ub_c', -1)]
    
    #Op
    model_vars = derive_intermed_acc_df(model_vars, perf_config.op_type)
    model_vars[VAR('df', -1)] = DataFormat(model_vars[VAR('df', -1)]).name
    model_vars['math_fidelity'] = MathFidelity(model_vars['math_fidelity']).name
    for i in range(get_num_input_operands(perf_config.op_type)):
        model_vars[VAR('tms', i)] = ""
    model_vars['target_op_name'] = "target_op"
    model_vars['untilize_output'] = "false"
    model_vars['device'] = "grayskull"

    if 'unary_type' in model_vars:
        model_vars['unary_type'] = UnaryType(model_vars['unary_type']).name
    else:
        model_vars['unary_type'] = 'nop'

    if 'binary_type' in model_vars:
        model_vars['binary_type'] = BinaryType(model_vars['binary_type']).name
    else:
        model_vars['binary_type'] = 'add'
    

    if perf_config.op_type == PerfTestType.MatmulSparse:
        model_vars['zero_strip_freq'] = model_vars['zero_strip_freq'] / 100.0
        model_vars['zero_ublock_freq'] = model_vars['zero_ublock_freq'] / 100.0
        model_vars['zero_tile_freq'] = model_vars['zero_tile_freq'] / 100.0

    if perf_config.op_type == PerfTestType.MatmulSparseNzCounts:
        # we need to limit number of non-zero strips to keep diagonal pattern
        # of the non-zero elements in the sparse input
        max_nz_strips = model_vars['mb_inner'] + model_vars['output_t'] - 1
        min_nz_strips = model_vars['output_t']
        nz_strips = random.randint(min_nz_strips, max_nz_strips)

        max_nz_ublocks = nz_strips * model_vars['output_mb_r']
        # limit non-zero ublocks to keep input 0 sparse for examples with bigger mblock_r
        max_nz_ublocks = min(max_nz_ublocks, 3500)
        nz_ublocks = random.randint(nz_strips, max_nz_ublocks)

        max_nz_tiles = nz_ublocks * model_vars['output_ub_r'] * model_vars['ub_inner']
        # limit non-zero tiles to keep input 0 sparse for examples with bigger u_kt
        max_nz_tiles = min(max_nz_tiles, 10000)
        nz_tiles = random.randint(nz_ublocks, max_nz_tiles)

        model_vars.update({"nz_strips": nz_strips, "nz_ublocks": nz_ublocks, "nz_tiles": nz_tiles})


    # In the following modes, the input queues are not prologued
    # If the input queues are prologued we should have one entry in that queue
    if (
        perf_config.perf_test_mode == PerfTestMode.single_op or 
        perf_config.op_type == PerfTestType.Dram or
        perf_config.op_type == PerfTestType.Tilizer
    ):
        model_vars["num_entries"] = model_vars["input_count"]
    else:
        model_vars["num_entries"] = 1
    model_vars, dram_assignment_valid = create_dram_buffer_strings(model_vars, perf_config)

    for i in range(get_num_input_operands(perf_config.op_type)):
        if (VAR('ublock_order', i) in model_vars):
            model_vars[VAR('ublock_order', i)] = UblockOrder(model_vars[VAR('ublock_order', i)]).name
        else:
            model_vars[VAR('ublock_order', i)] = 'r'
    if (VAR('ublock_order', -1) in model_vars):
        model_vars[VAR('ublock_order', -1)] = UblockOrder(model_vars[VAR('ublock_order', -1)]).name
    else:
        model_vars[VAR('ublock_order', -1)] = 'r'
    
    if perf_config.op_type == PerfTestType.MatmulBias:
        broadcast = model_vars["output_grid_size_r"] * model_vars["output_mb_r"] * model_vars["output_ub_r"]
        model_vars["input2_tms"] = f"broadcast: {{r: {broadcast} }}"
    if perf_config.op_type == PerfTestType.Fused0:
        broadcast_r = output_tensor_r
        broadcast_c = output_tensor_c
        model_vars["input1_tms"] = f"broadcast: {{r: {broadcast_r}}}, broadcast: {{c: {broadcast_c}}}"
        model_vars["input2_tms"] = f"broadcast: {{r: {broadcast_r}}}"
    if perf_config.op_type == PerfTestType.Fused2:
        broadcast_r = output_tensor_r
        broadcast_c = output_tensor_c // input_tensor_c[0]
        model_vars["input1_tms"] = f"broadcast: {{c: {broadcast_c} }}"
        model_vars["input3_tms"] = f"broadcast: {{r: {broadcast_r} }}"
        model_vars["input4_tms"] = f"broadcast: {{r: {broadcast_r} }}"
    if perf_config.reblocking_mode == ReblockTM.Hstack:
        hstack_factor = model_vars["output_grid_size_c"] // model_vars["input0_grid_size_c"]
        model_vars["input0_tms"] = f"hstack: {{{hstack_factor}}}"
    if perf_config.reblocking_mode == ReblockTM.Broadcast_r:
        broadcast_r_factor = output_tensor_r // input_tensor_r[0]
        model_vars["input0_tms"] = f"broadcast: {{r: {broadcast_r_factor}}}"
    
    core_used = [[0]*max_grid_size_c for i in range(max_grid_size_r)]
    for i in list(range(get_num_input_operands(perf_config.op_type))) + [-1]:
        model_vars[VAR('grid_loc_r', i)] = None
        model_vars[VAR('grid_loc_c', i)] = None
    model_vars['grid_loc_r'] = None
    model_vars['grid_loc_c'] = None

    if perf_config.perf_test_mode != PerfTestMode.single_op:
        # FIXME: Temporary hack to limit ub_size so that we don't get dest overflow errors in feeders
        if "ub_inner" in model_vars and "mb_inner" in model_vars:
            # The microblock inner dim for sparse matmuls is input1_ub_r
            if perf_config.op_type in [PerfTestType.MatmulSparse, PerfTestType.MatmulSparseNzCounts]:
                model_vars = override_input_dim(model_vars, 1, 'r')

            # The microblock inner dim for reduce max is input0_ub_{reduce_dim} if reduce_dim is "r" or "c"
            elif perf_config.op_type == PerfTestType.ReduceMax and ReduceDim(model_vars['reduce_dim']).name in ["r", "c"]:
                model_vars = override_input_dim(model_vars, 0, ReduceDim(model_vars['reduce_dim']).name)

            # For other matmuls, the microblock inner dims between activations and weights is input0_ub_c and input1_ub_r
            else:
                model_vars = override_input_dim(model_vars, 0, 'c')
                model_vars = override_input_dim(model_vars, 1, 'r')

    # Format mb_inner, ub_inner if op type is reduce max
    if perf_config.op_type == PerfTestType.ReduceMax:
        model_vars['reduce_dim'] = ReduceDim(model_vars['reduce_dim']).name
        if model_vars["reduce_dim"] == 'r' or model_vars["reduce_dim"] == 'c':
            model_vars["mb_inner"] = f",m_k: {model_vars['mb_inner']}"
            model_vars["ub_inner"] = f",u_kt: {model_vars['ub_inner']}"
            model_vars['z'] = ""
        elif model_vars["reduce_dim"] == 'z':
            model_vars["mb_inner"] = ""
            model_vars["ub_inner"] = ""
            model_vars['z'] = f",z: {model_vars['input0_t'] // model_vars['output_t']}"
        else:
            raise ValueError(f"Invalid reduce dim: {model_vars['reduce_dim']}")
    
    if is_matmul_op(perf_config.op_type):
        if model_vars['l1_acc']:
            assert model_vars["ARCH"] == ARCH.WH_B0.value, "L1 accumulation is only a feature of wormhole b0 matmul, not supported on other devices"
            
        model_vars['l1_acc'] = str(bool(model_vars['l1_acc'])).lower()

    if perf_config.op_type == PerfTestType.Unary:
        model_vars['approximate_mode'] = str(bool(model_vars['approximate_mode'])).lower()
        model_vars['vector_mode'] = SfpuVectorMode(model_vars['vector_mode']).name
        
    placement_valid = constrain_grid_loc(
        model_vars=model_vars,
        max_grid_size=[max_grid_size_r, max_grid_size_c],
        feeders_loc=[[VAR('grid_loc_r', i), VAR('grid_loc_c', i)] for i in range(get_num_input_operands(perf_config.op_type))],
        target_op_loc=['grid_loc_r', 'grid_loc_c'],
        drainer_loc=[VAR('grid_loc_r', -1), VAR('grid_loc_c', -1)],
        feeders_grid_size=[[VAR('grid_size_r', i), VAR('grid_size_c', i)] for i in range(get_num_input_operands(perf_config.op_type))],
        target_op_grid_size=[VAR('grid_size_r', -1), VAR('grid_size_c', -1)],
        drainer_grid_size=[VAR('grid_size_r', -1), VAR('grid_size_c', -1)],
        core_used=core_used,
        arch=model_vars["ARCH"],
        perf_config=perf_config,
    )
    return model_vars, placement_valid and dram_assignment_valid

def perf_sweep_constraint_model(solver, svars, perf_config: PerfOpSweepConfig, arch):
    solver = constraint_model(solver, svars, perf_config, arch)
    solver = constraint_perf_sweep_model(solver, svars, perf_config)
    return solver
