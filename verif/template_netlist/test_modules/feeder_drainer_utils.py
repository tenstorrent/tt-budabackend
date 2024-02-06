# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from z3 import *
from typing import Dict, List
from constraint_utils import *
from test_modules.common.device_architecture import DeviceArchitecture

def constraint_model_sweep_vars(solver, svars, svars_range):
    for var, val_range in svars_range.items():
        if len(val_range) == 0:
            continue
        constraint = "Or("
        for idx, val in enumerate(val_range):
            constraint += f"(svars['{var}'] == {val})"
            if idx < len(val_range) - 1:
                constraint += ", "
        constraint += ")"
        constraint_func = eval(constraint)
        # print (f"solver.add({constraint_func})")
        solver.add(constraint_func)
    return solver

def constraint_model_fixed_vars(solver, svars, svars_val):
    for var, value in svars_val.items():
        if value is None:
            continue
        solver.add(svars[var] == value)
        # print (f"solver.add({svars[var]} == {value})")
    return solver

def constraint_perf_sweep_model(solver, svars, perf_config):
    sweep_vars_keys = perf_config.sweep_vars.keys()
    
    if set(sweep_vars_keys).intersection(set(svars)) != set(sweep_vars_keys):
        print("Following variables in sweep_vars do not exist in svars:")
        print(set(sweep_vars_keys).difference(set(sweep_vars_keys).intersection(set(svars))))
        assert set(sweep_vars_keys).intersection(set(svars)) == set(sweep_vars_keys)
    if perf_config.skip_constraint_solver:
        if set(sweep_vars_keys) != set(svars):
            print(set(sweep_vars_keys).difference(set(svars)))
            print(set(svars).difference(set(sweep_vars_keys)))
            raise Exception("In skip_constraint_solver mode, all solver variables must have been overriden by sweep_vars (in sweep_params.py)")
        for sweep_var, val in perf_config.sweep_vars.items():
            assert isinstance(val, list), "Each sweep var must be overriden by a list of values (or an empty list to not override)"
            assert len(val) > 0, "In skip_constraint_solver mode, the value for every variable must be overriden"
    solver = constraint_model_sweep_vars(solver, svars, perf_config.sweep_vars)

    return solver

def constrain_tensor_t_dim(
    solver,
    input_t,
    output_t,
    input_buf_size_mb,
    output_buf_size_mb,
    reduce_dim,
    perf_config: PerfOpSweepConfig,
):
    for i in range(get_num_input_operands(perf_config.op_type)):

        solver.add(input_t[i] > 0)
        if (perf_config.op_type == PerfTestType.ReduceMax):
            solver.add(
                If (
                    reduce_dim == 3,
                    True,
                    input_t[i] == output_t
                )
            )
            solver.add(input_buf_size_mb[i] == 2 * input_t[i])
        elif perf_config.op_type in [PerfTestType.MatmulSparse, PerfTestType.MatmulSparseNzCounts]:
            if i == 1:
                # we support either matching t on input1 and output
                # or we reuse single t on the input1 for all output t
                solver.add(Implies(input_t[i] != 1, input_t[i] == output_t))

            solver.add(input_buf_size_mb[i] == 2 * input_t[i])
        else:
            if not (
                perf_config.reblocking_mode == ReblockTM.Hstack
            ):
                solver.add(input_t[i] == output_t)
            if not perf_config.reblocking_mode == ReblockTM.Broadcast_r:
                solver.add(input_buf_size_mb[i] == 2 * input_t[i])
    
    solver.add(output_t > 0)
    if (perf_config.op_type == PerfTestType.ReduceMax):
        solver.add(
            If (
                reduce_dim == 3,
                output_t == 1,
                True,
            )
        )
        solver.add(output_buf_size_mb == 2 * output_t)

def constrain_common_svars(
    solver,
    inputs_mb,
    inputs_ub,
    output_mb,
    output_ub,
    input_count,
    target_device,
    perf_config: PerfOpSweepConfig,
):

    max_mm_ub_dim = 8
    min_mm_ub_dim = 1
    max_input_count = 512
    # max_ub_dim = 8
    # min_ub_dim = 1
    # if perf_config.reblocking_mode != ReblockTM.Gather:
    #     solver.add(output_mb[0] == output_mb[1])

    solver.add(output_ub[0] * output_ub[1] <= 8) # Half dest
    solver.add(output_ub[0] >= min_mm_ub_dim)
    solver.add(output_ub[0] <= max_mm_ub_dim)
    solver.add(output_ub[1] >= min_mm_ub_dim)
    solver.add(output_ub[1] <= max_mm_ub_dim)
    solver.add(input_count <= max_input_count)
    solver.add(input_count >= 0)

    for i in range(get_num_input_operands(perf_config.op_type)):
        solver.add(inputs_mb[i][0] > 0)
        solver.add(inputs_ub[i][0] > 0)
        solver.add(inputs_mb[i][1] > 0)
        solver.add(inputs_ub[i][1] > 0)

    solver.add(output_mb[0] > 0)
    solver.add(output_ub[0] > 0)
    solver.add(output_mb[1] > 0)
    solver.add(output_ub[1] > 0)
    solver.add(target_device == 0)

def constrain_single_operand_tensor(
    solver,
    tensor: List[any],
    grid_size: List[any],
    mb_dims: List[any],
    ub_dims: List[any],
):
    solver.add(tensor[0] == grid_size[0] * mb_dims[0] * ub_dims[0])
    solver.add(tensor[1] == grid_size[1] * mb_dims[1] * ub_dims[1])
  
