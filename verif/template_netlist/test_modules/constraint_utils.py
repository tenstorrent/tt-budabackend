# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from cmath import e
import itertools
from z3 import *
from util import *
from enum import Enum
from typing import Dict, List, Optional, Tuple
import math

FP32_TILE_SIZE = 32 * 32 * 4 + 32
FP16_TILE_SIZE = 32 * 32 * 2 + 32
BFP8_TILE_SIZE = 32 * 32 * 1 + 32 + 4*16
BFP4_TILE_SIZE = 32 * 16     + 32 + 4*16
BFP2_TILE_SIZE = 32 * 8      + 32 + 4*16
# dramallocation.py
INT8_TILE_SIZE = 32 * 32 * 1 + 32

GRAYSKULL_GRID_R = 10
GRAYSKULL_GRID_C = 12
L1_MAX_USAGE_BYTES = 2**19 # 512kB per core

class ARCH(Enum):
    GS = 1
    WH_A0 = 2
    WH_B0 = 3
    BH = 4

class BinaryType(Enum):
    add = 1
    subtract = 2
    multiply = 3
    maximum  = 4

class UnaryType(Enum):
    exp = 1
    log = 2
    sqrt = 3
    gelu = 4
    reciprocal = 5
    sigmoid = 6
    gelu_derivative = 7
    nop = 8
    tanh = 9
    square = 10
    power = 11
    sine = 12
    cosine = 13
    lrelu = 14
    abs = 15
    dropout = 16

class QuantType(Enum):
    quantization = 1
    dequantization = 2
    requantization = 3

class TMType(Enum):
    no_tms = 1

    hslice = 2
    vslice = 3
    hstack = 4
    vstack = 5
    broadcast_r = 6
    broadcast_c = 7
    broadcast_z = 8

class DataFormat(Enum):
    Float16     = 1
    Float16_b   = 2
    Bfp8        = 3
    Bfp8_b      = 4
    Bfp4        = 5
    Bfp4_b      = 6
    Bfp2        = 7
    Bfp2_b      = 8
    RawUInt32   = 9
    Int32       = 10
    Int8        = 11
    Float32     = 12


class MathFidelity(Enum):
    LoFi    = 1
    HiFi2   = 2
    HiFi3   = 3
    HiFi4   = 4

class UblockOrder(Enum):
    r = 0
    c = 1

class ReduceDim(Enum):
    r   = 1
    c   = 2
    z   = 3

class SfpuVectorMode(Enum):
    rc   = 1
    r    = 2
    c    = 3

def get_data_format_tile_size_map(arch):
    if arch == "blackhole":
        return {
            DataFormat.Float32.value: math.ceil(FP32_TILE_SIZE / 64) * 64,
            DataFormat.Int32.value: math.ceil(FP32_TILE_SIZE / 64) * 64,
            DataFormat.RawUInt32.value: math.ceil(FP32_TILE_SIZE / 64) * 64,
            DataFormat.Float16.value: math.ceil(FP16_TILE_SIZE / 64) * 64,
            DataFormat.Float16_b.value: math.ceil(FP16_TILE_SIZE / 64) * 64,
            DataFormat.Bfp8.value: math.ceil(BFP8_TILE_SIZE / 64) * 64,
            DataFormat.Bfp8_b.value: math.ceil(BFP8_TILE_SIZE / 64) * 64,
            DataFormat.Bfp4.value: math.ceil(BFP4_TILE_SIZE / 64) * 64,
            DataFormat.Bfp4_b.value: math.ceil(BFP4_TILE_SIZE / 64) * 64,
            DataFormat.Bfp2.value: math.ceil(BFP2_TILE_SIZE / 64) * 64,
            DataFormat.Bfp2_b.value: math.ceil(BFP2_TILE_SIZE / 64) * 64,
            DataFormat.Int8.value: math.ceil(INT8_TILE_SIZE / 64) * 64,
        }
    else:
        return {
            DataFormat.Float32.value: FP32_TILE_SIZE,
            DataFormat.Int32.value: FP32_TILE_SIZE,
            DataFormat.RawUInt32.value: FP32_TILE_SIZE,
            DataFormat.Float16.value: FP16_TILE_SIZE,
            DataFormat.Float16_b.value: FP16_TILE_SIZE,
            DataFormat.Bfp8.value: BFP8_TILE_SIZE,
            DataFormat.Bfp8_b.value: BFP8_TILE_SIZE,
            DataFormat.Bfp4.value: BFP4_TILE_SIZE,
            DataFormat.Bfp4_b.value: BFP4_TILE_SIZE,
            DataFormat.Bfp2.value: BFP2_TILE_SIZE,
            DataFormat.Bfp2_b.value: BFP2_TILE_SIZE,
            DataFormat.Int8.value: INT8_TILE_SIZE
        }
    
