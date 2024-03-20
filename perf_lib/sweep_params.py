# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

from enum import Enum

from perf_test_base import OpType, ReblockTMPerfLib, is_binary_op
from logger_utils import logger

#######################################################
# Default values for each variable in perf-sweep
#######################################################
perf_sweep_base_vars_default = {
    "input_count": [128],       # Maximum: 512 -- verif/template_netlist/test_modules/feeder_drainer_utils.py:max_input_count
    "math_fidelity": [1, 4],    # List of MathFidelity's: verif/template_netlist/test_modules/constraint_utils.py:MathFidelity
    "target_device": [0],       # Is hardcoded to 0 in the constraints
    "ARCH": [1, 2, 3],          # 1: GS, 2: WH-A0, 3: WH-B0
}
perf_sweep_operand0_vars_default = {
    'input0_t': [1],
    'input0_grid_size_r': [1],
    'input0_grid_size_c': [1],
    'input0_mb_r': [],          # Default: Derived from output mb
    'input0_mb_c': [],          # Default: Derived from output mb
    'input0_ub_r': [],          # Default: Derived from output ub
    'input0_ub_c': [],          # Default: Derived from output ub
    'input0_buf_size_mb': [],   # Default: 2*t
    'input0_ublock_order': [],  # 0: r, 1: c
    'input0_df': [2],
    'input0_dest_acc_df': [],   # Constrained to be one of fp16, fp16_b, Int32, based on format of input/output
}
perf_sweep_operand1_vars_default = {
    'input1_t': [1],
    'input1_grid_size_r': [1],
    'input1_grid_size_c': [1],
    'input1_mb_r': [],          # Default: Derived from output mb
    'input1_mb_c': [],          # Default: Derived from output mb
    'input1_ub_r': [],          # Default: Derived from output ub
    'input1_ub_c': [],          # Default: Derived from output ub
    'input1_buf_size_mb': [],   # Default: 2*t
    'input1_ublock_order': [],  # 0: r, 1: c
    'input1_df': [2],
    'input1_dest_acc_df': [],   # Constrained to be one of fp16, fp16_b, Int32, based on format of input/output
}

perf_sweep_output_vars_default = {
    'output_t': [1],
    'output_grid_size_r': [1],
    'output_grid_size_c': [1],
    'output_mb_r': [2],
    'output_mb_c': [2],
    'output_ub_r': [2],         # Maximum: 8 -- verif/template_netlist/test_modules/feeder_drainer_utils.py:max_mm_ub_dim
    'output_ub_c': [4],         # Maximum: 8 -- verif/template_netlist/test_modules/feeder_drainer_utils.py:max_mm_ub_dim
    'output_buf_size_mb': [2],  # Default: 2
    'output_ublock_order': [],  # 0: r, 1: c
    'output_df': [2],
    "intermed_df": [2],         # This will get overriden to output data format unless l1 acc or int input
    'output_dest_acc_df': [],   # Constrained to be one of fp16, fp16_b, Int32, based on format of input/output
}