def constrain_across_operand_tensor_dims(
    solver,
    input_tensors,
    output_tensor,
    input_mb_dims,
    input_ub_dims,
    output_mb_dims,
    output_ub_dims,
    perf_config: PerfOpSweepConfig,
    inner_u,
    inner_m,
    input_t,
    output_t,
    reduce_dim,
):  
    if (
        perf_config.op_type == PerfTestType.Matmul or
        perf_config.op_type == PerfTestType.MatmulBias
    ):
        assert inner_u is not None
        assert inner_m is not None
        assert perf_config.reblocking_mode != ReblockTM.Gather
        solver.add(input_tensors[0][0] == output_tensor[0])
        solver.add(input_tensors[1][1] == output_tensor[1])
        solver.add(input_tensors[0][1] == input_tensors[1][0])
        solver.add(input_ub_dims[0][1] == inner_u)
        solver.add(input_ub_dims[1][0] == inner_u)
        solver.add(input_tensors[0][1] == inner_m * inner_u)
        solver.add(input_ub_dims[0][0] == output_ub_dims[0])
        solver.add(input_ub_dims[1][1] == output_ub_dims[1])
        solver.add(inner_m > 0)
        solver.add(inner_u > 0)
        if perf_config.op_type == PerfTestType.MatmulBias:
            solver.add(input_mb_dims[2][0] == 1)
            solver.add(input_ub_dims[2][0] == 1)
            solver.add(input_mb_dims[2][1] == output_mb_dims[1])
            solver.add(input_ub_dims[2][1] == output_ub_dims[1])
            solver.add(input_tensors[2][0] == 1)
            solver.add(input_tensors[2][1] == output_tensor[1])
    elif (
        perf_config.op_type in [PerfTestType.MatmulSparse, PerfTestType.MatmulSparseNzCounts]
    ):
        assert inner_u is not None
        assert inner_m is not None
        assert perf_config.reblocking_mode != ReblockTM.Gather
        solver.add(input_tensors[1][0] == inner_m * inner_u)
        solver.add(input_tensors[1][1] == output_tensor[1])
        solver.add(input_ub_dims[1][0] == inner_u)
        solver.add(input_ub_dims[1][1] == output_ub_dims[1])
        solver.add(inner_m > 0)
        solver.add(inner_u > 0)
        solver.add(input_mb_dims[1][1] == output_mb_dims[1])

        solver.add(input_ub_dims[0][0] == 1)
        solver.add(input_ub_dims[0][1] == 1)
        solver.add(input_ub_dims[2][0] == 1)
        solver.add(input_ub_dims[2][1] == 1)

        solver.add(input_mb_dims[0][0] == 1)
        solver.add(input_mb_dims[2][0] == 1)
    elif (
        perf_config.op_type == PerfTestType.ReduceMax
    ):
        assert inner_u is not None
        assert inner_m is not None
        assert reduce_dim is not None
        solver.add(inner_m > 0)
        solver.add(inner_u > 0)
        solver.add(
            If (
                reduce_dim == 1,
                And(
                    input_tensors[0][1] == output_tensor[1],
                    input_ub_dims[0][0] == inner_u,
                    input_mb_dims[0][0] == inner_m,
                    input_tensors[0][0] == inner_m * inner_u,
                    input_ub_dims[0][1] == output_ub_dims[1],
                    input_mb_dims[0][1] == output_mb_dims[1],
                    output_ub_dims[0] == 1,
                    output_mb_dims[0] == 1,
                    output_tensor[0] == 1,
                ),
                If(
                    reduce_dim == 2,
                    And(
                        input_tensors[0][0] == output_tensor[0],
                        input_ub_dims[0][1] == inner_u,
                        input_mb_dims[0][1] == inner_m,
                        input_tensors[0][1] == inner_m * inner_u,
                        input_ub_dims[0][0] == output_ub_dims[0],
                        input_mb_dims[0][0] == output_mb_dims[0],
                        output_ub_dims[1] == 1,
                        output_mb_dims[1] == 1,
                        output_tensor[1] == 1,
                    ),
                    And(
                        inner_m == 1,
                        inner_u == 1,
                        input_tensors[0][0] == output_tensor[0],
                        input_tensors[0][1] == output_tensor[1],
                        input_ub_dims[0][0] == output_ub_dims[0],
                        input_mb_dims[0][0] == output_mb_dims[0],
                        input_ub_dims[0][1] == output_ub_dims[1],
                        input_mb_dims[0][1] == output_mb_dims[1],
                    ),                    
                )
            )
        )
    elif (
        perf_config.op_type == PerfTestType.Binary or
        perf_config.op_type == PerfTestType.Unary or
        perf_config.op_type == PerfTestType.TM or 
        perf_config.op_type == PerfTestType.Dram or
        perf_config.op_type == PerfTestType.Pcie
    ):    
        if perf_config.reblocking_mode == ReblockTM.Gather:
            # We always run 1x8 to 1xN ublock dims
            solver.add(input_ub_dims[0][0] == output_ub_dims[0])
            solver.add(input_tensors[0][0] == output_tensor[0])
            solver.add(input_tensors[0][1] == output_tensor[1])
        elif perf_config.reblocking_mode == ReblockTM.Hstack:
            solver.add(input_ub_dims[0][0] == output_ub_dims[0])
            solver.add(input_ub_dims[0][1] == output_ub_dims[1])
            solver.add(input_mb_dims[0][0] == output_mb_dims[0])
            solver.add(input_mb_dims[0][1] == output_mb_dims[1])
            solver.add(input_tensors[0][0] == output_tensor[0])
            solver.add(input_tensors[0][1] * input_t[0] == output_tensor[1] * output_t)
        elif perf_config.reblocking_mode == ReblockTM.Broadcast_r:
            solver.add(input_ub_dims[0][0] == output_ub_dims[0])
            solver.add(input_ub_dims[0][1] == output_ub_dims[1])
            solver.add(input_mb_dims[0][1] == output_mb_dims[1])
            solver.add(input_tensors[0][1] == output_tensor[1])
        else:
            solver.add(input_tensors[0][0] == output_tensor[0])
            if is_binary_op(perf_config.op_type):
                solver.add(input_tensors[1][0] == output_tensor[0])
            
            solver.add(input_tensors[0][1] == output_tensor[1])
            if is_binary_op(perf_config.op_type):
                solver.add(input_tensors[1][1] == output_tensor[1])

            solver.add(input_ub_dims[0][0] == output_ub_dims[0])
            solver.add(input_ub_dims[0][1] == output_ub_dims[1])
            if is_binary_op(perf_config.op_type):
                solver.add(input_ub_dims[1][0] == output_ub_dims[0])
                solver.add(input_ub_dims[1][1] == output_ub_dims[1])
                solver.add(input_ub_dims[0][0] == input_ub_dims[1][0])
                solver.add(input_ub_dims[0][1] == input_ub_dims[1][1])
    elif (
        perf_config.op_type == PerfTestType.Fused2
    ):
        solver.add(input_tensors[2][0] == output_tensor[0])
        solver.add(input_tensors[2][1] == output_tensor[1])
        solver.add(input_mb_dims[2][0] == output_mb_dims[0])
        solver.add(input_mb_dims[2][1] == output_mb_dims[1])
        solver.add(input_ub_dims[2][0] == output_ub_dims[0])
        solver.add(input_ub_dims[2][1] == output_ub_dims[1])

        solver.add(input_tensors[0][0] == input_tensors[1][0])
        solver.add(input_tensors[0][1] == input_tensors[1][1])
        solver.add(input_mb_dims[0][0] == input_mb_dims[1][0])
        solver.add(input_mb_dims[0][1] == input_mb_dims[1][1])
        solver.add(input_ub_dims[0][0] == input_ub_dims[1][0])
        solver.add(input_ub_dims[0][1] == input_ub_dims[1][1])
        solver.add(input_tensors[0][0] == output_tensor[0])
        solver.add(input_mb_dims[0][0] == output_mb_dims[0])
        solver.add(input_ub_dims[0][0] == output_ub_dims[0])
        solver.add(input_ub_dims[0][1] == 1)
        solver.add(input_mb_dims[0][1] == 1)
        # solver.add(input_tensors[0][1] == 1)

        solver.add(input_tensors[3][0] == input_tensors[4][0])
        solver.add(input_tensors[3][1] == input_tensors[4][1])
        solver.add(input_mb_dims[3][0] == input_mb_dims[4][0])
        solver.add(input_mb_dims[3][1] == input_mb_dims[4][1])
        solver.add(input_ub_dims[3][0] == input_ub_dims[4][0])
        solver.add(input_ub_dims[3][1] == input_ub_dims[4][1])
        solver.add(input_tensors[3][1] == output_tensor[1])
        solver.add(input_mb_dims[3][1] == output_mb_dims[1])
        solver.add(input_ub_dims[3][1] == output_ub_dims[1])
        solver.add(input_tensors[3][0] == 1)
    elif (
        perf_config.op_type == PerfTestType.Fused0
    ):
        solver.add(input_tensors[0][0] == output_tensor[0])
        solver.add(input_tensors[0][1] == output_tensor[1])
        solver.add(input_mb_dims[0][0] == output_mb_dims[0])
        solver.add(input_mb_dims[0][1] == output_mb_dims[1])
        solver.add(input_ub_dims[0][0] == output_ub_dims[0])
        solver.add(input_ub_dims[0][1] == output_ub_dims[1])

        solver.add(input_tensors[1][0] == 1)
        solver.add(input_tensors[1][1] == 1)

        solver.add(input_tensors[2][0] == 1)
        solver.add(input_tensors[2][1] == output_tensor[1])
        solver.add(input_mb_dims[2][1] == output_mb_dims[1])
        solver.add(input_ub_dims[2][1] == output_ub_dims[1])
    elif (
        perf_config.op_type == PerfTestType.Splice
    ):
        solver.add(input_tensors[0][0] == output_tensor[0])
        solver.add(input_tensors[1][0] == output_tensor[0])
        solver.add(input_tensors[2][0] == output_tensor[0])
        
        solver.add(input_mb_dims[0][0] == output_mb_dims[0])
        solver.add(input_mb_dims[1][0] == output_mb_dims[0])
        solver.add(input_mb_dims[2][0] == output_mb_dims[0])

        solver.add(input_mb_dims[0][1] == input_mb_dims[2][1])
        solver.add(input_mb_dims[1][1] == input_mb_dims[2][1])

        solver.add(input_ub_dims[0][0] == output_ub_dims[0])
        solver.add(input_ub_dims[1][0] == output_ub_dims[0])
        solver.add(input_ub_dims[2][0] == output_ub_dims[0])

        solver.add(input_ub_dims[0][1] == output_ub_dims[1])
        solver.add(input_ub_dims[1][1] == output_ub_dims[1])
        solver.add(input_ub_dims[2][1] == output_ub_dims[1])

        solver.add(
            output_mb_dims[1] == input_mb_dims[0][1] + input_mb_dims[1][1] + input_mb_dims[2][1]
        )
    elif (
        perf_config.op_type == PerfTestType.Tilizer
    ):
        # input output tensor dims need to match
        solver.add(input_tensors[0][0] == output_tensor[0])
        solver.add(input_tensors[0][1] == output_tensor[1])

        # ublock_r of target op must be 1
        solver.add(output_ub_dims[0] == 1)

        # since we're splitting reads of input ub columns by a factor of output_mb_c
        # we need input_ub_c to be divisible by output_mb_c
        solver.add(input_ub_dims[0][1] % output_mb_dims[1] == 0)

        # Every tilizer op core needs to get n whole ublocks, cannot split fractions of ublocks between tilizer op cores
        solver.add((output_mb_dims[0] * output_ub_dims[0]) % input_ub_dims[0][0] == 0)
        solver.add((output_mb_dims[1] * output_ub_dims[1]) % input_ub_dims[0][1] == 0)
        # Tilizer op cores cannot collect from multiple queue buffers
        solver.add((output_mb_dims[0] * output_mb_dims[1] * output_ub_dims[0] * output_ub_dims[1]) <= (input_mb_dims[0][0] * input_mb_dims[0][1] * input_ub_dims[0][0] * input_ub_dims[0][1]))

    else:
        raise ValueError(f"There are no across operand constraints for op_type {perf_config.op_type}")
    
