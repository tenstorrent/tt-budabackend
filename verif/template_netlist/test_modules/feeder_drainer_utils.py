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
    input_data_formats,
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
    if perf_config.op_type in [
        PerfTestType.MatmulQuant,
        PerfTestType.MatmulBiasQuant,
        PerfTestType.Quant
    ]:
        solver.add(output_ub[0] * output_ub[1] <= 4) # 4 for integer ops
        
    elif perf_config.op_type in [
        PerfTestType.Matmul,
        PerfTestType.MatmulBias,
        PerfTestType.MatmulSparse,
        PerfTestType.Binary,
        PerfTestType.Unary,
    ]:
        for input_data_format in input_data_formats:
            # If any of the input data formats is integer, then we can only store 4 tiles in dest
            solver.add(
                Implies(
                    Or([input_data_format == data_format for data_format in data_formats_int]),
                    output_ub[0] * output_ub[1] <= 4
                )
            )
    
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
    if perf_config.op_type in [
        PerfTestType.Matmul,
        PerfTestType.MatmulBias,
        PerfTestType.MatmulQuant,
        PerfTestType.MatmulBiasQuant
    ]:
        assert inner_u is not None
        assert inner_m is not None
        assert perf_config.reblocking_mode != ReblockTM.Gather
        solver.add(input_tensors[0][0] == output_tensor[0])
        solver.add(input_tensors[1][1] == output_tensor[1])
        solver.add(input_ub_dims[0][1] == inner_u)
        solver.add(input_ub_dims[1][0] == inner_u)
        solver.add(input_tensors[0][1] == inner_m * inner_u)
        solver.add(input_tensors[0][1] == input_tensors[1][0])
        solver.add(input_ub_dims[0][0] == output_ub_dims[0])
        solver.add(input_ub_dims[1][1] == output_ub_dims[1])
        solver.add(inner_m > 0)
        solver.add(inner_u > 0)
        # Set constraints for bias input if needed
        if perf_config.op_type in [PerfTestType.MatmulBias, PerfTestType.MatmulBiasQuant]:
            solver.add(input_mb_dims[2][0] == 1)
            solver.add(input_ub_dims[2][0] == 1)
            solver.add(input_mb_dims[2][1] == output_mb_dims[1])
            solver.add(input_ub_dims[2][1] == output_ub_dims[1])
            solver.add(input_tensors[2][0] == 1)
            solver.add(input_tensors[2][1] == output_tensor[1])
        # Set constraints for scalar input if needed
        if perf_config.op_type in [PerfTestType.MatmulQuant, PerfTestType.MatmulBiasQuant]:
            scalar_input_index = 2 if perf_config.op_type == PerfTestType.MatmulQuant else 3
            solver.add(input_mb_dims[scalar_input_index][0] == output_mb_dims[0])
            solver.add(input_ub_dims[scalar_input_index][0] == output_ub_dims[0])
            solver.add(input_mb_dims[scalar_input_index][1] == output_mb_dims[1])
            solver.add(input_ub_dims[scalar_input_index][1] == output_ub_dims[1])
            solver.add(input_tensors[scalar_input_index][0] == output_tensor[0])
            solver.add(input_tensors[scalar_input_index][1] == output_tensor[1])
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
    elif perf_config.op_type in [
            PerfTestType.Binary,
            PerfTestType.Unary,
            PerfTestType.TM,
            PerfTestType.Dram,
            PerfTestType.Pcie,
            PerfTestType.Quant,
        ]:    
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
        all_data_format_vals = [e.value for e in DataFormat]
        for df_val in all_data_format_vals:
            df_tile_size = get_data_format_tile_size_map(arch)[df_val]
            solver.add(
                Implies(
                    data_format == df_val,
                    df_tile_size * mb_dims[0] * ub_dims[0] * mb_dims[1] * ub_dims[1] * buf_size_mb <= max_mem_usage,
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
    
    tiles_used_all_ops = []
    if perf_config.perf_test_mode == PerfTestMode.feeder_drainer:
        # feeder constraints
        tiles_used_feeder = (
            # input buffer
            2 * svars['input1_ub_r'] * svars['input1_ub_c'] +
            # output buffer
            svars['input1_buf_size_mb'] * svars['input1_mb_r'] * svars['input1_mb_c'] * svars['input1_ub_r'] * svars['input1_ub_c']
        )
        tiles_used_all_ops.append(tiles_used_feeder)

    # target op
    tiles_used_target = (
        # input buffers
        svars['input0_mb_c'] + 2 * svars['input1_mb_c'] * svars['input1_ub_c'] * svars['ub_inner'] + svars['input2_mb_c'] +
        # output (and intermediate buffer since they share space)
        svars['output_buf_size_mb'] * svars['output_mb_r'] * svars['output_mb_c'] * svars['output_ub_r'] * svars['output_ub_c']
    )
    
    tiles_used_all_ops.append(tiles_used_target)

    if perf_config.perf_test_mode != PerfTestMode.single_op:
        # drainer
        tiles_used_drainer = (
            # input buffers
            2 * svars['output_ub_r'] * svars['output_ub_c'] +
            # output
            svars['output_buf_size_mb'] * svars['output_mb_r'] * svars['output_mb_c'] * svars['output_ub_r'] * svars['output_ub_c']
        )

        tiles_used_all_ops.append(tiles_used_drainer)
    
    all_data_format_vals = [e.value for e in DataFormat]
    for tiles_used in tiles_used_all_ops:
        for df_val in all_data_format_vals:
            df_tile_size = get_data_format_tile_size_map(arch)[df_val]
            solver.add(
                Implies(
                    data_format == df_val, 
                    df_tile_size * tiles_used <= max_l1_buffer_size,
                )
            )

def get_dest_acc_data_format_constraints(
    input_data_formats,
    input_dest_acc_data_formats,
    output_data_format,
    output_dest_acc_data_format,
):
    # Constrain input dest accumulate data format for int ops: must be int32 for integer inputs
    # PerfSweep constraint: dest accumulate df = fp16 if input df is "a", fp16_b if input df is "b"
    input_dest_acc_data_format_constraints = []
    for input_operand_idx in range(len(input_data_formats)):
        input_data_format = input_data_formats[input_operand_idx]
        input_dest_acc_data_format = input_dest_acc_data_formats[input_operand_idx]
        input_dest_acc_data_format_constraints.append(
            And(
                Implies(
                    Or([input_data_format == data_format for data_format in data_formats_int]),
                    input_dest_acc_data_format == DataFormat.Int32.value
                ),
                Implies(
                    Or([input_data_format == data_format for data_format in data_formats_a]),
                    input_dest_acc_data_format == DataFormat.Float16.value
                ),
                Implies(
                    Or([input_data_format == data_format for data_format in data_formats_b]),
                    input_dest_acc_data_format == DataFormat.Float16_b.value
                ),
                Implies(
                    input_data_format == DataFormat.RawUInt32.value,
                    input_dest_acc_data_format == DataFormat.RawUInt32.value
                )
            )
        )
    
    output_dest_acc_data_format_constraint = And(
        # If the output data format is int or any input data format is int, use Int32 as dest acc df
        Implies(
            Or(
                Or([output_data_format == data_format for data_format in data_formats_int]), 
                Or([input_data_format == data_format for data_format in data_formats_int for input_data_format in input_data_formats])
            ),
            output_dest_acc_data_format == DataFormat.Int32.value
        ),
        
        # If no input data format is int and output data format is not int, output dest acc df will be Float16/Float16_b depending on output dataformat A/B
        Implies(
            And(
                Or([output_data_format == data_format for data_format in data_formats_a]), 
                And([input_data_format != data_format for data_format in data_formats_int for input_data_format in input_data_formats])
            ),
            output_dest_acc_data_format == DataFormat.Float16.value
        ),
        Implies(
            And(
                Or([output_data_format == data_format for data_format in data_formats_b]),
                And([input_data_format != data_format for data_format in data_formats_int for input_data_format in input_data_formats])
            ),
            output_dest_acc_data_format == DataFormat.Float16_b.value
        )
    )
    return input_dest_acc_data_format_constraints + [output_dest_acc_data_format_constraint]

# get input/output operand data format constraints for general ops when the input data format is integer
def get_int_operand_data_format_constraints_for_general_ops(
    input_data_formats,
    output_data_format,
    perf_config: PerfOpSweepConfig,
    binary_type = None,
):
    general_ops_that_support_int = [
        PerfTestType.Matmul, 
        PerfTestType.MatmulBias, 
        PerfTestType.MatmulSparse,
        PerfTestType.Binary,
        PerfTestType.Unary
    ]
    
    assert perf_config.op_type in general_ops_that_support_int
    
    operand_data_format_constraints = []
    
    if perf_config.op_type == PerfTestType.Unary:
        operand_data_format_constraints.append(Or(output_data_format == DataFormat.Int8.value, output_data_format == DataFormat.Int32.value))
        
    elif perf_config.op_type == PerfTestType.Binary:
        assert binary_type is not None
        
        for input_data_format in input_data_formats:
            # Add can have only Int8 or Int32 inputs for integer version of this op
            operand_data_format_constraints.append(
                Implies(
                    binary_type == BinaryType.add.value,
                    Or(input_data_format == DataFormat.Int8.value, input_data_format == DataFormat.Int32.value)
                )
            )

            # Sub/Mul can have only Int8 inputs for integer version of these ops
            operand_data_format_constraints.append(      
                Implies(
                    Or(binary_type == BinaryType.multiply.value, binary_type == BinaryType.subtract.value),
                    input_data_format == DataFormat.Int8.value,
                )
            )
            
        # If Int8 or Int32 input format is used, out data format must be Int8 or Int32 if dequant is not enabled
        operand_data_format_constraints.append(Or(output_data_format == DataFormat.Int8.value, output_data_format == DataFormat.Int32.value))
        

    elif perf_config.op_type in [PerfTestType.Matmul, PerfTestType.MatmulBias, PerfTestType.MatmulSparse]:
        # Matmul with int inputs must have input 0 in Int8 format.
        operand_data_format_constraints.append(input_data_formats[0] == DataFormat.Int8.value)
        
        # Matmul with int inputs must have input 1 in Int8 format.
        operand_data_format_constraints.append(input_data_formats[1] == DataFormat.Int8.value)
        
        # If Int8 or Int32 input format is used, out data format must be Int8 or Int32 if dequant is not enabled
        operand_data_format_constraints.append(Or(output_data_format == DataFormat.Int8.value, output_data_format == DataFormat.Int32.value))
        
        # Matmul with int inputs can't have bias input in non-Int32 format
        if perf_config.op_type == PerfTestType.MatmulBias:
            operand_data_format_constraints.append(input_data_formats[2] == DataFormat.Int32.value)
        
    return operand_data_format_constraints
    
def constrain_math_fidelity_and_data_format(
    solver,
    arch_str,
    perf_config: PerfOpSweepConfig,
    math_fidelity,
    input_data_formats,
    input_dest_acc_data_formats,
    output_data_format,
    output_dest_acc_data_format,
    intermed_data_format,
    l1_acc=None,
    binary_type=None,
    unary_type=None,
    dequant=None,
    requant=None,
    quant_type=None,
):  
    input_output_data_formats = input_data_formats + [output_data_format]
    non_encoding_operand_data_formats = (input_data_formats[:2] + [output_data_format]) if perf_config.op_type == PerfTestType.MatmulSparse else input_output_data_formats
    
    input_output_dest_acc_data_formats = input_dest_acc_data_formats + [output_dest_acc_data_format]
    assert len(input_output_data_formats) == len(input_output_dest_acc_data_formats)
    
    all_data_formats = input_data_formats + [output_data_format] + [intermed_data_format] + input_dest_acc_data_formats + [output_dest_acc_data_format]
    if arch_str.lower() == "wormhole" and perf_config.op_type == PerfTestType.Matmul:
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
        # If math-fidelity == HF --> data-format == Float16a/b or Int32/8
        for operand_data_format in non_encoding_operand_data_formats:
            solver.add(
                Implies(
                    math_fidelity == MathFidelity.LoFi.value,
                    Or (
                        operand_data_format == DataFormat.Bfp8.value, 
                        operand_data_format == DataFormat.Bfp8_b.value, 
                        operand_data_format == DataFormat.Bfp4.value, 
                        operand_data_format == DataFormat.Bfp4_b.value,
                    )
                )
            )
            solver.add(
                Implies(
                    math_fidelity == MathFidelity.HiFi4.value,
                    Or(
                        operand_data_format == DataFormat.Float32.value,
                        operand_data_format == DataFormat.Int32.value,
                        operand_data_format == DataFormat.Float16.value,
                        operand_data_format == DataFormat.Float16_b.value,
                        operand_data_format == DataFormat.Bfp8.value, 
                        operand_data_format == DataFormat.Bfp8_b.value, 
                        operand_data_format == DataFormat.Int8.value,
                    )
                )
            )
    
    
    all_data_format_vals = [e.value for e in DataFormat]
    for data_format in all_data_formats:
        solver.add(data_format <= max(all_data_format_vals))
        solver.add(data_format >= min(all_data_format_vals))

    # Add l1_acc data_format constraints
    if is_matmul_op(perf_config.op_type):
        assert l1_acc is not None
        # Output intermed df can only be fp32, fp16b when l1_acc is enabled
        solver.add(
            Implies(
                l1_acc == 1,
                Or(intermed_data_format == DataFormat.Float32.value, intermed_data_format == DataFormat.Float16_b.value)
            )
        )
        # Perf sweep constraint: output intermed df A/B must match input/output data format A/B if l1_acc is enabled (if not then it will be hardcoded to output df in extra config callback)
        # l1_acc will be constrained to 0 when we are using integer input dataformats
        for operand_data_format in non_encoding_operand_data_formats:
            solver.add(
                Implies(
                    l1_acc == 1,
                    If (
                        intermed_data_format == DataFormat.Float32.value,
                        Or([operand_data_format == data_format for data_format in data_formats_a]),
                        Or([operand_data_format == data_format for data_format in data_formats_b]),
                    ),
                )
            )
    
    # PerfSweep constraint: data format A/B for all input/output operands must match
    non_int_operand_data_format_constraints = []
    first_input_data_format = input_data_formats[0]
    for operand_data_format in non_encoding_operand_data_formats:
        # None of the data formats should be integer
        non_int_operand_data_format_constraints.append(
            And([operand_data_format != data_format for data_format in data_formats_int])
        )
        non_int_operand_data_format_constraints.append(
            Implies(
                Or([first_input_data_format == data_format for data_format in data_formats_a]),
                Or([operand_data_format == data_format for data_format in data_formats_a]),
            )
        )
        non_int_operand_data_format_constraints.append(
            Implies(
                Or([first_input_data_format == data_format for data_format in data_formats_b]),
                Or([operand_data_format == data_format for data_format in data_formats_b]),
            )
        )
    
    dest_acc_data_format_constraints = get_dest_acc_data_format_constraints(
        input_data_formats=input_data_formats, 
        input_dest_acc_data_formats=input_dest_acc_data_formats,
        output_data_format=output_data_format,
        output_dest_acc_data_format=output_dest_acc_data_format
    )
    
    non_int_all_data_format_constraints = non_int_operand_data_format_constraints + dest_acc_data_format_constraints
    
    # Add data format constraints for specific ops
    if perf_config.op_type == PerfTestType.Tilizer:
        for input_data_format in input_data_formats:
            # Tilizer op should support non-bfp input formats, i.e. float16 and float16_b
            solver.add(Or(input_data_format == DataFormat.Float16.value, input_data_format == DataFormat.Float16_b.value))
        solver.add(output_data_format != DataFormat.RawUInt32.value)
        solver.add(And(non_int_all_data_format_constraints))
        
    # These general ops can have integer inputs, apply constraints accordingly
    elif perf_config.op_type in [
        PerfTestType.Binary,
        PerfTestType.Matmul,
        PerfTestType.MatmulBias,
        PerfTestType.MatmulSparse,
        PerfTestType.Unary
    ]:
        # sparse encoding must be rawuint8/16/32
        if perf_config.op_type == PerfTestType.MatmulSparse:
            solver.add(input_data_formats[2] == DataFormat.RawUInt32.value)
            
        int_constraints = []
        
        # If Int8 or Int32 is used, fidelity must be Hifi4
        int_constraints.append(math_fidelity == MathFidelity.HiFi4.value)
        
        # If Int8 or Int32 format is used, interm data formats in op must be Int8 or Int32
        int_constraints.append(Or(intermed_data_format == DataFormat.Int8.value, intermed_data_format == DataFormat.Int32.value))
        
        int_constraints += dest_acc_data_format_constraints
        
        # If Int8 or Int32 format is used for any one of the input operands, dest accumulate data format must be int32 for target op
        int_constraints.append(output_dest_acc_data_format == DataFormat.Int32.value)
        
        int_constraints += get_int_operand_data_format_constraints_for_general_ops(
            input_data_formats=input_data_formats,
            output_data_format=output_data_format,
            perf_config=perf_config,
            binary_type=binary_type,
        )
        
        if unary_type is None:
            solver.add(
                If(
                    Or(
                        Or([input_df == DataFormat.Int8.value for input_df in input_data_formats]),
                        Or([input_df == DataFormat.Int32.value for input_df in input_data_formats])
                    ),
                    And(int_constraints),
                    And(non_int_all_data_format_constraints)
                )
            )
        else:
            assert perf_config.op_type == PerfTestType.Unary
            solver.add(
                If(
                    And(
                        unary_type == UnaryType.nop.value,
                        Or(
                            Or([input_df == DataFormat.Int8.value for input_df in input_data_formats]),
                            Or([input_df == DataFormat.Int32.value for input_df in input_data_formats])
                        )
                    ),
                    And(int_constraints),
                    And(non_int_all_data_format_constraints)
                )
            )

    elif perf_config.op_type in [
        PerfTestType.MatmulQuant,
        PerfTestType.MatmulBiasQuant
    ]:
        assert all(var is not None for var in (l1_acc, dequant, requant))
        
        # If Int8 or Int32 is used, fidelity must be Hifi4
        solver.add(math_fidelity == MathFidelity.HiFi4.value)
        
        # If Int8 or Int32 format is used, interm data formats in op must be Int8 or Int32
        solver.add(Or(intermed_data_format == DataFormat.Int8.value, intermed_data_format == DataFormat.Int32.value))
        
        solver.add(And(dest_acc_data_format_constraints))
        
        # Matmul with int inputs must have input 0, 1 in Int8 format.
        solver.add(And(input_data_formats[0] == DataFormat.Int8.value, input_data_formats[1] == DataFormat.Int8.value))
        
        if perf_config.op_type == PerfTestType.MatmulQuant:
            # scalar input must be fp32 for matmul with requant/dequant
            solver.add(input_data_formats[2] == DataFormat.Float32.value)
            
        elif perf_config.op_type == PerfTestType.MatmulBiasQuant:
            # bias has to be int32
            solver.add(input_data_formats[2] == DataFormat.Int32.value)      
            # scalar input must be fp32 for matmul with requant/dequant
            solver.add(input_data_formats[3] == DataFormat.Float32.value)
        
        solver.add(
            If(
                dequant == 1,
                # If dequant is enabled, output format cannot be integer
                And(output_data_format != DataFormat.Int8.value, output_data_format != DataFormat.Int32.value),
                # out data format must be Int8 or Int32 if dequant is not enabled
                Or(output_data_format == DataFormat.Int8.value, output_data_format == DataFormat.Int32.value)
            )
        )

    elif perf_config.op_type == PerfTestType.Quant:
        assert quant_type is not None
        
        # # Perf sweep constraint: constrain intermed data format of quantization op to int32
        # solver.add(intermed_data_format == DataFormat.Int32.value)
        
        ### Quantization constraints ###
        quant_constraints = []
        # quantization op must have all inputs data format Float32
        quant_constraints.append(And(input_data_formats[0] == DataFormat.Float32.value, input_data_formats[1] == DataFormat.Float32.value))
        
        # quantization op must have output data format Int8
        quant_constraints.append(output_data_format == DataFormat.Int8.value)
        
        quant_constraints.append(And(dest_acc_data_format_constraints))
        
        
        ### Dequant and Requant constraints ###
        dequant_constraints = []
        requant_constraints = []
        # If Int8 or Int32 is used, fidelity must be Hifi4
        dequant_constraints.append(math_fidelity == MathFidelity.HiFi4.value)
        
        # (de/re)quantization op must have input[0] data format Int32
        dequant_constraints.append(Or(input_data_formats[0] == DataFormat.Int8.value, input_data_formats[0] == DataFormat.Int32.value))

        # (de/re)quantization op must have input[1] data format Float32
        dequant_constraints.append(input_data_formats[1] == DataFormat.Float32.value)
        
        dequant_constraints.append(And(dest_acc_data_format_constraints))
        
        requant_constraints = dequant_constraints.copy()
        
        # dequantization op must have output data format Float32
        dequant_constraints.append(output_data_format == DataFormat.Float32.value)
        
        # requantization op must have output data format Int8
        requant_constraints.append(output_data_format == DataFormat.Int8.value)

        solver.add(
            Implies(
                quant_type == QuantType.quantization.value,
                And(quant_constraints)
            )
        )
        solver.add(
            Implies(
                quant_type == QuantType.dequantization.value,
                And(dequant_constraints)
            )
        )
        solver.add(
            Implies(
                quant_type == QuantType.requantization.value,
                And(requant_constraints)
            )
        )
    
    else:
        solver.add(And(non_int_all_data_format_constraints))
        
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
    
    if perf_config.op_type in [PerfTestType.MatmulBias, PerfTestType.MatmulBiasQuant]:
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
    # TODO: should we limit this for matmul quant?
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
        # square op grid in non-single op
        if not perf_config.perf_test_mode == PerfTestMode.single_op:
            solver.add(feeders_grid_size[0][0] == feeders_grid_size[0][1])
        # grid shape of all feeders should match target op grid shape
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
        perf_config.perf_test_mode != PerfTestMode.single_op and
        perf_config.reblocking_mode != ReblockTM.Hstack and
        perf_config.op_type != PerfTestType.ReduceMax and
        perf_config.op_type != PerfTestType.Tilizer
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
        # # Only run HiFi for binary mult
        # solver.add(
        #     If (
        #         svars["binary_type"] != BinaryType.multiply.value,
        #         svars["math_fidelity"] == 1,
        #         True,
        #     )
        # )
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

    elif perf_config.op_type in [PerfTestType.MatmulQuant, PerfTestType.MatmulBiasQuant]:
        solver.add(Or(svars["requant"] == 0, svars["requant"] == 1))
        solver.add(Or(svars["dequant"] == 0, svars["dequant"] == 1))
        # only one of requant/dequant can be enabled at a time
        solver.add(svars["requant"] + svars["dequant"] == 1)
        
        # If dequant is enabled, output data format will not be int
        # Thus we need a unique variable that defines the drainer dest accumulate df (cannot be int32 like output_dest_acc_df)
        output_df = svars[VAR("df", -1)]
        drainer_acc_df = svars["drainer_dest_acc_df"]
        solver.add(
            Implies(
                Or([output_df == data_format for data_format in data_formats_a]),
                drainer_acc_df == DataFormat.Float16.value
            )
        )
        solver.add(
            Implies(
                Or([output_df == data_format for data_format in data_formats_b]),
                drainer_acc_df == DataFormat.Float16_b.value
            )
        )
        solver.add(
            Implies(
                Or([output_df == data_format for data_format in data_formats_int]),
                drainer_acc_df == DataFormat.Int32.value
            )
        )
    elif perf_config.op_type == PerfTestType.Quant:
        all_quant_type_vals = [e.value for e in QuantType]
        solver.add(svars['quant_type'] <= max(all_quant_type_vals))
        solver.add(svars['quant_type'] >= min(all_quant_type_vals))
        
        output_df = svars[VAR("df", -1)]
        drainer_acc_df = svars["drainer_dest_acc_df"]
        solver.add(
            Implies(
                Or([output_df == data_format for data_format in data_formats_a]),
                drainer_acc_df == DataFormat.Float16.value
            )
        )
        solver.add(
            Implies(
                Or([output_df == data_format for data_format in data_formats_b]),
                drainer_acc_df == DataFormat.Float16_b.value
            )
        )
        solver.add(
            Implies(
                Or([output_df == data_format for data_format in data_formats_int]),
                drainer_acc_df == DataFormat.Int32.value
            )
        )
        
    if is_matmul_op(perf_config.op_type):
        solver.add(Or(svars['l1_acc'] == 0, svars['l1_acc'] == 1))
        # L1 accumulation is only a feature of wormhole b0 matmul, not supported on other devices
        solver.add(
            Implies(
                svars['l1_acc'] == 1,
                svars["ARCH"] == ARCH.WH_B0.value
            )
        )
        
        # Matmul with int inputs can't use l1 accumulation.
        input_data_formats=[svars[VAR("df", i)] for i in range(get_num_input_operands(perf_config.op_type))]
        Implies(
            Or(
                Or([input_df == DataFormat.Int8.value for input_df in input_data_formats]),
                Or([input_df == DataFormat.Int32.value for input_df in input_data_formats])
            ),
            svars['l1_acc'] == 0
        )
        
        
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
