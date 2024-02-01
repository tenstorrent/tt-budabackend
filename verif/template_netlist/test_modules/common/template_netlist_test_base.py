# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from abc import ABC, abstractmethod
import os
import os.path
import random
from typing import Callable, List, Dict, Optional, Set, Tuple
import yaml
from logging import Logger

from z3 import *

from test_modules.common.data_formats import get_tile_size_z3_var
from test_modules.common.device_architecture import DeviceArchitecture, WormholeB0Architecture
from test_modules.common.node import Node
from test_modules.common.data_formats import DataFormat
from test_modules.common.enums import TMS, BufferLoc, Dimension, NodeType, Prologue, UblockOrder, Untilize
from test_modules.common.op_input import (
    NetlistTMConfiguration,
    OpInput,
    PadUnpadParams,
    ScheduledOpInputNoTMs,
    TMInfo,
    OpInputNoTMs,
    OpInputTMs,
    ConsumerOpConfiguration,
    TensorDims,
    ScheduledOpInputTMs,
    ScheduledOpTMInfo,
    divisible_either_direction
)
from test_modules.common.resource_constraints import (
    ResourceConstrNode,
    ResourceConstrNodeSide,
    get_num_buffers_accessed_along_dim,
    get_num_buffers_accessed_along_dim_z3_expr,
    get_op2op_resource_usage,
    get_queue2op_resource_usage,
    get_op2op_resource_usage_z3,
    get_queue2op_resource_usage_z3
)
from test_modules.common.constants import ARCH_VAR_NAME, DATA_FORMAT_KEY, DEFAULT_PROLOGUE_KEY, SIZE_C_VAR_NAME, SIZE_R_VAR_NAME, TEMPLATE_VAR_PREFIX, MULTI_TM_TESTS_VALID_TMS
from test_modules.common.sweep import SweepContext, SweepVarsGroup
from test_modules.common.z3_utils import z3_max