def constrain_ublock_order(
    solver,
    input_ub_dims,
    output_ub_dims,
    feeders_ublock_order,
    target_op_ublock_order,
    perf_config: PerfOpSweepConfig,
):

    solver.add(Or(feeders_ublock_order[0] == 0, feeders_ublock_order[0] == 1))
    if is_binary_op(perf_config.op_type):
        solver.add(Or(feeders_ublock_order[1] == 0, feeders_ublock_order[1] == 1))
    
    # For now, constrain tilizer ublock order queue and op to r
    if perf_config.op_type == PerfTestType.Tilizer:
        solver.add(And(feeders_ublock_order[0] == 0, target_op_ublock_order == 0))

    solver.add(Or(target_op_ublock_order == 0, target_op_ublock_order == 1))
    if perf_config.reblocking_mode != ReblockTM.Gather:
        for i in range(get_num_input_operands(perf_config.op_type)):
            solver.add(feeders_ublock_order[i] == 0)
        solver.add(target_op_ublock_order == 0)
    else:
        # For gather, always use different ublock orders unless ublock dims are similar
        solver.add(
            If (
                And(
                    input_ub_dims[0][0] == output_ub_dims[0],
                    input_ub_dims[0][1] == output_ub_dims[1],
                ),
                True,
                target_op_ublock_order != feeders_ublock_order[0]
            )
        )


