# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from enum import Enum

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
    RawUInt32   = 9
    