perf_sweep_vars_for_op = {
    OpType.Matmul: {
        'mb_inner': [1, 2, 4, 8],
        'ub_inner': [2, 4, 8, 16],
        'l1_acc': [0],
    },
    OpType.MatmulBias: {
        'mb_inner': [1, 2, 4, 8],
        'ub_inner': [2, 4, 8, 16],

        'input2_t': [1],
        'input2_grid_size_r': [1],
        'input2_grid_size_c': [1, 2, 3, 4],
        'input2_mb_r': [1],
        'input2_mb_c': [],
        'input2_ub_r': [1],
        'input2_ub_c': [],
        'input2_buf_size_mb': [],
        'input2_ublock_order': [], # 0: r, 1: c
        'input2_df': [2],
        
        'l1_acc': [0],
    },
    OpType.MatmulQuant: {
        'mb_inner': [1, 2, 4, 8],
        'ub_inner': [2, 4, 8, 16],
        "math_fidelity": [4], 
        'input0_df': [11],
        'input1_df': [11],
        'intermed_df': [10],
        'output_df': [2, 10, 11],
        'l1_acc': [0],
        
        'input2_t': [1],
        'input2_grid_size_r': [],
        'input2_grid_size_c': [],
        'input2_mb_r': [],
        'input2_mb_c': [],
        'input2_ub_r': [],
        'input2_ub_c': [],
        'input2_buf_size_mb': [],
        'input2_ublock_order': [], # 0: r, 1: c
        'input2_df': [12],
        'input2_dest_acc_df': [],
        
        'output_ub_r': [1],        
        'output_ub_c': [1],     
            
        'drainer_dest_acc_df': [], # will be constrained and determined by output_df
        'requant': [0, 1],
        'dequant': [0, 1],
        'zero_point': [1.0],
    },
    OpType.MatmulBiasQuant: {
        'mb_inner': [1, 2, 4, 8],
        'ub_inner': [2, 4, 8, 16],
        "math_fidelity": [4], 
        'input0_df': [11],
        'input1_df': [11],
        'intermed_df': [10],
        'output_df': [2, 10, 11],
        'l1_acc': [0],
        
        
        'input2_t': [1],
        'input2_grid_size_r': [1],
        'input2_grid_size_c': [1, 2, 3, 4],
        'input2_mb_r': [1],
        'input2_mb_c': [],
        'input2_ub_r': [1],
        'input2_ub_c': [],
        'input2_buf_size_mb': [],
        'input2_ublock_order': [], # 0: r, 1: c
        'input2_df': [],
        'input2_dest_acc_df': [],
        
        'input3_t': [1],
        'input3_grid_size_r': [],
        'input3_grid_size_c': [],
        'input3_mb_r': [],
        'input3_mb_c': [],
        'input3_ub_r': [],
        'input3_ub_c': [],
        'input3_buf_size_mb': [],
        'input3_ublock_order': [], # 0: r, 1: c
        'input3_df': [],
        'input3_dest_acc_df': [],
        
        'output_ub_r': [1],        
        'output_ub_c': [1],  
        
        'drainer_dest_acc_df': [], # will be constrained and determined by output_df
        'requant': [0, 1],
        'dequant': [0, 1],
        'zero_point': [1.0],
    },
    OpType.MatmulSparse: {
        # Override data format to match exponent format
        # test_perf.py:extra_config_callback
        'mb_inner': [1, 2, 4, 8],
        'ub_inner': [4, 8, 16],

        'input0_t': [1],
        'input0_grid_size_r': [1, 2, 3, 4],
        'input0_grid_size_c': [1, 2, 3, 4],
        'input0_mb_r': [1],
        'input0_mb_c': [64],
        'input0_ub_r': [1],
        'input0_ub_c': [1],
        'input0_buf_size_mb': [],
        'input0_ublock_order': [], # 0: r, 1: c
        'input0_df': [2],
        
        'input1_df': [2],

        'input2_t': [1],
        'input2_grid_size_r': [1, 2, 3, 4],
        'input2_grid_size_c': [1, 2, 3, 4],
        'input2_mb_r': [1],
        'input2_mb_c': [8],
        'input2_ub_r': [1],
        'input2_ub_c': [1],
        'input2_buf_size_mb': [],
        'input2_ublock_order': [], # 0: r, 1: c
        'input2_df':[],
        
        'output_df': [2],

        'zero_strip_freq': [0],
        'zero_ublock_freq': [25, 50, 75],
        'zero_tile_freq': [25, 50, 75],

        'num_index_tiles': [],
        'sparse_num_tiles': [],
        
        'l1_acc': [0],
    },
    OpType.MatmulSparseNzCounts: {
        "math_fidelity": [1, 2, 3, 4],
        'mb_inner': [2, 7, 16],
        'ub_inner': [1, 2, 4],

        'input0_t': [1],
        'input0_grid_size_r': [1],
        'input0_grid_size_c': [1],
        'input0_mb_r': [1],
        'input0_mb_c': [20],
        'input0_ub_r': [1],
        'input0_ub_c': [1],
        'input0_buf_size_mb': [],
        'input0_ublock_order': [], # 0: r, 1: c

        'input1_t': [1],
        
        'input2_t': [1],
        'input2_grid_size_r': [1],
        'input2_grid_size_c': [1],
        'input2_mb_r': [1],
        'input2_mb_c': [3],
        'input2_ub_r': [1],
        'input2_ub_c': [1],
        'input2_buf_size_mb': [],
        'input2_ublock_order': [], # 0: r, 1: c
        'input2_df': [2],
        'input2_dest_acc_df': [],
        
        'output_t': list(range(1, 4)),
        'output_mb_r': [2, 4, 7],
        'output_mb_c': [1, 4],
        'output_ub_r': [2, 4],
        'output_ub_c': [1, 2],
        'output_df': [2],

        'num_index_tiles': [],
        'sparse_num_tiles': [],
    },
    OpType.Unary: {
        'unary_type': [i for i in range(1, 17)], # using the verif.template_netlist.test_modules.constraint_utils.UnaryType
        'approximate_mode': [0]
    },
    OpType.Binary: {
        'binary_type': [i for i in range(1, 5)] # using the verif.template_netlist.test_modules.constraint_utils.BinaryType
    },
    OpType.Dram: {
        'output_grid_size_r': [1],
        'output_grid_size_c': [1],
        'output_mb_r': [2, 4],
        'output_mb_c': [2, 4],
    },
    OpType.Fused0: {

        'input2_t': [],
        'input2_grid_size_r': [],
        'input2_grid_size_c': [],
        'input2_mb_r': [],
        'input2_mb_c': [],
        'input2_ub_r': [],
        'input2_ub_c': [],
        'input2_buf_size_mb': [],
        'input2_ublock_order': [], # 0: r, 1: c
        'input2_df': [2],
        'input2_dest_acc_df': [],
    },
    OpType.Fused2: {
        'input2_t': [],
        'input2_grid_size_r': [],
        'input2_grid_size_c': [],
        'input2_mb_r': [],
        'input2_mb_c': [],
        'input2_ub_r': [],
        'input2_ub_c': [],
        'input2_buf_size_mb': [],
        'input2_ublock_order': [], # 0: r, 1: c
        'input2_df': [2],
        'input2_dest_acc_df': [],
    
        'input3_t': [],
        'input3_grid_size_r': [],
        'input3_grid_size_c': [],
        'input3_mb_r': [],
        'input3_mb_c': [],
        'input3_ub_r': [],
        'input3_ub_c': [],
        'input3_buf_size_mb': [],
        'input3_ublock_order': [], # 0: r, 1: c
        'input3_df': [2],
        'input3_dest_acc_df': [],

        'input4_t': [],
        'input4_grid_size_r': [],
        'input4_grid_size_c': [],
        'input4_mb_r': [],
        'input4_mb_c': [],
        'input4_ub_r': [],
        'input4_ub_c': [],
        'input4_buf_size_mb': [],
        'input4_ublock_order': [], # 0: r, 1: c
        'input4_df': [2],
        'input4_dest_acc_df': [],
    },
    OpType.Pcie: {
        'output_grid_size_r': [1],
        'output_grid_size_c': [1],
        'output_mb_r': [2, 4],
        'output_mb_c': [2, 4],
    },
    OpType.ReduceMax: {
        'mb_inner': [1, 4, 8],
        'ub_inner': [2, 8, 16],
        'reduce_dim': [1, 2, 3],
        'input0_t': [1, 2, 4, 8],
        'output_mb_r': [1, 4],
        'output_mb_c': [1, 4],
        'output_ub_r': [1, 8],         # Maximum: 8 -- verif/template_netlist/test_modules/feeder_drainer_utils.py:max_mm_ub_dim
        'output_ub_c': [1, 8],         # Maximum: 8 -- verif/template_netlist/test_modules/feeder_drainer_utils.py:max_mm_ub_dim
    },
    OpType.Splice: {
        'input0_mb_c': [1, 2, 3, 4],          # Default: Derived from output mb

        'output_mb_r': [1, 2],
        'output_mb_c': [],
        'output_ub_r': [1],         # Maximum: 8 -- verif/template_netlist/test_modules/feeder_drainer_utils.py:max_mm_ub_dim
        'output_ub_c': [1, 4, 8],         # Maximum: 8 -- verif/template_netlist/test_modules/feeder_drainer_utils.py:max_mm_ub_dim

        'input0_index': [],
        'input0_length': [],
        'input0_stride': [],

        'input1_index': [],
        'input1_length': [],
        'input1_stride': [],

        'input2_index': [],
        'input2_length': [],
        'input2_stride': [],

        'input2_t': [],
        'input2_grid_size_r': [],
        'input2_grid_size_c': [],
        'input2_mb_r': [],
        'input2_mb_c': [],
        'input2_ub_r': [],
        'input2_ub_c': [],
        'input2_buf_size_mb': [],
        'input2_ublock_order': [], # 0: r, 1: c
        'input2_df': [2],
        'input2_dest_acc_df': [],
    },
    OpType.Tilizer: {
        'parallelization_factor_r': [1, 2],
        'parallelization_factor_c': [1, 2],
        'input0_mb_r': [2, 4],
        'input0_mb_c': [1, 2],
        'input0_ub_r': [2, 4],
        'input0_ub_c': [4, 8],
        'output_mb_r': [2, 4],
        'output_mb_c': [2, 4],
        'output_ub_r': [1, 2],         # Maximum: 8 -- verif/template_netlist/test_modules/feeder_drainer_utils.py:max_mm_ub_dim
        'output_ub_c': [4, 8],         # Maximum: 8 -- verif/template_netlist/test_modules/feeder_drainer_utils.py:max_mm_ub_dim
        'input0_grid_size_r': [1, 2, 4],
        'input0_grid_size_c': [1, 2, 4],
        'output_grid_size_r': [1, 2, 4],
        'output_grid_size_c': [1, 2, 4],
        'input0_df': [1],
        'output_df': [1],
    },
    OpType.Quant: {
        'quant_type': [i for i in range(1, 4)],
        
        'input0_df': [],
        'input1_df': [],
        'output_ub_r': [1],         # Maximum: 4
        'output_ub_c': [1],         # Maximum: 4
        
        'intermed_df': [10],
        'output_df': [],
        'output_dest_acc_df': [],
        
        'drainer_dest_acc_df': [],        # will be constrained and determined by output_df
        'zero_point': [1.0],
    },

}