def constrain_l1_usage(
    solver,
    mb_dims: List[any],
    ub_dims: List[any],
    data_format,
    buf_size_mb,
    arch,
    svars,
    perf_config: PerfOpSweepConfig,
):
    if perf_config.op_type in [PerfTestType.MatmulSparse, PerfTestType.MatmulSparseNzCounts]:
        constrain_sparse_matmul_l1_usage(
            solver=solver,
            svars=svars,
            arch=arch,
            perf_config=perf_config
        )
    else:
        max_mem_usage = L1_MAX_USAGE_BYTES + (32 * 1024)
        solver.add(
            If (
                data_format <= 2, # Float16 or Float16_b
                max_mem_usage >= FP16_TILE_SIZE * mb_dims[0] * ub_dims[0] * mb_dims[1] * ub_dims[1] * buf_size_mb,
                If (
                    data_format <= 4, # Bfp8 or Bfp8_b
                    max_mem_usage >= BFP8_TILE_SIZE * mb_dims[0] * ub_dims[0] * mb_dims[1] * ub_dims[1] * buf_size_mb,
                    True,
                )
            )
        )


def constrain_sparse_matmul_l1_usage(
    solver,
    svars,
    arch,
    perf_config: PerfOpSweepConfig,
):
    assert perf_config.op_type in [PerfTestType.MatmulSparse, PerfTestType.MatmulSparseNzCounts]

    max_l1_buffer_size = DeviceArchitecture.create_from_string(arch, 0).get_max_l1_mem_buffer_size()
    data_format = svars['output_df']
    
    if perf_config.perf_test_mode == PerfTestMode.feeder_drainer:
        # feeder constraints
        tiles_used = (
            # input buffer
            2 * svars['input1_ub_r'] * svars['input1_ub_c'] +
            # output buffer
            svars['input1_buf_size_mb'] * svars['input1_mb_r'] * svars['input1_mb_c'] * svars['input1_ub_r'] * svars['input1_ub_c']
        )

        solver.add(
            If (
                data_format <= 2, # Float16 or Float16_b
                max_l1_buffer_size >= FP16_TILE_SIZE * tiles_used,
                Implies (
                    data_format <= 4, # Bfp8 or Bfp8_b
                    max_l1_buffer_size >= BFP8_TILE_SIZE * tiles_used,
                )
            )
        )

    # target op
    tiles_used = (
        # input buffers
        svars['input0_mb_c'] + 2 * svars['input1_mb_c'] * svars['input1_ub_c'] * svars['ub_inner'] + svars['input2_mb_c'] +
        # output (and intermediate buffer since they share space)
        svars['output_buf_size_mb'] * svars['output_mb_r'] * svars['output_mb_c'] * svars['output_ub_r'] * svars['output_ub_c']
    )

    solver.add(
        If (
            data_format <= 2, # Float16 or Float16_b
            max_l1_buffer_size >= FP16_TILE_SIZE * tiles_used,
            Implies (
                data_format <= 4, # Bfp8 or Bfp8_b
                max_l1_buffer_size >= BFP8_TILE_SIZE * tiles_used,
            )
        )
    )

    if perf_config.perf_test_mode != PerfTestMode.single_op:
        # drainer
        tiles_used = (
            # input buffers
            2 * svars['output_ub_r'] * svars['output_ub_c'] +
            # output
            svars['output_buf_size_mb'] * svars['output_mb_r'] * svars['output_mb_c'] * svars['output_ub_r'] * svars['output_ub_c']
        )

        solver.add(
            If (
                data_format <= 2, # Float16 or Float16_b
                max_l1_buffer_size >= FP16_TILE_SIZE * tiles_used,
                Implies (
                    data_format <= 4, # Bfp8 or Bfp8_b
                    max_l1_buffer_size >= BFP8_TILE_SIZE * tiles_used,
                )
            )
        )

