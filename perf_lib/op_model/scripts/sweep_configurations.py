UNARY_CONFIGURATIONS = [
    {
        "op_type": "unary",
        "op_name": "nop",
        "data_file": "nop-op-performance.csv",
        "config_overrides": {
            "unary_type": [8],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "nop_int8",
        "data_file": "nop-int8-op-performance.csv",
        "config_overrides": {
            "unary_type": [8],
            "vector_mode": [1],
            "input0_df": [11],
            "output_df": [11],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "nop_int32",
        "data_file": "nop-int32-op-performance.csv",
        "config_overrides": {
            "unary_type": [8],
            "vector_mode": [1],
            "input0_df": [10],
            "output_df": [10],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "abs",
        "data_file": "abs-op-performance.csv",
        "config_overrides": {
            "unary_type": [15],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "cosine",
        "data_file": "cosine-op-performance.csv",
        "config_overrides": {
            "unary_type": [13],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "dropout",
        "data_file": "dropout-op-performance.csv",
        "config_overrides": {
            "unary_type": [16],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "exp",
        "data_file": "exp-op-performance.csv",
        "config_overrides": {
            "unary_type": [1],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "exp_approx",
        "data_file": "exp-approx-op-performance.csv",
        "config_overrides": {
            "unary_type": [1],
            "approximate_mode": [1],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "gelu_derivative",
        "data_file": "gelu-derivative-op-performance.csv",
        "config_overrides": {
            "unary_type": [7],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "gelu_derivative_approx",
        "data_file": "gelu-derivative-approx-op-performance.csv",
        "config_overrides": {
            "unary_type": [7],
            "approximate_mode": [1],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "gelu",
        "data_file": "gelu-op-performance.csv",
        "config_overrides": {
            "unary_type": [4],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "log",
        "data_file": "log-op-performance.csv",
        "config_overrides": {
            "unary_type": [2],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "lrelu",
        "data_file": "lrelu-op-performance.csv",
        "config_overrides": {
            "unary_type": [14],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "power",
        "data_file": "power-op-performance.csv",
        "config_overrides": {
            "unary_type": [11],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "reciprocal",
        "data_file": "reciprocal-op-performance.csv",
        "config_overrides": {
            "unary_type": [5],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "reciprocal_approx",
        "data_file": "reciprocal-approx-op-performance.csv",
        "config_overrides": {
            "unary_type": [5],
            "approximate_mode": [1],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "sigmoid",
        "data_file": "sigmoid-op-performance.csv",
        "config_overrides": {
            "unary_type": [6],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "sine",
        "data_file": "sine-op-performance.csv",
        "config_overrides": {
            "unary_type": [12],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "sqrt",
        "data_file": "sqrt-op-performance.csv",
        "config_overrides": {
            "unary_type": [3],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "square",
        "data_file": "square-op-performance.csv",
        "config_overrides": {
            "unary_type": [10],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "unary",
        "op_name": "tanh",
        "data_file": "tanh-op-performance.csv",
        "config_overrides": {
            "unary_type": [9],
            "math_fidelity": [1, 4],
            "input0_df": [2, 4],
            "output_df": [2, 4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
]

BINARY_CONFIGURATIONS = [
    {
        "op_type" : "binary",
        "op_name" : "add",
        "data_file" : "add-op-performance.csv",
        "config_overrides" : {
            "binary_type": [1],
            "output_mb_r" :[2,4,8],
            "output_mb_c" :[2,4,8],
            "output_ub_r": [1,2,4,8],
            "output_ub_c" :[1,2,4,8]
        }
    },
    {
        "op_type": "binary",
        "op_name": "add_int8",
        "data_file": "add-int8-op-performance.csv",
        "config_overrides": {
            "binary_type": [1],
            "input0_df": [11],
            "input1_df": [11],
            "intermed_df": [11],
            "output_df": [11],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "binary",
        "op_name": "add_int32",
        "data_file": "add-int32-op-performance.csv",
        "config_overrides": {
            "binary_type": [1],
            "input0_df": [10],
            "input1_df": [10],
            "intermed_df": [10],
            "output_df": [10],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4],
            "output_ub_c": [1, 2, 4],
        },
    },
    {
        "op_type": "binary",
        "op_name": "subtract",
        "data_file": "subtract-op-performance.csv",
        "config_overrides": {
            "binary_type": [2],
            "output_mb_r": [2, 4, 8],
            "output_mb_c": [2, 4, 8],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "binary",
        "op_name": "subtract_int8",
        "data_file": "subtract-int8-op-performance.csv",
        "config_overrides": {
            "binary_type": [2],
            "input0_df": [11],
            "input1_df": [11],
            "intermed_df": [11],
            "output_df": [11],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "binary",
        "op_name": "multiply",
        "data_file": "multiply-op-performance.csv",
        "config_overrides": {
            "binary_type": [3],
            "math_fidelity": [1],
            "input0_df": [4],
            "input1_df": [4],
            "intermed_df": [4],
            "output_df": [4],
            "output_mb_r": [2, 4, 8],
            "output_mb_c": [2, 4, 8],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "binary",
        "op_name": "multiply_int8",
        "data_file": "multiply-int8-op-performance.csv",
        "config_overrides": {
            "binary_type": [3],
            "input0_df": [11],
            "input1_df": [11],
            "intermed_df": [11],
            "output_df": [11],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "binary",
        "op_name": "maximum",
        "data_file": "maximum-op-performance.csv",
        "config_overrides": {
            "binary_type": [4],
            "input0_df": [4],
            "input1_df": [4],
            "intermed_df": [4],
            "output_df": [4],
            "output_mb_r": [2, 4, 7],
            "output_mb_c": [2, 4, 7],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "binary",
        "op_name": "maximum_int8",
        "data_file": "maximum-int8-op-performance.csv",
        "config_overrides": {
            "binary_type": [4],
            "input0_df": [11],
            "input1_df": [11],
            "intermed_df": [11],
            "output_df": [11],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "binary",
        "op_name": "maximum_int32",
        "data_file": "maximum-int32-op-performance.csv",
        "config_overrides": {
            "binary_type": [4],
            "input0_df": [10],
            "input1_df": [10],
            "intermed_df": [10],
            "output_df": [10],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
]

QUANT_CONFIGURATIONS = [
    {
        "op_type": "quant",
        "op_name": "quantization_int8",
        "data_file": "quantization-int8-op-performance.csv",
        "config_overrides": {
            "quant_type": [1],
            "output_df": [11],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "quant",
        "op_name": "dequantization_int8",
        "data_file": "dequantization-int8-op-performance.csv",
        "config_overrides": {
            "quant_type": [2],
            "input0_df": [11],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "quant",
        "op_name": "dequantization_int32",
        "data_file": "dequantization-int32-op-performance.csv",
        "config_overrides": {
            "quant_type": [2],
            "input0_df": [10],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "quant",
        "op_name": "requantization_int8",
        "data_file": "requantization-int8-op-performance.csv",
        "config_overrides": {
            "quant_type": [3],
            "input0_df": [11],
            "output_df": [11],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
    {
        "op_type": "quant",
        "op_name": "requantization_int32",
        "data_file": "requantization-int32-op-performance.csv",
        "config_overrides": {
            "quant_type": [3],
            "input0_df": [10],
            "output_df": [11],
            "output_mb_r": [2, 4],
            "output_mb_c": [2, 4],
            "output_ub_r": [1, 2, 4, 8],
            "output_ub_c": [1, 2, 4, 8],
        },
    },
]


MATMUL_CONFIGURATIONS = [
    {
        "op_type": "matmul",
        "op_name": "matmul",
        "data_file": "matmul-op-performance.csv",
        "config_overrides": {
            "output_mb_r": [2, 4, 8],
            "output_mb_c": [2, 4, 8],
            "output_ub_r": [2, 4],
            "output_ub_c": [2, 4],
            "ub_inner": [1, 2, 4, 8, 16],
            "mb_inner": [1, 2, 4, 8, 16, 32],
        },
    },
    {
        "op_type": "matmul",
        "op_name": "matmul_int32",
        "data_file": "matmul-int32-op-performance.csv",
        "config_overrides": {
            "input0_df": [11],
            "input1_df": [11],
            "intermed_df": [10],
            "output_df": [10],
            "output_mb_r": [2, 4, 8],
            "output_mb_c": [2, 4, 8],
            "output_ub_r": [2, 4],
            "output_ub_c": [2, 4],
            "ub_inner": [1, 2, 4, 8, 16],
            "mb_inner": [1, 2, 4, 8, 16, 32],
        },
    },
    {
        "op_type": "matmul_l1_acc",
        "op_name": "matmul_l1_acc",
        "data_file": "matmul-l1-acc-op-performance.csv",
        "config_overrides": {
            "l1_acc": [1],
            "output_mb_r": [2, 4, 8],
            "output_mb_c": [2, 4, 8],
            "output_ub_r": [2, 4],
            "output_ub_c": [2, 4],
            "ub_inner": [1, 2, 4, 8, 16],
            "mb_inner": [1, 2, 4, 8, 16, 32],
        },
    },
    {
        "op_type": "matmul_sparse_nz_counts",
        "op_name": "matmul_sparse_nz_counts",
        "data_file": "matmul-sparse-op-performance.csv",
        "additional_arguments": {"default": " --verbose-report --run-single-op"},
    },
]

OTHER_CONFIGURATIONS = [
    {
        "op_type": "splice",
        "op_name": "splice",
        "data_file": "splice-op-performance.csv",
        "additional_arguments": {"default": "  --run-prologue "},
    },
]

DEFAULT_CONFIGURATIONS = (
    UNARY_CONFIGURATIONS
    + BINARY_CONFIGURATIONS
    + QUANT_CONFIGURATIONS
    + MATMUL_CONFIGURATIONS
    + OTHER_CONFIGURATIONS
)