perf_sweep_vars_for_reblocking = {
    ReblockTMPerfLib.Normal: {},
    # Non Normal reblocking modes only apply to unary ops
    ReblockTMPerfLib.Gather: {
        'input0_ub_r': [1],
        'input0_ub_c': [8],
        'input0_mb_r': [1],
        'input0_mb_c': [2],
        'output_mb_r': [],
        'output_mb_c': [],
        'output_ub_r': [1],
        'output_ub_c': [1, 2, 4, 8],
        'unary_type': [8],
        'input0_grid_size_r': [4],
        'input0_grid_size_c': [1],
        'output_grid_size_r': [1],
        'output_grid_size_c': [1],
        "math_fidelity": [1, 4],
        "input0_df": [2, 4],
        "output_df": [2, 4],
        "input0_ublock_order": [0]
    },
    ReblockTMPerfLib.Hstack: {
        'input0_ub_r': [1],
        'input0_ub_c': [1],
        'input0_mb_r': [2],
        'input0_mb_c': [2],
        'output_ub_r': [1],
        'output_ub_c': [1],
        'output_mb_r': [2],
        'output_mb_c': [2],
        'unary_type': [8],
        'input0_grid_size_r': [1],
        'input0_grid_size_c': [1],
        'output_grid_size_r': [1],
        'output_grid_size_c': [1, 2, 4, 8, 12],
        "math_fidelity": [1, 4],
        "input0_df": [2, 4],
        "input0_ublock_order": [0],
        "output_ublock_order": [0],
        "input0_t": [1, 2, 4, 8, 12],
        "output_t": [1],
        "output_df": [2, 4],
    },
    ReblockTMPerfLib.Broadcast_r: {
        "input_count": [512],
        "math_fidelity": [1],
        'unary_type': [8],
        "input0_df": [4],
        'input0_grid_size_r': [1],
        'input0_grid_size_c': [1],
        'input0_mb_r': [1],
        'input0_mb_c': [1, 4, 8],
        'input0_ub_r': [1],
        'input0_ub_c': [1, 4, 8],
        'input0_buf_size_mb': [1, 2, 4, 8, 16],
        'output_grid_size_r': [1],
        'output_grid_size_c': [1],
        'output_mb_r': [3],
        'output_mb_c': [1, 4, 8],
        'output_ub_r': [1],
        'output_ub_c': [1, 4, 8],
        'output_buf_size_mb': [2],
        'output_df': [4],
    },
}