data_format_tile_size_map = {
    DataFormat.Float32.value: FP32_TILE_SIZE,
    DataFormat.Int32.value: FP32_TILE_SIZE,
    DataFormat.RawUInt32.value: FP32_TILE_SIZE,
    DataFormat.Float16.value: FP16_TILE_SIZE,
    DataFormat.Float16_b.value: FP16_TILE_SIZE,
    DataFormat.Bfp8.value: BFP8_TILE_SIZE,
    DataFormat.Bfp8_b.value: BFP8_TILE_SIZE,
    DataFormat.Bfp4.value: BFP4_TILE_SIZE,
    DataFormat.Bfp4_b.value: BFP4_TILE_SIZE,
    DataFormat.Bfp2.value: BFP2_TILE_SIZE,
    DataFormat.Bfp2_b.value: BFP2_TILE_SIZE,
    DataFormat.Int8.value: INT8_TILE_SIZE,
} 

data_formats_a = [
    DataFormat.Float32.value,
    DataFormat.Float16.value, 
    DataFormat.Bfp8.value,
    DataFormat.Bfp4.value,
    DataFormat.Bfp2.value,
]

data_formats_b = [
    DataFormat.Float16_b.value, 
    DataFormat.Bfp8_b.value,
    DataFormat.Bfp4_b.value,
    DataFormat.Bfp2_b.value   
]

data_formats_int = [
    DataFormat.Int32.value, 
    DataFormat.Int8.value,
]

def divisible_either_direction(a: z3.Var, b: z3.Var) -> z3.Var:
    return Or(a % b == 0, b % a == 0)

def constrain_max_min(*, solver, var, limits):
    solver.add(var >= limits[0])
    solver.add(var <= limits[1])

def constrain_ublock_size(*, solver, ub_r_var, ub_c_var, max_size):
    solver.add(ub_r_var > 0)
    solver.add(ub_c_var > 0)
    solver.add(ub_r_var * ub_c_var <= max_size)

def constrain_blocking(*, solver, tensor_dim_var, grid_dim_var, mb_dim_var, ub_dim_var, ub_limits, grid_limits):
    solver.add(tensor_dim_var == grid_dim_var * mb_dim_var * ub_dim_var)

    solver.add(grid_dim_var >= grid_limits[0])
    solver.add(grid_dim_var <= grid_limits[1])

    solver.add(mb_dim_var > 0)
    solver.add(ub_dim_var > 0)

    solver.add(ub_limits[0] <= ub_dim_var)
    solver.add(ub_dim_var <= ub_limits[1])

def constrain_output_l1(*, solver, max_usage_bytes, tile_size_bytes, mb_r, mb_c, ub_r, ub_c, buf_size_mb=None):
    #solver.add(max_usage_bytes >= tile_size_bytes * mb_r * ub_r * mb_c * ub_c * (buf_size_mb if buf_size_mb is not None else 1))
    solver.add(max_usage_bytes >= tile_size_bytes * mb_r * ub_r * mb_c * ub_c * (buf_size_mb if buf_size_mb is not None else 1))

def constrain_matmul_l1(*, solver, max_usage_bytes, tile_size_bytes, mb_r, mb_c, ub_r, ub_c, ukt, buf_size_mb):
    solver.add(max_usage_bytes >= tile_size_bytes * (ukt * (mb_r * ub_r + mb_c * ub_c) * 2 + mb_r * ub_r * mb_c * ub_c * buf_size_mb))

def constrain_forking_factor(*, solver, producer_grid_r_var, producer_grid_c_var, consumer_grid_r_var, consumer_grid_c_var):
    solver.add(producer_grid_r_var * producer_grid_c_var * 16 >= consumer_grid_r_var * consumer_grid_c_var)

def constrain_buf_size_mb(*, solver, buf_size_mb, input_count, t):
    solver.add(buf_size_mb > 0)
    solver.add(buf_size_mb <= 2 * t)
    solver.add(Or(buf_size_mb % 2 == 0, buf_size_mb == 1))
    solver.add((input_count * t) % buf_size_mb == 0)
    # ensure divisibility between single_buf_size_mb and t in either way
    solver.add(If(buf_size_mb == 1,
                  Or(buf_size_mb % t == 0, t % buf_size_mb == 0),
                  Or((buf_size_mb/2) % t == 0, t % (buf_size_mb/2) == 0)))

def constrain_input_count_and_entries(solver, input_count, *entries):
    solver.add(input_count % 2 == 0)
    solver.add(input_count > 0)
    for entry in entries:
        solver.add(entry > 0)
        solver.add(entry % input_count == 0)