def constrain_math_fidelity_and_data_format(
    solver,
    math_fidelity,
    input_data_formats,
    output_data_format,
    intermed_data_format,
    arch,
    perf_config: PerfOpSweepConfig,
    l1_acc = None,
):        
    input_output_data_formats = input_data_formats + [output_data_format]
    all_data_formats = input_data_formats + [output_data_format] + [intermed_data_format]
    if arch.lower() == "wormhole" and perf_config.op_type == PerfTestType.Matmul:
        solver.add(math_fidelity == 2)
    elif perf_config.op_type == PerfTestType.MatmulSparseNzCounts:
        solver.add(
            Implies(math_fidelity == MathFidelity.LoFi.value, Or(output_data_format == DataFormat.Bfp8.value, output_data_format == DataFormat.Bfp8_b.value))
        )
        solver.add(
            Implies(
                Or(math_fidelity == MathFidelity.HiFi2.value, math_fidelity == MathFidelity.HiFi3.value, math_fidelity == MathFidelity.HiFi4.value),
                Or(output_data_format == DataFormat.Float16.value, output_data_format == DataFormat.Float16_b.value)
            )
        )
        
    else:
        solver.add(Or(math_fidelity == 1, math_fidelity == 2, math_fidelity == 4))
        # If math-fidelity == LF --> data-format == Bfp8a/b or Bfp4a/b
        # If math-fidelity == HF --> data-format == Float16a/b
        for operand_data_format in input_output_data_formats:
            solver.add(
                If (
                    math_fidelity == 1,
                    Or(operand_data_format == 3, operand_data_format == 4, operand_data_format == 5, operand_data_format == 6),
                    If (
                        math_fidelity == 4,
                        Or(operand_data_format == 1, operand_data_format == 2),
                        True,
                    )
                )
            )
    
    
    all_data_format_vals = [e.value for e in DataFormat]
    for data_format in all_data_formats: 
        solver.add(data_format <= max(all_data_format_vals))
        solver.add(data_format >= min(all_data_format_vals))
        
    data_formats_a = [
        DataFormat.Float16.value, 
        DataFormat.Bfp8.value,
        DataFormat.Bfp4.value,
        DataFormat.Bfp2.value,
        DataFormat.RawUInt32.value
    ]
    
    data_formats_b = [
        DataFormat.Float16_b.value, 
        DataFormat.Bfp8_b.value,
        DataFormat.Bfp4_b.value,
        DataFormat.Bfp2_b.value   
    ]
    
    # data format A/B for all input/output operands must match
    first_input_data_format = input_data_formats[0]
    for operand_data_format in input_output_data_formats:
        solver.add(
            Implies(
                Or([first_input_data_format == data_format for data_format in data_formats_a]),
                Or([operand_data_format == data_format for data_format in data_formats_a])
            )
        )
        solver.add(
            Implies(
                Or([first_input_data_format == data_format for data_format in data_formats_b]),
                Or([operand_data_format == data_format for data_format in data_formats_b])
            )
        )
    
    if l1_acc is not None:
        assert is_matmul_op(perf_config.op_type)
        # TODO: These need to be updated if we add support for fp32
        # Output intermed df can only be fp16b or fp32 when l1_acc is enabled
        solver.add(
            Implies(
                l1_acc == 1,
                intermed_data_format == DataFormat.Float16_b.value,
            )
        )
        # output intermed df A/B must match input/output data format A/B if l1_acc is enabled
        # since intermed data format must be fp16b, input/output data format should have B format
        for operand_data_format in input_output_data_formats:
            solver.add(
                Implies(
                    l1_acc == 1,
                    Or([operand_data_format == data_format for data_format in data_formats_b]),
                )
            )
            
    elif perf_config.op_type == PerfTestType.Tilizer:
        for input_data_format in input_data_formats:
            # Tilizer op should support non-bfp input formats, i.e. float16 and float16_b
            solver.add(Or(input_data_format == DataFormat.Float16.value, input_data_format == DataFormat.Float16_b.value))
        solver.add(output_data_format != DataFormat.RawUInt32.value)

    
# For specific Ops/TMs that do not require grid size sweep, we hardcode the grid-size and location
# For the others we default to constrain_feeder_drainer_grid_loc
def hardcode_grid_loc(
    solver,
    max_grid_size,
    feeders_loc,
    target_op_loc,
    drainer_loc,
    
    feeders_grid_size,
    target_op_grid_size,
    drainer_grid_size,

    perf_config: PerfOpSweepConfig,
):
    assert perf_config.reblocking_mode == ReblockTM.Gather

    num_feeders = len(feeders_loc)

    if perf_config.reblocking_mode == ReblockTM.Gather:
    
        solver.add(feeders_loc[0][0] == 0)
        solver.add(feeders_loc[0][1] == 0)
        solver.add(target_op_loc[0] == 0)
        solver.add(target_op_loc[1] == 1)
        solver.add(drainer_loc[0] == 0)
        solver.add(drainer_loc[1] == 2)

        if num_feeders > 1:
            solver.add(feeders_loc[1][0] == 4)
            solver.add(feeders_loc[1][1] == 0)