#######################################################
# Default values for each variable in op-perf-testing on ci
#######################################################
op_test_base_vars_default = {
    "input_count": [128],
    "math_fidelity": [1],
    "target_device": [0], # Is hardcoded to 0 in the constraints
}
op_test_operand0_vars_default = {
    'input0_t': [1],
    'input0_grid_size_r': [1],
    'input0_grid_size_c': [1],
    'input0_df': [4],
    'input0_dest_acc_df': [],
}
op_test_operand1_vars_default = {
    'input1_t': [1],
    'input1_grid_size_r': [1],
    'input1_grid_size_c': [1],
    'input1_df': [4],
    'input1_dest_acc_df': [],
}

op_test_output_vars_default = {
    'output_t': [1],
    'output_mb_r': [2],
    'output_mb_c': [2],
    'output_ub_r': [2],
    'output_ub_c': [4],
    'output_grid_size_r': [1],
    'output_grid_size_c': [1],
    'output_df': [4],
    'output_dest_acc_df': [],
    "intermed_df": [4], # This will get overriden to output dataformat in extra config callback unless l1 acc
    'output_buf_size_mb': [2],  # Default: 2
}

op_test_vars_for_op = {
    OpType.Matmul: {
        'mb_inner': [1],
        'ub_inner': [2],
        'l1_acc': [0],
    },
    OpType.MatmulBias: {
        'mb_inner': [1],
        'ub_inner': [2],
        'input2_df': [4],
        'input2_dest_acc_df': [],
        'l1_acc': [0],
    },
    OpType.Unary: {
        'unary_type': [1]
    },
    OpType.Binary: {
        'binary_type': [1]
    },
    OpType.Dram: {
        'output_grid_size_r': [1],
        'output_grid_size_c': [1],
        'output_mb_r': [2, 4],
        'output_mb_c': [2, 4],
    },
    OpType.Fused0: {
        'input2_df': [4],
        'input2_dest_acc_df': [],
    },
    OpType.Fused2: {
        'input2_df': [4],
        'input2_dest_acc_df': [],
        'input3_df': [4],
        'input3_dest_acc_df': [],
        'input4_df': [4],
        'input4_dest_acc_df': [],
    },
    OpType.Pcie: {
        'output_grid_size_r': [1],
        'output_grid_size_c': [1],
        'output_mb_r': [2, 4],
        'output_mb_c': [2, 4],
        "input0_df": [2],
        "output_df": [2],
    },
}