def constrain_tms(*, solver, tm_type, tm_factor, input_t, output_t, input_tensor, output_tensor):
    solver.add(
        If(
            tm_type == TMType.hslice.value,
            And(input_t * tm_factor == output_t, input_tensor[0] == output_tensor[0], input_tensor[1] == output_tensor[1] * tm_factor),
            If(
                tm_type == TMType.vslice.value,
                And(input_t * tm_factor == output_t, input_tensor[0] == output_tensor[0] * tm_factor, input_tensor[1] == output_tensor[1]),
                If (
                    tm_type == TMType.hstack.value,
                    And(input_t == output_t * tm_factor, input_tensor[0] == output_tensor[0], input_tensor[1] * tm_factor == output_tensor[1]),
                    If (
                        tm_type == TMType.vstack.value,
                        And(input_t == output_t * tm_factor, input_tensor[0] * tm_factor == output_tensor[0], input_tensor[1] == output_tensor[1]),
                        If (
                            tm_type == TMType.broadcast_r.value,
                            And(input_t == output_t, input_tensor[0] * tm_factor == output_tensor[0], input_tensor[1] == output_tensor[1]),
                            If (
                                tm_type == TMType.broadcast_c.value,
                                And(input_t == output_t, input_tensor[0] == output_tensor[0], input_tensor[1] * tm_factor == output_tensor[1]),
                                If (
                                    tm_type == TMType.broadcast_z.value,
                                    And(input_t * tm_factor == output_t, input_tensor[0] == output_tensor[0], input_tensor[1] == output_tensor[1]),
                                    And(input_t == output_t, input_tensor[0] == output_tensor[0], input_tensor[1] == output_tensor[1]),
                                )
                            )
                        )
                    )
                )
            )
        )
    )