def find_loc_for_grid(
    core_used,
    grid_size_r,
    grid_size_c,
):
    assert grid_size_r < len(core_used)
    grid_loc_r = -1
    grid_loc_c = -1
    for row in range(len(core_used) - grid_size_r):
        assert grid_size_c < len(core_used[row])
        for col in range(len(core_used[row]) - grid_size_c):
            # print ("row = ", row, " col = ", col, " grid_size_r = ", grid_size_r, " grid_size_c = ", grid_size_c)
            # print (core_used)
            # print ("sum = ", sum([sum(core_used[i][col:col+grid_size_c]) for i in range(row, row+grid_size_r)]))
            if sum([sum(core_used[i][col:col+grid_size_c]) for i in range(row, row+grid_size_r)]) == 0:
                grid_loc_r = row
                grid_loc_c = col
                for i in range(row, row+grid_size_r):
                    for j in range(col, col+grid_size_c):
                        # print ("i = ", i, " j = ", j)
                        # print ("core_used before = ", core_used)
                        core_used[i][j] = 1
                        # print ("core_used after = ", core_used)
                return grid_loc_r, grid_loc_c, True
    
    return grid_loc_r, grid_loc_c, False

def constrain_grid_size(
    solver,
    max_grid_size,
    feeders_grid_size,
    target_op_grid_size,
    drainer_grid_size,
    reduce_dim,
    perf_config: PerfOpSweepConfig,
):
    num_feeders = len(feeders_grid_size)
    
    if perf_config.reblocking_mode == ReblockTM.Gather:
        solver.add(feeders_grid_size[0][0] == 4)
        solver.add(feeders_grid_size[0][1] == 1)
        solver.add(target_op_grid_size[0] == 1)
        solver.add(target_op_grid_size[1] == 1)
        solver.add(drainer_grid_size[0] == 1)
        solver.add(drainer_grid_size[1] == 1)
        if num_feeders > 1:
            solver.add(feeders_grid_size[1][0] == 4)
            solver.add(feeders_grid_size[1][1] == 1)
        return

    for i in range(get_num_input_operands(perf_config.op_type)):
        solver.add(feeders_grid_size[i][0] <= max_grid_size[0])
        solver.add(feeders_grid_size[i][1] <= max_grid_size[1])
        solver.add(feeders_grid_size[i][0] >= 0)
        solver.add(feeders_grid_size[i][1] >= 0)
    
    if perf_config.op_type == PerfTestType.MatmulBias:
        solver.add(feeders_grid_size[2][1] == target_op_grid_size[1])

    if perf_config.op_type in [PerfTestType.MatmulSparse, PerfTestType.MatmulSparseNzCounts]:
        solver.add(feeders_grid_size[2][0] == feeders_grid_size[1][0])
        solver.add(feeders_grid_size[2][1] == feeders_grid_size[1][1])
    if perf_config.op_type == PerfTestType.Fused0:
        solver.add(feeders_grid_size[0][0] == target_op_grid_size[0])
        solver.add(feeders_grid_size[0][1] == target_op_grid_size[1])
        solver.add(feeders_grid_size[1][0] == 1)
        solver.add(feeders_grid_size[1][1] == 1)
        solver.add(feeders_grid_size[2][0] == 1)
        solver.add(feeders_grid_size[2][1] == target_op_grid_size[1])
    elif perf_config.op_type == PerfTestType.Fused2:
        solver.add(feeders_grid_size[0][0] == target_op_grid_size[0])
        solver.add(feeders_grid_size[0][1] == target_op_grid_size[1])
        solver.add(feeders_grid_size[1][0] == target_op_grid_size[0])
        solver.add(feeders_grid_size[1][1] == target_op_grid_size[1])
        solver.add(feeders_grid_size[3][0] == 1)
        solver.add(feeders_grid_size[3][1] == target_op_grid_size[1])
        solver.add(feeders_grid_size[4][0] == 1)
        solver.add(feeders_grid_size[4][1] == target_op_grid_size[1])
    elif perf_config.reblocking_mode == ReblockTM.Hstack:
        solver.add(feeders_grid_size[0][0] == target_op_grid_size[0])
    elif perf_config.op_type == PerfTestType.Matmul and perf_config.perf_test_mode == PerfTestMode.single_op:
        solver.add(feeders_grid_size[0][0] == target_op_grid_size[0])
        solver.add(feeders_grid_size[0][1] == 1)
        solver.add(feeders_grid_size[1][0] == 1)
        solver.add(feeders_grid_size[1][1] == target_op_grid_size[1])
    elif perf_config.op_type == PerfTestType.ReduceMax:
        assert reduce_dim is not None
        solver.add(
            If (
                reduce_dim == 1,
                And(
                    feeders_grid_size[0][1] == target_op_grid_size[1],
                    feeders_grid_size[0][0] == target_op_grid_size[0],
                    feeders_grid_size[0][0] == 1,
                ),
                If(
                    reduce_dim == 2,
                    And(
                        feeders_grid_size[0][0] == target_op_grid_size[0],
                        feeders_grid_size[0][1] == target_op_grid_size[1],
                        feeders_grid_size[0][1] == 1,
                    ),
                    And(
                        feeders_grid_size[0][1] == target_op_grid_size[1],
                        feeders_grid_size[0][0] == target_op_grid_size[0],
                    ),                    
                )
            )
        )
    elif perf_config.op_type == PerfTestType.Tilizer:
        # op grid size must be >= queue grid size, but not less
        solver.add(target_op_grid_size[0] * target_op_grid_size[1] >= feeders_grid_size[0][0] * feeders_grid_size[0][1])
    else:
        if not perf_config.perf_test_mode == PerfTestMode.single_op:
            solver.add(feeders_grid_size[0][0] == feeders_grid_size[0][1])
        solver.add(feeders_grid_size[0][0] == target_op_grid_size[0])
        solver.add(feeders_grid_size[0][1] == target_op_grid_size[1])
        for op in range(num_feeders):
            solver.add(feeders_grid_size[0][0] == feeders_grid_size[op][0])
            solver.add(feeders_grid_size[0][1] == feeders_grid_size[op][1])

    solver.add(target_op_grid_size[0] <= max_grid_size[0])
    solver.add(target_op_grid_size[1] <= max_grid_size[1])
    solver.add(target_op_grid_size[0] >= 0)
    solver.add(target_op_grid_size[1] >= 0)
    solver.add(drainer_grid_size[0] <= max_grid_size[0])
    solver.add(drainer_grid_size[1] <= max_grid_size[1])
    solver.add(drainer_grid_size[0] >= 0)
    solver.add(drainer_grid_size[1] >= 0)

    # if perf_config.reblocking_mode == ReblockTM.Hstack:
    #     solver.add(drainer_grid_size[0] == target_op_grid_size[0])
    # else:
    solver.add(drainer_grid_size[0] == target_op_grid_size[0])
    solver.add(drainer_grid_size[1] == target_op_grid_size[1])
    if (
        not perf_config.perf_test_mode == PerfTestMode.single_op and
        not perf_config.reblocking_mode == ReblockTM.Hstack and
        not perf_config.op_type == PerfTestType.ReduceMax and
        not perf_config.op_type == PerfTestType.Tilizer
    ):
        solver.add(drainer_grid_size[0] == target_op_grid_size[1])

