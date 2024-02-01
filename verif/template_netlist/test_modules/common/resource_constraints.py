# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from typing import Dict, Tuple
from enum import Enum
from dataclasses import dataclass
from z3 import *

from test_modules.common.enums import UblockOrder, Dimension, NodeType

class ResourceConstrNodeSide(Enum):
    Producer = 0
    Consumer = 1

@dataclass
class ResourceConstrNode:
    """
    Helper class used to store relevant data from queues and ops used throughout 
    graph tests and TM tests. 
    
    This is a convenient way to provide uniform API for both graph and TM tests,
    which were written in completely different fashion and use it for validating
    solver solutions on op2op and dramq2op constraints.
    """
    
    name: str
    type: NodeType
    grid_size_x: int | z3.Var
    grid_size_y: int | z3.Var
    mb_n: int | z3.Var
    mb_m: int | z3.Var
    ub_c: int | z3.Var
    ub_r: int | z3.Var
    t_dim: int | z3.Var
    reduce_dim: Dimension | None    # Only exists for ReduceOp.
    buf_size_mb: int | z3.Var
    ublock_scan_order: int | z3.Var
    side: ResourceConstrNodeSide = ResourceConstrNodeSide.Producer
    input_index: int = 0
    tms: list = None

    def __post_init__(self):
        self.mb_n_tiles = self.mb_n * self.ub_c
        self.mb_m_tiles = self.mb_m * self.ub_r
        self.ub_c_tiles = self.ub_c
        self.ub_r_tiles = self.ub_r

    @property
    def has_row_major_ublock_scan_order(self) -> z3.Var | bool:
        return self.ublock_scan_order == UblockOrder.r.value

    @property
    def has_column_major_ublock_scan_order(self) -> z3.Var | bool:
        return self.ublock_scan_order == UblockOrder.c.value

def int_div_with_ceil(a: int, b: int) -> int:
    return (a + b - 1) // b
    
