# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from enum import Enum


class NodeType(Enum):
    """
    Defines supported node types.
    """

    # TODO: Probably merge unary and binary op into a single EtlwiseOp node type, since
    # they are pretty match the same

    InputQueue = 0
    OutputQueue = 1
    ParameterQueue = 2
    ConstantQueue = 3
    Epoch2EpochQueue = 4
    UnaryOp = 5
    BinaryOp = 6
    EltwiseOp = 7
    MatmulOp = 8
    ReduceOp = 9
    FusedOp = 10
    ScheduledMatmulOp = 11
    ScheduledReduceOp = 12

    def __str__(self):
        return self.name


class BufferLoc(Enum):
    """
    Defines supported queue buffers locations.
    """

    host = 0
    dram = 1


class Dimension(Enum):
    """
    Defines supported spatial dimensions.
    """

    Vertical = 0
    Horizontal = 1

    @classmethod
    def opposite(cls, dim: Dimension) -> Dimension:
        """
        Returns 'opposite' spatial dimension for a given dimension, i.e.
        return Vertical for Hoziontal and vice versa.

        Parameters
        ----------
        dim: Dimension
            Input dimension
        """

        return (
            Dimension.Vertical if dim == Dimension.Horizontal else Dimension.Horizontal
        )

    @staticmethod
    def create_from_string(dim: str) -> Dimension:
        """
        Utilitiy function for converting strings 'r' and 'c' which are used
        throughout netlist to Enum type.
        """
        if dim == "r":
            return Dimension.Vertical
        elif dim == "c":
            return Dimension.Horizontal
        else:
            assert False, f"Unsupported dimension {dim}"


class Untilize(Enum):
    """
    Defines supported untilize attribute values of prologue queues.
    """

    false = 0
    true = 1


class Prologue(Enum):
    """
    Defines supported values for prologue attribute.
    """

    false = 0
    true = 1


class UblockOrder(Enum):
    """
    Defines supported values for ublock order attribute.
    """

    r = 0
    c = 1


class TMS(Enum):
    """
    Defines supported TMs.
    """

    c_broadcast = 1
    r_broadcast = 2
    t_broadcast = 3
    transpose = 4
    hslice = 5
    vslice = 6
    hstack = 7
    vstack = 8
    tile_broadcast = 9


class TileBroadcastDimension(Enum):
    """Define supported tile_broadcast dimensions."""

    r = 0
    c = 1