def default_matmul_constraints(*, solver, svars,
    max_tensor_r, max_tensor_c,
    min_tensor_r, min_tensor_c,
    max_input_t,
    max_grid_size_r, max_grid_size_c,
    min_grid_size_r, min_grid_size_c,
    max_ub_dim, min_ub_dim):

    # outer dim constraints
    solver.add(svars['mm_input0_tensor_r'] == svars['output_tensor_r'])
    solver.add(svars['mm_input1_tensor_c'] == svars['output_tensor_c'])

    # inner dim constraints
    solver.add(svars['mm_input0_tensor_c'] == svars['mm_input1_tensor_r'])
    solver.add(svars['mm_input0_tensor_c'] == svars['mb_inner'] * svars['ub_inner'])
    solver.add(svars['mb_inner'] > 0)
    solver.add(svars['ub_inner'] > 0)

    # buf_size_mb constraints
    constrain_buf_size_mb(solver=solver, buf_size_mb=svars['output_buf_size_mb'], input_count=svars['input_count'], t=svars['output_t'])

    # L1 Memory Usage
    constrain_matmul_l1(
        solver=solver,
        max_usage_bytes = L1_MAX_USAGE_BYTES,
        tile_size_bytes = FP16_TILE_SIZE,
        mb_r = svars['output_mb_r'],
        ub_r = svars['output_ub_r'],
        mb_c = svars['output_mb_c'],
        ub_c = svars['output_ub_c'],
        ukt  = svars['ub_inner'],
        buf_size_mb=svars['output_buf_size_mb']
    )

    #input0 side
    solver.add(max_tensor_r >= svars['input0_tensor_r'])
    solver.add(min_tensor_r <= svars['input0_tensor_r'])
    solver.add(max_tensor_c >= svars['input0_tensor_c'])
    solver.add(min_tensor_c <= svars['input0_tensor_c'])
    solver.add(max_input_t >= svars['input0_t'])
    solver.add(0 < svars['input0_t'])

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['input0_tensor_r'],
        grid_dim_var=svars['input0_grid_size_r'],
        mb_dim_var=svars['input0_mb_r'],
        ub_dim_var=svars['input0_ub_r'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_r, max_grid_size_r),
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['input0_tensor_c'],
        grid_dim_var=svars['input0_grid_size_c'],
        mb_dim_var=svars['input0_mb_c'],
        ub_dim_var=svars['input0_ub_c'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_c, max_grid_size_c),
    )

    #input1 side
    solver.add(max_tensor_r >= svars['input1_tensor_r'])
    solver.add(min_tensor_r <= svars['input1_tensor_r'])
    solver.add(max_tensor_c >= svars['input1_tensor_c'])
    solver.add(min_tensor_c <= svars['input1_tensor_c'])
    solver.add(max_input_t >= svars['input1_t'])
    solver.add(0 < svars['input1_t'])

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['input1_tensor_r'],
        grid_dim_var=svars['input1_grid_size_r'],
        mb_dim_var=svars['input1_mb_r'],
        ub_dim_var=svars['input1_ub_r'],
        ub_limits=(min_ub_dim, max_ub_dim),
        grid_limits=(min_grid_size_r, max_grid_size_r),
    )

    constrain_blocking(
        solver=solver,
        tensor_dim_var=svars['input1_tensor_c'],
        grid_dim_var=svars['input1_grid_size_c'],
        mb_dim_var=svars['input1_mb_c'],
        ub_dim_var=svars['input1_ub_c'],
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

    # constrain to half dest
    constrain_ublock_size(
        solver=solver,
        ub_r_var=svars['output_ub_r'],
        ub_c_var=svars['output_ub_c'],
        max_size=8 # 8 tiles, half dest
    )

# op_list is a list of tuples of coordinates of rectangles to constrain
def op_placement_constraints(*, solver, op_list, max_grid_size_r, max_grid_size_c):
    # Grid boundaries
    for (x1, y1, x2, y2) in op_list:
        constrain_rectangle(solver, max_grid_size_r, max_grid_size_c, x1, y1, x2, y2)

    for i in range(len(op_list)):
        for j in range(i+1, len(op_list)):
            (r1_x1, r1_y1, r1_x2, r1_y2) = op_list[i]
            (r2_x1, r2_y1, r2_x2, r2_y2) = op_list[j]
            # See https://silentmatt.com/rectangle-intersection/ for why this works.
            solver.add(Not(And(r1_x1 < r2_x2, r1_x2 > r2_x1, r1_y1 < r2_y2, r1_y2 > r2_y1)))

def constrain_rectangle(solver, max_grid_size_r, max_grid_size_c, x1, y1, x2, y2):
    solver.add(0 <= x1)
    solver.add(x1 < max_grid_size_c)
    solver.add(0 <= y1)
    solver.add(y1 < max_grid_size_r)
    # The bottom-right coordianate of the rectangle has range ((1, max_grid_size_c), (1, max_grid_size_r))
    # since each rectangle has non-zero width and height i.e. must contain at least one core.
    solver.add(0 < x2)
    solver.add(x2 <= max_grid_size_c)
    solver.add(0 < y2)
    solver.add(y2 <= max_grid_size_r)

def constrain_grid_location(solver, grid_loc_r, grid_loc_c, grid_size_r, grid_size_c, grid_limit_r, grid_limit_c):
    solver.add(grid_loc_r >= 0)
    solver.add(grid_loc_c >= 0)
    solver.add(grid_loc_r + grid_size_r <= grid_limit_r)
    solver.add(grid_loc_c + grid_size_c <= grid_limit_c)

def calculate_num_buffers_and_size(grid_r, grid_c, input_count, mb_r, mb_c, ub_r, ub_c, t, tile_size=FP16_TILE_SIZE):
    return (
        grid_r * grid_c,
        input_count * mb_r * mb_c * ub_r * ub_c * t * tile_size
    )

def get_queue_buffer_size(data_format: str, num_entries, t, mblock_dim, ublock_dim, arch: str):
    df_val = DataFormat[data_format].value
    if df_val in get_data_format_tile_size_map(arch):
        tile_size = get_data_format_tile_size_map(arch)[df_val]
    else:
        raise "Invalid data-format"

    num_tiles = num_entries * t * mblock_dim[0] * mblock_dim[1] * ublock_dim[0] * ublock_dim[1]
    return tile_size * num_tiles

def VAR(var_name, operand_index):
    if operand_index == -1:
        return "output_" + var_name
    elif operand_index >= 0:
        return f"input{operand_index}_" + var_name
    else:
        raise ValueError("Invalid operand index")

def is_format_type_a(data_format: DataFormat):
    if data_format.value in data_formats_a:
        return True
    
    if data_format.value in data_formats_b:
        return False
    
    raise f"Data format {data_format.name} does not have A/B type."

def is_format_int(data_format: DataFormat):
    return data_format.value in data_formats_int

def is_int_supported(op_type: PerfTestType, unary_type=None):
    if op_type in [
        PerfTestType.Matmul, 
        PerfTestType.MatmulBias, 
        PerfTestType.MatmulSparse,
        PerfTestType.MatmulQuant, 
        PerfTestType.MatmulBiasQuant,
        PerfTestType.Quant,
        PerfTestType.Binary
    ]:
        return True
    elif op_type == PerfTestType.Unary and unary_type == UnaryType.nop.value:
        return True
    
    return False