def get_reblocking_and_tm_factors(producer: ResourceConstrNode, 
                                  consumer: ResourceConstrNode) -> Dict:
    """ 
    Helper function which calculates reblocking and TM factors.

    In all computations below, if the consumer is matmul, the consumer input 
    mblock/ublock size along the inner dimension corresponds to the 
    mblock_k/ublock_k parameters, and the grid size along the inner dimension 
    should be set to 1. 

    Parameters
    ----------
    producer
        ResourceConstrNode containing necessary info for the producer 
        node, such as grid size, mblock and ublock size, etc.

    consumer
        ResourceConstrNode containing necessary info for the consumer 
        node, such as grid size, mblock and ublock size, etc.

    Returns
    -------
    Dict
        Contains packed info about reblocking and TMs.
    """

    # TODO: Make these fields a part of ConsumerOpConfiguration class in op_input.py as export it from a class
    #  inheriting from TemplateNetlistTestBase.

    reblock_r = False
    reblock_c = False
    gather_r = False
    gather_c = False

    if consumer.type == NodeType.MatmulOp or consumer.type == NodeType.ScheduledMatmulOp:
        inner_matmul_dim = Dimension.Horizontal if consumer.input_index == 0 else Dimension.Vertical
        if inner_matmul_dim == Dimension.Horizontal:
            # matmul input 0 -> outer dimension = r. Need to reblock across rt 
            # dimension, whole ct is broadcast to each row.
            reblock_r = True
            reblock_c = False
            gather_r = False
            gather_c = True
        else:
            # matmul input 1 -> outer dimension = c.
            reblock_r = False
            reblock_c = True
            gather_r = True
            gather_c = False
    else:
        # Eltwise input (doesn't matter if unary or binary), need to reblock 
        # across both rt and ct.
        reblock_r = True
        reblock_c = True
        gather_r = False
        gather_c = False

    no_reblocking = (
        (not reblock_r or producer.grid_size_y == consumer.grid_size_y) and
        (not reblock_c or producer.grid_size_x == consumer.grid_size_x) and
        producer.mb_m == (consumer.mb_m_tiles // consumer.ub_r_tiles) and
        producer.mb_n == (consumer.mb_n_tiles // consumer.ub_c_tiles) and
        producer.ub_r == consumer.ub_r_tiles and
        producer.ub_c == consumer.ub_c_tiles and
        producer.ublock_scan_order == consumer.ublock_scan_order
    )

    # If there are TMs, compute aggregate TM factors.
    tm_has_transpose = False
    rbcast_factor = 1
    cbcast_factor = 1
    vslice_factor = 1
    hslice_factor = 1
    vstack_factor = 1
    hstack_factor = 1

    for tm in consumer.tms:
        tm_type = tm["tm_type"]
        tm_factor = tm["factor"]

        if tm_type == "transpose":
            tm_has_transpose = tm["factor"]
            continue

        if tm_type == "vslice":
            vslice_factor *= tm_factor
        elif tm_type == "hslice":
            hslice_factor *= tm_factor
        elif tm_type == "vstack":
            vstack_factor *= tm_factor
        elif tm_type == "hstack":
            hstack_factor *= tm_factor
        elif tm_type == "rbcast":
            rbcast_factor *= tm_factor
        elif tm_type == "cbcast":
            cbcast_factor *= tm_factor

    return {
        "no_reblocking": no_reblocking, 
        "reblock_r": reblock_r,
        "reblock_c": reblock_c,
        "gather_r": gather_r,
        "gather_c": gather_c,
        "tm_has_transpose": tm_has_transpose,
        "rbcast_factor": rbcast_factor,
        "cbcast_factor": cbcast_factor,
        "vslice_factor": vslice_factor,
        "hslice_factor": hslice_factor,
        "vstack_factor": vstack_factor,
        "hstack_factor": hstack_factor
    }

def get_op2op_resource_usage(producer: ResourceConstrNode, 
                             consumer: ResourceConstrNode) -> Tuple[int, int, int]:
    """ 
    Calculate values relevant for op->op resource usage.

    If an op has DRAM queue outputs, each such output takes one out of 8 DRAM 
    output descriptor structures in NCRISC firmware. This is a separate limited 
    resource in addition to fork streams, which are used for both DRAM and op 
    outputs. Therefore, we need separate resource constrants for DRAM output 
    fork fanout, as well as total output fork fanout.

    For each op->op fork destination, we may have implicit forks due to 
    reblocking and TMs. The sum of the worst-case resource usage of implicit 
    forks across the op->op forking destinations must not exceed the limits
    stated in DeviceArchitecture.

    Moreover, if an op also has an op input, these resources must be added to 
    the ones used for consumer-side functionality.

    Parameters
    ----------
    producer: ResourceConstrNode
        ResourceConstrNode containing necessary info for the producer 
        op, such as grid size, mblock and ublock size, etc.

    consumer: ResourceConstrNode
        ResourceConstrNode containing necessary info for the consumer 
        op, such as grid size, mblock and ublock size, etc.

    Returns
    -------
    tuple of 3 integers
        producer_max_fork_streams_used_per_core
            Maximum streams used on a single producer core for fork 
            implementation.

        producer_max_scatter_stream_phases_used_per_core
            Maximum stream phases that need to be encoded in blob on a single 
            producer core for scatter implementation (for TMs/reblocking).

        consumer_max_gather_stream_phases_used_per_core 
            Maximum stream phases that need to be encoded in blob on a single
            consumer core for gather implementation (for TMs/reblocking and/or 
            matmul rows/columns).
    """
    reblock_tm_params = get_reblocking_and_tm_factors(producer, consumer)
    
    if len(consumer.tms) == 0 and reblock_tm_params["no_reblocking"]:
        producer_max_fork_streams_used_per_core = 1
        producer_max_scatter_stream_phases_used_per_core = 1
        consumer_max_gather_stream_phases_used_per_core = 1

        if reblock_tm_params["gather_r"]:
            consumer_max_gather_stream_phases_used_per_core = producer.grid_size_y
        elif reblock_tm_params["gather_c"]:
            consumer_max_gather_stream_phases_used_per_core = producer.grid_size_x
    else:
        # Block of tiles from a single producer core post-TM, before reblocking 
        # onto the consumer grid. Pessimistically take only slice TMs into 
        # account. (We could make this more complex and less pessimistic by 
        # considering broadcasts and stacks.)
        effective_producer_mblock_tiles_c = (
            producer.mb_m_tiles if reblock_tm_params["tm_has_transpose"] else producer.mb_n_tiles
        ) // reblock_tm_params["hslice_factor"]

        effective_producer_mblock_tiles_r = (
            producer.mb_n_tiles if reblock_tm_params["tm_has_transpose"] else producer.mb_m_tiles
        ) // reblock_tm_params["vslice_factor"]

        # Variables for estimating pipe implementation efficiency (initialized 
        # to the most pessimistic value).
        # How many back-to-back tiles at consumer are received from the same 
        # producer core?
        consumer_min_tiles_from_same_producer_core = 1 

        # How many tiles from each producer core are sent out from contiguous 
        # locations?
        producer_core_min_contiguous_tiles_sent = 1 

        # ublocks are scanned in row-major order, so if producer mblock is 
        # divisible by consumer ublock in c dimension, we're guaranteed a 
        # minimum granularity for the above variables.   
        if effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0:
            consumer_min_tiles_from_same_producer_core = consumer.ub_c_tiles
            producer_core_min_contiguous_tiles_sent = math.gcd(
                consumer.ub_c_tiles, 
                producer.ub_c_tiles
            ) 

            # If the other dimension is also divisible, we get whole consumer 
            # ublocks from a single producer core.
            if effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0:
                consumer_min_tiles_from_same_producer_core *= consumer.ub_r_tiles

                # To get contiguous consumer-side tiles, we need c dimension
                # to be the same.
                if consumer.ub_c_tiles == producer.ub_c_tiles:
                    producer_core_min_contiguous_tiles_sent *= math.gcd(
                        consumer.ub_r_tiles, 
                        producer.ub_r_tiles
                    )

                # If ublock scan order is the same, we can get contiguous tiles 
                # from multiple ublocks. 
                if (producer.has_row_major_ublock_scan_order and consumer.has_row_major_ublock_scan_order):
                    cont_consumer_ublocks_c = math.gcd(
                        effective_producer_mblock_tiles_c, 
                        consumer.mb_n_tiles
                    ) // consumer.ub_c_tiles

                    consumer_min_tiles_from_same_producer_core *= cont_consumer_ublocks_c

                    # If both ublock dimensions are the same, producer buffer 
                    # sends tiles in order across ublocks. 
                    if (producer.ub_r_tiles == consumer.ub_r_tiles and 
                        producer.ub_c_tiles == consumer.ub_c_tiles):
                        producer_core_min_contiguous_tiles_sent *= cont_consumer_ublocks_c

                elif (producer.has_column_major_ublock_scan_order and consumer.has_column_major_ublock_scan_order):
                    cont_consumer_ublocks_r = math.gcd(
                        effective_producer_mblock_tiles_r, 
                        consumer.mb_m_tiles
                    ) // consumer.ub_r_tiles
                    
                    consumer_min_tiles_from_same_producer_core *= cont_consumer_ublocks_r

                    if (producer.ub_r_tiles == consumer.ub_r_tiles and 
                        producer.ub_c_tiles == consumer.ub_c_tiles):
                        producer_core_min_contiguous_tiles_sent *= cont_consumer_ublocks_r

        # Pessimistically, multiply all TM factors along each dimension, even 
        # though in some cases they may cancel out.
        tm_c_factor = (reblock_tm_params["cbcast_factor"] *
                       reblock_tm_params["hslice_factor"] * 
                       reblock_tm_params["hstack_factor"])

        tm_r_factor = (reblock_tm_params["rbcast_factor"] *
                       reblock_tm_params["vslice_factor"] *
                       reblock_tm_params["vstack_factor"])

        effective_consumer_grid_size_r = consumer.grid_size_y * tm_r_factor
        effective_consumer_grid_size_c = consumer.grid_size_x * tm_c_factor

        effective_consumer_grid_size_r_reblock = (
            effective_consumer_grid_size_c if reblock_tm_params["tm_has_transpose"] else
            effective_consumer_grid_size_r
        )
        effective_consumer_grid_size_c_reblock = (
            effective_consumer_grid_size_r if reblock_tm_params["tm_has_transpose"] else
            effective_consumer_grid_size_c
        )

        reblock_tm_fork_r_factor_int = 1
        reblock_tm_fork_c_factor_int = 1

        if producer.grid_size_y % effective_consumer_grid_size_r_reblock == 0:
            reblock_tm_fork_r_factor_int = 1
        elif effective_consumer_grid_size_r_reblock % producer.grid_size_y == 0:
            reblock_tm_fork_r_factor_int = (
                effective_consumer_grid_size_r_reblock // producer.grid_size_y
            )
        else:
            # We must account for cases such as reblocking from dimension 7->6, 
            # where producer cores may have a fanout of 2 or 3 because of 
            # non-divisibility.
            reblock_tm_fork_r_factor_int = int_div_with_ceil(
                effective_consumer_grid_size_r_reblock,
                producer.grid_size_y
            ) + 1

        if producer.grid_size_x % effective_consumer_grid_size_c_reblock == 0:
            reblock_tm_fork_c_factor_int = 1
        elif effective_consumer_grid_size_c_reblock % producer.grid_size_x == 0:
            reblock_tm_fork_c_factor_int = (
                effective_consumer_grid_size_c_reblock // producer.grid_size_x
            )
        else:
            reblock_tm_fork_c_factor_int = int_div_with_ceil(
                effective_consumer_grid_size_c_reblock,
                producer.grid_size_x
            ) + 1

        # First return value: max streams used by a single core for forking.
        producer_max_fork_streams_used_per_core = 1
        if reblock_tm_params["reblock_r"]:
            producer_max_fork_streams_used_per_core *= reblock_tm_fork_r_factor_int
        if reblock_tm_params["reblock_c"]:
            producer_max_fork_streams_used_per_core *= reblock_tm_fork_c_factor_int

        # The final result can't be higher than the total number of consumer 
        # cores (for matmul only along the single reblocking dimension).
        consumer_effective_grid_size = (
            (consumer.grid_size_y if reblock_tm_params["reblock_r"] else 1) *
            (consumer.grid_size_x if reblock_tm_params["reblock_c"] else 1) 
        )

        if (producer_max_fork_streams_used_per_core > 
            consumer_effective_grid_size):
            producer_max_fork_streams_used_per_core = consumer_effective_grid_size

        # Second return value: max stream phases that need to be encoded in 
        # the blob on a single producer core. Each access to the next tile 
        # that's non-contiguous implies a stream phase transition that requires
        # a new blob entry. Worst case: we access tiles at random indexes across
        # the output buffer stream. Broadcast may be implemented efficiently, 
        # but stack requires multiple phases to send out multiple copies of the
        # same tile.
        bcast_factor = reblock_tm_params["cbcast_factor"] * reblock_tm_params["rbcast_factor"]

        if bcast_factor > 1:
            # For now, assume we can do efficient broadcast implementation only
            # if the producer data format is a single tile. Later we can revise 
            # this if it's too restrictive (either by adding other useful cases
            # that will be efficiently implemented by the present pipegen, or by
            # enhancing pipegen to handle more complex looping efficiently). 
            if (producer.t_dim * 
                producer.grid_size_x * producer.mb_n_tiles *
                producer.grid_size_y * producer.mb_m_tiles) == 1:
                bcast_factor = 1

        producer_max_scatter_stream_phases_used_per_core = (
            producer.mb_n_tiles * producer.mb_m_tiles *
            reblock_tm_params["hstack_factor"] * 
            reblock_tm_params["vstack_factor"] *
            producer.buf_size_mb * bcast_factor
        )   

        # Divide worst-case with the minimum contiguous tiles sent from producer
        # as calculated above. 
        producer_max_scatter_stream_phases_used_per_core = int_div_with_ceil(
            producer_max_scatter_stream_phases_used_per_core,
            producer_core_min_contiguous_tiles_sent
        )

        # Third return value: max stream phases that need to be encoded in the 
        # blob on a single consumer core. Each transition between producer cores
        # when gathering tiles implies a stream phase transition that requires a
        # new blob entry. If we have slice, multiple consumer t's correspond to 
        # a single producer t, after which pipes are periodic.
        total_slice_factor = (reblock_tm_params["vslice_factor"] * 
                              reblock_tm_params["hslice_factor"])
        gather_pipe_period_tiles = (
            producer.buf_size_mb * total_slice_factor * 
            consumer.mb_m_tiles * consumer.mb_n_tiles
        )
        
        # Need to multiply with 2 since we're using 2-stage gather for 
        # performance. 
        consumer_max_gather_stream_phases_used_per_core = (
            2 * int_div_with_ceil(
                gather_pipe_period_tiles,
                consumer_min_tiles_from_same_producer_core
            )
        )

    return (producer_max_fork_streams_used_per_core, 
            producer_max_scatter_stream_phases_used_per_core,
            consumer_max_gather_stream_phases_used_per_core)
        
def get_queue2op_resource_usage(queue: ResourceConstrNode, 
                                op: ResourceConstrNode) -> int:
    """ 
    Calculate values relevant for queue->op resource usage.

    DRAM pipes with >2K inputs might overflow the blob space. This function
    estimates pipe input size, with accounted granulation factor if shapes of
    producer and consumer are such that allow for contiguous reading of tiles.

    Parameters
    ----------    
    queue
        ResourceConstrNode containing necessary info for the producer 
        queue, such as grid size, mblock and ublock size, etc.

    op
        ResourceConstrNode containing necessary info for the consumer 
        op, such as grid size, mblock and ublock size, etc.

    Returns
    -------
    dram_op_pipe_inputs
        Estimate of number of inputs to the queue -> op pipe.
    """
    reblock_tm_params = get_reblocking_and_tm_factors(queue, op)

    if len(op.tms) == 0 and reblock_tm_params["no_reblocking"]:
        # Simple in-order pipe.
        queue_op_pipe_inputs = 1
    else:
        # Take into account the granularity of tile access.
        # Block of tiles from a single producer core post-TM, before reblocking 
        # onto the consumer grid. Pessimistically take only slice TMs into 
        # account. We could make this more complex and less pessimistic by 
        # considering broadcasts and stacks.
        effective_queue_mblock_tiles_c = (
            queue.mb_m_tiles if reblock_tm_params["tm_has_transpose"] else queue.mb_n_tiles
        ) // reblock_tm_params["hslice_factor"]
        
        effective_queue_mblock_tiles_r = (
            queue.mb_n_tiles if reblock_tm_params["tm_has_transpose"] else queue.mb_m_tiles
        ) // reblock_tm_params["vslice_factor"]

        # Factor >=1 which describes how many tiles are read contiguously,
        # effectively reducing number of inputs to the queue -> op pipe by 
        # dividing total input tiles per op core. In worst case, tiles are read
        # one by one.
        queue_min_contiguous_tiles_read = 1

        # ublocks are scanned in row-major order, so if dram mblock is divisible
        # by op ublock in c dimension, we're guaranteed a minimum granularity
        # for the above variable.   
        if effective_queue_mblock_tiles_c % op.ub_c_tiles == 0:
            queue_min_contiguous_tiles_read = math.gcd(op.ub_c_tiles, queue.ub_c_tiles) 

            # If the other dimension is also divisible, we get whole op ublocks
            # from a single dram buffer.
            if effective_queue_mblock_tiles_r % op.ub_r_tiles == 0:
                # To get contiguous op-side tiles, we need c dimension to be the
                # same.
                if op.ub_c_tiles == queue.ub_c_tiles:
                    queue_min_contiguous_tiles_read *= math.gcd(op.ub_r_tiles, queue.ub_r_tiles)

                # If ublock scan order is the same, we can get contiguous tiles 
                # from multiple ublocks. If both ublock dimensions are the same, 
                # producer buffer sends tiles in order across ublocks. 
                if (
                    queue.has_row_major_ublock_scan_order and op.has_row_major_ublock_scan_order and
                    queue.ub_r_tiles == op.ub_r_tiles and queue.ub_c_tiles == op.ub_c_tiles
                ):
                    cont_consumer_ublocks_c = math.gcd(
                        effective_queue_mblock_tiles_c, 
                        op.mb_n_tiles
                    ) // op.ub_c_tiles

                    queue_min_contiguous_tiles_read *= cont_consumer_ublocks_c

                elif (
                    queue.has_column_major_ublock_scan_order and op.has_column_major_ublock_scan_order and
                    queue.ub_r_tiles == op.ub_r_tiles and queue.ub_c_tiles == op.ub_c_tiles
                ):
                    cont_consumer_ublocks_r = math.gcd(
                        effective_queue_mblock_tiles_r, 
                        op.mb_m_tiles
                    ) // op.ub_r_tiles
                    
                    queue_min_contiguous_tiles_read *= cont_consumer_ublocks_r
                    
        # Total tiles per input per consumer op core.
        input_tiles_per_op_core = op.t_dim * op.mb_m_tiles * op.mb_n_tiles

        queue_op_pipe_inputs = input_tiles_per_op_core // queue_min_contiguous_tiles_read

    return queue_op_pipe_inputs

def get_num_buffers_accessed_along_dim(op: ResourceConstrNode, 
                                       queue: ResourceConstrNode,
                                       dim: Dimension) -> int:
    """
    Calculates factor representing number of dram queue buffers 'op' must
    access across given dimension.

    Parameters
    ----------
    op
        Op of interest in op-queue connection.

    queue
        Queue of interest in op-queue connection.

    dim
        Across which dimesion should num of buffers be calculated.

    Returns
    -------
    int
        Number of dram queue buffers 'op' must access across given dimension.
    """
    if dim == Dimension.Horizontal:
        grid_size_attr = "grid_size_x"
        mb_attr = "mb_n"
        ub_attr = "ub_c"
    else:
        grid_size_attr = "grid_size_y"
        mb_attr = "mb_m"
        ub_attr = "ub_r"

    op_grid_size = getattr(op, grid_size_attr)
    queue_grid_size = getattr(queue, grid_size_attr)
    queue_mb_size = getattr(queue, mb_attr)
    queue_ub_size = getattr(queue, ub_attr)
    
    queue_num_tiles = queue_grid_size * queue_mb_size * queue_ub_size
    tiles_per_op_core_needed = queue_num_tiles // op_grid_size
    tiles_per_dram_buf = queue_mb_size * queue_ub_size

    if queue_num_tiles % op_grid_size == 0:
        if tiles_per_op_core_needed % tiles_per_dram_buf == 0:
            return tiles_per_op_core_needed // tiles_per_dram_buf
        elif tiles_per_dram_buf % tiles_per_op_core_needed == 0:
            return 1
        else:
            return int_div_with_ceil(tiles_per_op_core_needed, tiles_per_dram_buf) + 1
    else:
        return queue_grid_size

def int_div_with_ceil_z3(a: z3.Var, b: z3.Var) -> z3.Var:
    return (a + b - 1) / b

def gcd_z3(a: z3.Var, b: z3.Var, depth: int = 0) -> z3.Var:
    """
    Helper function to calculate GCD for two z3 vars. 
    
    Since there is no other way to write it other then in form of a z3
    expression, infinite recursion calls happen in else branch of If statement.
    In order to escape from them, a limit of 10 is used ad hoc, which should
    return correct results for ranges of a and b we are dealing with. If not,
    just increase it.
    """
    if depth > 10:
        return b
    
    return If(a == 0, b, gcd_z3(b % a, a, depth + 1))

def get_reblocking_and_tm_factors_z3(producer: ResourceConstrNode,
                                     consumer: ResourceConstrNode,
                                     solver: z3.Solver) -> Dict:
    """
    Rewrite of get_reblocking_and_tm_factors in z3 syntax.

    See docstring for get_reblocking_and_tm_factors.
    """
    reblock_tm_params = get_reblocking_and_tm_factors(producer, consumer)

    no_reblocking_z3 = Bool(f"no_reblocking_{producer.name}_{consumer.name}")
    reblock_r_z3 = Bool(f"reblock_r_{producer.name}_{consumer.name}")
    reblock_c_z3 = Bool(f"reblock_c_{producer.name}_{consumer.name}")
    gather_r_z3 = Bool(f"gather_r_{producer.name}_{consumer.name}")
    gather_c_z3 = Bool(f"gather_c_{producer.name}_{consumer.name}")
    tm_has_transpose_z3 = Bool(f"tm_has_transpose_{producer.name}_{consumer.name}")
    rbcast_factor_z3 = Int(f"rbcast_factor_{producer.name}_{consumer.name}")
    cbcast_factor_z3 = Int(f"cbcast_factor_{producer.name}_{consumer.name}")
    vslice_factor_z3 = Int(f"vslice_factor_{producer.name}_{consumer.name}")
    hslice_factor_z3 = Int(f"hslice_factor_{producer.name}_{consumer.name}")
    vstack_factor_z3 = Int(f"vstack_factor_{producer.name}_{consumer.name}")
    hstack_factor_z3 = Int(f"hstack_factor_{producer.name}_{consumer.name}")

    no_reblocking = If(
        And(
            Or(not reblock_tm_params["reblock_r"], 
               producer.grid_size_y == consumer.grid_size_y),
            Or(not reblock_tm_params["reblock_c"], 
               producer.grid_size_x == consumer.grid_size_x),
            producer.mb_m == (consumer.mb_m_tiles / consumer.ub_r_tiles),
            producer.mb_n == (consumer.mb_n_tiles / consumer.ub_c_tiles),
            producer.ub_r == consumer.ub_r_tiles,
            producer.ub_c == consumer.ub_c_tiles,
            producer.ublock_scan_order == consumer.ublock_scan_order
        ),
        True,
        False
    )

    solver.add(
        no_reblocking_z3 == no_reblocking,
        reblock_r_z3 == reblock_tm_params["reblock_r"],
        reblock_c_z3 == reblock_tm_params["reblock_c"],
        gather_r_z3 == reblock_tm_params["gather_r"],
        gather_c_z3 == reblock_tm_params["gather_c"],
        tm_has_transpose_z3 == reblock_tm_params["tm_has_transpose"],
        rbcast_factor_z3 == reblock_tm_params["rbcast_factor"],
        cbcast_factor_z3 == reblock_tm_params["cbcast_factor"],
        vslice_factor_z3 == reblock_tm_params["vslice_factor"],
        hslice_factor_z3 == reblock_tm_params["hslice_factor"],
        vstack_factor_z3 == reblock_tm_params["vstack_factor"],
        hstack_factor_z3 == reblock_tm_params["hstack_factor"]
    )

    return {
        "no_reblocking": no_reblocking_z3,
        "reblock_r": reblock_r_z3,
        "reblock_c": reblock_c_z3,
        "gather_r": gather_r_z3,
        "gather_c": gather_c_z3,
        "tm_has_transpose": tm_has_transpose_z3,
        "rbcast_factor": rbcast_factor_z3,
        "cbcast_factor": cbcast_factor_z3,
        "vslice_factor": vslice_factor_z3,
        "hslice_factor": hslice_factor_z3,
        "vstack_factor": vstack_factor_z3,
        "hstack_factor": hstack_factor_z3
    }

def get_op2op_resource_usage_z3(producer: ResourceConstrNode, 
                                consumer: ResourceConstrNode, 
                                solver: z3.Solver) -> Tuple[z3.Var, z3.Var, z3.Var]:
    """
    Rewrite of get_op2op_resource_usage in z3 syntax.

    See docstring for get_op2op_resource_usage for more info.
    """
    def get_first_case(producer, consumer, reblock_tm_params, solver):
        producer_max_fork_streams_used_per_core = \
            Int(f"producer_max_fork_streams_used_per_core_1_"
                f"{producer.name}_{consumer.name}")
        producer_max_scatter_stream_phases_used_per_core = \
            Int(f"producer_max_scatter_stream_phases_used_per_core_1_"
                f"{producer.name}_{consumer.name}")
        consumer_max_gather_stream_phases_used_per_core = \
            Int(f"consumer_max_gather_stream_phases_used_per_core_1_"
                f"{producer.name}_{consumer.name}")

        solver.add(
            producer_max_fork_streams_used_per_core == 1,
            producer_max_scatter_stream_phases_used_per_core == 1,
            If(
                reblock_tm_params["gather_r"] == True,
                consumer_max_gather_stream_phases_used_per_core == producer.grid_size_y,
                If(
                    reblock_tm_params["gather_c"] == True,
                    consumer_max_gather_stream_phases_used_per_core == producer.grid_size_x,
                    consumer_max_gather_stream_phases_used_per_core == 1
                )
            )
        )

        return (producer_max_fork_streams_used_per_core, 
                producer_max_scatter_stream_phases_used_per_core,
                consumer_max_gather_stream_phases_used_per_core)

    def get_second_case(
        producer: ResourceConstrNode, 
        consumer: ResourceConstrNode, 
        reblock_tm_params: dict, 
        solver: z3.Solver
    ):
        # ----- effective_producer_mblock_tiles_c -----
        effective_producer_mblock_tiles_c = Int(f"effective_producer_mblock_tiles_c_"
                                                f"{producer.name}_{consumer.name}")

        solver.add(
            If(
                reblock_tm_params["tm_has_transpose"] == True,
                effective_producer_mblock_tiles_c == (
                    producer.mb_m_tiles / reblock_tm_params["hslice_factor"]
                ),
                effective_producer_mblock_tiles_c == (
                    producer.mb_n_tiles / reblock_tm_params["hslice_factor"]
                )
            )
        )

        # ----- effective_producer_mblock_tiles_r -----
        effective_producer_mblock_tiles_r = Int(f"effective_producer_mblock_tiles_r_"
                                                f"{producer.name}_{consumer.name}")

        solver.add(
            If(
                reblock_tm_params["tm_has_transpose"] == True,
                effective_producer_mblock_tiles_r == (
                    producer.mb_n_tiles / reblock_tm_params["vslice_factor"]
                ),
                effective_producer_mblock_tiles_r == (
                    producer.mb_m_tiles / reblock_tm_params["vslice_factor"]
                )
            )
        )

        # ----- consumer_min_tiles_from_same_producer_core -----
        consumer_min_tiles_from_same_producer_core = \
            Int(f"consumer_min_tiles_from_same_producer_core_"
                f"{producer.name}_{consumer.name}")
     
        cont_consumer_ublocks_c = gcd_z3(
            effective_producer_mblock_tiles_c, 
            consumer.mb_n_tiles
        ) / consumer.ub_c_tiles

        cont_consumer_ublocks_r = gcd_z3(
            effective_producer_mblock_tiles_r, 
            consumer.mb_m_tiles
        ) / consumer.ub_r_tiles

        solver.add(
            Implies(
                effective_producer_mblock_tiles_c % consumer.ub_c_tiles != 0,
                consumer_min_tiles_from_same_producer_core == 1
            )
        )
        
        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles != 0
                ),
                consumer_min_tiles_from_same_producer_core == consumer.ub_c_tiles
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    producer.ublock_scan_order != consumer.ublock_scan_order
                ),
                consumer_min_tiles_from_same_producer_core == (
                    consumer.ub_c_tiles * consumer.ub_r_tiles
                )
            )
        )
        
        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    producer.has_row_major_ublock_scan_order,
                    consumer.has_row_major_ublock_scan_order
                ),
                consumer_min_tiles_from_same_producer_core == (
                    consumer.ub_c_tiles * consumer.ub_r_tiles * cont_consumer_ublocks_c
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    producer.has_column_major_ublock_scan_order,
                    consumer.has_column_major_ublock_scan_order
                ),
                consumer_min_tiles_from_same_producer_core == (
                    consumer.ub_c_tiles * consumer.ub_r_tiles * cont_consumer_ublocks_r
                )
            )
        )

        # ----- producer_core_min_contiguous_tiles_sent -----
        producer_core_min_contiguous_tiles_sent = \
            Int(f"producer_core_min_contiguous_tiles_sent_"
                f"{producer.name}_{consumer.name}")

        solver.add(
            Implies(
                effective_producer_mblock_tiles_c % consumer.ub_c_tiles != 0,
                producer_core_min_contiguous_tiles_sent == 1
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles != 0
                ),
                producer_core_min_contiguous_tiles_sent == gcd_z3(
                    consumer.ub_c_tiles, 
                    producer.ub_c_tiles
                ) 
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles != producer.ub_c_tiles,
                    producer.ublock_scan_order != consumer.ublock_scan_order
                ),
                producer_core_min_contiguous_tiles_sent == gcd_z3(
                    consumer.ub_c_tiles, 
                    producer.ub_c_tiles
                ) 
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles == producer.ub_c_tiles,
                    producer.ublock_scan_order != consumer.ublock_scan_order
                ),
                producer_core_min_contiguous_tiles_sent == (
                    gcd_z3(consumer.ub_c_tiles, producer.ub_c_tiles) *
                    gcd_z3(consumer.ub_r_tiles, producer.ub_r_tiles)
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles == producer.ub_c_tiles,
                    producer.has_row_major_ublock_scan_order,
                    consumer.has_row_major_ublock_scan_order,
                    Not(And(
                        producer.ub_r_tiles == consumer.ub_r_tiles,
                        producer.ub_c_tiles == consumer.ub_c_tiles
                    ))
                ),
                producer_core_min_contiguous_tiles_sent == (
                    gcd_z3(consumer.ub_c_tiles, producer.ub_c_tiles) *
                    gcd_z3(consumer.ub_r_tiles, producer.ub_r_tiles)
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles == producer.ub_c_tiles,
                    producer.has_row_major_ublock_scan_order,
                    consumer.has_row_major_ublock_scan_order,
                    producer.ub_r_tiles == consumer.ub_r_tiles,
                    producer.ub_c_tiles == consumer.ub_c_tiles
                ),
                producer_core_min_contiguous_tiles_sent == (
                    gcd_z3(consumer.ub_c_tiles, producer.ub_c_tiles) *
                    gcd_z3(consumer.ub_r_tiles, producer.ub_r_tiles) *
                    cont_consumer_ublocks_c
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles != producer.ub_c_tiles,
                    producer.has_row_major_ublock_scan_order,
                    consumer.has_row_major_ublock_scan_order,
                    Not(And(
                        producer.ub_r_tiles == consumer.ub_r_tiles,
                        producer.ub_c_tiles == consumer.ub_c_tiles
                    ))
                ),
                producer_core_min_contiguous_tiles_sent == (
                    gcd_z3(consumer.ub_c_tiles, producer.ub_c_tiles) 
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles != producer.ub_c_tiles,
                    producer.has_row_major_ublock_scan_order,
                    consumer.has_row_major_ublock_scan_order,
                    producer.ub_r_tiles == consumer.ub_r_tiles,
                    producer.ub_c_tiles == consumer.ub_c_tiles
                ),
                producer_core_min_contiguous_tiles_sent == (
                    gcd_z3(consumer.ub_c_tiles, producer.ub_c_tiles) *
                    cont_consumer_ublocks_c
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles == producer.ub_c_tiles,
                    producer.has_column_major_ublock_scan_order,
                    consumer.has_column_major_ublock_scan_order,
                    Not(And(
                        producer.ub_r_tiles == consumer.ub_r_tiles,
                        producer.ub_c_tiles == consumer.ub_c_tiles
                    ))
                ),
                producer_core_min_contiguous_tiles_sent == (
                    gcd_z3(consumer.ub_c_tiles, producer.ub_c_tiles) *
                    gcd_z3(consumer.ub_r_tiles, producer.ub_r_tiles)
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles == producer.ub_c_tiles,
                    producer.has_column_major_ublock_scan_order,
                    consumer.has_column_major_ublock_scan_order,
                    producer.ub_r_tiles == consumer.ub_r_tiles,
                    producer.ub_c_tiles == consumer.ub_c_tiles
                ),
                producer_core_min_contiguous_tiles_sent == (
                    gcd_z3(consumer.ub_c_tiles, producer.ub_c_tiles) *
                    gcd_z3(consumer.ub_r_tiles, producer.ub_r_tiles) *
                    cont_consumer_ublocks_r
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles != producer.ub_c_tiles,
                    producer.has_column_major_ublock_scan_order,
                    consumer.has_column_major_ublock_scan_order,
                    Not(And(
                        producer.ub_r_tiles == consumer.ub_r_tiles,
                        producer.ub_c_tiles == consumer.ub_c_tiles
                    ))
                ),
                producer_core_min_contiguous_tiles_sent == (
                    gcd_z3(consumer.ub_c_tiles, producer.ub_c_tiles) 
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_producer_mblock_tiles_c % consumer.ub_c_tiles == 0,
                    effective_producer_mblock_tiles_r % consumer.ub_r_tiles == 0,
                    consumer.ub_c_tiles != producer.ub_c_tiles,
                    producer.has_column_major_ublock_scan_order,
                    consumer.has_column_major_ublock_scan_order,
                    producer.ub_r_tiles == consumer.ub_r_tiles,
                    producer.ub_c_tiles == consumer.ub_c_tiles
                ),
                producer_core_min_contiguous_tiles_sent == (
                    gcd_z3(consumer.ub_c_tiles, producer.ub_c_tiles) *
                    cont_consumer_ublocks_r
                )
            )
        )

        # ----- effective_consumer_grid_size_r_reblock -----
        effective_consumer_grid_size_r_reblock = \
            Int(f"effective_consumer_grid_size_r_reblock_"
                f"{producer.name}_{consumer.name}")

        tm_c_factor = (reblock_tm_params["cbcast_factor"] *
                       reblock_tm_params["hslice_factor"] * 
                       reblock_tm_params["hstack_factor"])

        tm_r_factor = (reblock_tm_params["rbcast_factor"] *
                       reblock_tm_params["vslice_factor"] *
                       reblock_tm_params["vstack_factor"])

        effective_consumer_grid_size_r = consumer.grid_size_y * tm_r_factor
        effective_consumer_grid_size_c = consumer.grid_size_x * tm_c_factor

        solver.add(
            If(
                reblock_tm_params["tm_has_transpose"] == True,
                effective_consumer_grid_size_r_reblock == effective_consumer_grid_size_c,
                effective_consumer_grid_size_r_reblock == effective_consumer_grid_size_r
            )
        )

        # ----- effective_consumer_grid_size_c_reblock -----
        effective_consumer_grid_size_c_reblock = \
            Int(f"effective_consumer_grid_size_c_reblock_"
                f"{producer.name}_{consumer.name}")

        solver.add(
            If(
                reblock_tm_params["tm_has_transpose"] == True,
                effective_consumer_grid_size_c_reblock == effective_consumer_grid_size_r,
                effective_consumer_grid_size_c_reblock == effective_consumer_grid_size_c
            )
        )

        # ----- reblock_tm_fork_r_factor_int -----
        reblock_tm_fork_r_factor_int = \
            Int(f"reblock_tm_fork_r_factor_int_"
                f"{producer.name}_{consumer.name}")

        solver.add(
            Implies(
                producer.grid_size_y % effective_consumer_grid_size_r_reblock == 0,
                reblock_tm_fork_r_factor_int == 1
            )
        )

        solver.add(
            Implies(
                And(
                    producer.grid_size_y % effective_consumer_grid_size_r_reblock != 0,
                    effective_consumer_grid_size_r_reblock % producer.grid_size_y == 0
                ),
                reblock_tm_fork_r_factor_int == (
                    effective_consumer_grid_size_r_reblock / producer.grid_size_y
                )
            )
        )

        solver.add(
            Implies(
                And(
                    producer.grid_size_y % effective_consumer_grid_size_r_reblock != 0,
                    effective_consumer_grid_size_r_reblock % producer.grid_size_y != 0
                ),
                reblock_tm_fork_r_factor_int == int_div_with_ceil_z3(
                    effective_consumer_grid_size_r_reblock,
                    producer.grid_size_y
                ) + 1
            )
        )

        # ----- reblock_tm_fork_c_factor_int -----
        reblock_tm_fork_c_factor_int = Int(f"reblock_tm_fork_c_factor_int_"
                                           f"{producer.name}_{consumer.name}")

        solver.add(
            Implies(
                producer.grid_size_x % effective_consumer_grid_size_c_reblock == 0,
                reblock_tm_fork_c_factor_int == 1
            )
        )

        solver.add(
            Implies(
                And(
                    producer.grid_size_x % effective_consumer_grid_size_c_reblock != 0,
                    effective_consumer_grid_size_c_reblock % producer.grid_size_x == 0
                ),
                reblock_tm_fork_c_factor_int == (
                    effective_consumer_grid_size_c_reblock / producer.grid_size_x
                )
            )
        )

        solver.add(
            Implies(
                And(
                    producer.grid_size_x % effective_consumer_grid_size_c_reblock != 0,
                    effective_consumer_grid_size_c_reblock % producer.grid_size_x != 0
                ),
                reblock_tm_fork_c_factor_int == int_div_with_ceil_z3(
                    effective_consumer_grid_size_c_reblock,
                    producer.grid_size_x
                ) + 1
            )
        )

        # ----- consumer_effective_grid_size -----
        consumer_effective_grid_size = Int(f"consumer_effective_grid_size_"
                                           f"{producer.name}_{consumer.name}")

        solver.add(
            Implies(
                And(
                    reblock_tm_params["reblock_r"] == True,
                    reblock_tm_params["reblock_c"] == False
                ),
                consumer_effective_grid_size == consumer.grid_size_y
            )
        )

        solver.add(
            Implies(
                And(
                    reblock_tm_params["reblock_r"] == False,
                    reblock_tm_params["reblock_c"] == True
                ),
                consumer_effective_grid_size == consumer.grid_size_x
            )
        )

        solver.add(
            Implies(
                And(
                    reblock_tm_params["reblock_r"] == True,
                    reblock_tm_params["reblock_c"] == True
                ),
                consumer_effective_grid_size == (
                    consumer.grid_size_x * consumer.grid_size_y
                )
            )
        )

        solver.add(
            Implies(
                And(
                    reblock_tm_params["reblock_r"] == False,
                    reblock_tm_params["reblock_c"] == False
                ),
                consumer_effective_grid_size == 1
            )
        )

        # ----- producer_max_fork_streams_used_per_core -----
        producer_max_fork_streams_used_per_core = \
            Int(f"producer_max_fork_streams_used_per_core_2_"
                f"{producer.name}_{consumer.name}")
        producer_max_fork_streams_used_per_core_aux = \
            Int(f"producer_max_fork_streams_used_per_core_aux_"
                f"{producer.name}_{consumer.name}")

        solver.add(
            Implies(
                And(
                    reblock_tm_params["reblock_r"] == True,
                    reblock_tm_params["reblock_c"] == False
                ),
                producer_max_fork_streams_used_per_core_aux == reblock_tm_fork_r_factor_int
            )
        )

        solver.add(
            Implies(
                And(
                    reblock_tm_params["reblock_r"] == False,
                    reblock_tm_params["reblock_c"] == True
                ),
                producer_max_fork_streams_used_per_core_aux == reblock_tm_fork_c_factor_int
            )
        )

        solver.add(
            Implies(
                And(
                    reblock_tm_params["reblock_r"] == True,
                    reblock_tm_params["reblock_c"] == True
                ),
                producer_max_fork_streams_used_per_core_aux == (
                    reblock_tm_fork_r_factor_int * reblock_tm_fork_c_factor_int
                )
            )
        )

        solver.add(
            Implies(
                And(
                    reblock_tm_params["reblock_r"] == False,
                    reblock_tm_params["reblock_c"] == False
                ),
                producer_max_fork_streams_used_per_core_aux == 1
            )
        )

        solver.add(
            If(
                (producer_max_fork_streams_used_per_core_aux > 
                 consumer_effective_grid_size),
                producer_max_fork_streams_used_per_core == \
                    consumer_effective_grid_size,
                producer_max_fork_streams_used_per_core == \
                    producer_max_fork_streams_used_per_core_aux
            )
        )

        # ----- bcast_factor -----
        bcast_factor = Int(f"bcast_factor_{producer.name}_{consumer.name}")

        solver.add(
            If(
                # if
                And(
                    (reblock_tm_params["cbcast_factor"] * 
                     reblock_tm_params["rbcast_factor"]) > 1,
                    (producer.t_dim * 
                     producer.grid_size_x * producer.grid_size_y *  
                     producer.mb_n_tiles * producer.mb_m_tiles) == 1
                ),
                # then
                bcast_factor == 1,
                # else
                bcast_factor == (
                    reblock_tm_params["cbcast_factor"] * 
                    reblock_tm_params["rbcast_factor"]
                )
            )
        )

        # ----- producer_max_scatter_stream_phases_used_per_core -----
        producer_max_scatter_stream_phases_used_per_core = \
            Int(f"producer_max_scatter_stream_phases_used_per_core_2_"
                f"{producer.name}_{consumer.name}")

        producer_max_scatter_stream_phases_used_per_core_aux = (
            producer.mb_n_tiles * producer.mb_m_tiles *
            reblock_tm_params["hstack_factor"] * 
            reblock_tm_params["vstack_factor"] *
            producer.buf_size_mb * bcast_factor
        )  

        solver.add(
            producer_max_scatter_stream_phases_used_per_core == int_div_with_ceil_z3(
                producer_max_scatter_stream_phases_used_per_core_aux,
                producer_core_min_contiguous_tiles_sent
            )
        )

        # ----- consumer_max_gather_stream_phases_used_per_core -----
        consumer_max_gather_stream_phases_used_per_core = \
            Int(f"consumer_max_gather_stream_phases_used_per_core_2_"
                f"{producer.name}_{consumer.name}")

        total_slice_factor = (reblock_tm_params["vslice_factor"] * 
                              reblock_tm_params["hslice_factor"])

        gather_pipe_period_tiles = (
            producer.buf_size_mb * total_slice_factor * 
            consumer.mb_m_tiles * consumer.mb_n_tiles
        )

        solver.add(
            consumer_max_gather_stream_phases_used_per_core == (
                2 * int_div_with_ceil_z3(
                    gather_pipe_period_tiles,
                    consumer_min_tiles_from_same_producer_core
                )
            )
        )

        return (producer_max_fork_streams_used_per_core, 
                producer_max_scatter_stream_phases_used_per_core,
                consumer_max_gather_stream_phases_used_per_core)

    reblock_tm_params = get_reblocking_and_tm_factors_z3(producer, consumer, solver)

    (producer_max_fork_streams_used_per_core_1,
     producer_max_scatter_stream_phases_used_per_core_1,
     consumer_max_gather_stream_phases_used_per_core_1) = \
        get_first_case(producer, consumer, reblock_tm_params, solver)

    (producer_max_fork_streams_used_per_core_2,
     producer_max_scatter_stream_phases_used_per_core_2,
     consumer_max_gather_stream_phases_used_per_core_2) = \
        get_second_case(producer, consumer, reblock_tm_params, solver)

    producer_max_fork_streams_used_per_core = \
        Int(f"producer_max_fork_streams_used_per_core_"
            f"{producer.name}_{consumer.name}")
    producer_max_scatter_stream_phases_used_per_core = \
        Int(f"producer_max_scatter_stream_phases_used_per_core_"
            f"{producer.name}_{consumer.name}")
    consumer_max_gather_stream_phases_used_per_core = \
        Int(f"consumer_max_gather_stream_phases_used_per_core_"
            f"{producer.name}_{consumer.name}")

    solver.add(
        If(
            # if
            And(
                len(consumer.tms) == 0,
                reblock_tm_params["no_reblocking"] == True
            ),
            # then
            And(
                producer_max_fork_streams_used_per_core == \
                    producer_max_fork_streams_used_per_core_1,
                producer_max_scatter_stream_phases_used_per_core == \
                    producer_max_scatter_stream_phases_used_per_core_1,
                consumer_max_gather_stream_phases_used_per_core == \
                    consumer_max_gather_stream_phases_used_per_core_1,
            ),
            # else
            And(
                producer_max_fork_streams_used_per_core == \
                    producer_max_fork_streams_used_per_core_2,
                producer_max_scatter_stream_phases_used_per_core == \
                    producer_max_scatter_stream_phases_used_per_core_2,
                consumer_max_gather_stream_phases_used_per_core == \
                    consumer_max_gather_stream_phases_used_per_core_2,
            )
        )
    )

    return (producer_max_fork_streams_used_per_core, 
            producer_max_scatter_stream_phases_used_per_core,
            consumer_max_gather_stream_phases_used_per_core)