class TemplateNetlistTestBase(ABC):
    """
    Base class for all the test generators based on template netlist.

    Individual test modules should extend this class and override
    function add_additional_constraints() to add specific constraints.

    Additionally, additional sweep vars can also be exported by overriding
    function export_additional_sweep_vars().
    """

    def __init__(self,
                 solver: z3.Solver,
                 svars: Dict[str, z3.Var],
                 template_path: str,
                 arch: DeviceArchitecture,
                 verbose: bool = True) -> None:
        """Initializes TemplateNetlistTestBase.

        Parameters
        ----------
        solver:
            Z3 solver to add constraints to.
        svars:
            Dictionary to store all created Z3 variables with their name: value pairs.
        template_path:
            Template file location.
        arch:
            Device architecture configuration.
        verbose:
            Print logs to stdout.

        """
        super().__init__()
        self.solver = solver
        self.svars = svars
        self.arch = arch
        self.verbose = verbose

        self.nodes = {}
        self.queues = []
        self.ops = []

        self.sweep_context: Optional[SweepContext] = None

        self.fused_op_id_to_node: Dict[int, Node] = {}
        self.fused_op_input_to_subop_consumer: Dict[str, Dict[str, Node]] = {}
        self.op_input_tm_map: Dict[str, Dict[str, OpInputTMs]] = {}
        self.op_input_pad_unpad: Dict[str, Dict[str, PadUnpadParams]] = {}
        self.consumer_op_config_map: Dict[str, Dict[int, ConsumerOpConfiguration]] = {}

        self.op_constrainers: Dict[NodeType, Callable] = {
            NodeType.EltwiseOp: self.constrain_eltwise_op,
            NodeType.MatmulOp: self.constrain_matmul_op,
            NodeType.FusedOp: self.constrain_fused_op,
            NodeType.ReduceOp: self.constrain_reduce_op,
        }

        self.op_input_buffer_size_tiles_calculator: Dict[NodeType, Callable] = {
            NodeType.EltwiseOp: self.get_eltwise_op_input_buffer_size_tiles,
            NodeType.MatmulOp: self.get_matmul_op_input_buffer_size_tiles,
            NodeType.ScheduledMatmulOp: self.get_scheduled_matmul_op_input_buffer_size_tiles,
            NodeType.FusedOp: self.get_fused_op_input_buffer_size_tiles,
            NodeType.ReduceOp: self.get_reduce_op_input_buffer_size_tiles,
            NodeType.ScheduledReduceOp: self.get_reduce_op_input_buffer_size_tiles
        }

        self.op_input_dims_constrainers: Dict[NodeType, Callable] = {
            NodeType.EltwiseOp: self.constrain_eltwise_op_input_dims,
            NodeType.MatmulOp: self.constrain_matmul_op_input_dims,
            NodeType.ScheduledMatmulOp: self.constrain_scheduled_matmul_op_input_dims,
            NodeType.FusedOp: self.constrain_fused_op_input_dims,
            NodeType.ReduceOp: self.constrain_reduce_op_input_dims,
            NodeType.ScheduledReduceOp: self.constrain_scheduled_reduce_op_input_dims
        }

        self.op_consumer_confiugration_providers: Dict[NodeType, Callable] = {
            NodeType.EltwiseOp: self.get_eltwise_op_consumer_config,
            NodeType.FusedOp: self.get_fused_op_consumer_config,
            NodeType.MatmulOp: self.get_matmul_op_consumer_config,
            NodeType.ScheduledMatmulOp: self.get_scheduled_matmul_op_consumer_config,
            NodeType.ReduceOp: self.get_reduce_op_consumer_config,
            NodeType.ScheduledReduceOp: self.get_reduce_op_consumer_config
        }

        self.op_ethernet_links_usage_calculator: Dict[NodeType, Callable] = {
            NodeType.EltwiseOp: self.get_eltwise_op_ethernet_links_usage,
            NodeType.MatmulOp: self.get_matmul_op_ethernet_links_usage
        }

        self.read_template(template_path)
        self.assign_global_graph_attributes()

    def assign_global_graph_attributes(self) -> None:
        """
        Assigns global graph attributes which may be used in various constraints
        across all nodes.
        """
        self.data_format = self.add_var(DATA_FORMAT_KEY)
        self.default_prologue = self.add_var(DEFAULT_PROLOGUE_KEY)

    def read_template(self, template_path: str) -> None:
        """
        Reads template dictionary from file.

        Parameters
        ----------
        template_path: str
            Template file location.
        """
        assert os.path.exists(template_path), \
            f"Template file: '{template_path}' not found."

        with open(template_path, "r") as file:
            self.template_dict = yaml.safe_load(file.read())

    def generate_graph(self) -> None:
        """Reads template dictionary and creates a graph of all the nodes representing
        queues and ops.
        """
        self.graphs: List[str] = [gname for gname in self.template_dict["graphs"].keys()]

        self.read_input_counts()
        self.read_queues()
        self.read_ops()
        self.make_point_to_point_connections()

    def check_graph_validity(self) -> None:
        """
        Check if generated graph is valid before proceeding any further.

        For now, only op to queues fanout is checked. If an op has DRAM queue
        outputs, each such output takes one out of 8 DRAM output descriptor
        structures in NCRISC firmware, and therefore each core is limited to
        max_op_queues_fanout number of queues it can output to.
        """
        for op in self.ops:
            op_queues_fanout = self.get_op2queues_resource_usage(op, self.queues)

            assert (op_queues_fanout <= self.arch.max_op_queues_fanout), \
                   (
                        f"ERROR: graph is not valid because of "
                        f"max_op_queues_fanout constraint "
                        f"for op ({op.name}) "
                        f"({op_queues_fanout} > "
                        f"{self.arch.max_op_queues_fanout})"
                    )

    def constrain_op_node(self, op_node: Node) -> None:
        """Apply constraints which are specific for the given node.

        Parameters
        ----------
        op_node:
            Op node.
        """
        constrain_op_impl =\
             TemplateNetlistTestBase._get_op_callable(op_node, self.op_constrainers)

        constrain_op_impl(op_node)

    def get_op_input_buffer_size_tiles(self, op_node: Node, input_idx: int) -> z3.Var:
        """Retursn input buffer size of a given op node on the given input index. The size of the
        buffer is returned in tiles, with no double buffering.

        Parameters
        ----------
        op_node:
            Op node.
        input_idx:
            Input index.
        """
        get_op_input_buffer_size_tiles_impl =\
            TemplateNetlistTestBase._get_op_callable(op_node, self.op_input_buffer_size_tiles_calculator)

        return get_op_input_buffer_size_tiles_impl(op_node, input_idx)

    def constrain_op_input_dims(self, op_node: Node) -> Callable:
        """Apply constraints regarding input tensors dimension which are specific for a given
        op node.

        Parameters
        ----------
        op_node:
            Op node.
        """
        constrain_op_input_dims_impl =\
            TemplateNetlistTestBase._get_op_callable(op_node, self.op_input_dims_constrainers)

        constrain_op_input_dims_impl(op_node)

    def get_op_consumer_config(self, op_node: Node, input_idx: int) -> ConsumerOpConfiguration:
        """Returns consumer configufation of the given op node from the perspective of the produer
        node on the given input index.

        Parameters
        ----------
        op_node:
            Op node.
        input_idx:
            Input index.
        """
        get_op_consumer_config_impl =\
            TemplateNetlistTestBase._get_op_callable(op_node, self.op_consumer_confiugration_providers)

        return get_op_consumer_config_impl(op_node, input_idx)

    def get_op_ethernet_links_usage(self, op_node: Node, input_idx: int) -> int:
        """Returns ethernet links usage for a given op on a given input index.

        Parameters
        ----------
        op_node:
            Op node.
        input_idx:
            Input index.
        """
        get_op_ethernet_links_usage_impl =\
            TemplateNetlistTestBase._get_op_callable(op_node, self.op_ethernet_links_usage_calculator)

        return get_op_ethernet_links_usage_impl(op_node, input_idx)

    @staticmethod
    def _get_op_callable(op_node: Node, mapping: Dict[NodeType, Callable]) -> Callable:
        """Returns callable for specific for op node from the given callable mapping. Throws if no
        appropriate callable can be found.

        Parameters
        ----------
        op_node:
            Op node.
        mapping:
            Mapping: op node type -> callable
        """
        op_type = op_node.type
        if op_type not in mapping:
            raise RuntimeError(f"Unsupported OP type: {op_type}.")
        return mapping[op_type]

    def add_var(self, name: str) -> z3.Var:
        """Adds z3 Variable with a given name to the solver.

        Parameters
        ----------
        name:
            Name of the variable.
        """
        setattr(self, name, Int(name))
        var = getattr(self, name)
        self.svars[var.sexpr()] = var
        return var

    def get_real_consumer_op(self, producer: Node, consumer: Node) -> Node:
        """Returns:
            1) consumer if the consumer node is not fused op
            2) scheduled op which consumes the producer node otherwise

        # TODO this can be removed after resource constraints are cleaned up.

        Parameters
        ----------
        producer:
            Producer node.
        consumer:
            Consumer op.
        """
        if consumer.type != NodeType.FusedOp:
            return consumer

        return self.fused_op_input_to_subop_consumer[consumer.name][producer.name]

    def make_point_to_point_connections(self) -> None:
        """Connect inputs with outputs."""
        for consumer_op in self.ops:
            self.connect_consumer_to_producers(consumer_op)

            # TODO: Eventually add scheduled ops to self.ops list and avoid
            # special treatment of the scheduled ops where it is not needed.
            if consumer_op.type == NodeType.FusedOp:
                # Map fused op inputs to scheduled op inputs.
                for input_name in consumer_op.inputs:
                    fused_op_input = self.op_input_tm_map[consumer_op.name][input_name]
                    fused_op_pad_unpad = self.op_input_pad_unpad[consumer_op.name][input_name]

                    scheduled_consumer_op = self.get_fused_op_input_consumer_op(consumer_op, input_name)

                    if scheduled_consumer_op.name not in self.op_input_tm_map:
                        self.op_input_tm_map[scheduled_consumer_op.name] = {}

                    if scheduled_consumer_op.name not in self.op_input_pad_unpad:
                        self.op_input_pad_unpad[scheduled_consumer_op.name] = {}

                    self.op_input_tm_map[scheduled_consumer_op.name][input_name] = fused_op_input
                    self.op_input_pad_unpad[scheduled_consumer_op.name][input_name] = fused_op_pad_unpad

                # Make the rest of the consumer-producer connections (coming from the intermediate buffers).
                for scheduled_consumer_op in consumer_op.scheduled_ops:
                    self.connect_consumer_to_producers(scheduled_consumer_op)

    def connect_consumer_to_producers(self, consumer_op: Node) -> None:
        """Connect the passed consumer op with all the producers."""
        for input_idx, input_node_name in enumerate(consumer_op.inputs):
            if consumer_op.name not in self.op_input_tm_map:
                self.op_input_tm_map[consumer_op.name] = {}

            if input_node_name in self.op_input_tm_map[consumer_op.name]:
                # We already connected this input.
                continue

            producer_node = self.nodes[input_node_name]
            op_input_tm_configs = self.export_op_input_tm_configuration(consumer_op, input_idx)

            if hasattr(consumer_op, "fused_op"):
                op_input = self.create_scheduled_op_input(
                    producer=producer_node,
                    consumer=consumer_op,
                    input_idx=input_idx,
                    op_input_tm_configs=op_input_tm_configs,
                )
            else:
                consumer_op_config = self.get_op_consumer_config(consumer_op, input_idx)
                op_input = self.create_op_input(
                    producer=producer_node,
                    consumer=consumer_op,
                    op_input_tm_configs=op_input_tm_configs,
                    consumer_config=consumer_op_config,
                    pad_unpad_params=self.op_input_pad_unpad[consumer_op.name][input_node_name]
                )

            self.op_input_tm_map[consumer_op.name][input_node_name] = op_input

    @abstractmethod
    def export_op_input_tm_configuration(self, op_node: Node, input_idx: int) -> List[NetlistTMConfiguration]:
        """
        Exports TM configuration parsed from netlist so that appropriate OpInput object can be
        created. Has to be implemented in subclasses because different test types work with TMs
        in a different way.

        Parameters
        ----------
        op_node:
            Consumer op node.
        input_idx:
            Input index.
        """
        pass

    def create_op_input(
        self,
        producer: Node,
        consumer: Node,
        op_input_tm_configs: List[NetlistTMConfiguration],
        consumer_config: ConsumerOpConfiguration,
        pad_unpad_params: PadUnpadParams
    ) -> OpInput:
        """Make a point-to-point connection between producer and the consumer for the case where
        there are TMs on that connection.

        producer:
            Producer node.
        consumer:
            Consumer node.
        op_input_tm_configs:
            Configuration of OP input TMs.
        consumer_config:
            Configuration of the consumer op.
        pad_unpad_params:
            Values for padding and unpadding arguments.
        """
        if not op_input_tm_configs:
            return OpInputNoTMs(producer=producer, pad_unpad_params=pad_unpad_params)

        input_idx = self.get_consumers_producer_input_index(producer, consumer)

        op_input = OpInputTMs(
            prod_fullt_buf=self.add_var(
                f"{consumer.name}_prod_fullt_buf_{input_idx}"
            ),
            p2p_buf_size_mb=self.add_var(
                f"{consumer.name}_buf_size_mb_{input_idx}"
            ),
            row_major_stack=self.add_var(
                f"{consumer.name}_row_major_stack_{input_idx}"
            ),
            total_slice_factor=self.add_var(
                f"{consumer.name}_total_slice_factor_{input_idx}"
            ),
            total_stack_factor=self.add_var(
                f"{consumer.name}_total_stack_factor_{input_idx}"
            ),
            producer_block_pre_stack_ct=self.add_var(
                f"{consumer.name}_producer_block_pre_stack_ct_{input_idx}"
            ),
            producer_block_pre_stack_rt=self.add_var(
                f"{consumer.name}_producer_block_pre_stack_rt_{input_idx}"
            ),
            stacking_block_ct=self.add_var(
                f"{consumer.name}_stacking_block_ct_{input_idx}"
            ),
            stacking_block_rt=self.add_var(
                f"{consumer.name}_stacking_block_rt_{input_idx}"
            ),
            effective_hstack_factor=self.add_var(
                f"{consumer.name}_effective_hstack_factor_{input_idx}"
            ),
            effective_vstack_factor=self.add_var(
                f"{consumer.name}_effective_vstack_factor_{input_idx}"
            ),
            post_padding_stacking_block_rt=self.add_var(
                f"{consumer.name}_post_padding_stacking_block_rt_{input_idx}"
            ),
            post_padding_stacking_block_ct=self.add_var(
                f"{consumer.name}_post_padding_stacking_block_ct_{input_idx}"
            ),
            post_padding_effective_hstack_factor=self.add_var(
                f"{consumer.name}_post_padding_effective_hstack_factor_{input_idx}"
            ),
            post_padding_effective_vstack_factor=self.add_var(
                f"{consumer.name}_post_padding_effective_vstack_factor_{input_idx}"
            ),
            producer=producer,
            pad_unpad_params=pad_unpad_params,
            consumer_config=consumer_config,
            tms=[
                self.create_op_input_single_tm(producer, consumer, tm_idx, tm_config)
                for tm_idx, tm_config in enumerate(op_input_tm_configs)
            ]
        )

        op_input.initialize_variables(self.solver)
        if not self.op_input_supports_transpose(consumer, input_idx):
            for tm_info in op_input.get_tms():
                self.solver.add(tm_info.tm_type != TMS.transpose.value)

        if producer.is_queue():
            self.solver.add(
                op_input.prod_fullt_buf == 1,
                op_input.p2p_buf_size_mb == producer.buf_size_mb
            )

        return op_input

    def create_op_input_single_tm(self, producer: Node, consumer: Node, tm_idx: int, tm_config: NetlistTMConfiguration) -> TMInfo:
        """Create object representing a single TM on a p2p connection between producer and consumer.

        Parameters
        ----------
        producer:
            Producer node.
        consumer:
            Consumer node.
        tm_idx:
            Index of this TM in the list of all the TMs on this p2p connection. Used for z3 variable
            naming, so that we avoid duplicate names.
        tm_config:
            TM netlist configuration.
        """
        input_idx = self.get_consumers_producer_input_index(producer, consumer)
        tm_info = TMInfo(
            tm_type=self.add_var(
                f"{consumer.name}_tm_type_{input_idx}_{tm_idx}"
            ),
            tm_arg=self.add_var(
                f"{consumer.name}_tm_arg_{input_idx}_{tm_idx}"
            ),
            post_tm_rt=self.add_var(
                f"{consumer.name}_post_tm_rt_{input_idx}_{tm_idx}"
            ),
            post_tm_ct=self.add_var(
                f"{consumer.name}_post_tm_ct_{input_idx}_{tm_idx}"
            ),
            post_tm_t=self.add_var(
                f"{consumer.name}_post_tm_t_{input_idx}_{tm_idx}"
            ),
            c_bcast_factor=self.add_var(
                f"{consumer.name}_c_bcast_factor_{input_idx}_{tm_idx}"
            ),
            r_bcast_factor=self.add_var(
                f"{consumer.name}_r_bcast_factor_{input_idx}_{tm_idx}"
            ),
            t_bcast_factor=self.add_var(
                f"{consumer.name}_t_bcast_factor_{input_idx}_{tm_idx}"
            ),
            hslice_factor=self.add_var(
                f"{consumer.name}_hslice_factor_{input_idx}_{tm_idx}"
            ),
            vslice_factor=self.add_var(
                f"{consumer.name}_vslice_factor_{input_idx}_{tm_idx}"
            ),
            hstack_factor=self.add_var(
                f"{consumer.name}_hstack_factor_{input_idx}_{tm_idx}"
            ),
            vstack_factor=self.add_var(
                f"{consumer.name}_vstack_factor_{input_idx}_{tm_idx}"
            )
        )

        if tm_config.tm_type is not None:
            self.solver.add(tm_info.tm_type == tm_config.tm_type)

        if tm_config.tm_arg is not None:
            # tm_config.tm_arg is the z3 variable mapped to the template variable.
            self.solver.add(tm_info.tm_arg == tm_config.tm_arg)

        return tm_info

    def create_scheduled_op_input(
        self,
        producer: Node,
        consumer: Node,
        input_idx: int,
        op_input_tm_configs: List[NetlistTMConfiguration],
    ) -> OpInput:
        """Create op input for a scheduled op.

        Parameters
        ----------
        producer:
            Producer node.
        consumer:
            Consumer node.
        input_idx:
            Index of the consumer input.
        op_input_tm_configs:
            Configuration of OP input TMs.
        """
        if not op_input_tm_configs:
            return ScheduledOpInputNoTMs(producer)

        tms = [
            ScheduledOpTMInfo(
                tm_type=tm_config.tm_type,
                tm_arg=tm_config.tm_arg,
                post_tm_rt=self.add_var(f"{consumer.name}_post_tm_rt_{input_idx}_{tm_idx}"),
                post_tm_ct=self.add_var(f"{consumer.name}_post_tm_ct_{input_idx}_{tm_idx}"),
            )
            for tm_idx, tm_config in enumerate(op_input_tm_configs)
        ]

        return ScheduledOpInputTMs(producer, tms)

    def op_input_supports_transpose(self, op_node: Node, input_idx: int) -> bool:
        """Checks if the op node supports transpose TM on the given input index.

        Parameters
        ----------
        op_node:
            Op node.
        input_idx:
            Input index.
        """
        # Transpose is supported on matmul in1.
        if op_node.is_matmul_op() and input_idx == 1:
            return True

        # Transpose is supported on nop/datacopy op type.
        if op_node.is_nop():
            return True

        # In all other cases, transpose is not supported.
        return False

    def op_input_supports_padding(self, op_node: Node, input_idx: int) -> Tuple[bool, bool]:
        """Checks if the op node supports padding on the given input index along given dimension.
        Returned tuple is of format [supports_r_padding, supports_c_padding]

        Parameters
        ----------
        op_node:
            Op node.
        input_idx:
            Input index.
        """
        supports_r_padding = True
        supports_c_padding = True

        # TODO: Add check for fused ops if padding is needed there (if fused op input is actually an input of
        # matmul scheduled op).
        if op_node.is_matmul_op():
            if input_idx == 0:
                supports_c_padding = False
            elif input_idx == 1:
                supports_r_padding = False

        return (supports_r_padding, supports_c_padding)

    def read_queues(self) -> None:
        """
        Creates queue nodes from the template netlist with
        corresponding z3 variables for their attributes.
        """
        for q_name, q_dict in self.template_dict['queues'].items():
            # Read template queues and put them in dict.
            q_node = Node(q_name, Node.get_queue_type(q_name, q_dict))
            self.nodes[q_name] = q_node
            self.queues.append(q_node)

            if q_node.type in [NodeType.InputQueue, NodeType.OutputQueue]:
                # Used to constrain queue size.
                q_node.add_raw_attribute(SIZE_R_VAR_NAME)
                q_node.add_raw_attribute(SIZE_C_VAR_NAME)

            q_node.input = q_dict["input"]

            q_node.add_template_attribute("entries", q_dict["entries"])

            q_node.add_raw_attribute("buffers_size")

            q_node.add_constrained_attribute("grid_size_y", q_dict["grid_size"][0], self.solver)
            q_node.add_constrained_attribute("grid_size_x", q_dict["grid_size"][1], self.solver)

            q_node.add_template_attribute("t_dim", q_dict["t"])

            q_node.add_constrained_attribute("mb_m", q_dict["mblock"][0], self.solver)
            q_node.add_constrained_attribute("mb_n", q_dict["mblock"][1], self.solver)

            q_node.add_constrained_attribute("ub_r", q_dict["ublock"][0], self.solver)
            q_node.add_constrained_attribute("ub_c", q_dict["ublock"][1], self.solver)

            q_node.add_template_attribute("df", q_dict["df"])

            q_node.buf_size_mb = q_node.t_dim

            q_node.add_template_attribute("loc", q_dict["loc"])
            q_node.loc_template_str = q_dict[q_dict["loc"]][len(TEMPLATE_VAR_PREFIX):]

            # For output queues, it is constrained by the feeder op, so it will
            # be set automatically.
            ublock_order_netlist_field = q_dict.get("ublock_order", "r")
            q_node.add_raw_attribute("ublock_order")

            if TemplateNetlistTestBase.is_field_template_var(ublock_order_netlist_field):
                # Queue's micro block scan order is constrained by the feeder op
                # so we can't randomize it here.
                q_node.ublock_order_template_str = q_dict["ublock_order"][len(TEMPLATE_VAR_PREFIX):]
            else:
                # If we don't set the ublock scan order for queue explicitly, we set it to be
                # whatever is present in the netlist, if not present, default is r.
                self.solver.add(q_node.ublock_order == UblockOrder[ublock_order_netlist_field].value)

        self.read_prologues()

    def read_prologues(self) -> None:
        """
        Adds prologue attributes to the queue nodes with corresponding template
        keys from the netlist.
        """
        for program in self.template_dict["programs"]:
            program_name = next(iter(program))
            program_content = program[program_name]

            for program_instr in program_content:
                instr_key = next(iter(program_instr))

                if instr_key != 'execute':
                    continue

                graph_name = program_instr[instr_key]['graph_name']
                queues_settings = program_instr[instr_key].get('queue_settings', {})

                for q_name, q_settings in queues_settings.items():
                    self.nodes[q_name].add_template_attribute(
                        f"{graph_name}_prologue", q_settings['prologue']
                    )

    def get_input_queues(self) -> List[Node]:
        """Returns list of input queues."""
        return list(filter(lambda queue: queue.type == NodeType.InputQueue, self.queues))

    def get_output_queues(self) -> List[Node]:
        """Returns list of output queues."""
        return list(filter(lambda queue: queue.type == NodeType.OutputQueue, self.queues))

    def read_ops(self) -> None:
        """
        Creates OP nodes from the template netlist with
        corresponding z3 variables for their attributes. Parses all the nodes
        in order to be able to find the test nodes, i.e. nodes which have
        templated input TMs.
        """
        for g_name, g_dict in self.template_dict['graphs'].items():
            for op_name, op_params in g_dict.items():
                if not isinstance(op_params, dict):
                    continue
                op_type = TemplateNetlistTestBase.get_node_type_from_netlist(op_params)

                op = Node(op_name, op_type)
                self.nodes[op_name] = op
                self.ops.append(op)

                op.inputs = op_params["inputs"]
                op.op_type = op_params["type"]
                op.graph = g_name

                op.add_constrained_attribute("grid_loc_y", op_params["grid_loc"][0], self.solver, 0)
                op.add_constrained_attribute("grid_loc_x", op_params["grid_loc"][1], self.solver, 0)

                op.add_constrained_attribute("grid_size_y", op_params["grid_size"][0], self.solver)
                op.add_constrained_attribute("grid_size_x", op_params["grid_size"][1], self.solver)

                op.add_constrained_attribute("t_dim", op_params["t"], self.solver)

                op.add_constrained_attribute("mb_m", op_params["mblock"][0], self.solver)
                op.add_constrained_attribute("mb_n", op_params["mblock"][1], self.solver)

                op.add_constrained_attribute("ub_r", op_params["ublock"][0], self.solver)
                op.add_constrained_attribute("ub_c", op_params["ublock"][1], self.solver)

                for input_index in range(len(op.inputs)):
                    op.add_template_attribute(f"in{input_index}_df", op_params["in_df"][input_index])

                if "input_buf_min_size_tiles" in op_params:
                    for input_index in range(len(op.inputs)):
                        op.add_constrained_attribute(
                            f"input{input_index}_buf_min_size_tiles",
                            op_params["input_buf_min_size_tiles"][input_index],
                            self.solver,
                            0
                        )

                op.add_template_attribute("out_df", op_params["out_df"])
                op.add_template_attribute("intermed_df", op_params["intermed_df"])
                op.add_template_attribute("acc_df", op_params["acc_df"])

                op.add_template_attribute("buf_size_mb", op_params["buf_size_mb"])

                if op.type == NodeType.FusedOp:
                    op.fused_op_id = op_params["attributes"]["fused_op_id"]
                    self.fused_op_id_to_node[op.fused_op_id] = op

                if op.is_reduction():
                    op.add_constrained_attribute("m_k", op_params["attributes"]["m_k"], self.solver)
                    op.add_constrained_attribute("u_kt", op_params["attributes"]["u_kt"], self.solver)

                if op.is_matmul_op():
                    op.bias = op_params["attributes"].get("bias", False)

                if op.type == NodeType.ReduceOp:
                    op.reduce_dim = Dimension.create_from_string(op_params["attributes"]["dim"])

                ublock_order_netlist_field = op_params.get("ublock_order", "r")
                op.add_raw_attribute("ublock_order")

                op.is_output = False
                if "untilize_output" in op_params:
                    op.add_template_attribute("untilize", op_params["untilize_output"])
                else:
                    op.untilize = Int("untilize")

                op.gradient_op = op_params.get("gradient_op", False)

                # Set prologue attr for op just for the sake of easier memory
                # constraints.
                for gname in self.graphs:
                    setattr(op, f"{gname}_prologue", self.default_prologue)

                if TemplateNetlistTestBase.is_field_template_var(ublock_order_netlist_field):
                    op.ublock_order_template_str = ublock_order_netlist_field[len(TEMPLATE_VAR_PREFIX):]

                    if op.type == NodeType.MatmulOp:
                        self.solver.add(op.ublock_order == UblockOrder.r.value)
                    else:
                        self.solver.add(Or(
                            op.ublock_order == UblockOrder.r.value,
                            op.ublock_order == UblockOrder.c.value
                        ))
                else:
                    self.solver.add(op.ublock_order == UblockOrder[ublock_order_netlist_field].value)

                self.parse_op_tms(op, op_params)
                self.parse_op_pad_unpad(op, op_params)

        self.read_fused_op_schedules()
        self.find_output_ops()

    @abstractmethod
    def parse_op_tms(self, op_node: Node, op_params: Dict[str, any]) -> None:
        """
        Parse op TMs from netlist. Implement in subclasses.

        Parameters
        ----------
        op_node:
            Op node which is currently being created.
        op_params:
            Op params read from netlist template yaml.
        """
        pass

    def parse_op_pad_unpad(self, op_node: Node, op_params: Dict[str, any]) -> None:
        if op_node.name not in self.op_input_pad_unpad:
            self.op_input_pad_unpad[op_node.name] = {}

        for input_idx, input_name in enumerate(op_node.inputs):
            unpad_params = op_params.get(f"input_{input_idx}_unpad", {})
            pad_params = op_params.get(f"input_{input_idx}_pad", {})

            unpad_rt = TemplateNetlistTestBase.get_template_var_field(unpad_params, "rt", 0)
            unpad_ct = TemplateNetlistTestBase.get_template_var_field(unpad_params, "ct", 0)
            pad_rt = TemplateNetlistTestBase.get_template_var_field(pad_params, "rt", 0)
            pad_ct = TemplateNetlistTestBase.get_template_var_field(pad_params, "ct", 0)

            if unpad_rt:
                op_node.add_constrained_attribute(f"input_{input_idx}_unpad_rt", unpad_rt, self.solver, 0)
            if unpad_ct:
                op_node.add_constrained_attribute(f"input_{input_idx}_unpad_ct", unpad_ct, self.solver, 0)
            if pad_rt:
                op_node.add_constrained_attribute(f"input_{input_idx}_pad_rt", pad_rt, self.solver, 0)
            if pad_ct:
                op_node.add_constrained_attribute(f"input_{input_idx}_pad_ct", pad_ct, self.solver, 0)

            self.op_input_pad_unpad[op_node.name][input_name] = PadUnpadParams(
                pad_ct=getattr(op_node, f"input_{input_idx}_pad_ct", pad_ct),
                pad_rt=getattr(op_node, f"input_{input_idx}_pad_rt", pad_rt),
                unpad_rt=getattr(op_node, f"input_{input_idx}_unpad_rt", unpad_rt),
                unpad_ct=getattr(op_node, f"input_{input_idx}_unpad_ct", unpad_ct)
            )

    def find_output_ops(self) -> None:
        """
        Sets `is_output` attribute to True for OP nodes that are output into
        some output queue.
        """
        for queue in self.get_output_queues():
            output_op = self.nodes[queue.input]
            output_op.is_output = True

    def read_fused_op_schedules(self) -> None:
        """Reads fused op schedules and parses scheduled ops."""
        if "fused_ops" not in self.template_dict:
            return

        for fused_op_id, fused_op_info in self.template_dict["fused_ops"].items():
            op_node = self.fused_op_id_to_node[fused_op_id]
            op_node.scheduled_ops = []
            op_node.n_intermediates = fused_op_info["intermediates"]

            self.fused_op_input_to_subop_consumer[op_node.name] = {}

            # interediate buffer -> scheduled op mapping.
            # Enables the reuse of intermediate buffers/dest register.
            # Required to constrain the shapes throughout the scheduled ops.
            interm_buffer_to_subop: Dict[str, str] = {}

            op_node.has_reciprocal_scheduled_op = False
            op_node.has_reduce_scheduled_op = False
            for schedules in fused_op_info["schedules"]:
                for schedule in schedules:
                    for scheduled_op_name, scheduled_op_info in schedule.items():
                        scheduled_op = self.read_fused_op_scheduled_op(
                            op_node, scheduled_op_name, scheduled_op_info, interm_buffer_to_subop
                        )
                        op_node.scheduled_ops.append(scheduled_op)

    def read_fused_op_scheduled_op(
        self,
        fused_op: Node,
        op_name: str,
        op_params: Dict[str, any],
        interm_buffer_to_subop: Dict[str, str]
    ) -> Node:
        """Parses a single scheduled op from netlist template.

        Parameters
        ----------
        fused_op:
            'Parent' fused op node of the scheduled op node which will be created.
        op_name:
            Name of the scheduled op.
        op_params:
            Dict representing template netlist params of the scheduled op.
        interm_buffer_to_subop:
            Mapping between intermediate buffer name and the scheduled op which writes to that
            intermediate buffer. Used to rewise inputs/output of the scheduled ops.
        """
        scheduled_op = Node(
            name=TemplateNetlistTestBase.get_fused_op_scheduled_op_name(fused_op.name, op_name),
            type=TemplateNetlistTestBase.get_node_type_from_netlist(op_params, is_scheduled=True),
        )
        self.nodes[scheduled_op.name] = scheduled_op
        scheduled_op.inputs = op_params["inputs"]
        scheduled_op.t_dim = fused_op.t_dim
        scheduled_op.buf_size_mb = fused_op.buf_size_mb
        scheduled_op.op_type = op_params["type"]

        scheduled_op.grid_size_x = fused_op.grid_size_x
        scheduled_op.grid_size_y = fused_op.grid_size_y

        scheduled_op.ublock_order = fused_op.ublock_order

        scheduled_op.acc_df = fused_op.acc_df

        scheduled_op.add_constrained_attribute("mb_m", op_params["mblock"][0], self.solver)
        scheduled_op.add_constrained_attribute("mb_n", op_params["mblock"][1], self.solver)

        scheduled_op.add_constrained_attribute("ub_r", op_params["ublock"][0], self.solver)
        scheduled_op.add_constrained_attribute("ub_c", op_params["ublock"][1], self.solver)

        scheduled_op.bias = False

        if scheduled_op.is_reduction():
            scheduled_op.add_constrained_attribute(
                "m_k", op_params["attributes"]["m_k"], self.solver
            )
            scheduled_op.add_constrained_attribute(
                "u_kt", op_params["attributes"]["u_kt"], self.solver
            )

        if scheduled_op.op_type == "reciprocal":
            fused_op.has_reciprocal_scheduled_op = True
        if scheduled_op.type == NodeType.ScheduledReduceOp:
            fused_op.has_reduce_scheduled_op = True


        # Remap inputs.
        for input_idx, input_name in enumerate(scheduled_op.inputs):
            if TemplateNetlistTestBase.is_temporary_buffer(input_name):
                scheduled_op.inputs[input_idx] = interm_buffer_to_subop[input_name]
            else:
                fused_op_input_idx = int(input_name.lstrip("input"))
                scheduled_op.inputs[input_idx] = fused_op.inputs[fused_op_input_idx]

        # Remap output.
        output_buffer = op_params["output"]
        if TemplateNetlistTestBase.is_temporary_buffer(output_buffer):
            interm_buffer_to_subop[output_buffer] = scheduled_op.name

        scheduled_op.fused_op = fused_op

        if scheduled_op.type == NodeType.ScheduledReduceOp:
            scheduled_op.reduce_dim = Dimension.create_from_string(op_params["attributes"]["dim"])

        for subop_input in op_params["inputs"]:
            self.fused_op_input_to_subop_consumer[fused_op.name][subop_input] = scheduled_op

        self.parse_op_tms(scheduled_op, op_params)

        return scheduled_op

    def constraint_model(self) -> Dict[str, z3.Var]:
        """Defines all the constraints and adds them to the solver.

        Returns
        -------
        Dictionary of solver variables, var_name: z3_var.
        """
        self.read_test_args()
        self.generate_graph()
        self.check_graph_validity()
        self.global_constraints()
        self.constrain_queues()
        self.constrain_ops()
        self.constrain_point_to_point_connections()
        self.constrain_data_format()
        self.additional_constraints()

        for _, node in self.nodes.items():
            self.svars.update(node.vars)

        return self.svars

    def read_test_args(self) -> None:
        """
        Reads test arguments from the template and generates corresponding Z3 variables.
        """
        test_config = self.template_dict["test-config"]["test-args"]
        self.microbatch_count = self.add_var(
            test_config["microbatch_count"][len(TEMPLATE_VAR_PREFIX):]
        )
        self.microbatch_size = self.add_var(
            test_config["microbatch_size"][len(TEMPLATE_VAR_PREFIX):]
        )
        self.minibatch_count = self.add_var(
            test_config["minibatch_count"][len(TEMPLATE_VAR_PREFIX):]
        )

        self.input_count = self.microbatch_size

    def read_input_counts(self) -> None:
        """Creates dict mapping graph name to input count variable of that graph."""
        self.input_count_map = {}
        for gname in self.graphs:
            graph_input_count_template = self.template_dict["graphs"][gname]["input_count"]
            self.input_count_map[gname] = self.add_var(
                graph_input_count_template[len(TEMPLATE_VAR_PREFIX):]
            )

    @abstractmethod
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        """Exports sweep vars which are common for all multi TM tests - TM types
        and TM arguments.

        Returns
        -------
        List of exported sweep variables.
        """
        pass

    def export_coverage_vars(self) -> List[str]:
        """
        Export var names for which solver will guarantee that at least one var has different value.
        Override in test modules to export specific var names.
        """
        return []

    def global_constraints(self) -> None:
        """
        Defines global graph constraints regarding microbatch count,
        microbatch size and minibatch count.
        """
        self.solver.add(self.microbatch_count > 0)
        self.solver.add(self.microbatch_size > 0)
        self.solver.add(self.minibatch_count > 0)

        self.solver.add(self.default_prologue == Prologue.false.value)

        for gname, input_count in self.input_count_map.items():
            if "opt" in gname:
                self.solver.add(input_count == 1)
            else:
                self.solver.add(input_count == self.input_count)

    def constrain_point_to_point_connections(self) -> None:
        """Applies constraints regarding TMs which are located in each of
        the producer -> test op connections.
        """
        for consumer_node in self.ops:
            for producer_name in consumer_node.inputs:
                producer_node = self.nodes[producer_name]
                self.constrain_point_to_point_connection(producer_node, consumer_node)
                if not producer_node.is_queue() and producer_node.graph != consumer_node.graph:
                    self.constrain_multi_chip_connection(producer_node, consumer_node)
                self.constrain_padding_on_connection(producer_node, consumer_node)

            if consumer_node.type == NodeType.FusedOp:
                # TODO: Eventually add scheduled ops to self.ops list and avoid
                # special treatment of the scheduled ops where it is not needed.
                for scheduled_consumer_op in consumer_node.scheduled_ops:
                    for producer_name in scheduled_consumer_op.inputs:
                        producer_node = self.nodes[producer_name]
                        self.constrain_point_to_point_connection(producer_node, scheduled_consumer_op)

    def constrain_point_to_point_connection(self, producer: Node, consumer: Node) -> None:
        """Constraints single producer -> consumer point-to-point connections.

        Parameters
        ----------
        producer:
            Producer node.
        consumer:
            Consumer node.
        """
        op_input = self.op_input_tm_map[consumer.name][producer.name]
        op_input.constrain(self.solver)

    def constrain_padding_on_connection(self, producer: Node, consumer: Node) -> None:
        """
        Constraints pad/unpad values on a given producer -> consumer point-to-point connection.
        Constraints are as follows - we can't pad and unpad more than a microblock worth of tiles along
        the specified dimension, as that would result in cores which are only processing padding values.

        Parameters
        ----------
        producer:
            Producer node.
        consumer:
            Consumer node.
        """
        pad_unpad_params = self.op_input_pad_unpad[consumer.name][producer.name]

        self.solver.add(pad_unpad_params.unpad_rt < producer.mb_m * producer.ub_r)
        self.solver.add(pad_unpad_params.unpad_ct < producer.mb_n * producer.ub_c)

        self.solver.add(pad_unpad_params.pad_rt < consumer.mb_m * consumer.ub_r)
        self.solver.add(pad_unpad_params.pad_ct < consumer.mb_n * consumer.ub_c)

    def constrain_multi_chip_connection(self, producer: Node, consumer: Node) -> None:
        """
        Constraints ethernet streams usage for a multi chip connection between producer and consumer.

        Parameters
        ----------
        producer:
            Producer node.
        consumer:
            Consumer node.
        """
        # If the architecture does not support communication over ethernet, then there is nothing to constrain.
        if self.arch.max_ethernet_links <= 0:
            return

        input_idx = self.get_consumers_producer_input_index(producer, consumer)
        ethernet_links_usage = self.get_op_ethernet_links_usage(consumer, input_idx)
        self.solver.add(ethernet_links_usage <= self.arch.max_ethernet_links)

    def constrain_ops(self) -> None:
        """Constraints all the OPs in the graph.
        """
        self.constrain_op_placement()
        for op_node in self.ops:
            # OP fits on a grid, ublock fits in dest register, etc.
            self.define_common_op_constraints(op_node)

            # Add OP type specific constraints.
            self.constrain_op_node(op_node)

    def default_op_constraints_for_op_type(self, op_node: Node) -> None:
        """Apply default op constraints for op node given op node's type. Defaults constraints
        include constraining input dimensions and L1 usage.

        Parameters
        ----------
        op_node:
            Op node.
        """
        # Constrain OP input dims.
        self.constrain_op_input_dims(op_node)

        # Constrain OP L1 usage.
        self.constrain_op_l1_usage(op_node)

    def constrain_op_placement(self) -> None:
        """Constraints ops such that no two OPs are placed on the same core
        in the grid.
        """
        # No two ops can overlap with each other.
        for i, op_i in enumerate(self.ops):
            for j, op_j in enumerate(self.ops):
                if i >= j or op_i.graph != op_j.graph:
                    continue
                self.solver.add(
                    Or(
                        op_i.grid_loc_x + op_i.grid_size_x <= op_j.grid_loc_x,
                        op_j.grid_loc_x + op_j.grid_size_x <= op_i.grid_loc_x,
                        op_i.grid_loc_y + op_i.grid_size_y <= op_j.grid_loc_y,
                        op_j.grid_loc_y + op_j.grid_size_y <= op_i.grid_loc_y
                    )
                )

    def constrain_dram_queues_number(self, op: Node, queue_inputs: List[Node]):
        """Constrains grid sizes of dram queues.

        Parameters
        ----------
        op: Node
            Node corresponding to an operation.
        queue_inputs: list of Node
            List of nodes corresponding to dram queues that are inputs
            to the op.
        """
        # This is not a totally precise constraint regarding dram queues. It
        # roughly estimates number of needed dram queues and discards most of
        # bad solutions, however there is more precise validation check done in
        # valid_config_callback function. This is done to avoid very complex
        # constraint affecting solver's runtime too much.
        self.solver.add(
            Sum([queue.get_grid_size_var() for queue in queue_inputs]) <=
            op.get_grid_size_var() *
            self.arch.max_num_queue_buffers_accessed)

    def constrain_num_queue_buffers_accessed(self, op: Node) -> None:
        """
        Helper function for check_resource_usage which adds num of queue buffers
        accessed constraints for this op to the solver (which are too complex and
        thus added only when needed).

        See docstring for get_num_queue_buffers_accessed.
        """
        # Aux accumulator variable.
        total_num_queue_buffers_accessed_z3 = 0

        queues_feeding_this_op = list(filter(
            lambda q: q.name in op.inputs,
            self.queues
        ))

        # Accumulate num of accessed input buffers for each input queue.
        for queue in queues_feeding_this_op:
            # queue and op Nodes exported as ResourceConstrNodes for the sake
            # of common API. Input index is not relevant here since it is not
            # used in get_num_buffers_accessed_along_dim_z3_expr calculations.
            queue_node = self.export_producer_queue_for_resource_constraints_from_z3(
                node=queue
            )

            op_node = self.export_consumer_op_for_resource_constraints_from_z3(
                node=op,
                input_index=self.get_consumers_producer_input_index(queue, op)
            )

            num_buffers_x_z3 = get_num_buffers_accessed_along_dim_z3_expr(
                op_node, queue_node, Dimension.Horizontal, self.solver
            )
            num_buffers_y_z3 = get_num_buffers_accessed_along_dim_z3_expr(
                op_node, queue_node, Dimension.Vertical, self.solver
            )

            total_num_queue_buffers_accessed_z3 += num_buffers_x_z3 * num_buffers_y_z3

        # Every core of op maps to exactly one buffer of output queue,
        # therefore we need to include op2queue fanout in total calculation.
        op2queues_fanout = self.get_op2queues_resource_usage(op, self.queues)
        total_num_queue_buffers_accessed_z3 += op2queues_fanout

        op.add_raw_attribute("total_num_queue_buffers_accessed")

        self.solver.add(
            op.total_num_queue_buffers_accessed == total_num_queue_buffers_accessed_z3
        )
        self.solver.add(
            op.total_num_queue_buffers_accessed > 0,
            (op.total_num_queue_buffers_accessed <=
             self.arch.max_num_queue_buffers_accessed)
        )

    def define_common_op_constraints(self, op_node: Node) -> None:
        """Defines common constraints for all the OP types.

        Parameters
        ----------
        op_node:
            OP node for which to define constraints.
        """
        # Op fits on a grid.
        self.solver.add(op_node.grid_loc_x + op_node.grid_size_x <= self.arch.grid_size_x)
        self.solver.add(op_node.grid_loc_y + op_node.grid_size_y <= self.arch.grid_size_y)

        # Constrain maximum number of active DRAM queues
        self.constrain_dram_queues_number(
            op_node,
            [self.nodes[q_name] for q_name in op_node.inputs if self.nodes[q_name].is_queue()]
        )

        self.constrain_ublock_size(op_node)

        self.constrain_op_inputs_forking_factor(op_node)

        self.constrain_op_inputs_data_formats(op_node)

        self.constrain_op_untilize(op_node)

        self.constrain_op_input_buf_min_size_tiles(op_node)

        self.constrain_buf_size_mb(op_node)

        self.constrain_op_padding(op_node)

    def constrain_ublock_size(self, op_node: Node) -> None:
        """
        Constrains size of ublock to fit the dest register.

        Parameters
        ----------
        op_node:
            Op node for which to add ublock size constraints.
        """

        # Special case for WHB0: if dest is in FP32, then ublock cannot have
        # more than 4 tiles in dest. If dest is not FP32, then ublock can
        # have up to 8 tiles in dest.
        # In all other cases we can place 2 ublocks of tiles in dest register.
        self.solver.add(
            If(
                And(
                    isinstance(self.arch, WormholeB0Architecture),
                    op_node.acc_df == DataFormat.Float32.value
                ),
                op_node.ub_r * op_node.ub_c <= self.arch.max_tiles_in_dest / 4,
                op_node.ub_r * op_node.ub_c <= self.arch.max_tiles_in_dest / 2
            )
        )

    def constrain_op_input_buf_min_size_tiles(self, op_node: Node) -> None:
        """
        Constrains input_buf_min_size_tiles attribute of this op for all it's inputs.

        This attribute basically dictates buffering factor for input ublocks. It
        can be as big as needed, to compensate latency of op inputs which take
        longer to calculate, instead of having intermediate nop node. For
        practical purposes, it is limited to max_input_buf_size_tiles here. Also,
        divisibility with minimum number of tiles op has to have in order to make
        progress is forced. Generally, this number is a single ublock for
        elementwise and reduce ops and a strip of ublocks along the outer
        dimension for matmul. We don't really need to force this divisibility
        since net2pipe rounds up input_buf_min_size_tiles_var to nearest
        divisible number, since L1 constraints are easier this way, otherwise we
        would have to calculate the number net2pipe rounds up to, and use that
        number in L1 constraints for ops.
        """

        for input_idx in range(len(op_node.inputs)):
            input_buf_min_size_tiles_var = op_node.get_input_buf_min_size_tiles_var(input_idx)

            if input_buf_min_size_tiles_var is None or input_buf_min_size_tiles_var == 0:
                continue

            # Global constraints to prevent very large input buffering.
            self.solver.add(
                input_buf_min_size_tiles_var <= self.arch.max_input_buf_size_tiles
            )

            input_buffer_size_tiles = self.get_op_input_buffer_size_tiles(op_node, input_idx)
            self.solver.add(input_buf_min_size_tiles_var % input_buffer_size_tiles == 0)

    def constrain_op_inputs_data_formats(self, op_node: Node) -> None:
        """
        For each of the inputs, constraint it's out df to be the same as op_node's in_df.

        Parameters
        ----------
        op_node:
            Op node.
        """
        for input_idx, input_name in enumerate(op_node.inputs):
            in_node = self.nodes[input_name]
            self.solver.add(
                in_node.get_data_format_var() == op_node.get_input_data_format_var(input_idx)
            )

    def constrain_op_inputs_forking_factor(self, op_node: Node) -> None:
        """
        For each of the inputs, constrain forking factor.

        Parameters
        ----------
        op_node:
            Op node.
        """
        for input_name in op_node.inputs:
            in_node = self.nodes[input_name]

            self.solver.add(
                in_node.grid_size_x * in_node.grid_size_y * 16 >= (
                    op_node.grid_size_x * op_node.grid_size_y
                )
            )

    def constrain_op_untilize(self, op_node: Node) -> None:
        """
        Constrain op's untilize attribute to be false if op is not an output op and randomize it
        if the op is output op.

        Parameters
        ----------
        op_node:
            Op node.
        """
        if op_node.is_output:
            self.solver.add(
                op_node.untilize >= Untilize.false.value,
                op_node.untilize <= Untilize.true.value
            )
            self.solver.add(Implies(
                op_node.out_df < DataFormat.Float16_b.value,
                op_node.untilize == Untilize.false.value
            ))
        else:
            self.solver.add(op_node.untilize == Untilize.false.value)

    def constrain_buf_size_mb(self, op_node: Node) -> None:
        """Defines constraints for OP's buffer size.

        Parameters
        ----------
        op_node:
            OP node for which to define constraints.
        """
        self.solver.add(op_node.buf_size_mb > 0)

        self.solver.add(Or(op_node.buf_size_mb % 2 == 0, op_node.buf_size_mb == 1))
        self.solver.add((self.microbatch_size * op_node.t_dim) % op_node.buf_size_mb == 0)

        # Ensure divisibility between single_buf_size_mb and t in either way.
        self.solver.add(
            If(
                op_node.buf_size_mb == 1,
                divisible_either_direction(op_node.buf_size_mb, op_node.t_dim),
                divisible_either_direction(
                    op_node.buf_size_mb / 2, op_node.t_dim
                )
            )
        )

    def constrain_op_padding(self, op_node: Node) -> None:
        """Defines constraints for padding for each input of a node along R and C dimension.

        Parameters
        ----------
        op_node:
            OP node for which to define constraints.
        """
        for input_idx, input_name in enumerate(op_node.inputs):
            in_pad_unpad = self.op_input_pad_unpad[op_node.name][input_name]
            supports_r_padding, supports_c_padding = self.op_input_supports_padding(op_node, input_idx)
            if not supports_r_padding:
                self.solver.add(in_pad_unpad.pad_rt == 0)
            if not supports_c_padding:
                self.solver.add(in_pad_unpad.pad_ct == 0)

    def get_op_input_buffer_size_bytes(self, op_node: Node, input_idx: int) -> z3.Var:
        """
        Return size of the op's input buffer for the input on a given index in bytes.

        Parameters
        ----------
        op_node:
            Op node.
        input_idx:
            Input index.
        """
        in_node = self.nodes[op_node.inputs[input_idx]]

        kernel_input_buffer_size_tiles = 2 * self.get_op_input_buffer_size_tiles(op_node, input_idx)
        input_buffer_min_size_tiles = op_node.get_input_buf_min_size_tiles_var(input_idx)

        op_input_buffer_size_tiles = z3_max(
            kernel_input_buffer_size_tiles, input_buffer_min_size_tiles
        )

        return op_input_buffer_size_tiles * in_node.get_tile_size_var()

    def constrain_op_l1_usage(self, op_node: Node) -> None:
        """Constraints L1 usage for a given op node such that all input, prologue, intermediate
        and outputs buffer can fit into available L1 space.

        Parameters
        ----------
        op_node:
            Op node.
        """
        input_buffers_required_space = Sum([
            self.get_op_input_buffer_size_bytes(op_node, input_idx)
            for input_idx in range(len(op_node.inputs))
        ])

        intermed_buffers_required_space = (
            self.get_num_intermediate_buffers(op_node) * self.get_intermediate_buffer_size_bytes(op_node)
        )

        prologue_buffers_required_space = Sum([
            self.get_op_prologue_buffers_size_bytes(op_node, input_idx)
            for input_idx in range(len(op_node.inputs))
        ])

        output_buffer_required_space = self.get_op_output_buffer_size_bytes(op_node)

        self.solver.add(
            self.arch.get_max_l1_mem_buffer_size() >= (
                input_buffers_required_space + intermed_buffers_required_space +\
                    prologue_buffers_required_space + output_buffer_required_space
            )
        )

    def constrain_eltwise_op(self, op_node: Node) -> None:
        """Defines constraints which are specific to eltiwse OPs, i.e. unary and binary operators.
        Override in subclass to add additional constraints.

        Parameters
        ----------
        op_node:
            Op node for which to define constraints.
        """
        self.default_op_constraints_for_op_type(op_node)

    def constrain_eltwise_op_input_dims(self, op_node: Node) -> None:
        """Defines constraints for eltwise op's input tensors dimensions.

        Parameters
        ----------
        op_node:
            Eltwise op node.
        """
        for input_idx in range(len(op_node.inputs)):
            self.constrain_op_input_along_dim(op_node, input_idx, Dimension.Horizontal)
            self.constrain_op_input_along_dim(op_node, input_idx, Dimension.Vertical)

    def get_op_output_buffer_size_bytes(self, op_node: Node) -> z3.Var:
        """Returns output buffer size for a given op node.

        Parameters
        ----------
        op_node:
            Op node.
        """
        mb_m = If(op_node.untilize == Untilize.true.value, 1, op_node.mb_m)

        return op_node.get_tile_size_var() * mb_m * op_node.ub_r * \
            op_node.mb_n * op_node.ub_c * op_node.buf_size_mb

    def get_op_prologue_buffers_size_bytes(self, op_node: Node, input_idx: int) -> z3.Var:
        """Returns required buffer size for prologue read for input on a given index.

        Lets enumerate mblocks in queue from 1..5 in first row, 6..10 in second,
        11..15 in third row. The algorithm distributes mblocks on cores like:

        ----------------------
        | 1  3  5  |   2  4  |
        | 11 13 15 |   12 14 |
        ----------------------
        | 6 8 10   |   7 9   |
        |          |         |
        ----------------------

        It can be concluded that mblocks are not uniformly distributed across
        cores, and in worst case, one core can buffer

        ceil(queue.grid_size_x / op.grid_size_x) *
        ceil(queue.grid_size_y / op.grid_size_y)

        Parameters
        ----------
        op_node:
            Op node.
        input_idx:
            Input index.
        """
        in_node = self.nodes[op_node.inputs[input_idx]]

        return If(
            in_node.get_prologue_var(op_node.graph) == Prologue.true.value,
            ((in_node.grid_size_x + op_node.grid_size_x - 1) / op_node.grid_size_x) *
            ((in_node.grid_size_y + op_node.grid_size_y - 1) / op_node.grid_size_y) *
            in_node.get_macro_block_t_size_var() * in_node.get_tile_size_var(),
            0
        )

    def get_eltwise_op_input_buffer_size_tiles(self, op_node: Node, input_idx) -> z3.Var:
        """Returns input buffer size of an input on a given input index for a given eltwise op. The
        size of the buffer is return in tiles (with on double buffering).

        Parameters
        ----------
        op_node:
            Eltwise op node.
        input_idx:
            Input index, ignored here, since the buffer size for both in0 and in1 are the same.
        """
        return op_node.ub_r * op_node.ub_c

    def constrain_op_input_along_dim(self, op_node: Node, input_idx: int, dim: Dimension) -> None:
        """Constraints input to have the same size along a given dimension as the consumer op.

        Parameters
        ----------
        op_node:
            Consumer op node.
        input_idx:
            Input index.
        dim:
            Dimension.
        """
        input_dims = self.get_op_inputs_tensor_dims(op_node, input_idx)
        size_along_dim = input_dims.rt if dim == Dimension.Vertical else input_dims.ct

        self.solver.add(
            size_along_dim == op_node.get_tiles_along_dim(dim),
            input_dims.t == op_node.t_dim
        )

    def constrain_matmul_op(self, op_node: Node) -> None:
        """Defines constraints which are specific for matmul OP. Override in subclass to add
        additional constraints.

        Parameters
        ----------
        op_node:
            OP node for which to define constraints.
        """
        self.default_op_constraints_for_op_type(op_node)

    def constrain_matmul_op_input_dims(self, op_node: Node) -> None:
        """Defines constraints for matmul op's input tensors' dimensions.

        Paramters
        ---------
        op_node:
            Matmul op node.
        """
        # Constrain outer dimension.
        self.constrain_op_input_along_dim(op_node, 0, Dimension.Vertical)
        self.constrain_op_input_along_dim(op_node, 1, Dimension.Horizontal)

        # Constrain inner dimension.
        self.constrain_matmul_op_inner_dimension(op_node)

        if op_node.bias:
            self.constrain_op_input_along_dim(op_node, 2, Dimension.Horizontal)
            self.constrain_op_input_along_dim(op_node, 2, Dimension.Vertical)

            # Bias must be column matrix before applying TMs.
            bias_input = self.op_input_tm_map[op_node.name][op_node.inputs[2]]
            self.solver.add(bias_input.get_unpadded_input_rt() == 1)

    def constrain_matmul_op_inner_dimension(self, op_node: Node) -> None:
        """Defines constraints for matmul op which state that inner dimension of the inputs
        must match.

        Parameters
        ----------
        op_node:
            Matmul op node.
        """
        in0_dims = self.get_op_inputs_tensor_dims(op_node, 0)
        in1_dims = self.get_op_inputs_tensor_dims(op_node, 1)

        self.solver.add(in0_dims.ct == in1_dims.rt)
        self.solver.add(in0_dims.ct == op_node.m_k * op_node.u_kt)

    def get_matmul_op_input_buffer_size_tiles(self, op_node: Node, input_idx: int) -> z3.Var:
        """Returns input buffer size of matmul op on a given input index. The size of the buffer is
        returned in tiles, with no double buffering.

        Parameters
        ----------
        op_node:
            Matmul op node.
        input_idx:
            Input index.
        """
        if input_idx == 0:
            return op_node.ub_r * op_node.u_kt * op_node.mb_m
        elif input_idx == 1:
            return op_node.ub_c * op_node.u_kt * op_node.mb_n
        elif input_idx == 2:
            assert op_node.bias, "Matmul op has to have bias set to True in order to have 3 inputs"
            return op_node.ub_r * op_node.ub_c

    def get_scheduled_matmul_op_input_buffer_size_tiles(self, op_node: Node, input_idx: int) -> z3.Var:
        """Returns input buffer size of scheduled matmul op (matmul inside of fused op) on a
        given input index. The size of the buffer is returned in tiles, with no double buffering.

        Parameters
        ----------
        op_node:
            Scheduled matmul op node.
        input_idx:
            Input index.
        """
        if input_idx == 0:
            return self.get_matmul_op_input_buffer_size_tiles(op_node, 0)

        return op_node.ub_c * op_node.m_k * op_node.u_kt * op_node.mb_n

    def constrain_scheduled_matmul_op_input_dims(self, op_node: Node) -> None:
        """Defines constraints for scheduled matmul op which state that inner dimension of the
        inputs must match and that in1 must be a row matrix. Moreover, reblocking is not supported.

        Parameters
        ----------
        op_node:
            Scheduled matmul op node.
        """
        self.constrain_matmul_op_input_dims(op_node)

        # Scheduled matmul input 1 has to be a column matrix to fit the input buffer.
        in1_dims = self.get_op_inputs_tensor_dims(op_node, 1)
        self.solver.add(in1_dims.ct == 1)

        input_0_op = self.nodes[op_node.inputs[0]]
        input_1_op = self.nodes[op_node.inputs[1]]

        # Scheduled matmul:
        #   - has to have input and output dimensions matching m_k and u_kt
        #   - doesn't support reblocking.
        self.solver.add(input_0_op.mb_n == op_node.m_k)
        self.solver.add(input_0_op.ub_c == op_node.u_kt)
        self.solver.add(input_0_op.mb_m == op_node.mb_m)
        self.solver.add(input_0_op.ub_r == op_node.ub_r)

        self.solver.add(input_1_op.mb_m == op_node.m_k)
        self.solver.add(input_1_op.ub_r == op_node.u_kt)
        self.solver.add(input_1_op.mb_n == op_node.mb_n)
        self.solver.add(input_1_op.ub_c == op_node.ub_c)

    def constrain_scheduled_reduce_op_input_dims(self, op_node: Node) -> None:
        """Defines constraints for scheduled reduce op which state that reduced input dimension
        must match m_k and u_kt attributes. Moreover, reblocking is not supported.

        Parameters
        ----------
        op_node:
            Scheduled reduce op node.
        """
        self.constrain_reduce_op_input_dims(op_node)

        input_0 = self.nodes[op_node.inputs[0]]

        # Scheduled reduce:
        #   - has to have input and output dimensions matching m_k and u_kt
        #   - doesn't support reblocking.
        if op_node.reduce_dim == Dimension.Vertical:
            self.solver.add(input_0.mb_m == op_node.m_k)
            self.solver.add(input_0.ub_r == op_node.u_kt)
            self.solver.add(input_0.mb_n == op_node.mb_n)
            self.solver.add(input_0.ub_c == op_node.ub_c)
        elif op_node.reduce_dim == Dimension.Horizontal:
            self.solver.add(input_0.mb_n == op_node.m_k)
            self.solver.add(input_0.ub_c == op_node.u_kt)
            self.solver.add(input_0.mb_m == op_node.mb_m)
            self.solver.add(input_0.ub_r == op_node.ub_r)

    def constrain_reduce_op(self, op_node: Node) -> None:
        """
        Defines constraints which are specific to reduce ops. Override in subclass to add
        additional constraints.

        Parameters
        ----------
        op_node:
            Reduce op node.
        """
        self.default_op_constraints_for_op_type(op_node)

    def get_reduce_op_input_buffer_size_tiles(self, op_node: Node, input_idx: int) -> z3.Var:
        """
        Returns input buffer size of an input on the input index of the given reduce op.The size
        of the buffer is returned in tiles, with no double buffering.

        Parameters
        ----------
        op_node:
            Reduce op node.
        input_idx:
            Input index.
        """
        if op_node.reduce_dim == Dimension.Vertical:
            return op_node.ub_c * op_node.u_kt
        else:
            return op_node.ub_r * op_node.u_kt

    def constrain_reduce_op_input_dims(self, op_node: Node) -> None:
        """
        Constraints reduce op's input tensors' dimensions.

        Parameters
        ----------
        op_node:
            Reduce op node.
        """
        in_dims = self.get_op_inputs_tensor_dims(op_node, 0)
        reduce_inner_dimension = op_node.m_k * op_node.u_kt

        self.constrain_op_input_along_dim(op_node, 0, Dimension.opposite(op_node.reduce_dim))

        if op_node.reduce_dim == Dimension.Vertical:
            self.solver.add(
                op_node.get_vertical_size_var() == 1, in_dims.rt == reduce_inner_dimension
            )
        elif op_node.reduce_dim == Dimension.Horizontal:
            self.solver.add(
                op_node.get_horizontal_size_var() == 1, in_dims.ct == reduce_inner_dimension
            )

    def constrain_fused_op(self, op_node: Node) -> None:
        """Defines constraints which are specific to fused ops. Override in subclass to add
        additional constraints.

        Parameters
        ----------
        op_node:
            Op node for which to define constraints.
        """
        self.default_op_constraints_for_op_type(op_node)

        for scheduled_op in op_node.scheduled_ops:
            self.constrain_ublock_size(scheduled_op)

    def constrain_fused_op_input_dims(self, op_node: Node) -> None:
        """Constraints fused op's input tensors' dimensions.

        Parameters
        ----------
        op_node:
            Fused op node.
        """
        # Constraint dims throughout scheduled ops.
        for scheduled_op in op_node.scheduled_ops:
            self.constrain_op_input_dims(scheduled_op)

        # Constrain dims of output scheduled op and fused op.
        output_scheduled_op = op_node.scheduled_ops[-1]
        self.solver.add(
            op_node.mb_m == output_scheduled_op.mb_m,
            op_node.mb_n == output_scheduled_op.mb_n,
            op_node.ub_r == output_scheduled_op.ub_r,
            op_node.ub_c == output_scheduled_op.ub_c
        )

    def get_fused_op_input_buffer_size_tiles(self, op_node: Node, input_idx: int) -> z3.Var:
        """Returns input buffer size of an input on the input index of the given fused op.The size
        of the buffer is returned in tiles, with no double buffering.

        Parameters
        ----------
        op_node:
            Fused op node.
        input_idx:
            Input index.
        """
        # Remap the fused op input to a scheduled op input.
        fused_op_input_name = op_node.inputs[input_idx]
        fused_op_input_node = self.nodes[fused_op_input_name]
        scheduled_op = self.get_fused_op_input_consumer_op(op_node, fused_op_input_name)
        scheduled_op_input_index = self.get_consumers_producer_input_index(
            fused_op_input_node, scheduled_op
        )

        return self.get_op_input_buffer_size_tiles(scheduled_op, scheduled_op_input_index)

    def get_num_intermediate_buffers(self, op_node: Node) -> int:
        """Returns the number of intermediate buffers for a given op node.

        Parameters
        ----------
        op_node:
            Op node.
        """
        if op_node.type != NodeType.FusedOp:
            return 0

        return op_node.n_intermediates

    def get_intermediate_buffer_size_bytes(self, op_node: Node) -> z3.Var:
        """Returns the size of the intermediate buffer for a given op node.
        Here we assume that the op_node is of type fused op and one of the two statements holds:
          1) All scheduled ops have the same microblock size
          2) Output scheduled op has the largest microblock size
        Ideally, this should take into account sizing intermediate buffer size to the largest
        scheduled op's microblock size.

        Parameters
        op_node:
            Op node.
        """
        return 2 * op_node.ub_r * op_node.ub_c * get_tile_size_z3_var(op_node.intermed_df)

    def get_fused_op_input_consumer_op(self, op_node: Node, input_name: str) -> Node:
        """Returns which scheduled op of a given fused op node consumer the given input.

        Parameters
        ----------
        op_node:
            Fused op node.
        input_name:
            Input name.
        """
        return self.fused_op_input_to_subop_consumer[op_node.name][input_name]

    def get_eltwise_op_consumer_config(self, op_node: Node, input_idx: int) -> ConsumerOpConfiguration:
        """Returns consumer config for eltwise op for a given input index.

        Parameters
        ----------
        op_node:
            Eltwise op node.
        input_idx:
            Input index, ignored here.
        """
        return ConsumerOpConfiguration(
            grid_size_r=op_node.grid_size_y,
            grid_size_c=op_node.grid_size_x,
            mblock_r=op_node.mb_m,
            mblock_c=op_node.mb_n,
            ublock_r=op_node.ub_r,
            ublock_c=op_node.ub_c,
            ublock_scan_order=op_node.ublock_order
        )

    def get_matmul_op_consumer_config(self, op_node: Node, input_idx: int) -> ConsumerOpConfiguration:
        """Returns consumer config for matmul op for a given input index.

        Parameters
        ----------
        op_node:
            Matmul op node.
        input_idx:
            Input index.
        """
        if input_idx == 0:
            return ConsumerOpConfiguration(
                grid_size_r=op_node.grid_size_y,
                grid_size_c=1,
                mblock_r=op_node.mb_m,
                mblock_c=op_node.m_k,
                ublock_r=op_node.ub_r,
                ublock_c=op_node.u_kt,
                ublock_scan_order=UblockOrder.c.value
            )
        elif input_idx == 1:
            return ConsumerOpConfiguration(
                grid_size_r=1,
                grid_size_c=op_node.grid_size_x,
                mblock_r=op_node.m_k,
                mblock_c=op_node.mb_n,
                ublock_r=op_node.u_kt,
                ublock_c=op_node.ub_c,
                ublock_scan_order=UblockOrder.r.value
            )
        elif input_idx == 2:
            assert op_node.bias, "Matmul op must have bias set to True to be able to have 3 inputs"
            return self.get_eltwise_op_consumer_config(op_node, input_idx)
        else:
            raise RuntimeError(f"Index: {input_idx} out of range for matmul op.")

    def get_scheduled_matmul_op_consumer_config(self, op_node: Node, input_idx: int) -> ConsumerOpConfiguration:
        """Returns consumer config for a matmul op which is inside of fused op (i.e. a scheduled
        matmul op) for a given input index.

        Parameters
        ----------
        op_node:
            Scheduled matmul op node.
        input_idx:
            Input index.
        """
        consumer_config = self.get_matmul_op_consumer_config(op_node, input_idx)
        # The only difference between regular and scheduled matmul (i.e. matmul inside of fused op)
        # is that scheduled matmul forces row major ublock scan order on both input 0 and 1
        consumer_config.ublock_scan_order = UblockOrder.r.value
        return consumer_config

    def get_reduce_op_consumer_config(self, op_node: Node, input_idx: int) -> ConsumerOpConfiguration:
        """
        Returns consumer config for eltwise op for a given input index.

        Parameters
        ----------
        op_node:
            Reduce op node.
        input_idx:
            Input index, ignored here.
        """
        consumer_config = self.get_eltwise_op_consumer_config(op_node, input_idx)
        if op_node.reduce_dim == Dimension.Vertical:
            consumer_config.mblock_r = op_node.m_k
            consumer_config.ublock_r = op_node.u_kt
        elif op_node.reduce_dim == Dimension.Horizontal:
            consumer_config.mblock_c = op_node.m_k
            consumer_config.ublock_c = op_node.u_kt
        return consumer_config

    def get_fused_op_consumer_config(self, op_node: Node, input_idx: int) -> ConsumerOpConfiguration:
        """Returns consumer config of the given fused op node for a given input index.

        Parameters
        ----------
        op_node:
            Fused op node.
        input_idx:
            Input index.
        """
        input_name = op_node.inputs[input_idx]
        scheduled_op = self.get_fused_op_input_consumer_op(op_node, input_name)
        scheduled_op_input_idx = self.get_consumers_producer_input_index(
            self.nodes[input_name], scheduled_op
        )

        return self.get_op_consumer_config(scheduled_op, scheduled_op_input_idx)

    def get_eltwise_op_ethernet_links_usage(self, op_node: Node, input_idx: int) -> int:
        """
        Returns ethernet links usage for a given eltwise op.

        Parameters
        ----------
        op_node:
            Eltwise op node.
        input_idx:
            Input index, unused here as it is not relevant.
        """
        return op_node.grid_size_x * op_node.grid_size_y

    def get_matmul_op_ethernet_links_usage(self, op_node: Node, input_idx: int) -> int:
        """
        Returns ethernet links usage for a given matmul op w.r.t. to the given input index.

        Parameters
        ----------
        op_node:
            Eltwise op node.
        input_idx:
            Input index.
        """
        if input_idx == 0:
            return op_node.grid_size_y
        elif input_idx == 1:
            return op_node.grid_size_x
        else:
            raise RuntimeError(f"Input index out of range (2), got: {input_idx}.")

    def constrain_queues(self) -> None:
        """Constraints all the queues in the graph."""
        for q_node in self.queues:
            self.define_common_queue_constraints(q_node)

            if q_node.type == NodeType.OutputQueue:
                self.constrain_output_queue(q_node)
            elif q_node.type == NodeType.InputQueue:
                self.constrain_input_queue(q_node)
            elif q_node.type == NodeType.ParameterQueue:
                self.constrain_parameter_queue(q_node)
            elif q_node.type == NodeType.ConstantQueue:
                self.constrain_constant_queue(q_node)

        self.constrain_queues_fed_by_ops()

    def define_common_queue_constraints(self, q_node: Node) -> None:
        """Defines constraints which are common for all the queue types.

        Parameters
        ----------
        q_node:
            Queue node for which to define constraints.
        """
        # Queue can fit on a chip.
        self.solver.add(q_node.grid_size_x <= self.arch.grid_size_x)
        self.solver.add(q_node.grid_size_y <= self.arch.grid_size_y)

        # Microblock in tiles can fit in half dest register.
        self.solver.add(q_node.ub_r * q_node.ub_c <= self.arch.max_tiles_in_dest / 2)

        self.constrain_dram_buffers_size(q_node)
        self.constrain_dram_channel(q_node)
        self.constrain_queue_entries(q_node)
        self.constrain_queue_prologue(q_node)
        self.constrain_queue_location(q_node)

    def constrain_dram_channel(self, q_node: Node) -> None:
        """Constraints queue such that it's input can fit in a DRAM channel.

        Parameters
        ----------
        q_node:
            Queue node for which to define constraints.
        """
        # Each tensor must fit into a DDR channel.
        tiles_per_channel = self.arch.get_dram_buffer_size() / q_node.get_tile_size_var()

        self.solver.add(
            tiles_per_channel >= (
                q_node.get_horizontal_size_var() * q_node.get_vertical_size_var() * q_node.t_dim *
                self.microbatch_size * self.microbatch_count * self.minibatch_count
            )
        )

    def constrain_dram_buffers_size(self, q_node: Node) -> None:
        """Constraints DRAM buffers so that they match the respective tensor size.

        Parameters
        ----------
        q_node:
            Queue node for which to define constraints.
        """
        self.solver.add(
            q_node.buffers_size == (
                q_node.mb_m * q_node.mb_n * q_node.ub_r * q_node.ub_c * q_node.t_dim *
                q_node.entries * q_node.get_tile_size_var() + self.arch.dram_queue_header_size
            )
        )

    def constrain_queue_entries(self, q_node: Node) -> None:
        """Constraints number of queue's entries.

        Parameters
        ----------
        q_node:
            Queue node.
        """
        if q_node.type in [NodeType.InputQueue, NodeType.OutputQueue, NodeType.Epoch2EpochQueue]:
            self.solver.add(q_node.entries == self.microbatch_count * self.microbatch_size)
        else:
            self.solver.add(q_node.entries == 1)

    def constrain_queue_prologue(self, q_node: Node) -> None:
        """Constraints queue node's prologue var.

        Parameters
        ----------
        q_node:
            Queue node.
        """
        feeder_op = self.nodes.get(q_node.input, None)
        grad_queue = feeder_op.gradient_op if feeder_op else False

        for graph_name in self.graphs:
            prologue = q_node.get_prologue_var(graph_name)
            if prologue is None:
                continue

            if (q_node.type == NodeType.InputQueue or
                q_node.type == NodeType.Epoch2EpochQueue or
                "opt" in graph_name):
                self.solver.add(prologue == Prologue.false.value)
            elif (q_node.type == NodeType.ConstantQueue or
                  grad_queue and graph_name == feeder_op.graph):
                # Gradient queue has to have prologue and epilogue set to
                # true in the graph of gradient op that is feeding the
                # queue.
                self.solver.add(prologue == Prologue.true.value)
            else:
                # For NodeType.ParameterQueue prologue will be randomly chosen.
                assert q_node.type != NodeType.OutputQueue
                self.solver.add(prologue >= Prologue.false.value,
                                prologue <= Prologue.true.value)

    def constrain_queue_location(self, q_node: Node) -> None:
        """
        Constraints all queues, except for output queues, to be located in DRAM.

        Parameters
        ----------
        q_node:
            Queue node.s
        """

        if q_node.type != NodeType.OutputQueue:
            self.solver.add(q_node.loc == BufferLoc.dram.value)

    def constrain_input_queue(self, q_node: Node) -> None:
        """Defines constraints which are specific for input queues.

        Parameters
        ----------
        q_node:
            Input queue node for which to define constraints.
        """
        self.solver.add(q_node.ublock_order == UblockOrder.r.value)

        self.solver.add(
            q_node.get_vertical_size_var() * self.arch.tile_r == q_node.get_size_r_var()
        )
        self.solver.add(
            q_node.get_horizontal_size_var() * self.arch.tile_c == q_node.get_size_c_var()
        )

    def constrain_output_queue(self, q_node: Node) -> None:
        """Defines constraints which are specific for output queues.

        Parameters
        ----------
        q_node:
            Queue node for which to define constraints.
        """
        q_input_op = self.nodes[q_node.input]

        self.solver.add(
            q_node.loc >= BufferLoc.host.value,
            q_node.loc <= BufferLoc.dram.value
        )

        self.solver.add(
            q_node.get_vertical_size_var() * self.arch.tile_r == q_node.get_size_r_var()
        )
        self.solver.add(
            q_node.get_horizontal_size_var() * self.arch.tile_c == q_node.get_size_c_var()
        )

        # If output buffer is on the host, we must untilize output.
        self.solver.add(Implies(
            q_node.loc == BufferLoc.host.value, q_input_op.untilize == Untilize.true.value
        ))

        self.constrain_output_queue_grid_size(q_node)

        self.constrain_output_queue_size(q_node)

    def constrain_output_queue_grid_size(self, q_node: Node) -> None:
        """
        Constrain shape of the output queue.

        Parameters
        ----------
        q_node:
            Output queue node.
        """
        q_input_op = self.nodes[q_node.input]

        # Grid size has to match the grid size of the producer op.
        self.solver.add(
            If(
                q_input_op.untilize == Untilize.true.value,
                And(q_node.grid_size_y == 1,
                    q_node.grid_size_x == 1,
                    q_node.ub_r == 1,
                    q_node.ub_c == 1),
                And(q_node.grid_size_y == q_input_op.grid_size_y,
                    q_node.grid_size_x == q_input_op.grid_size_x,
                    q_node.mb_m == q_input_op.mb_m,
                    q_node.mb_n == q_input_op.mb_n,
                    q_node.ub_r == q_input_op.ub_r,
                    q_node.ub_c == q_input_op.ub_c)
            )
        )

        # T dimensions has to match.
        self.solver.add(q_input_op.t_dim == q_node.t_dim)

        # Micro block scan order has to match.
        self.solver.add(q_input_op.ublock_order == q_node.ublock_order)

    def constrain_output_queue_size(self, output_queue: Node) -> None:
        """
        Defines constraints for output queue node's size.

        Parameters
        ----------
        output_queue: Node
            Output queue node.
        """
        feeder_op = self.nodes[output_queue.input]
        self.solver.add(
            output_queue.get_horizontal_size_var() == feeder_op.get_horizontal_size_var()
        )
        self.solver.add(
            output_queue.get_vertical_size_var() == feeder_op.get_vertical_size_var()
        )

    def constrain_parameter_queue(self, param_queue: Node):
        """
        Defines constraints for the parameter queue node.

        This should be overriden in graph test classes to add
        constraints for weight queues outer size. Bias queue size will
        be inferred from the add ops, and inner side of weight queues
        from the matmul ops.

        Parameters
        ----------
        param_queue: Node
            Parameter queue node.
        """
        pass

    def constrain_constant_queue(self, constant_queue: Node) -> None:
        """
        Defines constraints for the constant queue node.

        Parameters
        ----------
        constant_queue: Node
            Constant queue node.
        """
        self.solver.add(
            constant_queue.grid_size_y == 1, constant_queue.mb_m == 1, constant_queue.ub_r == 1
        )
        self.solver.add(
            constant_queue.grid_size_x == 1, constant_queue.mb_n == 1, constant_queue.ub_c == 1
        )

    def constrain_queues_fed_by_ops(self) -> None:
        """
        Defines grid/block constraints for queues that are fed by ops, except
        for the output queue.
        """
        for q_node in self.queues:
            if q_node.type == NodeType.OutputQueue:
                continue

            feeder_op = self.nodes.get(q_node.input, None)

            if feeder_op:
                self.constrain_producer_consumer_same_size(q_node, feeder_op)

    def constrain_producer_consumer_same_size(self, producer: Node, consumer: Node) -> None:
        """Constraints the producer and consumer node to have all the same parameters.

        Parameters
        ----------
        producer:
            Producer node.
        consumer:
            Consumer node.
        """
        # Grid shape has to match.
        self.solver.add(And(
            producer.t_dim == consumer.t_dim,
            producer.grid_size_x == consumer.grid_size_x,
            producer.grid_size_y == consumer.grid_size_y,
            producer.mb_m == consumer.mb_m,
            producer.mb_n == consumer.mb_n,
            producer.ub_r == consumer.ub_r,
            producer.ub_c == consumer.ub_c
        ))

        # Micro block scan order has to match.
        self.solver.add(
            producer.ublock_order == consumer.ublock_order
        )

    def constrain_data_format(self) -> None:
        """Constraints data formats for all nodes in the graph."""
        self.solver.add(self.data_format >= DataFormat.Bfp8_b.value,
                        self.data_format <= DataFormat.Float32.value)

        for q_node in self.queues:
            self.solver.add(q_node.df == self.data_format)

        for op_node in self.ops:
            if op_node.op_type == "reciprocal":
                self.solver.add(
                    If(
                        self.data_format < DataFormat.Float16_b.value,
                        If(
                            self.data_format == DataFormat.Bfp8_b.value,
                            op_node.out_df == DataFormat.Float16_b.value,
                            op_node.out_df == DataFormat.Float16.value
                        ),
                        op_node.out_df == self.data_format
                    )
                )
            else:
                self.solver.add(op_node.out_df == self.data_format)

            # tenstorrent/budabackend#1464
            # Inputs of type a data formats cannot be used with reduce op using dest acc Float32
            if self.arch.supports_float32_accumulation and (
                op_node.type == NodeType.ReduceOp or (op_node.type == NodeType.FusedOp and op_node.has_reduce_scheduled_op)
            ):
                self.solver.add(
                    Implies(
                        op_node.acc_df == DataFormat.Float32.value,
                        And(self.data_format != DataFormat.Bfp8.value, self.data_format != DataFormat.Float16.value)
                    )
                )
                
            # If the architecture doesn't support Float32 accumulation
            if not self.arch.supports_float32_accumulation:
                self.solver.add(
                    If(
                        op_node.out_df < DataFormat.Float32.value,
                        op_node.acc_df == op_node.out_df,
                        op_node.acc_df == DataFormat.Float16.value
                    )
                )
            else:
                self.solver.add(op_node.acc_df == op_node.out_df)

            self.solver.add(op_node.intermed_df == op_node.out_df)

    @abstractmethod
    def additional_constraints(self) -> None:
        """Override in base class to add additional constraints.
        """
        pass

    def export_additional_sweep_vars(self) -> List[SweepVarsGroup]:
        """Override in base class to export additional sweep vars.
        """
        return []

    def constrain_tensor_size(self, q_node: Node, max_tensor_in_tiles: int) -> None:
        """Constraints tensor size (for a given queue). Idea behind this is to constrain
        input sizes so that we limit test execution time.

        Parameters
        ----------
        q_node:
            Queue node for which to define constraints for.
        max_tensor_in_tiles:
            Max total number of tiles a tensor can have, in all dimension.
        """
        self.solver.add(
            q_node.get_vertical_size_var() * q_node.get_horizontal_size_var() *
            q_node.t_dim * self.microbatch_size * self.microbatch_count * self.minibatch_count
            <= max_tensor_in_tiles
        )

    def get_op_inputs_tensor_dims(self, op_node: Node, input_idx: int) -> TensorDims:
        """Returns the tensor dimensions of an input of a given node on the given input index, after
        applying all the TMs, if any.

        Parameters
        ----------
        op_node:
            Node that we want to get input dimensions of.
        input_idx:
            Indef of the input.
        """
        input_name = op_node.inputs[input_idx]

        op_input = self.op_input_tm_map[op_node.name][input_name]
        return op_input.tensor_dims()

    def get_consumers_producer_input_index(self, producer: Node, consumer: Node) -> int:
        """Returns index of producer node in the consumer node's input list.

        Parameters
        ----------
        producer:
            Producer node - op or queue.
        consumer:
            Consumer node - op.
        """
        return consumer.inputs.index(producer.name)

    def postprocess(self, model_vars: Dict[str, any]) -> None:
        """"Processes some Z3 variables after solver generated a
        solution.

        Solver generates integer variables exclusively and
        postprocessing may be used to convert some of those variables
        into string, bool or some other form.

        Parameters
        ----------
        model_vars: dict
            Dictionary of variable names and their generated values.
        """
        model_vars[ARCH_VAR_NAME] = str(self.arch)

        # Define inp_dram_buffers and out_dram buffers strings for dram queues.
        self.create_dram_buffer_strings(model_vars)

        # Fix queue node's location.
        self.fix_queue_vars(model_vars)

        # Define ublock scan order based on ublock scan order int z3 var.
        self.fix_ublock_scan_order(model_vars)

        # Fix untilize_output variable on ops.
        self.fix_output_ops_untilize(model_vars)

        # Fix data format (int -> str) representation.
        self.fix_data_formats(model_vars)

    def fix_output_ops_untilize(self, model_vars: Dict[str, any]) -> None:
        """
        Fix output op node's untilize attribute to be lowercase.

        Parameters
        ----------
        model_vars:
            Dictionary of variable names and their generated values.
        """
        for op_node in self.ops:
            if op_node.is_output:
                untilize_key = op_node.untilize.sexpr()
                model_vars[untilize_key] = TemplateNetlistTestBase.get_bool_var_string(model_vars, untilize_key)

    def fix_queue_vars(self, model_vars: Dict[str, any]) -> None:
        """
        Fix queue's variables - prologue and buffer locations.

        Parameters
        ----------
        model_vars:
            Dictionary of variables names and their generated values.
        """
        for q_node in self.queues:
            loc_key = q_node.loc.sexpr()
            model_vars[loc_key] = BufferLoc(model_vars[loc_key]).name

            if q_node.type != NodeType.OutputQueue:
                for gname in self.graphs:
                    TemplateNetlistTestBase.fix_prologue_format(q_node, gname, model_vars)

    def fix_data_formats(self, model_vars: Dict[str, any]) -> None:
        """Updates `model_vars` to replace data format integer values with the
        corresponding names.

        Parameters
        ----------
        model_vars:
            Dictionary of variable names and their generated values.
        """
        # count unique data format var names
        data_format_var_names: Set[str] = set()
        for q_node in self.queues:
            data_format_var_names.add(q_node.df.sexpr())

        for op_node in self.ops:
            for input_idx in range(len(op_node.inputs)):
                data_format_var_names.add(getattr(op_node, f"in{input_idx}_df").sexpr())
            data_format_var_names.add(op_node.acc_df.sexpr())
            data_format_var_names.add(op_node.out_df.sexpr())
            data_format_var_names.add(op_node.intermed_df.sexpr())

        for df_var_name in data_format_var_names:
            model_vars[df_var_name] = DataFormat(model_vars[df_var_name]).name

    def fix_ublock_scan_order(self, model_vars: Dict[str, any]) -> None:
        """Updates `model_vars` with ublock scan order set for every node in the
        graph based on the internal node variables.

        Parameters
        ----------
        model_vars:
            Dictionary of variable names and their generated values.
        """
        for node in self.nodes.values():
            if hasattr(node, "ublock_order") and hasattr(node, "ublock_order_template_str"):
                if model_vars[node.ublock_order.sexpr()] == UblockOrder.r.value:
                    model_vars[node.ublock_order_template_str] = "r"
                else:
                    model_vars[node.ublock_order_template_str] = "c"

    def create_dram_buffer_strings(self, model_vars: Dict[str, any]) -> None:
        """Updates `model_vars` with dram buffer descriptions for every
        queue given its grid and tensor size.

        Parameters
        ----------
        model_vars:
            Dictionary of variable names and their generated values.
        """
        curr_start_addr = [self.arch.dram_buffer_start_addr] * self.arch.num_dram_channels
        curr_hots_addr = 0

        for q_node in self.queues:
            has_assigned_dram_channeld = False

            grid_size_x = q_node.get_attr_from_model(model_vars, "grid_size_x")
            grid_size_y = q_node.get_attr_from_model(model_vars, "grid_size_y")

            q_buffer_count = grid_size_x * grid_size_y
            q_buffers_size = q_node.get_attr_from_model(model_vars, "buffers_size")

            # Host buffer allocation.
            if model_vars[q_node.loc.sexpr()] == BufferLoc.host.value:
                model_vars[q_node.loc_template_str] = '[' + hex(curr_hots_addr) + ']'
                curr_hots_addr += q_buffers_size
                continue

            # Dram buffer allocation.
            dram_channel = random.choice(range(self.arch.num_dram_channels))
            while not has_assigned_dram_channeld:
                q_end_address = curr_start_addr[dram_channel] + q_buffer_count * q_buffers_size
                if q_end_address  < self.arch.dram_buffer_end_addr:
                    has_assigned_dram_channeld = True
                else:
                    dram_channel = random.choice(range(self.arch.num_dram_channels))

            model_vars[q_node.loc_template_str] = TemplateNetlistTestBase.dram_string(
                channel=dram_channel,
                start_addr=curr_start_addr[dram_channel],
                buffer_count=q_buffer_count,
                buffer_size=q_buffers_size
            )

            curr_start_addr[dram_channel] += q_buffer_count * q_buffers_size
            # Dram addresses need to be a multiple of NOC data width since it
            # does not support unaligned accesses.
            curr_start_addr[dram_channel] +=\
                 curr_start_addr[dram_channel] % self.arch.noc_data_word_width_bytes

    @staticmethod
    def fix_prologue_format(queue: Node, gname: str, model_vars: Dict) -> None:
        """
        Fix queue's prolog format.

        Parameters
        ----------
        queue:
            Queue node.
        gname:
            Graph name.
        model_vars:
            Dictionary of variables names and their generated values.
        """
        prologue_var = queue.get_prologue_var(gname)
        if prologue_var is None:
            return

        var_key = prologue_var.sexpr()
        model_vars[var_key] = TemplateNetlistTestBase.get_bool_var_string(model_vars, var_key)

    @staticmethod
    def get_bool_var_string(model_vars: Dict, var_key: str) -> str:
        """Converts bool var given by `var_key` to string
        representation.

        Parameters
        ----------
        model_vars: dict
            Dictionary of variable names and their generated values.
        var_key: str
            Variable key.
        """
        return str(bool(model_vars[var_key])).lower()

    def export_producer_queue_for_resource_constraints_from_model(
        self,
        node: Node,
        model_vars: dict
    ) -> ResourceConstrNode:
        """
        Simple wrapper arround default producer node export for queue nodes.
        See docstring of TemplateNetlistTestBase::export_producer_node_for_resource_constraints_from_model
        for more details.
        """
        return self.export_producer_node_for_resource_constraints_from_model(node, model_vars)

    def export_producer_op_for_resource_constraints_from_model(
        self,
        node: Node,
        model_vars: dict
    ) -> ResourceConstrNode:
        """
        Simple wrapper arround default producer node export for op nodes.
        See docstring of TemplateNetlistTestBase::export_producer_node_for_resource_constraints_from_model
        for more details.
        """
        return self.export_producer_node_for_resource_constraints_from_model(node, model_vars)

    def export_producer_queue_for_resource_constraints_from_z3(self, node: Node,) -> ResourceConstrNode:
        """
        Simple wrapper arround default producer node export for queue nodes.
        See docstring of TemplateNetlistTestBase::export_producer_node_for_resource_constraints_from_z3
        for more details.
        """
        return self.export_producer_node_for_resource_constraints_from_z3(node)

    def export_producer_op_for_resource_constraints_from_z3(self, node: Node) -> ResourceConstrNode:
        """
        Simple wrapper arround default producer node export for op nodes.
        See docstring of TemplateNetlistTestBase::export_producer_node_for_resource_constraints_from_z3
        for more details.
        """
        return self.export_producer_node_for_resource_constraints_from_z3(node)

    def export_producer_node_for_resource_constraints_from_model(
        self,
        node: Node,
        model_vars: dict
    ) -> ResourceConstrNode:
        """
        Export producer node for resource constraints from model vars. Applies to both queues and ops.

        Parameters
        ----------
        node:
            Node - queue or op.
        model_vars:
            Mapping between z3 var names and solver values.
        """
        return ResourceConstrNode(
            name=node.name,
            type=node.type,
            grid_size_x=node.get_attr_from_model(model_vars, "grid_size_x"),
            grid_size_y=node.get_attr_from_model(model_vars, "grid_size_y"),
            mb_n=node.get_attr_from_model(model_vars, "mb_n"),
            mb_m=node.get_attr_from_model(model_vars, "mb_m"),
            ub_c=node.get_attr_from_model(model_vars, "ub_c"),
            ub_r=node.get_attr_from_model(model_vars, "ub_r"),
            t_dim=node.get_attr_from_model(model_vars, "t_dim"),
            reduce_dim=(node.reduce_dim if node.type == NodeType.ReduceOp else None),
            buf_size_mb=node.get_attr_from_model(model_vars, "buf_size_mb"),
            ublock_scan_order=node.get_attr_from_model(model_vars, "ublock_order"),
            side=ResourceConstrNodeSide.Producer,
            input_index=0,  # Doesn't really matter on producer side.
            tms=[]
        )

    def export_producer_node_for_resource_constraints_from_z3(self, node: Node) -> ResourceConstrNode:
        """
        Export producer node for resource constraints from z3. Applies to both queues and ops.

        Parameters
        ----------
        node:
            Node - queue or op.
        """
        return ResourceConstrNode(
                name=node.name,
                type=node.type,
                grid_size_x=node.grid_size_x,
                grid_size_y=node.grid_size_y,
                mb_n=node.mb_n,
                mb_m=node.mb_m,
                ub_c=node.ub_c,
                ub_r=node.ub_r,
                t_dim=node.t_dim,
                reduce_dim=(node.reduce_dim if node.type == NodeType.ReduceOp else None),
                buf_size_mb=node.buf_size_mb,
                ublock_scan_order=node.ublock_order,
                side=ResourceConstrNodeSide.Producer,
                input_index=0,  # Doesn't really matter on producer side.
                tms=[]
            )

    def export_consumer_op_for_resource_constraints_from_model(
        self,
        node: Node,
        model_vars: dict,
        input_index: int
    ) -> ResourceConstrNode:
        op_input_tms = self.get_node_tms(node, input_index, model_vars)
        consumer_op_config = self.get_op_consumer_config(node, input_index)

        return ResourceConstrNode(
            name=node.name,
            type=node.type,
            grid_size_x=consumer_op_config.get_attr_from_model("grid_size_c", model_vars),
            grid_size_y=consumer_op_config.get_attr_from_model("grid_size_r", model_vars),
            mb_n=consumer_op_config.get_attr_from_model("mblock_c", model_vars),
            mb_m=consumer_op_config.get_attr_from_model("mblock_r", model_vars),
            ub_c=consumer_op_config.get_attr_from_model("ublock_c", model_vars),
            ub_r=consumer_op_config.get_attr_from_model("ublock_r", model_vars),
            t_dim=model_vars[node.t_dim.sexpr()],
            reduce_dim=(node.reduce_dim if node.type == NodeType.ReduceOp else None),
            buf_size_mb=model_vars[node.buf_size_mb.sexpr()],
            ublock_scan_order=consumer_op_config.get_attr_from_model("ublock_scan_order", model_vars),
            side=ResourceConstrNodeSide.Consumer,
            input_index=input_index,
            tms=op_input_tms
        )

    def export_consumer_op_for_resource_constraints_from_z3(
        self,
        node: Node,
        input_index: int
    ) -> ResourceConstrNode:
        op_input_tms = self.get_node_tms(node, input_index, model_vars=None)
        consumer_op_config = self.get_op_consumer_config(node, input_index)

        return ResourceConstrNode(
            name=node.name,
            type=node.type,
            grid_size_x=consumer_op_config.grid_size_c,
            grid_size_y=consumer_op_config.grid_size_r,
            mb_n=consumer_op_config.mblock_c,
            mb_m=consumer_op_config.mblock_r,
            ub_c=consumer_op_config.ublock_c,
            ub_r=consumer_op_config.ublock_r,
            t_dim=node.t_dim,
            reduce_dim=(node.reduce_dim if node.type == NodeType.ReduceOp else None),
            buf_size_mb=node.buf_size_mb,
            ublock_scan_order=consumer_op_config.ublock_scan_order,
            side=ResourceConstrNodeSide.Consumer,
            input_index=input_index,
            tms=op_input_tms
        )

    def get_node_tms(
        self,
        node: Node,
        input_idx: int,
        model_vars: Optional[Dict]
    ) -> List[Dict[str, str | int]]:
        """For a given node, returns a list of TM dictionaries required for
        resource usage constraints.

        Parameters
        ----------
        node:
            Node.
        model_vars:
            Dict of calculated z3 variables.
        input_idx:
            Index of the input of a given that for which we want to get tms.

        Returns
        -------
        List of dicts of format (example bellow):
        {
            tm_type: hslice,
            factor: 2
        }
        """
        tms = []
        tm_enum_to_name = {
            TMS.transpose.value: "transpose",
            TMS.hslice.value: "hslice",
            TMS.vslice.value: "vslice",
            TMS.hstack.value: "hstack",
            TMS.vstack.value: "vstack",
            TMS.c_broadcast.value: "cbcast",
            TMS.r_broadcast.value: "rbcast",
            TMS.t_broadcast.value: "zbcast"
        }

        input_name = node.inputs[input_idx]
        op_input = self.op_input_tm_map[node.name][input_name]
        op_input_tms = op_input.get_tms()

        if len(op_input_tms) == 0:
            return []

        if model_vars is not None:
            for tm in op_input_tms:
                if model_vars[tm.tm_type.sexpr()] == TMS.transpose.value:
                    tms.append({
                        "tm_type": "transpose",
                        "factor": True
                    })
                else:
                    tms.append({
                        "tm_type": tm_enum_to_name[model_vars[tm.tm_type.sexpr()]],
                        "factor": model_vars[tm.tm_arg.sexpr()]
                    })
        else:
            for tm_type in MULTI_TM_TESTS_VALID_TMS:
                if tm_type == TMS.transpose:
                    tms.append({
                        "tm_type": "transpose",
                        "factor": Or([
                            tm.tm_type == TMS.transpose.value for tm in op_input_tms
                        ])
                    })
                else:
                    tms.append({
                        "tm_type": tm_enum_to_name[tm_type.value],
                        "factor": op_input.tm_factor[tm_type]
                    })

        return tms

    @staticmethod
    def dram_string(channel: int, start_addr: int, buffer_count: int, buffer_size: int) -> str:
        """Returns string representing sequence of pairs of dram
        channel and starting address.

        It may return string like "[[0, 0xABCDABCD], [0, 0xBCDABCDA]]".

        Parameters
        ----------
        channel: int
            Dram channel.
        buffer_count: int
            Number of buffers.
        buffer_size: int
            Size of each buffer.

        Returns
        -------
        str
        """
        return ("[" +
                    ", ".join([f"[{channel}, {hex(addr)}]"
                        for addr in range(start_addr,
                                          start_addr + buffer_count * buffer_size,
                                          buffer_size)])
                    + "]")

    @staticmethod
    def is_field_template_var(field: str) -> bool:
        """Checks if netlist field is a template variable.

        Parameters
        ----------
        field:
            Netlist field.

        Returns
        -------
        If the netlist field is a template variable, i.e. if it starts with
        the template var prefix.
        """
        return field.startswith(TEMPLATE_VAR_PREFIX)

    @staticmethod
    def get_fused_op_scheduled_op_name(fused_op_name: str, subop_name: str) -> str:
        return f"{fused_op_name}.{subop_name}"

    @staticmethod
    def is_intermediate_buffer(buffer_name: str):
        return buffer_name.startswith("interm")

    @staticmethod
    def is_dest_register(buffer_name: str):
        return buffer_name == "dest"

    @staticmethod
    def is_temporary_buffer(buffer_name: str):
        return (
            TemplateNetlistTestBase.is_intermediate_buffer(buffer_name) or
            TemplateNetlistTestBase.is_dest_register(buffer_name)
        )

    @staticmethod
    def get_node_type_from_netlist(op_params: Dict[str, any], is_scheduled: bool = False) -> NodeType:
        # Scheduled matmul is different than the regular matmul because it forces row major
        # ublock scan order on both input0 and input1.
        matmul_op_type = NodeType.ScheduledMatmulOp if is_scheduled else NodeType.MatmulOp
        reduce_op_type = NodeType.ScheduledReduceOp if is_scheduled else NodeType.ReduceOp

        return (
            NodeType.FusedOp if op_params["type"] == "fused_op" else (
                matmul_op_type if op_params["type"] == "matmul" else (
                    reduce_op_type if op_params["type"] == "reduce" else (
                        NodeType.EltwiseOp
                    )
                )
            )
        )

    def get_num_queue_buffers_accessed(self, op: Node, queues: List[Node], model_vars: Dict) -> int:
        """
        Helper function for check_resource_usage which calculates how many
        queue buffers are accessed for every op core that receives input from
        and/or outputs to DRAM.

        Parameters
        ----------
        op:
            Op of interest.
        queues:
            List of all queues in graph.
        model_vars:
            Dictionary of variable names and their solver.check() generated
            values.

        Returns
        -------
        int
            Number of buffers op must access.
        """
        # Aux accumulator variable.
        total_num_queue_buffers_accessed = 0

        queues_feeding_this_op = list(filter(
            lambda q: q.name in op.inputs,
            queues
        ))

        # Accumulate num of accessed input buffers for each input queue.
        for queue in queues_feeding_this_op:
            # queue and op Nodes exported as ResourceConstrNodes for the sake
            # of common API. Input index is not relevant here since it is not
            # used in get_num_buffers_accessed_along_dim calculations.
            queue_node = self.export_producer_queue_for_resource_constraints_from_model(
                node=queue,
                model_vars=model_vars
            )

            op_node = self.export_consumer_op_for_resource_constraints_from_model(
                node=op,
                input_index=self.get_consumers_producer_input_index(queue, op),
                model_vars=model_vars
            )

            num_buffers_x = get_num_buffers_accessed_along_dim(
                op_node, queue_node, Dimension.Horizontal
            )
            num_buffers_y = get_num_buffers_accessed_along_dim(
                op_node, queue_node, Dimension.Vertical
            )

            total_num_queue_buffers_accessed += num_buffers_x * num_buffers_y

        # Every core of op maps to exactly one buffer of output queue,
        # therefore we need to include op2queue fanout in total calculation.
        op_queues_fanout = self.get_op2queues_resource_usage(op, queues)
        total_num_queue_buffers_accessed += op_queues_fanout

        return total_num_queue_buffers_accessed

    def constrain_fork_streams(self, op: Node) -> None:
        """
        Helper function for check_resource_usage which adds extra streams
        constraints for this op to the solver (which are too complex and thus
        added only when needed).
        """
        total_op_queues_fanout_z3 = \
            self.get_op2queues_resource_usage(op, self.queues)
        (producer_max_fork_streams_used_per_core_z3,
         producer_max_scatter_stream_phases_used_per_core_z3) = \
            self.get_op2consumer_ops_resource_usage_z3_expr(op, self.ops, self.solver)

        total_fork_streams_used_per_core_z3 = (
            total_op_queues_fanout_z3 +
            producer_max_fork_streams_used_per_core_z3
        )

        op.add_raw_attribute("total_fork_streams_used_per_core")

        self.solver.add(
            op.total_fork_streams_used_per_core == total_fork_streams_used_per_core_z3
        )
        self.solver.add(
            op.total_fork_streams_used_per_core >= 0,
            (op.total_fork_streams_used_per_core <= self.arch.max_fork_streams_used_per_core)
        )

    def constrain_pipe_inputs(self, op: Node) -> None:
        """
        Helper function for check_resource_usage which adds queues2op pipe inputs
        constraints for this op to the solver (which are too complex and thus
        added only when needed).
        """
        total_queues_op_pipe_inputs_z3 = \
            self.get_queues2op_resource_usage_z3_expr(op, self.queues, self.solver)

        op.add_raw_attribute("total_queues_op_pipe_inputs")

        self.solver.add(
            op.total_queues_op_pipe_inputs == total_queues_op_pipe_inputs_z3
        )
        self.solver.add(
            op.total_queues_op_pipe_inputs >= 0,
            (op.total_queues_op_pipe_inputs <= self.arch.max_queues_op_pipe_inputs)
        )

    def constrain_stream_phases(self, op: Node) -> None:
        """
        Helper function for check_resource_usage which adds stream phases
        constraints for this op to the solver (which are too complex and thus
        added only when needed).
        """
        (producer_max_fork_streams_used_per_core_z3,
         producer_max_scatter_stream_phases_used_per_core_z3) = \
            self.get_op2consumer_ops_resource_usage_z3_expr(op, self.ops, self.solver)
        consumer_max_gather_stream_phases_used_per_core_z3 = \
            self.get_producer_ops2op_resource_usage_z3_expr(op, self.ops, self.solver)

        total_stream_phases_used_per_core_z3 = (
            producer_max_scatter_stream_phases_used_per_core_z3 +
            consumer_max_gather_stream_phases_used_per_core_z3
        )

        op.add_raw_attribute("total_stream_phases_used_per_core")

        self.solver.add(
            op.total_stream_phases_used_per_core == total_stream_phases_used_per_core_z3
        )
        self.solver.add(
            op.total_stream_phases_used_per_core >= 0,
            (op.total_stream_phases_used_per_core <= self.arch.max_stream_phases_per_core)
        )

    def get_op2queues_resource_usage(self,
                                     op: Node,
                                     queues: List[Node]) -> int:
        """
        Helper function for check_resource_usage and constrain_extra_streams
        which calculates op -> queues fanout.

        This function is used in both check_resource_usage and
        constrain_extra_streams since it always returns an int. Fanout of an op
        if a property of the netlist as is, it is not templated nor changed,
        thus it is not a function of any other z3 vars.

        Parameters
        ----------
        op:
            Producer op in this connection.
        queues:
            List of all queues in graph from which queues that op outputs to
            are filtered.

        Returns
        -------
        int:
            Fanout of this op - num of queues it outputs to.
        """
        queues_fed_by_this_op = list(filter(
            lambda q: q.input == op.name,
            queues
        ))

        return len(queues_fed_by_this_op)

    def get_op2consumer_ops_resource_usage(self,
                                           op: Node,
                                           ops: List[Node],
                                           model_vars: Dict) -> Tuple[int, int]:
        """
        Helper function for check_resource_usage which calculates fork streams
        and stream phases used per core for this op -> consumer ops connections.

        See related docstring for get_op2consumer_ops_resource_usage_z3_expr
        for more info.

        Parameters
        ----------
        op:
            Producer op in this connection.

        ops:
            List of all ops in graph from which consumer ops for this op are
            filtered.

        model_vars:
            Dictionary of variable names and their solver.check() generated
            values.

        Returns
        -------
        tuple of 2 integers:
            total_producer_fork_streams_used_per_core:
                Maximum streams used on a single producer core for fork
                implementation.

            total_producer_stream_phases_used_per_core:
                Maximum stream phases that need to be encoded in blob on a single
                producer core for scatter implementation (for TMs/reblocking).
        """
        total_producer_fork_streams_used_per_core = 0
        total_producer_stream_phases_used_per_core = 0

        ops_fed_by_this_op = list(filter(
            lambda consumer_op: op.name in consumer_op.inputs,
            ops
        ))

        for consumer_op in ops_fed_by_this_op:
            # Take special care of consumer ops that have this op as input
            # multiple times (usually twice, for example: square function which
            # is implemented by eltwise multiply of two inputs).
            input_indexes = [idx for idx, input_name in
                             enumerate(consumer_op.inputs)
                             if input_name == op.name]

            for input_index in input_indexes:
                (producer_max_fork_streams_used_per_core,
                 producer_max_scatter_stream_phases_used_per_core,
                 consumer_max_gather_stream_phases_used_per_core) = \
                    get_op2op_resource_usage(
                        self.export_producer_op_for_resource_constraints_from_model(
                            node=op,
                            model_vars=model_vars
                        ),
                        self.export_consumer_op_for_resource_constraints_from_model(
                            node=consumer_op,
                            model_vars=model_vars,
                            input_index=input_index
                        )
                    )

                total_producer_fork_streams_used_per_core += \
                    producer_max_fork_streams_used_per_core
                total_producer_stream_phases_used_per_core += \
                    producer_max_scatter_stream_phases_used_per_core
                # Ignore the third return value, it will be checked in
                # check_producer_ops2this_op_resource_usage.

        return (total_producer_fork_streams_used_per_core,
                total_producer_stream_phases_used_per_core)

    def get_queues2op_resource_usage(self,
                                     op: Node,
                                     queues: list[Node],
                                     model_vars: dict) -> int:
        """
        Helper function for check_resource_usage which calculates number of
        pipe inputs for queues -> op connections.

        See related docstring for get_queues2op_resource_usage_z3_expr for more
        info.

        Parameters
        ----------
        op:
            Consumer op in this connection.
        queues:
            List of all queues in graph from which queues that feed this op
            are filtered.
        model_vars:
            Dictionary of variable names and their solver.check() generated
            values.

        Returns
        -------
        total_queues_op_pipe_inputs:
            Estimate of number of inputs to the queue -> op pipe.
        total_prologue_streams_used_per_core:
            Estimate of number of prologue streams used per core.
        """
        total_queues_op_pipe_inputs = 0
        total_prologue_streams_used_per_core = 0

        queues_feeding_this_op = list(filter(
            lambda q: q.name in op.inputs,
            queues
        ))

        for queue in queues_feeding_this_op:
            real_op = self.get_real_consumer_op(queue, op)
            input_index = real_op.inputs.index(queue.name)
            queue_op_pipe_inputs = \
                get_queue2op_resource_usage(
                    self.export_producer_queue_for_resource_constraints_from_model(
                        node=queue,
                        model_vars=model_vars,
                    ),
                    self.export_consumer_op_for_resource_constraints_from_model(
                        node=real_op,
                        model_vars=model_vars,
                        input_index=input_index
                    )
                )

            total_queues_op_pipe_inputs += queue_op_pipe_inputs

            # NOTE: This constraint is not entirely correct, since it doesn't account for the reblocking/TMs, it may
            # be posssible that more streams are used, e.g. if the prefetch type for prolog buffer is set to PreTM
            # additional streams may be used for gather, of if a prolog buffer on one core is used by some other op
            # cores additional fork streams may be used.
            queue_prologue_var_for_op_graph = queue.get_prologue_var(op.graph)
            is_queue_prologue_read = queue_prologue_var_for_op_graph is not None and model_vars[queue_prologue_var_for_op_graph.sexpr()]
            if is_queue_prologue_read > 0:
                grid_diff_across_x = math.ceil(model_vars[queue.grid_size_x.sexpr()] / model_vars[op.grid_size_x.sexpr()])
                grid_diff_across_y = math.ceil(model_vars[queue.grid_size_y.sexpr()] / model_vars[op.grid_size_y.sexpr()])
                total_prologue_streams_used_per_core = grid_diff_across_x * grid_diff_across_y

        return total_queues_op_pipe_inputs, total_prologue_streams_used_per_core

    def get_producer_ops2op_resource_usage(self,
                                           op: Node,
                                           ops: list[Node],
                                           model_vars: dict) -> int:
        """
        Helper function for check_resource_usage which calculates stream
        phases used per core for producer ops -> this op connections.

        See related docstring for get_producer_ops2op_resource_usage_z3_expr
        for more info.

        Parameters
        ----------
        op:
            Consumer op in this connection.
        ops:
            List of all ops in graph from which producer ops for this op are
            filtered.
        model_vars:
            Dictionary of variable names and their solver.check() generated
            values.

        Returns
        -------
        total_consumer_stream_phases_used_per_core:
            Maximum stream phases that need to be encoded in blob on a single
            consumer core for gather implementation (for TMs/reblocking and/or
            matmul rows/columns).
        total_gather_streams_user_per_core:
            Estimate of the number of gather extra streams which are used per core.
        """
        total_consumer_stream_phases_used_per_core = 0
        total_gather_streams_user_per_core = 0

        ops_feeding_this_op = list(filter(
            lambda producer_op: producer_op.name in op.inputs,
            ops
        ))

        current_gather_streams_on_core = 0

        for producer_op in ops_feeding_this_op:
            real_op = self.get_real_consumer_op(producer_op, op)
            input_index = real_op.inputs.index(producer_op.name)
            (producer_max_fork_streams_used_per_core,
             producer_max_scatter_stream_phases_used_per_core,
             consumer_max_gather_stream_phases_used_per_core) = \
                get_op2op_resource_usage(
                    self.export_producer_op_for_resource_constraints_from_model(
                        node=producer_op,
                        model_vars=model_vars
                    ),
                    self.export_consumer_op_for_resource_constraints_from_model(
                        node=real_op,
                        model_vars=model_vars,
                        input_index=input_index
                    )
                )

            # NOTE: These are new checks for the number of gather streams used. It is a relatively simple heuristic
            # which assumes that when the producer op is placed on multiple cores and there is some kind of reblocking
            # between the producer and the consumer we always gather from all the buffers on the grid, which doesn't
            # always have to be the case, depending on reblocking/TMs. This is not really an issue since the number of
            # extra streams is clamped to 3, as that is the number of relay streams that pipegen uses to hide latency
            # when implementing gather. When we pass the max number of allocated gather streams for a given architecture
            # (3 for GS, 4 otherwise) we only allocate one extra stream which performs gather and skip the relay stream
            # allocation, so the total counter is only increased by one in that case.
            # The only case where this could really cause problems is the fused op with max inputs, all inputs are
            # coming from producer ops, every producer op is multi grid and there is heavy reblocking between the
            # producer and consumer. We currently don't have such cases.
            if (self.is_multi_grid_node(producer_op, model_vars) and \
                self.has_reblocking_on_connection(producer_op, op, model_vars)):
                if current_gather_streams_on_core >= self.arch.max_gather_stream_user_per_core:
                    total_gather_streams_user_per_core += 1
                else:
                    producer_grid_x = model_vars[producer_op.grid_size_x.sexpr()]
                    producer_grid_y = model_vars[producer_op.grid_size_y.sexpr()]
                    max_buffers_to_gather_from = producer_grid_x * producer_grid_y
                    total_gather_streams_user_per_core += min(
                        max_buffers_to_gather_from, self.arch.max_gather_relay_streams
                    )
                    current_gather_streams_on_core += 1

            total_consumer_stream_phases_used_per_core += \
                consumer_max_gather_stream_phases_used_per_core
            # Ignore other two return values, they are checked in
            # check_this_op2consumer_ops_resource_usage.

        return total_consumer_stream_phases_used_per_core, total_gather_streams_user_per_core

    def get_op2consumer_ops_resource_usage_z3_expr(self,
                                                   op: Node,
                                                   ops: List[Node],
                                                   solver: z3.Solver) -> Tuple[z3.Var, z3.Var]:
        """
        Helper function for constrain_extra_streams and constrain_stream_phases
        which calculates fork streams and stream phases used per core for this
        op -> consumer ops connections in form of a long z3 symbolic expression
        and additional constraints added to the solver.

        This function is almost identical to get_op2consumer_ops_resource_usage,
        but to combine them into one would take a messy if statement in the
        middle of the function to make a distinction between call which
        is done with concrete values from model_vars, and a call which only
        uses symbolic z3 expressions and constraints. Examining functions
        get_op2op_resource_usage and get_op2op_resource_usage_z3 one can clearly
        see that control flow is completely written in a different manner when
        using z3 statements instead of classic Python ones.

        Solver object is needed because it is impossible to write resource
        constraints in form of one symbolic expression. Along the way new z3
        objects need to be created and added to the solver in order to break
        down the complex calculations and symbolic dependencies to finally be
        able to make a statement as simple as

        self.solver.add(
            op.total_stream_phases_used_per_core >= 0,
            (op.total_stream_phases_used_per_core <=
             self.arch.max_stream_phases_per_core)
        )

        for example.

        Parameters
        ----------
        op:
            Producer op in this connection.
        ops:
            List of all ops in graph from which consumer ops for this op are
            filtered.
        solver:
            z3 Solver object.

        Returns
        -------
        tuple of 2 z3.Vars:
            total_producer_fork_streams_used_per_core:
                Maximum streams used on a single producer core for fork
                implementation in form of a z3 expression.

            total_producer_stream_phases_used_per_core:
                Maximum stream phases that need to be encoded in blob on a single
                producer core for scatter implementation (for TMs/reblocking)
                in form of a z3 expression.
        """
        total_producer_fork_streams_used_per_core = 0
        total_producer_stream_phases_used_per_core = 0

        ops_fed_by_this_op = list(filter(
            lambda consumer_op: op.name in consumer_op.inputs,
            ops
        ))

        for consumer_op in ops_fed_by_this_op:
            # Take special care of consumer ops that have this op as input
            # multiple times (usually twice, for example: square function which
            # is implemented by eltwise multiply of two inputs).
            input_indexes = [idx for idx, input_name in
                             enumerate(consumer_op.inputs)
                             if input_name == op.name]

            for input_index in input_indexes:
                (producer_max_fork_streams_used_per_core,
                 producer_max_scatter_stream_phases_used_per_core,
                 consumer_max_gather_stream_phases_used_per_core) = \
                    get_op2op_resource_usage_z3(
                        self.export_producer_op_for_resource_constraints_from_z3(
                            node=op
                        ),
                        self.export_consumer_op_for_resource_constraints_from_z3(
                            node=consumer_op,
                            input_index=input_index
                        ),
                        solver
                    )

                total_producer_fork_streams_used_per_core += \
                    producer_max_fork_streams_used_per_core
                total_producer_stream_phases_used_per_core += \
                    producer_max_scatter_stream_phases_used_per_core
                # Ignore the third return value, it will be checked in
                # check_producer_ops2this_op_resource_usage.

        return (total_producer_fork_streams_used_per_core,
                total_producer_stream_phases_used_per_core)

    def get_queues2op_resource_usage_z3_expr(self,
                                             op: Node,
                                             queues: List[Node],
                                             solver: z3.Solver) -> z3.Var:
        """
        Helper function for check_resource_usage which calculates number of
        pipe inputs for queues -> op connections.

        See docstring for get_op2consumer_ops_resource_usage_z3_expr for more
        info.

        Parameters
        ----------
        op:
            Consumer op in this connection.
        queues:
            List of all queues in graph from which queues that feed this op
            are filtered.
        solver:
            z3 Solver object.

        Returns
        -------
        total_queues_op_pipe_inputs:
            Estimate of number of inputs to the queue -> op pipe in form of a
            z3 expression.
        """
        total_queues_op_pipe_inputs = 0

        queues_feeding_this_op = list(filter(
            lambda q: q.name in op.inputs,
            queues
        ))

        for queue in queues_feeding_this_op:
            real_op = self.get_real_consumer_op(queue, op)
            input_index = real_op.inputs.index(queue.name)

            queue_op_pipe_inputs = \
                get_queue2op_resource_usage_z3(
                    self.export_producer_queue_for_resource_constraints_from_z3(
                        node=queue
                    ),
                    self.export_consumer_op_for_resource_constraints_from_z3(
                        node=real_op,
                        input_index=input_index
                    ),
                    solver
                )

            total_queues_op_pipe_inputs += queue_op_pipe_inputs

        return total_queues_op_pipe_inputs

    def get_producer_ops2op_resource_usage_z3_expr(self,
                                                   op: Node,
                                                   ops: List[Node],
                                                   solver: z3.Solver) -> z3.Var:
        """
        Helper function for check_resource_usage which calculates stream
        phases used per core for producer ops -> this op connections.

        See docstring for get_op2consumer_ops_resource_usage_z3_expr for more
        info.

        Parameters
        ----------
        op:
            Consumer op in this connection.
        ops:
            List of all ops in graph from which producer ops for this op are
            filtered.
        solver:
            z3 Solver object.

        Returns
        -------
        total_consumer_stream_phases_used_per_core:
            Maximum stream phases that need to be encoded in blob on a single
            consumer core for gather implementation (for TMs/reblocking and/or
            matmul rows/columns) in form of a z3 expression.
        """
        total_consumer_stream_phases_used_per_core = 0

        ops_feeding_this_op = list(filter(
            lambda producer_op: producer_op.name in op.inputs,
            ops
        ))

        for producer_op in ops_feeding_this_op:
            real_op = self.get_real_consumer_op(producer_op, op)
            input_index = real_op.inputs.index(producer_op.name)

            (producer_max_fork_streams_used_per_core,
             producer_max_scatter_stream_phases_used_per_core,
             consumer_max_gather_stream_phases_used_per_core) = \
                get_op2op_resource_usage_z3(
                    self.export_producer_op_for_resource_constraints_from_z3(
                        node=producer_op
                    ),
                    self.export_consumer_op_for_resource_constraints_from_z3(
                        node=real_op,
                        input_index=input_index
                    ),
                    solver
                )

            total_consumer_stream_phases_used_per_core += \
                consumer_max_gather_stream_phases_used_per_core
            # Ignore other two return values, they are checked in
            # check_this_op2consumer_ops_resource_usage.

        return total_consumer_stream_phases_used_per_core

    def check_resource_usage(self, model_vars: Dict, logger: Logger) -> bool:
        """
        Wrapper function which checks if resource usage constraints are
        satisfied.

        For each op in graph, there are two cases and in each of them two
        subcases that need to be checked.

        (1) op is producer
            (1.1) if op forks to multiple queues, fanout must to be within
                  limits.
            (1.2) if op is producer for multiple consumer ops, cumulative num
                  of fork streams and stream phases per core must be within
                  limits.

        (2) op is consumer
            (2.1) if op is consumer for multiple queues, number of q -> op
                  pipe inputs must be within limits.
            (2.2) if op is consumer for multiple producer ops, cumulative num
                  of stream phases per core must be within limits.

        Since calculations for resource constraints are not trivial, nor their
        z3 form can be written in a simple manner, in order not to overload
        solver with too many constraints (at times making it impossible for it
        to generate solutions in reasonable time), resource constraints (either
        for fork streams, stream phases or number of inputs to queue->op pipe)
        are added only when straightforward calculations with values we have in
        model_vars show some value is out of bounds.

        Parameters
        ----------
        model_vars:
            Dictionary of variable names and their solver.check() generated
            values.
        logger:
            Print info messages along the way if verbose output is desired.

        Returns
        -------
        bool:
            Whether generated config is valid or not based on aforementioned
            criteria.
        """
        for op in self.ops:
            # Aux accumulator variables.
            total_extra_streams_user_per_core = 0
            total_stream_phases_used_per_core = 0
            total_fork_streams_used_per_core = 0
            total_queues_op_pipe_inputs = 0

            # (0): Check how many DRAM buffers this op accesses.
            num_queue_buffers_accessed = self.get_num_queue_buffers_accessed(
                op, self.queues, model_vars
            )

            # (1): this op is producer.

            # (1.1): if op forks to one or more queues, check if fanout is
            # within limits.
            op_queues_fanout = self.get_op2queues_resource_usage(
                op, self.queues
            )

            # (1.2): if op forks to one or more ops, op2op constraints have to
            # be satisfied.
            (producer_max_fork_streams_used_per_core,
             producer_max_scatter_stream_phases_used_per_core) = \
                self.get_op2consumer_ops_resource_usage(
                    op, self.ops, model_vars
                )

            total_stream_phases_used_per_core += \
                producer_max_scatter_stream_phases_used_per_core

            # (2): this op is consumer.

            # (2.1): check if queues2op pipe inputs and prolog streams used are within limits.
            total_queues_op_pipe_inputs, total_prologue_streams_user_per_core = \
                self.get_queues2op_resource_usage(
                    op, self.queues, model_vars
                )

            # (2.2): if this op is consumer for another op, op2op constraints
            # have to be satisfied.
            consumer_max_gather_stream_phases_used_per_core, gather_stream_user_per_core = \
                self.get_producer_ops2op_resource_usage(
                    op, self.ops, model_vars
                )

            total_stream_phases_used_per_core += \
                consumer_max_gather_stream_phases_used_per_core

            total_fork_streams_used_per_core += \
                op_queues_fanout + producer_max_fork_streams_used_per_core

            total_extra_streams_user_per_core += (
                total_fork_streams_used_per_core
                + gather_stream_user_per_core
                + total_prologue_streams_user_per_core
            )

            # Check return values from previous cases.
            if (num_queue_buffers_accessed > self.arch.max_num_queue_buffers_accessed):
                logger.warning(
                    f"Solution rejected because of num_queue_buffers_accessed constraint "
                    f"for op {op.name} ({num_queue_buffers_accessed} > "
                    f"{self.arch.max_num_queue_buffers_accessed})"
                )
                logger.info(f"Constraining number of queue buffers for this op.")
                # This solution did not pass check because number of queue
                # buffers accessed exceeds max allowed value. Thus we constrain
                # num queue buffers for this op.
                self.constrain_num_queue_buffers_accessed(op)
                return False

            if (total_fork_streams_used_per_core >
                self.arch.max_fork_streams_used_per_core):
                logger.warning(
                    f"Solution rejected because of max_fork_streams_per_core constraint "
                    f"for op ({op.name}) ({total_fork_streams_used_per_core} > "
                    f"{self.arch.max_fork_streams_used_per_core})"
                )
                logger.info(f"Adding fork streams constraint for this op.")
                # This solution did not pass check because number of fork streams
                # exceeds max allowed value. Thus we constrain fork streams for
                # this op.
                self.constrain_fork_streams(op)
                return False

            if (total_extra_streams_user_per_core >
                self.arch.max_extra_streams_used_per_core):
                logger.warning(
                    f"Solution rejected because of max_extra_streams_per_core constraint "
                    f"for op ({op.name}) ({total_extra_streams_user_per_core} > "
                    f"{self.arch.max_extra_streams_used_per_core})"
                )
                # TODO: Implement z3 constraints for gather and prefetch streams if necessary.
                return False

            if (total_queues_op_pipe_inputs >
                self.arch.max_queues_op_pipe_inputs):
                logger.warning(
                    f"Solution rejected because of max_queues_op_pipe_inputs constraint "
                    f"for op ({op.name}) ({total_queues_op_pipe_inputs} > "
                    f"{self.arch.max_queues_op_pipe_inputs})"
                )
                logger.info(f"Adding pipe inputs constraint for this op.")
                # This solution did not pass check because number of inputs to
                # queues -> op pipe exceeds max allowed value. Thus we constrain
                # pipe inputs for this op.
                self.constrain_pipe_inputs(op)
                return False

            if (total_stream_phases_used_per_core >
                self.arch.max_stream_phases_per_core):
                logger.warning(
                    f"Solution rejected because of max_stream_phases_per_core constraint "
                    f"for op ({op.name}) ({total_stream_phases_used_per_core} > "
                    f"{self.arch.max_stream_phases_per_core})")
                logger.info(f"Adding stream phases constraint for this op.")
                # This solution did not pass check because number of stream phases
                # exceeds max allowed value. Thus we constrain stream phases for
                # this op.
                self.constrain_stream_phases(op)
                return False

        # Passed all checks, config is ok.
        return True

    def valid_config_callback(self, model_vars: Dict, logger: Logger) -> bool:
        """
        Function used to evaluate validity of a generated config by the solver.

        This function may be used to evaluate whether a solution is valid. It
        can be useful in order to avoid adding very complex constraints to the
        solver and thus increasing its runtime.

        Parameters
        ----------
        model_vars:
            Dictionary of variable names and their generated values
            representing generated config.
        logger:
            Print info messages along the way if verbose output is desired.

        Returns
        -------
        bool:
            Whether generated config is valid or not.
        """
        return self.check_resource_usage(model_vars, logger)

    def push_sweep_context(self, sweep_context: SweepContext) -> None:
        """Sets the curent sweep context.

        Parameters
        ----------
        sweep_context:
            Sweep context.
        """
        self.sweep_context = sweep_context

    def pop_sweep_context(self) -> None:
        """Removes current sweep context."""
        self.sweep_context = None

    def is_multi_grid_node(self, node: Node, model_vars: dict) -> bool:
        """Returns if the current node is placed on multiple cores on the grid."""
        is_multi_grid_x = model_vars[node.grid_size_x.sexpr()] > 1
        is_multi_grid_y = model_vars[node.grid_size_y.sexpr()] > 1

        return is_multi_grid_x or is_multi_grid_y

    def has_reblocking_on_connection(self, producer: Node, consumer: Node, model_vars: dict) -> bool:
        """Returns true if the reblocking is present on a producer->consumer connection."""
        reblocking_var_names = ["grid_size_x", "grid_size_y", "mb_m", "mb_n", "ub_r", "ub_c", "ublock_order"]
        has_any_tms = len(self.op_input_tm_map[consumer.name][producer.name].get_tms()) > 0
        return any(
            model_vars[getattr(producer, var_name).sexpr()] != model_vars[getattr(consumer, var_name).sexpr()]
            for var_name in reblocking_var_names
        ) or has_any_tms

    @staticmethod
    def get_template_var_field(params_dict: Dict[str, any], key_name: str, default_value: any = None) -> Optional[any]:
        """
        Tries to get template field name from a given params dict. If the field associated with the given
        key exists, but it is not a template field (i.e. it doens't start with TEMPLATE_VAR_PREFIX) exception
        is thrown.

        Parameters
        ----------
        params_dict:
            Parameters dict from which to try to extract field.
        key_name:
            Name of the field.
        default_value:
            Default value to return if the field is not present in the params dict.
        """
        field = params_dict.get(key_name, default_value)
        if field and not TemplateNetlistTestBase.is_field_template_var(field):
            raise RuntimeError(f"Field {key_name} is expected to be template field but it is: '{field}'.")
        return field
