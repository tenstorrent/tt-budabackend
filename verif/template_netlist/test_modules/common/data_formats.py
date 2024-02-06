# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from enum import Enum
from z3 import *

# Tile size per data format
TILE_SIZE_BFP2 = 256 + 32 + 64
TILE_SIZE_BFP4 = 512 + 32 + 64
TILE_SIZE_BFP8 = 32 * 32 + 32 + 64
TILE_SIZE_FP16 = 32 * 32 * 2 + 32
TILE_SIZE_FP32 = 32 * 32 * 4 + 32

class DataFormat(Enum):
    """
    Defines supported data formats.

    NOTE Changing the order of these affects the test constraints!
    """
    Bfp2_b      = 0
    Bfp2        = 1
    Bfp4_b      = 2
    Bfp4        = 3
    Bfp8_b      = 4
    Bfp8        = 5
    Float16_b   = 6
    Float16     = 7
    Float32     = 8

def get_tile_size(data_format: DataFormat) -> int:
    """Returns tile size for a given data format.

    Parameters
    ----------
    data_format: DataFormat
        Data format.

    Returns
    -------
    int
    """
    if data_format == DataFormat.Bfp2_b or data_format == DataFormat.Bfp2:
        return TILE_SIZE_BFP2
    elif data_format == DataFormat.Bfp4_b or data_format == DataFormat.Bfp4:
        return TILE_SIZE_BFP4
    elif data_format == DataFormat.Bfp8_b or data_format == DataFormat.Bfp8:
        return TILE_SIZE_BFP8
    elif data_format == DataFormat.Float16_b or data_format == DataFormat.Float16:
        return TILE_SIZE_FP16
    else:
        return TILE_SIZE_FP32

def get_tile_size_z3_var(df_var: z3.Var) -> z3.Var:
    """Returns z3 variable representing tile size for given data
    format.

    Parameters
    ----------
    df_var: z3.Var
        Data format variable.

    Returns
    -------
    z3.Var
    """
    return If(
        And(
            df_var >= DataFormat.Bfp2_b.value,
            df_var <= DataFormat.Bfp2.value
        ),
        TILE_SIZE_BFP2,
        If(
            And(
                df_var >= DataFormat.Bfp4_b.value,
                df_var <= DataFormat.Bfp4.value
            ),
            TILE_SIZE_BFP4,
            If(
                And(
                    df_var >= DataFormat.Bfp8_b.value,
                    df_var <= DataFormat.Bfp8.value
                ),
                TILE_SIZE_BFP8,
                If(
                    And(
                        df_var >= DataFormat.Float16_b.value,
                        df_var <= DataFormat.Float16.value
                    ),
                    TILE_SIZE_FP16, 
                    TILE_SIZE_FP32
                )
            )
        )
    )