def get_queue2op_resource_usage_z3(queue, op, solver):
    """
    Rewrite of get_queue2op_resource_usage in z3 syntax.

    See docstring for get_queue2op_resource_usage for more info.
    """
    def get_first_case():
        return 1

    def get_second_case(queue, op, reblock_tm_params, solver):
        # ----- effective_queue_mblock_tiles_c -----
        effective_queue_mblock_tiles_c = Int(f"effective_queue_mblock_tiles_c_"
                                             f"{queue.name}_{op.name}")

        solver.add(
            If(
                # If
                reblock_tm_params["tm_has_transpose"] == True,
                effective_queue_mblock_tiles_c == (
                    queue.mb_m_tiles / reblock_tm_params["hslice_factor"]
                ),
                effective_queue_mblock_tiles_c == (
                    queue.mb_n_tiles / reblock_tm_params["hslice_factor"]
                )
            )
        )

        # ----- effective_queue_mblock_tiles_r -----
        effective_queue_mblock_tiles_r = Int(f"effective_queue_mblock_tiles_r_"
                                             f"{queue.name}_{op.name}")

        solver.add(
            If(
                reblock_tm_params["tm_has_transpose"] == True,
                effective_queue_mblock_tiles_r == (
                    queue.mb_n_tiles / reblock_tm_params["vslice_factor"]
                ),
                effective_queue_mblock_tiles_r == (
                    queue.mb_m_tiles / reblock_tm_params["vslice_factor"]
                )
            )
        )

        # ----- queue_min_contiguous_tiles_read -----
        queue_min_contiguous_tiles_read = Int(f"queue_min_contiguous_tiles_read_"
                                              f"{queue.name}_{op.name}")

        cont_consumer_ublocks_c = gcd_z3(
            effective_queue_mblock_tiles_c, 
            op.mb_n_tiles
        ) / op.ub_c_tiles

        cont_consumer_ublocks_r = gcd_z3(
            effective_queue_mblock_tiles_r, 
            op.mb_m_tiles
        ) / op.ub_r_tiles

        solver.add(
            Implies(
                effective_queue_mblock_tiles_c % op.ub_c_tiles != 0,
                queue_min_contiguous_tiles_read == 1
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles != 0
                ),
                queue_min_contiguous_tiles_read == gcd_z3(
                    op.ub_c_tiles, 
                    queue.ub_c_tiles
                ) 
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles != queue.ub_c_tiles,
                    queue.ublock_scan_order != op.ublock_scan_order
                ),
                queue_min_contiguous_tiles_read == gcd_z3(
                    op.ub_c_tiles, 
                    queue.ub_c_tiles
                ) 
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles == queue.ub_c_tiles,
                    queue.ublock_scan_order != op.ublock_scan_order
                ),
                queue_min_contiguous_tiles_read == (
                    gcd_z3(op.ub_c_tiles, queue.ub_c_tiles) *
                    gcd_z3(op.ub_r_tiles, queue.ub_r_tiles)
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles == queue.ub_c_tiles,
                    queue.has_row_major_ublock_scan_order,
                    op.has_row_major_ublock_scan_order,
                    Not(And(
                        queue.ub_r_tiles == op.ub_r_tiles,
                        queue.ub_c_tiles == op.ub_c_tiles
                    ))
                ),
                queue_min_contiguous_tiles_read == (
                    gcd_z3(op.ub_c_tiles, queue.ub_c_tiles) *
                    gcd_z3(op.ub_r_tiles, queue.ub_r_tiles)
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles == queue.ub_c_tiles,
                    queue.has_row_major_ublock_scan_order,
                    op.has_row_major_ublock_scan_order,
                    queue.ub_r_tiles == op.ub_r_tiles,
                    queue.ub_c_tiles == op.ub_c_tiles
                ),
                queue_min_contiguous_tiles_read == (
                    gcd_z3(op.ub_c_tiles, queue.ub_c_tiles) *
                    gcd_z3(op.ub_r_tiles, queue.ub_r_tiles) *
                    cont_consumer_ublocks_c
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles != queue.ub_c_tiles,
                    queue.has_row_major_ublock_scan_order,
                    op.has_row_major_ublock_scan_order,
                    Not(And(
                        queue.ub_r_tiles == op.ub_r_tiles,
                        queue.ub_c_tiles == op.ub_c_tiles
                    ))
                ),
                queue_min_contiguous_tiles_read == (
                    gcd_z3(op.ub_c_tiles, queue.ub_c_tiles) 
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles != queue.ub_c_tiles,
                    queue.has_row_major_ublock_scan_order,
                    op.has_row_major_ublock_scan_order,
                    queue.ub_r_tiles == op.ub_r_tiles,
                    queue.ub_c_tiles == op.ub_c_tiles
                ),
                queue_min_contiguous_tiles_read == (
                    gcd_z3(op.ub_c_tiles, queue.ub_c_tiles) *
                    cont_consumer_ublocks_c
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles == queue.ub_c_tiles,
                    queue.has_column_major_ublock_scan_order,
                    op.has_column_major_ublock_scan_order,
                    Not(And(
                        queue.ub_r_tiles == op.ub_r_tiles,
                        queue.ub_c_tiles == op.ub_c_tiles
                    ))
                ),
                queue_min_contiguous_tiles_read == (
                    gcd_z3(op.ub_c_tiles, queue.ub_c_tiles) *
                    gcd_z3(op.ub_r_tiles, queue.ub_r_tiles)
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles == queue.ub_c_tiles,
                    queue.has_column_major_ublock_scan_order,
                    op.has_column_major_ublock_scan_order,
                    queue.ub_r_tiles == op.ub_r_tiles,
                    queue.ub_c_tiles == op.ub_c_tiles
                ),
                queue_min_contiguous_tiles_read == (
                    gcd_z3(op.ub_c_tiles, queue.ub_c_tiles) *
                    gcd_z3(op.ub_r_tiles, queue.ub_r_tiles) *
                    cont_consumer_ublocks_r
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles != queue.ub_c_tiles,
                    queue.has_column_major_ublock_scan_order,
                    op.has_column_major_ublock_scan_order,
                    Not(And(
                        queue.ub_r_tiles == op.ub_r_tiles,
                        queue.ub_c_tiles == op.ub_c_tiles
                    ))
                ),
                queue_min_contiguous_tiles_read == (
                    gcd_z3(op.ub_c_tiles, queue.ub_c_tiles) 
                )
            )
        )

        solver.add(
            Implies(
                And(
                    effective_queue_mblock_tiles_c % op.ub_c_tiles == 0,
                    effective_queue_mblock_tiles_r % op.ub_r_tiles == 0,
                    op.ub_c_tiles != queue.ub_c_tiles,
                    queue.has_column_major_ublock_scan_order,
                    op.has_column_major_ublock_scan_order,
                    queue.ub_r_tiles == op.ub_r_tiles,
                    queue.ub_c_tiles == op.ub_c_tiles
                ),
                queue_min_contiguous_tiles_read == (
                    gcd_z3(op.ub_c_tiles, queue.ub_c_tiles) *
                    cont_consumer_ublocks_r
                )
            )
        )

        # ----- input_tiles_per_op_core -----
        input_tiles_per_op_core = Int(f"input_tiles_per_op_core_"
                                      f"{queue.name}_{op.name}")

        solver.add(
            input_tiles_per_op_core == op.t_dim * op.mb_m_tiles * op.mb_n_tiles
        )

        # ----- queue_op_pipe_inputs -----
        queue_op_pipe_inputs = Int(f"queue_op_pipe_inputs_2_"
                                   f"{queue.name}_{op.name}")

        solver.add(
            queue_op_pipe_inputs == input_tiles_per_op_core / queue_min_contiguous_tiles_read
        )

        return queue_op_pipe_inputs

    reblock_tm_params = get_reblocking_and_tm_factors_z3(queue, op, solver)

    queue_op_pipe_inputs_1 = get_first_case()
    queue_op_pipe_inputs_2 = get_second_case(queue, op, reblock_tm_params, solver)

    queue_op_pipe_inputs = Int(f"queue_op_pipe_inputs_{queue.name}_{op.name}")

    solver.add(
        If(
            And(
                len(op.tms) == 0,
                reblock_tm_params["no_reblocking"] == True
            ),
            queue_op_pipe_inputs == queue_op_pipe_inputs_1,
            queue_op_pipe_inputs == queue_op_pipe_inputs_2
        )
    )

    return queue_op_pipe_inputs

