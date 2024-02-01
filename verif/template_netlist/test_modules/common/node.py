# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from z3 import *
from typing import Optional, Dict, List, Union

from test_modules.common.enums import Dimension, NodeType
from test_modules.common.constants import TEMPLATE_VAR_PREFIX, SIZE_C_VAR_NAME, SIZE_R_VAR_NAME
from test_modules.common.data_formats import get_tile_size_z3_var
from test_modules.common.resource_constraints import ResourceConstrNode, ResourceConstrNodeSide

class Node:
    """
    Defines netlist nodes that we create constraints for.
    """

    def __init__(self, name: str, type: NodeType) -> None:
        """
        Initializes Node.

        Parameters
        ----------
        name: str
            Node name from the netlist.

        type: NodeType
            Node type.
        """
        self.name = name
        self.type = type
        self.vars = {}

    def add_attribute(self, name: str, var_name: str) -> None:
        """
        Adds attribute to the node and assigns new z3 variable to it.

        Parameters
        ----------
        name: str
            Attribute name.

        var_name: str
            Name for the z3 variable that will be created.
        """
        setattr(self, name, Int(var_name))
        var = getattr(self, name)
        self.vars[var.sexpr()] = var

    def add_template_attribute(self, name: str, template_str: str) -> None:
        """
        Adds attribute to the node and assigns new z3 variable to it.

        Parameters
        ----------
        name: str
            Attribute name.

        template_str: str
            Template string being value of this node in the netlist.
            Used as a z3 variable name so it can later be replaced
            in the generated netlists with the solved variable value.
        """
        self.add_attribute(name, template_str[len(TEMPLATE_VAR_PREFIX):])

    def add_raw_attribute(self, name: str) -> None:
        """
        Adds attribute which does not come from template file and gives it name 
        in the format {node_name}_{attr_name}.

        Parameters
        ----------
        name: str
            Attribute name.
        """
        self.add_attribute(name, f"{self.name}_{name}")

    def add_constrained_attribute(self, 
                                  name: str, 
                                  template_str: str, 
                                  solver: Solver, 
                                  min_value: int = 1) -> None:
        """
        Adds attribute to the node and assigns new z3 variable to it, with 
        constraint that it has to be larger or equal to some specified value.

        Parameters
        ----------
        name: str
            Attribute name.

        template_str: str
            Template string being value of this node in the netlist.
            Used as a z3 variable name so it can later be replaced
            in the generated netlists with the solved variable value.

        solver: Solver
            Solver to add constraint to.

        min_value: int
            Minimal value for constraining the generated z3 variable.
        """
        self.add_template_attribute(name, template_str)
        solver.add(getattr(self, name) >= min_value)

    def is_queue(self) -> bool:
        """Returns True if the node is of queue type, False otherwise."""
        return (self.type == NodeType.InputQueue or
                self.type == NodeType.OutputQueue or
                self.type == NodeType.ParameterQueue or
                self.type == NodeType.ConstantQueue or
                self.type == NodeType.Epoch2EpochQueue)

    def is_eltwise_op(self) -> bool:
        """Returns True if the node is of type unary or binary op."""
        return self.type == NodeType.UnaryOp or self.type == NodeType.BinaryOp

    def is_matmul_op(self) -> bool:
        """Returns True if the node is of type matmul op."""
        return self.type == NodeType.MatmulOp or self.type == NodeType.ScheduledMatmulOp

    def is_reduce_op(self) -> bool:
        """Returns True if the node is of type reduce op."""
        return self.type == NodeType.ReduceOp or self.type == NodeType.ScheduledReduceOp

    def is_nop(self) -> bool:
        """Returns True if the node is op type nop (i.e. datacopy)."""
        assert hasattr(self, "op_type")
        return self.op_type == "nop" or self.op_type == "datacopy"

    def is_reduction(self) -> bool:
        """Returns True if the op is reduction, i.e. matmul or reduce."""
        return self.is_matmul_op() or self.is_reduce_op()

    def get_macro_block_t_size_var(self) -> z3.Var:
        """
        Returns z3 variable representing macro block size of this node with full
        t buffering, in tiles.
        """
        return self.t_dim * self.mb_m * self.mb_n * self.ub_r * self.ub_c

    def get_tiles_along_dim(self, dim: Dimension) -> z3.Var:
        if dim == Dimension.Vertical:
            return self.get_vertical_size_var()
        else:
            return self.get_horizontal_size_var()

    def get_vertical_size_var(self) -> z3.Var:
        """Returns z3 variable representing vertical size of this node, in tiles."""
        return self.grid_size_y * self.mb_m * self.ub_r

    def get_horizontal_size_var(self) -> z3.Var:
        """Returns z3 variable representing horizontal size of this node, in tiles."""
        return self.grid_size_x * self.mb_n * self.ub_c

    def get_grid_size_var(self) -> z3.Var:
        """Returns z3 variable representing grid size of this node."""
        return self.grid_size_x * self.grid_size_y

    def get_size_r_var(self) -> z3.Var:
        """Returns vertical size variable of the input or output queue."""
        assert self.type in [NodeType.InputQueue, NodeType.OutputQueue]
        return getattr(self, SIZE_R_VAR_NAME)

    def get_size_c_var(self) -> z3.Var:
        """Returns horizontal size variable of the input or output queue."""
        assert self.type in [NodeType.InputQueue, NodeType.OutputQueue]
        return getattr(self, SIZE_C_VAR_NAME)

    def get_data_format_var(self) -> z3.Var:
        """
        Returns appropriate data format attribute of this node, to be considered
        when the node is input to an op.

        Returns
        -------
        z3.Var
            Variable representing chosen data format.
        """
        return self.df if self.is_queue() else self.out_df

    def get_input_data_format_var(self, input_idx: int) -> z3.Var:
        """
        Returns data format of the input on a given input index.

        Parameters
        ----------
        input_idx: int
            Input index.
        """
        assert not self.is_queue()
        return getattr(self, f"in{input_idx}_df")

    def get_input_buf_min_size_tiles_var(self, input_index: int) -> Union[z3.Var, int]:
        """
        Returns input_buf_min_size_tiles attribute for input on input_index if
        it has one, otherwise 0.
        """
        return getattr(self, f"input{input_index}_buf_min_size_tiles", 0)

    def get_prologue_var(self, graph_name: str) -> Optional[z3.Var]:
        """Returns prologue attribute of node if it has one, otherwise None."""
        return getattr(self, f"{graph_name}_prologue", None)

    def get_tile_size_var(self) -> z3.Var:
        """Returns z3 variable representing tile size for this node."""
        return get_tile_size_z3_var(self.get_data_format_var())

    def get_attr_from_model(self, model_vars: dict, attr_name: str) -> int:
        """
        Returns solved attribute variable value from the model.

        Parameters
        ----------
        model_vars: dict
            Dictionary of attribute keys and their values.

        attr_name: str
            Attribute name.

        Returns
        -------
        int
        """
        assert hasattr(self, attr_name)
        attr_key = getattr(self, attr_name).sexpr()
        try:
            return model_vars[attr_key]
        except KeyError:
            print(f"Attribute {attr_key} not found in the given model")

    @staticmethod
    def get_queue_type(q_name: str, q_dict: dict) -> NodeType:
        """Returns appropriate node type for a netlist queue node.

        Parameters
        ----------
        q_name: str
            Queue name.
        q_dict: dict
            Queue node dictionary from the netlist.

        Returns
        -------
        NodeType
        """
        if q_dict['type'] == "ram":
            return NodeType.ParameterQueue
        elif q_name.startswith("e2e"):
            return NodeType.Epoch2EpochQueue
        elif q_name.startswith("constant_") or q_name.startswith("lc."):
            return NodeType.ConstantQueue
        elif q_dict['input'] != "HOST":
            return NodeType.OutputQueue
        else:
            return NodeType.InputQueue
