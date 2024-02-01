# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from z3 import *

def divisible_either_direction(a: z3.Var, b: z3.Var) -> z3.Var:
    """
    Enforce that the two given z3 variables are divisible in either direction.

    Parameters
    ----------
    a:
        First operand.
    b:
        Second operand.
    """
    return Or(a % b == 0, b % a == 0)

def z3_max(a: z3.Var, b: z3.Var) -> z3.Var:
    """
    Return max of the two given z3 variables.

    Parameters
    ----------
    a:
        First operand.
    b:
        Second operand.
    """
    return If(a > b, a, b)