op_test_vars_for_reblocking = {
    ReblockTMPerfLib.Normal: {},
    ReblockTMPerfLib.Gather: {
        'input0_ub_r': [1],
        'input0_ub_c': [8],
        'input0_mb_r': [1],
        'input0_mb_c': [2],
        'output_ub_r': [1],
        'output_ub_c': [1, 2, 4, 8],
        'unary_type': [8],
        'input0_grid_size_r': [4],
        'input0_grid_size_c': [1],
        'output_grid_size_r': [1],
        'output_grid_size_c': [1],
    },
}

def get_perf_sweep_vars_for_op(op_type: OpType, reblocking_mode: ReblockTMPerfLib):

    assert op_type in perf_sweep_vars_for_op
    perf_sweep_vars = perf_sweep_base_vars_default.copy()
    perf_sweep_vars.update(perf_sweep_operand0_vars_default)
    if is_binary_op(op_type):
        perf_sweep_vars.update(perf_sweep_operand1_vars_default)
    perf_sweep_vars.update(perf_sweep_output_vars_default)
    perf_sweep_vars.update(perf_sweep_vars_for_op[op_type])
    perf_sweep_vars.update(perf_sweep_vars_for_reblocking[reblocking_mode])
    logger.info(f"performance sweep vars: {perf_sweep_vars}")
    return perf_sweep_vars

def modify_op_test_sweep_vars(override_vars, op_type: OpType):
    if 'grid_size' in override_vars:
        grid_size = override_vars['grid_size']
        override_vars.update(
            {
                'input0_grid_size_r': grid_size,
                'input0_grid_size_c': grid_size,
                'output_grid_size_r': grid_size,
                'output_grid_size_c': grid_size,
            }
        )
        if is_binary_op(op_type):
            override_vars.update(
                {
                    'input1_grid_size_r': grid_size,
                    'input1_grid_size_c': grid_size,
                }
            )
        del override_vars['grid_size']

def get_op_test_sweep_vars(op_type: OpType, reblocking_mode: ReblockTMPerfLib, sweep_override_vars):

    assert op_type in perf_sweep_vars_for_op
    op_test_vars = op_test_base_vars_default.copy()
    op_test_vars.update(op_test_operand0_vars_default)
    if is_binary_op(op_type):
        op_test_vars.update(op_test_operand1_vars_default)
    op_test_vars.update(op_test_output_vars_default)
    op_test_vars.update(op_test_vars_for_op[op_type])
    modify_op_test_sweep_vars(sweep_override_vars, op_type)
    op_test_vars.update(sweep_override_vars)
    op_test_vars.update(op_test_vars_for_reblocking[reblocking_mode])
    logger.info(f"op_test_vars: {op_test_vars}")
    return op_test_vars
