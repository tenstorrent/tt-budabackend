# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from abc import ABC
from enum import Enum
import random
from typing import Dict, List
from pathlib import Path

from z3 import *

from test_modules.common.node import Node
from test_modules.common.device_architecture import (
    DeviceArchitecture,
    GrayskullArchitecture,
    WormholeB0Architecture,
)
from test_modules.common.enums import TMS, BufferLoc, NodeType, Prologue, UblockOrder, Untilize
from test_modules.common.op_input import NetlistTMConfiguration, OpInputTMs
from test_modules.common.template_netlist_test_base import TemplateNetlistTestBase
from test_modules.data_movement.data_movement_common_constraints import *


class DataMovementPerfTestType(Enum):
    DramUnicast = 0
    PackerUnicast = 1
    UnicastToDram = 2

    DramGather = 3
    PackerGather = 4
    GatherToDram = 5

    DramSingleFork = 6
    DramMultiFork = 7
    PackerSingleFork = 8
    PackerMultiFork = 9

    DramMulticast = 10
    DramGatherMulticast = 11
    PackerMulticast = 12
    PackerGatherMulticast = 13

    PipeScatter = 14
    GatherPipeScatter = 15
    PipeScatterMulticast = 16
    GatherPipeScatterMulticast = 17


class SourceBufferType(Enum):
    NonScatter = 0
    Scatter = 1


