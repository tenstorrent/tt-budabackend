# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from abc import ABC
from typing import Dict, List
from pathlib import Path

from z3 import *

from test_modules.common.node import Node
from test_modules.common.constants import *
from test_modules.common.device_architecture import *
from test_modules.common.limits import GeneralLimits
from test_modules.common.enums import TMS, NodeType, TileBroadcastDimension
from test_modules.common.op_input import NetlistTMConfiguration, OpInputTMs
from test_modules.common.resource_constraints import *
from test_modules.common.template_netlist_test_base import TemplateNetlistTestBase

class GraphTestBase(TemplateNetlistTestBase, ABC):
    """
    Base class of universal random test generator for graphs.

    Individual graph tests should extend this class and override
    function additional_constraints() to add specific constraints.
    """

    def __init__(self,
                 solver: Solver,
                 svars: dict,
                 general_limits: GeneralLimits,
                 template_path: str,
                 device_architecture: DeviceArchitecture) -> None:
        """
        Initializes GraphTestBase.

        Parameters
        ----------
        solver: Solver
            Z3 solver to add constraints to.

        svars: dict
            Dictionary to store all created Z3 variables with their name: value pairs.

        general_limits: GeneralLimits
            General limits to use.

        template_path: str
            Template file location.

        device_architecture: DeviceArchitecture
            Device architecture configuration.
        """
        super().__init__(
            solver=solver,
            svars=svars,
            template_path=template_path,
            arch=device_architecture,
            verbose=False
        )
        self.general_limits = general_limits
        self.op_input_tms_format = "input_{}_tms"

    # @override
    def constrain_point_to_point_connection(self, producer: Node, consumer: Node) -> None:
        super().constrain_point_to_point_connection(producer, consumer)

        # In graph tests, we always force producers to have full-t buffering.
        op_input = self.op_input_tm_map[consumer.name][producer.name]
        # Full t buffering refers only to inputs with stack TMs - so OpInputNoTMs and
        # ScheduledOpInputTMs do not operate with these z3 variables.
        if isinstance(op_input, OpInputTMs):
            self.solver.add(
                op_input.prod_fullt_buf == 1, op_input.p2p_buf_size_mb == producer.buf_size_mb
            )
        elif not producer.is_queue():
            # Queues already have buf_size_mb == t so we would run into contradiction.
            self.solver.add(producer.buf_size_mb == 2)

    # @override
    def parse_op_tms(self, op_node: Node, op_params: Dict[str, any]) -> None:
        for input_idx  in range(len(op_node.inputs)):
            input_tms_attr_name = self.op_input_tms_format.format(input_idx)
            op_input_tms = op_params.get(input_tms_attr_name, [])

            tm_configs = [
                self.make_tm_config_from_netlist(
                    op_node, GraphTestBase.dictify_netlist_tm(tm), tm_idx, input_idx
                )
                for tm_idx, tm in enumerate(op_input_tms)
            ]
            setattr(op_node, input_tms_attr_name, tm_configs)

    # @override
    def export_op_input_tm_configuration(self, op_node: Node, input_idx: int) -> List[NetlistTMConfiguration]:
        input_tms_attr_name = self.op_input_tms_format.format(input_idx)
        return getattr(op_node, input_tms_attr_name, [])

    # @override
    def assign_global_graph_attributes(self) -> None:
        super().assign_global_graph_attributes()
        self.global_t_dim = self.add_var(GLOBAL_T_DIM_KEY)

    # @override
    def global_constraints(self) -> None:
        super().global_constraints()
        self.solver.add(
            self.global_t_dim >= self.general_limits.min_t_dim_size,
            self.global_t_dim <= self.general_limits.max_t_dim_size
        )
        self.solver.add(
            self.minibatch_count >= self.general_limits.min_minibatch_count,
            self.minibatch_count <= self.general_limits.max_minibatch_count
        )
        self.solver.add(
            self.microbatch_count >= self.general_limits.min_microbatch_count,
            self.microbatch_count <= self.general_limits.max_microbatch_count
        )
        self.solver.add(
            self.input_count >= self.general_limits.min_input_count,
            self.input_count <= self.general_limits.max_input_count
        )
        self.solver.add(
            self.minibatch_count * self.microbatch_count * self.input_count * self.global_t_dim <=
            self.general_limits.max_graph_execution_iterations
        )

    # @override
    def constrain_queues(self) -> None:
        super().constrain_queues()

        # In graph tests, we constrain all the queus to have the same T dimension.
        for q_node in self.queues:
            if q_node.type == NodeType.ConstantQueue:
                # Optimization of L1 space for constant queues
                # We can either fill them on full t, or fill just one plane and
                # broadcast t times on z.
                self.solver.add(Or(q_node.t_dim == 1,
                                   q_node.t_dim == self.global_t_dim))

    # @override
    def constrain_input_queue(self, input_queue: Node) -> None:
        super().constrain_input_queue(input_queue)

        # Additional constraints regarding input size.
        size_r = input_queue.get_size_r_var()
        size_c = input_queue.get_size_c_var()
        self.solver.add(
            size_r >= self.general_limits.min_input_size_r,
            size_r <= self.general_limits.max_input_size_r
        )
        self.solver.add(
            size_c >= self.general_limits.min_input_size_c,
            size_c <= self.general_limits.max_input_size_c
        )
        self.solver.add(size_r % self.arch.tile_r == 0)
        self.solver.add(size_c % self.arch.tile_c == 0)

        self.solver.add(input_queue.mb_m <= size_r / self.arch.tile_r)
        self.solver.add(input_queue.mb_n <= size_c / self.arch.tile_c)

    def make_tm_config_from_netlist(
        self,
        op: Node,
        netlist_tm: Dict[str, any],
        tm_idx: int,
        input_idx: int
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
            tm_type = TMS.c_broadcast if broadcast_type == "c" else (
                TMS.r_broadcast if broadcast_type == "r" else (
                    TMS.t_broadcast
                )
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
            "test_modules.graph_tests." + filename.strip(".py")
            for filename in os.listdir(Path(__file__).parent.resolve())
            if filename.startswith("test_") and filename.endswith(".py")
        ]

    # @override
    def postprocess(self, model_vars: Dict[str, any]) -> None:
        super().postprocess(model_vars)

        self.fix_tile_broadcast_dimension(model_vars)

    def fix_tile_broadcast_dimension(self, model_vars: Dict[str, any]) -> None:
        """Updates `model_vars` to replace tile_broadcast dimension integer values with the
        corresponding names: 1/2 (TileBroadcastDimension enum values) -> r/c (TileBroadcastDimension
        enum names). Solver generates integer values only, so values for variables expecting strings
        or other types need to be converted.

        # Parameters
        ----------
        model_vars:
            Dictionary of variable names and their generated values.
        """
        for op in self.ops:
            if op.type == NodeType.FusedOp:
                for scheduled_op in op.scheduled_ops:
                    for input_idx in range(len(scheduled_op.inputs)):
                        # TM configs are initialized in parse_op_tms method.
                        input_tms_attr_name = self.op_input_tms_format.format(input_idx)
                        for tm_config in getattr(scheduled_op, input_tms_attr_name):
                            if tm_config.tm_type == TMS.tile_broadcast.value:
                                model_vars[
                                    tm_config.tm_arg.sexpr()
                                ] = TileBroadcastDimension(
                                    model_vars[tm_config.tm_arg.sexpr()]
                                ).name