def constrain_grid_loc(
    model_vars,
    max_grid_size,
    feeders_loc,
    target_op_loc,
    drainer_loc,
    
    feeders_grid_size,
    target_op_grid_size,
    drainer_grid_size,

    core_used,
    arch,
    perf_config: PerfOpSweepConfig,
):
    num_feeders = len(feeders_loc)
    placement_valid = True
    if perf_config.reblocking_mode == ReblockTM.Gather:
        model_vars[feeders_loc[0][0]] = 0
        model_vars[feeders_loc[0][1]] = 0
        model_vars[target_op_loc[0]] = 0
        model_vars[target_op_loc[1]] = 1
        model_vars[drainer_loc[0]] = 0
        model_vars[drainer_loc[1]] = 2
        if num_feeders > 1:
            model_vars[feeders_loc[1][0]] = 4
            model_vars[feeders_loc[1][1]] = 0
        return placement_valid
    
    if (
        perf_config.perf_test_mode == PerfTestMode.prologue or
        perf_config.op_type == PerfTestType.Dram or
        perf_config.op_type == PerfTestType.Pcie
    ):
        for i in range(get_num_input_operands(perf_config.op_type)):
            model_vars[feeders_loc[i][0]] = 0
            model_vars[feeders_loc[i][1]] = 0

        model_vars[target_op_loc[0]] = 0
        model_vars[target_op_loc[1]] = 0
        model_vars[drainer_loc[1]] = 0
        model_vars[drainer_loc[0]] = model_vars[target_op_loc[0]] + model_vars[target_op_grid_size[0]]
        assert model_vars[drainer_loc[0]] + model_vars[drainer_grid_size[0]] <= max_grid_size[0]
        assert model_vars[drainer_loc[1]] + model_vars[drainer_grid_size[1]] <= max_grid_size[1]
    elif perf_config.perf_test_mode == PerfTestMode.single_op:
        for i in range(get_num_input_operands(perf_config.op_type)):
            model_vars[feeders_loc[i][0]] = 0
            model_vars[feeders_loc[i][1]] = 0
        model_vars[target_op_loc[0]] = 0
        model_vars[target_op_loc[1]] = 0
        model_vars[drainer_loc[1]] = 0
        model_vars[drainer_loc[0]] = 0
    elif num_feeders < 3 and model_vars[feeders_grid_size[0][0]] == 1:
        model_vars[feeders_loc[0][0]] = 0
        model_vars[feeders_loc[0][1]] = 0
        if num_feeders > 1:
            if ARCH.GS.value:
                model_vars[feeders_loc[1][0]] = 1
                model_vars[feeders_loc[1][1]] = 1
            else:
                model_vars[feeders_loc[1][0]] = 4
                model_vars[feeders_loc[1][1]] = 0
        if num_feeders > 2:
            model_vars[feeders_loc[2][0]] = 1
            model_vars[feeders_loc[2][1]] = 1 
        model_vars[target_op_loc[0]] = 1
        model_vars[target_op_loc[1]] = 0
        model_vars[drainer_loc[0]] = 2
        model_vars[drainer_loc[1]] = 0
    else:
        for i in range(get_num_input_operands(perf_config.op_type)):
            loc_r, loc_c, placement_valid = find_loc_for_grid(
                core_used,
                model_vars[feeders_grid_size[i][0]],
                model_vars[feeders_grid_size[i][1]],
            )
            # print (f"loc_r = {loc_r}, loc_c = {loc_c}")
            model_vars[feeders_loc[i][0]] = loc_r
            model_vars[feeders_loc[i][1]] = loc_c
        loc_r, loc_c, placement_valid = find_loc_for_grid(core_used, model_vars[target_op_grid_size[0]], model_vars[target_op_grid_size[1]])
        # print (f"loc_r = {loc_r}, loc_c = {loc_c}")
        model_vars[target_op_loc[0]] = loc_r
        model_vars[target_op_loc[1]] = loc_c
        loc_r, loc_c, placement_valid = find_loc_for_grid(core_used, model_vars[drainer_grid_size[0]], model_vars[drainer_grid_size[1]])
        # print (f"loc_r = {loc_r}, loc_c = {loc_c}")
        model_vars[drainer_loc[0]] = loc_r
        model_vars[drainer_loc[1]] = loc_c
    return placement_valid

