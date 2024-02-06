# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from abc import ABC
from enum import IntEnum
from typing import Dict, List
from pathlib import Path

from z3 import *

from test_modules.common.enums import UblockOrder, Untilize
from test_modules.common.node import Node
from test_modules.common.device_architecture import (
    DeviceArchitecture,
)
from test_modules.data_movement.data_movement_common_constraints import *
from test_modules.graph_tests.graph_test_base import GeneralLimits, GraphTestBase

MAX_INPUT_SIZE_TILES_R = 256
MAX_INPUT_SIZE_TILES_C = 256
MIN_INPUT_SIZE_TILES_R = 1
MIN_INPUT_SIZE_TILES_C = 1
MAX_TENSOR_SIZE_IN_TILES = 64 * 1024

class DataMovementPipeTestType(IntEnum):
    PackerUnicast = 0
    DramUnicast = 1
    UnicastToDram = 2
    DramPrefetchPostTM = 3
    DramPrefetchPreTM = 4

    PackerGather = 5
    DramGather = 6
    GatherToDram = 7

    DramMulticast = 8
    DramGatherMulticast = 9
    PackerMulticast = 10
    PackerGatherMulticast = 11

    PackerParallelFork = 12
    DramParallelFork = 13
    PackerSerialFork = 14



class DataMovementPipeTestBase(GraphTestBase, ABC):
    """
    Base class for data movement pipe tests.
    """

    def __init__(
        self,
        solver: Solver,
        svars: dict,
        general_limits: GeneralLimits,
        template_path: str,
        device_architecture: DeviceArchitecture,
    ) -> None:
        super().__init__(
            solver=solver,
            svars=svars,
            general_limits=general_limits,
            template_path=template_path,
            device_architecture=device_architecture,
        )

    # @override
    def generate_graph(self) -> None:
        super().generate_graph()

    # @override
    def constrain_input_queue(self, q_node: Node) -> None:
        super().constrain_input_queue(q_node)
        self.constrain_tensor_size(q_node, MAX_TENSOR_SIZE_IN_TILES)

    # @override
    def global_constraints(self) -> None:
        super().global_constraints()

        self.solver.add(self.microbatch_count == 1, self.microbatch_size == 1, self.minibatch_count == 1)

    # @override
    def constrain_point_to_point_connections(self) -> None:
        super().constrain_point_to_point_connections()

    # @override
    def constrain_output_queue(self, q_node: Node) -> None:
        super().constrain_output_queue(q_node)

        feeder_op = self.nodes[q_node.input]
        self.solver.add(
            Implies(
                feeder_op.untilize == Untilize.true.value,
                q_node.ublock_order == UblockOrder.r.value,
            )
        )

    def get_feeder_ops(self) -> List[Node]:
        """Returns a list of all the feeder ops."""

        def __is_feeder_op(op):
            return op.name.startswith("feeder")

        return filter(__is_feeder_op, self.ops)

    def get_target_op_ops(self) -> List[Node]:
        """Returns the list of all the target ops."""

        def __is_target_op(op):
            return op.name.startswith("target_op")

        target_ops = list(filter(__is_target_op, self.ops))
        assert target_ops, "No target ops found in the template."

        return target_ops

    @staticmethod
    def get_test_module_names() -> List[str]:
        return [
            "test_modules.data_movement_pipe_tests." + filename.strip(".py")
            for filename in os.listdir(Path(__file__).parent.resolve())
            if filename.startswith("test_") and filename.endswith(".py")
        ]

    @staticmethod
    def _create_general_limits(device_arch: DeviceArchitecture) -> GeneralLimits:
        """
        Creates general limits for pipe test constraints.

        Parameters
        ----------
        device_arch: DeviceArchitecture
            Device architecture configuration.
        """
        limits = GeneralLimits(device_arch)
        limits.max_input_size_r = MAX_INPUT_SIZE_TILES_R * device_arch.tile_r
        limits.min_input_size_r = MIN_INPUT_SIZE_TILES_R * device_arch.tile_r
        limits.max_input_size_c = MAX_INPUT_SIZE_TILES_C * device_arch.tile_c
        limits.min_input_size_c = MIN_INPUT_SIZE_TILES_C * device_arch.tile_c

        return limits