class DataMovementPerfTestBase(TemplateNetlistTestBase, ABC):
    """
    Base class for data movement perf tests.
    """

    def __init__(
        self,
        solver: Solver,
        svars: dict,
        template_path: str,
        device_architecture: DeviceArchitecture,
    ) -> None:
        super().__init__(
            solver=solver,
            svars=svars,
            template_path=template_path,
            arch=device_architecture,
            verbose=False,
        )
        self.op_input_tms_format = "input_{}_tms"
        self.queue_dram_channels_map: Dict[str, List[int]] = {}

    # @override
    def generate_graph(self) -> None:
        super().generate_graph()

        self.populeate_queue_dram_channels_map()

    # @override
    def global_constraints(self) -> None:
        super().global_constraints()

        # TODO: Extract constant
        self.solver.add(
            self.microbatch_count == 1, self.microbatch_size == 128, self.minibatch_count == 1
        )

    # @override
    def constrain_point_to_point_connections(self) -> None:
        super().constrain_point_to_point_connections()

        self.constrain_feeder_op_connections()

    # @override
    def constrain_op_untilize(self, op_node: Node) -> None:
        super().constrain_op_untilize(op_node)

        self.solver.add(op_node.untilize == False)

    def constrain_feeder_op_connections(self) -> None:
        """Constraints feeder op nodes input connections."""

        # No reblocking between input dram nodes and feeder op(s).
        for feeder_op in self.get_feeder_ops():
            for feeder_input_name in feeder_op.inputs:
                input_node = self.nodes[feeder_input_name]
                constrain_no_reblocking_on_connection(self.solver, input_node, feeder_op)

    # @override
    def constrain_parameter_queue(self, param_queue: Node):
        super().constrain_parameter_queue(param_queue)

        for graph_name in self.graphs:
            self.solver.add(param_queue.get_prologue_var(graph_name) == Prologue.true.value)
            self.solver.add(
                Or(
                    param_queue.ublock_order == UblockOrder.r.value,
                    param_queue.ublock_order == UblockOrder.c.value,
                )
            )

    # @override
    def define_common_queue_constraints(self, q_node: Node) -> None:
        super().define_common_queue_constraints(q_node)

        self.solver.add(q_node.loc == BufferLoc.dram.value)

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

    # @override
    def parse_op_tms(self, op_node: Node, op_params: Dict[str, any]) -> None:
        for input_idx in range(len(op_node.inputs)):
            input_tms_attr_name = self.op_input_tms_format.format(input_idx)
            op_input_tms = op_params.get(input_tms_attr_name, [])

            tm_configs = [
                self.make_tm_config_from_netlist(
                    op_node, DataMovementPerfTestBase.dictify_netlist_tm(tm), tm_idx, input_idx
                )
                for tm_idx, tm in enumerate(op_input_tms)
            ]
            setattr(op_node, input_tms_attr_name, tm_configs)

    # @override
    def export_op_input_tm_configuration(
        self, op_node: Node, input_idx: int
    ) -> List[NetlistTMConfiguration]:
        input_tms_attr_name = self.op_input_tms_format.format(input_idx)
        return getattr(op_node, input_tms_attr_name, [])

    # @override
    def create_dram_buffer_strings(self, model_vars: Dict[str, any]) -> None:
        curr_start_addr = [self.arch.dram_buffer_start_addr] * self.arch.num_dram_channels

        for q_node in self.queues:
            grid_size_x = q_node.get_attr_from_model(model_vars, "grid_size_x")
            grid_size_y = q_node.get_attr_from_model(model_vars, "grid_size_y")

            q_buffer_count = grid_size_x * grid_size_y
            q_buffers_size = q_node.get_attr_from_model(model_vars, "buffers_size")

            # Dram buffer allocation.
            queue_has_allocated_dram_channel = False
            queue_dram_channels = self.get_available_dram_channels_for_queue(q_node)
            for dram_channel in queue_dram_channels:
                q_end_address = curr_start_addr[dram_channel] + q_buffer_count * q_buffers_size
                if q_end_address < self.arch.dram_buffer_end_addr:
                    queue_has_allocated_dram_channel = True
                    break
            assert (
                queue_has_allocated_dram_channel
            ), f"Failed to allocate DRAM channel for queue '{q_node.name}' with available channels: {queue_dram_channels}"

            model_vars[q_node.loc_template_str] = TemplateNetlistTestBase.dram_string(
                channel=dram_channel,
                start_addr=curr_start_addr[dram_channel],
                buffer_count=q_buffer_count,
                buffer_size=q_buffers_size,
            )

            curr_start_addr[dram_channel] += q_buffer_count * q_buffers_size
            # Dram addresses need to be a multiple of NOC data width since it
            # does not support unaligned accesses.
            curr_start_addr[dram_channel] += (
                curr_start_addr[dram_channel] % self.arch.noc_data_word_width_bytes
            )

    def get_available_dram_channels_for_queue(self, queue: Node) -> List[int]:
        """
        Returns a list of available dram channels on which queue's buffers can be placed. Can be overriden
        in concrete test calsses.

        Parameters
        ----------
        queue:
            Queue node for which to get available dram channels.
        """
        return self.queue_dram_channels_map[queue.name]

    def populeate_queue_dram_channels_map(self) -> None:
        """
        Compute a list of available dram channels for every queue such that it is optimally configured.
        """
        available_dram_channels = self.get_dram_channels_config_for_arch()
        num_input_configs = 2
        output_config_idx = 2

        def __dfs(node: Node):
            if node.name in self.queue_dram_channels_map:
                return

            node_inputs = []

            if node.is_queue():
                if node.type == NodeType.OutputQueue:
                    node_inputs.append(node.input)
                else:
                    input_node_idx = len(self.queue_dram_channels_map)
                    self.queue_dram_channels_map[node.name] = available_dram_channels[
                        input_node_idx % num_input_configs
                    ]
            else:
                node_inputs.extend(node.inputs)

            for node_input_name in node_inputs:
                __dfs(self.nodes[node_input_name])

        for queue in self.get_output_queues():
            __dfs(queue)

        for queue in self.get_output_queues():
            self.queue_dram_channels_map[queue.name] = available_dram_channels[output_config_idx]

    def get_dram_channels_config_for_arch(self) -> List[List[int]]:
        if isinstance(self.arch, WormholeB0Architecture):
            return [
                [0, 2],  # Input 0
                [1, 3],  # Input 1
                [4, 5],  # Output
            ]
        elif isinstance(self.arch, GrayskullArchitecture):
            return [
                [0, 2],  # Input 0
                [5, 7],  # Input 1
                [1, 3],  # Output
            ]
        else:
            raise RuntimeError(f"Unsupported device architecture.")

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

    def get_drainer_ops(self) -> List[Node]:
        """Returns a list of all the drainer ops."""

        def __is_drainer_op(op):
            return op.name.startswith("drainer")

        return filter(__is_drainer_op, self.ops)

    def make_tm_config_from_netlist(
        self, op: Node, netlist_tm: Dict[str, any], tm_idx: int, input_idx: int
    ) -> NetlistTMConfiguration:
        """
        Make TM configuration to be used to constrain the OP input tms from netlist.

        Parameters
        ----------
        op:
            Consumer op node.
        netlist_tm:
            Dictionary representing op input tms read from template netlist yaml.
        tm_idx:
            Index of the current TM in the list of OP input TMs.
        input_idx:
            Input index.
        """
        tm_netlist_type, tm_netlist_arg = next(iter(netlist_tm.items()))
        if tm_netlist_type == "transpose":
            tm_type = TMS.transpose
            tm_arg_z3_var = None
        elif tm_netlist_type == "broadcast":
            broadcast_type, broadcast_arg = next(iter(tm_netlist_arg.items()))
            tm_type = (
                TMS.c_broadcast
                if broadcast_type == "c"
                else (TMS.r_broadcast if broadcast_type == "r" else (TMS.t_broadcast))
            )
            tm_arg_z3_var = broadcast_arg
        else:
            tm_type = TMS[tm_netlist_type]
            tm_arg_z3_var = tm_netlist_arg

        tm_arg_var_name = f"input_{input_idx}_tm_{tm_idx}_arg"
        if tm_arg_z3_var:
            # tile_broadcast argumentcan be 0/1 (mapped to r/c), and default min_value is 1,
            # so we need to set it explicitly.
            op.add_constrained_attribute(tm_arg_var_name, tm_arg_z3_var, self.solver, min_value=0)

        return NetlistTMConfiguration(
            tm_type=tm_type.value, tm_arg=getattr(op, tm_arg_var_name, None)
        )

    @staticmethod
    def dictify_netlist_tm(netlist_tm: Dict[str, any] | any) -> Dict[str, any]:
        """
        Converts parsed TM from netlist to dict. It is a no op in case of all the TMs apart
        from transpose, where it converts it to a dict {'transpose': None}. Enables all the TMs
        to be created in the uniform manner.

        Parameters
        ----------
        netlist_tm:
            Parsed TM from netlist to be converted to dict.
        """
        if isinstance(netlist_tm, dict):
            return netlist_tm
        return {netlist_tm: None}

    @staticmethod
    def get_test_module_names() -> List[str]:
        return [
            "test_modules.data_movement.perf_tests." + filename.strip(".py")
            for filename in os.listdir(Path(__file__).parent.resolve())
            if filename.startswith("test_") and filename.endswith(".py")
        ]