def constrain_op_specific_vars(solver, svars, perf_config: PerfOpSweepConfig, input_tensor_c):
    
    if (perf_config.op_type == PerfTestType.Unary):
        
        all_unary_type_vals = [e.value for e in UnaryType]
        solver.add(svars['unary_type'] <= max(all_unary_type_vals))
        solver.add(svars['unary_type'] >= min(all_unary_type_vals))
        solver.add(Or(svars['approximate_mode'] == 0, svars['approximate_mode'] == 1))
        all_modes = [e.value for e in SfpuVectorMode]
        solver.add(svars['vector_mode'] <= max(all_modes))
        solver.add(svars['vector_mode'] >= min(all_modes))
    
    elif (perf_config.op_type == PerfTestType.Binary):
        
        all_binary_type_vals = [e.value for e in BinaryType]
        solver.add(svars['binary_type'] <= max(all_binary_type_vals))
        solver.add(svars['binary_type'] >= min(all_binary_type_vals))
        # Only run HiFi for binary mult
        solver.add(
            If (
                svars["binary_type"] != BinaryType.multiply.value,
                svars["math_fidelity"] == 1,
                True,
            )
        )
    elif (perf_config.op_type == PerfTestType.ReduceMax):
        all_dims = [e.value for e in ReduceDim]
        solver.add(svars['reduce_dim'] <= max(all_dims))
        solver.add(svars['reduce_dim'] >= min(all_dims))
    
    elif (perf_config.op_type == PerfTestType.Splice):
        solver.add(svars['input0_index'] == 0)
        solver.add(svars['input1_index'] == 0)
        solver.add(svars['input2_index'] == 0)

        solver.add(svars['input0_length'] == svars['input0_mb_c'])
        solver.add(svars['input1_length'] == svars['input1_mb_c'])
        solver.add(svars['input2_length'] == svars['input2_mb_c'])

        solver.add(svars['input0_stride'] == svars['input0_mb_c'])
        solver.add(svars['input1_stride'] == svars['input1_mb_c'])
        solver.add(svars['input2_stride'] == svars['input2_mb_c'])
        
    elif (perf_config.op_type in [PerfTestType.MatmulSparse, PerfTestType.MatmulSparseNzCounts]):
        input0_tensor_c, input2_tensor_c = input_tensor_c[0], input_tensor_c[2]
        solver.add(svars['sparse_num_tiles'] == input0_tensor_c)
        solver.add(svars['num_index_tiles'] == input2_tensor_c)

        if perf_config.op_type == PerfTestType.MatmulSparse:
            solver.add(svars['zero_strip_freq'] <= 100)
            solver.add(svars['zero_strip_freq'] >= 0)

            solver.add(svars['zero_ublock_freq'] <= 100)
            solver.add(svars['zero_ublock_freq'] >= 0)

            solver.add(svars['zero_tile_freq'] <= 100)
            solver.add(svars['zero_tile_freq'] >= 0)

            # to reduce the parameter space
            solver.add(svars['zero_tile_freq'] == svars['zero_ublock_freq'])
            
    elif (perf_config.op_type == PerfTestType.Tilizer):
        solver.add(svars["parallelization_factor_r"] * svars["input0_grid_size_r"] == svars["output_grid_size_r"])
        solver.add(svars["parallelization_factor_c"] * svars["input0_grid_size_c"] == svars["output_grid_size_c"])

    if is_matmul_op(perf_config.op_type):
        solver.add(Or(svars['l1_acc'] == 0, svars['l1_acc'] == 1))
        
# In prologue mode, the feeder grid sizes do not matter.
# However to ensure we won't sweep over its values, we need to fix that to a single number
def set_feeder_grid_sizes_in_prologue(
    solver,
    svars,
    inputs_grid_sizes: List[List[str]],
    perf_config: PerfOpSweepConfig,
):
    if perf_config.perf_test_mode == PerfTestMode.prologue:
        # assert inputs_grid_sizes[0][0] in perf_config.sweep_vars
        # assert inputs_grid_sizes[0][1] in perf_config.sweep_vars
        # if is_binary_op(perf_config.op_type):
        #     assert inputs_grid_sizes[1][0] in perf_config.sweep_vars
        #     assert inputs_grid_sizes[1][1] in perf_config.sweep_vars

        if inputs_grid_sizes[0][0] in perf_config.sweep_vars:
            solver.add(svars[inputs_grid_sizes[0][0]] == perf_config.sweep_vars[inputs_grid_sizes[0][0]][0])
        else:
            solver.add(svars[inputs_grid_sizes[0][0]] == 0)
        if inputs_grid_sizes[0][1] in perf_config.sweep_vars:
            solver.add(svars[inputs_grid_sizes[0][1]] == perf_config.sweep_vars[inputs_grid_sizes[0][1]][0])
        else:
            solver.add(svars[inputs_grid_sizes[0][1]] == 0)
        if is_binary_op(perf_config.op_type):
            if inputs_grid_sizes[1][0] in perf_config.sweep_vars:
                solver.add(svars[inputs_grid_sizes[1][0]] == perf_config.sweep_vars[inputs_grid_sizes[1][0]][0])
            else:
                solver.add(svars[inputs_grid_sizes[1][0]] == 0)
            if inputs_grid_sizes[1][1] in perf_config.sweep_vars:
                solver.add(svars[inputs_grid_sizes[1][1]] == perf_config.sweep_vars[inputs_grid_sizes[1][1]][0])
            else:
                solver.add(svars[inputs_grid_sizes[1][1]] == 0)
