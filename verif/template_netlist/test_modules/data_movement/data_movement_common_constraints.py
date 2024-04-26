# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from z3 import *

from test_modules.common.node import Node

def constrain_node_grid_size_greater_than_one(solver: z3.Solver, node: Node) -> None:
    """Constrains node grid size to be greater than one."""
    solver.add(Or(node.grid_size_x > 1, node.grid_size_y > 1))

def constrain_node_grid_size_equal_to_one(solver: z3.Solver, node: Node) -> None:
    """Constrains node grid size to be equal to one."""
    solver.add(And(node.grid_size_x == 1, node.grid_size_y == 1))

def constrain_no_reblocking_on_connection_except_mb_mn(solver: z3.Solver, producer: Node, consumer: Node) -> None:
    """Constrains producer and consumer nodes in such a way that there is no reblocking on this connection, except for
    macro block size.

    Parameters
    ----------
    solver:
        Z3 solver.
    producer:
        Producer node.
    consumer:
        Consumer node.
    """
    solver.add(
        And(
            producer.grid_size_x == consumer.grid_size_x,
            producer.grid_size_y == consumer.grid_size_y,
            producer.ub_r == consumer.ub_r,
            producer.ub_c == consumer.ub_c,
            producer.ublock_order == consumer.ublock_order,
        )
    )

def constrain_no_reblocking_on_connection(solver: z3.Solver, producer: Node, consumer: Node) -> None:
    """Constrains producer and consumer nodes in such a way that there is no reblocking on this connection.

    Parameters
    ----------
    producer:
        Producer node.
    consumer:
        Consumer node.
    """
    solver.add(
        And(
            producer.grid_size_x == consumer.grid_size_x,
            producer.grid_size_y == consumer.grid_size_y,
            producer.mb_m == consumer.mb_m,
            producer.mb_n == consumer.mb_n,
            producer.ub_r == consumer.ub_r,
            producer.ub_c == consumer.ub_c,
            producer.ublock_order == consumer.ublock_order,
        )
    )

def force_reblocking_on_connection(solver: z3.Solver, producer: Node, consumer: Node) -> None:
    """Constrains producer and consumer nodes in such a way that there is reblocking on this connection.

    Parameters
    ----------
    solver:
        Z3 solver.
    producer:
        Producer node.
    consumer:
        Consumer node.
    """
    solver.add(
        Or(
            producer.grid_size_x != consumer.grid_size_x,
            producer.grid_size_y != consumer.grid_size_y,
            producer.mb_m != consumer.mb_m,
            producer.mb_n != consumer.mb_n,
            producer.ub_r != consumer.ub_r,
            producer.ub_c != consumer.ub_c,
            producer.ublock_order != consumer.ublock_order,
        )
    )