def get_num_buffers_accessed_along_dim_z3_expr(op: ResourceConstrNode, 
                                               queue: ResourceConstrNode, 
                                               dim: Dimension, 
                                               solver: z3.Solver) -> z3.Var:
    """
    Rewrite of get_num_buffers_accessed_along_dim in z3 syntax.

    See docstring for get_num_buffers_accessed_along_dim for more info.
    """
    if dim == Dimension.Horizontal:
        grid_size_attr = "grid_size_x"
        mb_attr = "mb_n"
        ub_attr = "ub_c"
        dim_str = "c"
    else:
        grid_size_attr = "grid_size_y"
        mb_attr = "mb_m"
        ub_attr = "ub_r"
        dim_str = "r"

    op_grid_size = getattr(op, grid_size_attr)
    queue_grid_size = getattr(queue, grid_size_attr)
    queue_mb_size = getattr(queue, mb_attr)
    queue_ub_size = getattr(queue, ub_attr)

    queue_num_tiles = queue_grid_size * queue_mb_size * queue_ub_size
    tiles_per_op_core_needed = queue_num_tiles / op_grid_size
    tiles_per_dram_buf = queue_mb_size * queue_ub_size

    # ----- queue_num_buffers -----
    queue_num_buffers = Int(f"queue_num_buffers_{dim_str}_"
                            f"{queue.name}_{op.name}")

    solver.add(
        Implies(
            queue_num_tiles % op_grid_size != 0,
            queue_num_buffers == queue_grid_size
        )
    )

    solver.add(
        Implies(
            And(
                queue_num_tiles % op_grid_size == 0,
                tiles_per_op_core_needed % tiles_per_dram_buf == 0
            ),
            queue_num_buffers == tiles_per_op_core_needed / tiles_per_dram_buf
        )
    )

    solver.add(
        Implies(
            And(
                queue_num_tiles % op_grid_size == 0,
                tiles_per_op_core_needed % tiles_per_dram_buf != 0,
                tiles_per_dram_buf % tiles_per_op_core_needed == 0
            ),
            queue_num_buffers == 1
        )
    )

    solver.add(
        Implies(
            And(
                queue_num_tiles % op_grid_size == 0,
                tiles_per_op_core_needed % tiles_per_dram_buf != 0,
                tiles_per_dram_buf % tiles_per_op_core_needed != 0
            ),
            queue_num_buffers == int_div_with_ceil_z3(
                tiles_per_op_core_needed,
                tiles_per_dram_buf
            ) + 1
        )
    )

    return queue_num_buffers