# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from test_modules.common.constants import MULTI_TM_TESTS_VALID_TMS
from test_modules.common.enums import TMS, Dimension, TileBroadcastDimension, UblockOrder
from test_modules.common.node import Node
from test_modules.common.z3_utils import divisible_either_direction
from z3 import *


@dataclass
class TensorDims:
    """
    Defines tensor's dimensions.

    Attributes
    ----------
    rt:
        Variable reprsenting number of tiles in a row of tensor.
    ct:
        Variable representing number of tiles in a column of tensor.
    t:
        Variable representing number of blocks in t dimension.
    """

    rt: z3.Var
    ct: z3.Var
    t: z3.Var


@dataclass
class PadUnpadParams:
    """
    Defines padding/unpadding parameters on a single node -> op connection. The order of applications is
    op_input_shape -> unpad -> TMs -> pad -> output_shape. Consumers output shape (mblock size, ublock size)
    always accounts for the padding applied.

    Attributes
    ----------
    unpad_rt:
        Number of tiles to unpad along the R dimension.
    unpad_ct:
        Number of tiles to unpad along the C dimension.
    pad_rt:
        Number of tiles to pad along the R dimension.
    pad_ct:
        Number of tiles to pad along the C dimension.
    """

    unpad_rt: z3.Var | int = 0
    unpad_ct: z3.Var | int = 0
    pad_rt: z3.Var | int = 0
    pad_ct: z3.Var | int = 0


@dataclass
class NetlistTMConfiguration:
    """
    TM configuration parsed from netlist.

    Attributes
    ----------
    tm_type:
        Type of the TM.
    tm_arg:
        Argument of the TM.
    """

    tm_type: Optional[z3.Var | TMS] = None
    tm_arg: Optional[z3.Var | int] = None

    @staticmethod
    def randomized() -> NetlistTMConfiguration:
        """Signals the solver to randomize both TM type and arg on this connection."""
        return NetlistTMConfiguration()


@dataclass
class TMInfo:
    """
    Encapsulates TM information such as TM type and arguments, as well as the running values
    of TM factors and tensor shapes after applying this TM.

    Attributes
    ----------
    tm_type:
        Variable representing type of the current TM.
    tm_arg:
        Variable representing argument of the TM.
    post_tm_rt:
        Number of tiles vertically after applying this TM.
    post_tm_ct:
        Number of tiles horizontally after applying this TM.
    post_tm_t:
        T dimension after applying this TM.
    c_bcast_factor:
        Running value of c broadcast factor after applying this TM.
    r_bcast_factor:
        Running value of r broadcast factor after applying this TM.
    t_bcast_factor:
        Running value of t broadcast factor after applying this TM.
    hslice_factor:
        Running value of hslice factor after applying this TM.
    vslice_factor:
        Running value of vslice factor after applying this TM.
    hstack_factor:
        Running value of hstack factor after applying this TM.
    vstack_factor:
        Running value of vstack factor after applying this TM.
    adjusted_slice_factor:
        Running value of adjusted of total slice factor, divided by the stack factor in case integer
        division is possible, after applying this TM.
    multi_t_reach_stack:
        Running value that reperesents if it is necessary to access tiles from multiple Ts to
        implement stacking.
    """

    # TM type and args.
    tm_type: z3.Var
    tm_arg: z3.Var

    # Tensor shape after applying current TM.
    post_tm_rt: z3.Var
    post_tm_ct: z3.Var
    post_tm_t: z3.Var

    # Cumulative TM factors after applying current TM.
    c_bcast_factor: z3.Var
    r_bcast_factor: z3.Var
    t_bcast_factor: z3.Var
    hslice_factor: z3.Var
    vslice_factor: z3.Var
    hstack_factor: z3.Var
    vstack_factor: z3.Var
    adjusted_slice_factor: z3.Var
    multi_t_reach_stack: z3.Var

    def constrain_valid_tm(self, solver: z3.Solver) -> None:
        """Defines basic constraints which prevent the variables from having
        unsupported values (e.g. negative value for TM argument).

        Parameters
        ----------
        solver:
            z3 solver to which to add constraints.
        """
        # Constrain TM type and arg to have valid values.
        valid_tm_conditions = [
            self.tm_type == tm_type.value for tm_type in MULTI_TM_TESTS_VALID_TMS
        ]
        solver.add(Or(valid_tm_conditions))

        solver.add(self.tm_arg >= 1)

    @property
    def tm_factor(self) -> Dict[TMS, z3.Var]:
        """Mapping between TM type and a corresponding cumulative factor."""
        return {
            TMS.c_broadcast: self.c_bcast_factor,
            TMS.r_broadcast: self.r_bcast_factor,
            TMS.t_broadcast: self.t_bcast_factor,
            TMS.hslice: self.hslice_factor,
            TMS.vslice: self.vslice_factor,
            TMS.hstack: self.hstack_factor,
            TMS.vstack: self.vstack_factor,
        }


@dataclass
class ScheduledOpTMInfo:
    """
    Encapsulates TM information such as TM type and arguments for scheduled ops
    inside the fused op. Differences from the TMInfo:
     - We are not randomizing TM type for scheduled ops
     - We support only broadcast TMs for scheduled ops.

    Attributes
    ----------
    tm_type:
        TM type, set in the template.
    tm_arg:
        Variable representing argument of the TM.
    post_tm_rt:
        Number of tiles vertically after applying this TM.
    post_tm_ct:
        Number of tiles horizontally after applying this TM.
    """

    # TM type and args.
    tm_type: TMS
    tm_arg: z3.Var

    # Tensor shape after applying current TM.
    post_tm_rt: z3.Var
    post_tm_ct: z3.Var


@dataclass
class ConsumerOpConfiguration:
    """
    Defines consumer configuration from the perspective of the producer node. Used in phased
    stacking constraints and resource constraints. Generally useful for matmul/reduce ops where
    the inner dimension is lost and replaced with u_kt/m_k. 'From the producer's perspecive'
    generally means on which input index the producer is located. since e.g. for matmul
    different ublock scan orders are used for in0 and in1.

    Attributes
    ----------
    grid_size_r:
        Consumer's grid size r from the producer's perspective.
    grid_size_c:
        Consumer's grid size c from the produer's perspective.
    mblock_r:
        Consumer's mblock r from the producer's perspective.
    mblock_c:
        Consumer's mblock c from the producer's perspective.
    ublock_r:
        Consumer's ublock r from the producer's perspective.
    ublock_c:
        Consumer's ublock c from the producer's perspective.
    row_major_ublock_scan_order:
        Does the consumer have row major ublock scan order from the producer's perspective.
    """

    # TODO: Add other fields which are specific to producer index such as reblock_[r/c] and gather_[r/c]
    #  and use it for node export for resource constraints.

    grid_size_r: z3.Var
    grid_size_c: z3.Var
    mblock_r: z3.Var
    mblock_c: z3.Var
    ublock_r: z3.Var
    ublock_c: z3.Var
    ublock_scan_order: z3.Var | int

    def get_attr_from_model(self, attr_name: str, model_vars: Dict) -> int:
        """
        Returns attribute value computed in model vars. If the attribute is of primitive type,
        then it simply returns it's value.

        Parameters
        ----------
        attr_name:
            Name of the attribute.
        model_vars:
            Mapping between solver var names and values.
        """
        attribute = getattr(self, attr_name)
        if isinstance(attribute, (int, bool, str)):
            return attribute
        else:
            return model_vars[attribute.sexpr()]


@dataclass
class OpInput(ABC):
    """
    Base class for representing OP inputs. Defines input tensor's dimensions.
    """

    @abstractmethod
    def get_tms(self) -> List[TMInfo]:
        """Returns the list of all the TMs on this input connection."""
        pass

    @abstractmethod
    def get_rt(self) -> z3.Var:
        """Returns number of tiles vertically."""
        pass

    @abstractmethod
    def get_ct(self) -> z3.Var:
        """Returns number of tiles horizontally."""
        pass

    @abstractmethod
    def get_t(self) -> z3.Var:
        """Returns T dimension."""
        pass

    @abstractmethod
    def constrain(self, solver: z3.Solver) -> None:
        """Constraint producer -> consumer point-to-point connection.
        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        pass

    def tensor_dims(self) -> TensorDims:
        """Returns input tensor's size along all the dimensions."""
        return TensorDims(rt=self.get_rt(), ct=self.get_ct(), t=self.get_t())


@dataclass
class PipeOpInputBase(OpInput):
    """
    Base class for all OP inputs which are implemented as pipes (everything but scheduled ops connections).

    Attributes
    ----------
    producer:
        Producer node.
    pad_unpad_params:
        Padding and unpadding params on this connection.
    """

    producer: Node
    pad_unpad_params: PadUnpadParams

    # @override
    def get_rt(self) -> z3.Var:
        return self.get_padded_output_rt()

    # @override
    def get_ct(self) -> z3.Var:
        return self.get_padded_output_ct()

    # @override
    def get_t(self) -> z3.Var:
        return self.get_post_tm_t()

    def get_unpadded_input_rt(self) -> z3.Var:
        """Returns size in tiles along R dimension of input tensor after unpadding."""
        return self.producer.get_vertical_size_var() - self.pad_unpad_params.unpad_rt

    def get_unpadded_input_ct(self) -> z3.Var:
        """Returns size in tiles along C dimension of input tensor after unpadding."""
        return self.producer.get_horizontal_size_var() - self.pad_unpad_params.unpad_ct

    def get_padded_output_rt(self) -> z3.Var:
        """Returns size in tiles along R dimension of output tensor after TMs and padding have been applied."""
        return self.get_post_tm_rt() + self.pad_unpad_params.pad_rt

    def get_padded_output_ct(self) -> z3.Var:
        """Returns size in tiles along C dimension of output tensor after TMs and padding have been applied."""
        return self.get_post_tm_ct() + self.pad_unpad_params.pad_ct

    @abstractmethod
    def get_post_tm_rt(self) -> z3.Var:
        """Returns size in tiles along R dimension of output tensor after TMs have been applied."""
        pass

    @abstractmethod
    def get_post_tm_ct(self) -> z3.Var:
        """Returns size in tiles along C dimension of output tensor after TMs have been applied."""
        pass

    @abstractmethod
    def get_post_tm_t(self) -> z3.Var:
        """Returns the T dimension of output tensor after TMs have been applied."""
        pass


@dataclass
class OpInputNoTMs(PipeOpInputBase):
    """
    Represents simple OP input, i.e. with no TMs.
    """

    # @override
    def get_post_tm_ct(self) -> z3.Var:
        return self.get_unpadded_input_ct()

    # @override
    def get_post_tm_rt(self) -> z3.Var:
        return self.get_unpadded_input_rt()

    # @override
    def get_post_tm_t(self) -> z3.Var:
        return self.producer.t_dim

    # @override
    def get_tms(self) -> List[TMInfo]:
        return []

    # @override
    def constrain(self, solver: z3.Solver) -> None:
        pass


@dataclass
class OpInputTMs(PipeOpInputBase):
    """
    Represents OP input with multiple TMs.

    Attributes
    ----------
    prod_fullt_buf:
        Does the producer have full-t buffering.
    p2p_buf_size_mb:
        Minimum good buf_size_mb w.r.t. to this point-to-point connection.
    row_major_stack:
        Are TMs row major stack.
    total_slice_factor:
        Total slice factor, i.e. total_hslice_factor * total_vslice_factor.
    total_stack_factor:
        Total stack factor, i.e. total_hstack_factor * total_vstack_factor.
    producer_block_pre_stack_ct:
        Input tensor's number of tiles horizontally before applying any stacking TMs.
    producer_block_pre_stack_rt:
        Input tensor's number of tiles vertically before applying any stacking TMs.
    stacking_block_ct:
        Input tensor's number of tiles horizontally corresponding to one original producer-side t,
        mapped in the stacking order.
    stacking_block_rt:
        Input tensor's number of tiles vertically corresponding to one original producer-side t,
        mapped in the stacking order.
    effective_hstack_factor:
        Effective hstack factor.
    effective_vstack_factor:
        Effective vstack factor.

    consumer_config:
        Consumer configuration w.r.t. this input.
    tms:
        List of all the TMs on this point-to-point connection.
    """

    prod_fullt_buf: z3.Var
    p2p_buf_size_mb: z3.Var
    # TODO probably move this to TMInfo class to be consistent with net2pipe.
    row_major_stack: z3.Var
    total_slice_factor: z3.Var
    total_stack_factor: z3.Var
    producer_block_pre_stack_ct: z3.Var
    producer_block_pre_stack_rt: z3.Var
    stacking_block_ct: z3.Var
    stacking_block_rt: z3.Var
    effective_hstack_factor: z3.Var
    effective_vstack_factor: z3.Var
    post_padding_stacking_block_ct: z3.Var
    post_padding_stacking_block_rt: z3.Var
    post_padding_effective_hstack_factor: z3.Var
    post_padding_effective_vstack_factor: z3.Var

    consumer_config: ConsumerOpConfiguration
    tms: List[TMInfo] = field(default_factory=list)

    def initialize_variables(self, solver: z3.Solver) -> None:
        """Defines basic constraints which prevent the variables from having
        unsupported values (e.g. negative value for TM argument).

        Parameters
        ----------
        solver:
            z3 solver to which to add constraints.
        """
        # Constrain TMs to be valid TMs.
        for tm in self.tms:
            tm.constrain_valid_tm(solver)

        # Constrain valid options for other relevant variables.
        solver.add(Or(self.prod_fullt_buf == 0, self.prod_fullt_buf == 1))
        solver.add(Or(self.row_major_stack == 0, self.row_major_stack == 1))

        # Pre-stack and post other TMs tensor size after the padding has been removed.
        solver.add(self.producer_block_pre_stack_ct == self.get_post_tm_ct() / self.hstack_factor)
        solver.add(self.producer_block_pre_stack_rt == self.get_post_tm_rt() / self.vstack_factor)

        # Total slice and stack factor.
        solver.add(self.total_stack_factor == self.hstack_factor * self.vstack_factor)
        solver.add(self.total_slice_factor == self.hslice_factor * self.vslice_factor)

        # Constrain other variables to have some default value in case we don't care about their
        # actual value, e.g. when we have full-t buffering we don't really care what the effective
        # stack factor is
        solver.add(
            self.stacking_block_ct > 0,
            self.stacking_block_rt > 0,
            self.effective_hstack_factor > 0,
            self.effective_vstack_factor > 0,
            self.post_padding_stacking_block_ct > 0,
            self.post_padding_stacking_block_rt > 0,
            self.post_padding_effective_hstack_factor > 0,
            self.post_padding_effective_vstack_factor > 0,
        )

    @property
    def tm_factor(self) -> Dict[TMS, z3.Var]:
        """Mapping between TM type and a corresponding cumulative factor."""
        return {
            TMS.c_broadcast: self.tms[-1].c_bcast_factor,
            TMS.r_broadcast: self.tms[-1].r_bcast_factor,
            TMS.t_broadcast: self.tms[-1].t_bcast_factor,
            TMS.hslice: self.tms[-1].hslice_factor,
            TMS.vslice: self.tms[-1].vslice_factor,
            TMS.hstack: self.tms[-1].hstack_factor,
            TMS.vstack: self.tms[-1].vstack_factor,
        }

    # @override
    def get_tms(self) -> List[TMInfo]:
        return self.tms

    # @override
    def get_post_tm_rt(self) -> z3.Var:
        return self.tms[-1].post_tm_rt

    # @override
    def get_post_tm_ct(self) -> z3.Var:
        return self.tms[-1].post_tm_ct

    # @override
    def get_post_tm_t(self) -> z3.Var:
        return self.tms[-1].post_tm_t

    @property
    def hstack_factor(self) -> z3.Var:
        """Total hstack factor."""
        return self.tms[-1].hstack_factor

    @property
    def vstack_factor(self) -> z3.Var:
        """Total vstack factor."""
        return self.tms[-1].vstack_factor

    @property
    def hslice_factor(self) -> z3.Var:
        """Total hslice factor."""
        return self.tms[-1].hslice_factor

    @property
    def vslice_factor(self) -> z3.Var:
        """Total vslice factor."""
        return self.tms[-1].vslice_factor

    @property
    def c_bcast_factor(self) -> z3.Var:
        """Total c broadcast factor."""
        return self.tms[-1].c_bcast_factor

    @property
    def r_bcast_factor(self) -> z3.Var:
        """Total r broadcast factor."""
        return self.tms[-1].r_bcast_factor

    @property
    def t_bcast_factor(self) -> z3.Var:
        """Total t broadcast factor."""
        return self.tms[-1].t_bcast_factor

    @property
    def adjusted_slice_factor(self) -> z3.Var:
        """Total adjusted slice factor"""
        return self.tms[-1].adjusted_slice_factor

    @property
    def multi_t_reach_stack(self) -> z3.Var:
        """Total multi t reach stack"""
        return self.tms[-1].multi_t_reach_stack

    @property
    def stack_factor(self) -> Dict[Dimension, z3.Var]:
        """Returns a mapping between spatial dimension and the corresponding stack factor.
        Mapping is:
            Dimension.Horizontal -> hstack_factor
            Dimension.Vertical -> vstack_factor
        """
        return {Dimension.Horizontal: self.hstack_factor, Dimension.Vertical: self.vstack_factor}

    @property
    def slice_factor(self) -> Dict[Dimension, z3.Var]:
        """Returns a mapping between spatial dimension and the corresponding slice factor.
        Mapping is:
            Dimension.Horizontal -> hslice_factor
            Dimension.Vertical -> vslice_factor
        """
        return {Dimension.Horizontal: self.hslice_factor, Dimension.Vertical: self.vslice_factor}

    @property
    def stacking_block_tiles(self) -> Dict[Dimension, z3.Var]:
        """Returns a mapping between spatial dimension and the corresponding number of tiles which
        stems from one original producer t in a given spatial dimension.
        Mapping is:
            Dimension.Horizontal -> column tiles
            Dimension.Vertical -> row tiles
        """
        return {
            Dimension.Horizontal: self.stacking_block_ct,
            Dimension.Vertical: self.stacking_block_rt,
        }

    @property
    def effective_stack_factor(self) -> Dict[Dimension, z3.Var]:
        """Returns a mapping between spatial dimension and the corresponding effective stack factor.
        Mapping is:
            Dimension.Horizontal -> effective hstack factor
            Dimension.Vertical -> effective vstack factor
        """
        return {
            Dimension.Horizontal: self.effective_hstack_factor,
            Dimension.Vertical: self.effective_vstack_factor,
        }

    @property
    def bcast_factor(self) -> Dict[Dimension, z3.Var]:
        """Returns a mapping between spatial dimension and the broadcast factor along the given
        spatial dimension.
        Mapping is:
            Dimension.Horizontal -> column broadcast factor
            Dimension.Vertical -> row broadcast factor
        """
        return {Dimension.Horizontal: self.c_bcast_factor, Dimension.Vertical: self.r_bcast_factor}

    @property
    def total_slice_stack_ratio(self) -> Dict[Dimension, z3.Var]:
        """Returns a mapping between spatial dimension and the ratio between the total stack factor
        and cumulative stack factor along the given spatial dimension.
        Mapping is:
            Dimension.Horizontal -> horizontal ratio (total_stack / hstack)
            Dimension.Vertical -> vertical ratio (total_stack / vstack)
        """
        return {dim: self.total_slice_factor / self.stack_factor[dim] for dim in Dimension}

    @property
    def producer_block_tiles_pre_stack(self) -> Dict[Dimension, z3.Var]:
        """Returns a mapping between spatial dimension and producer's size in tiles
        along the dimension, before applying stack TMs but after applying all the
        other TMs."""
        return {
            Dimension.Horizontal: self.producer_block_pre_stack_ct,
            Dimension.Vertical: self.producer_block_pre_stack_rt,
        }

    def constrain(self, solver: z3.Solver) -> None:
        """See docstring of OpInput::constrain"""
        self.constrain_tm_sequence(solver)
        self.calculate_total_tm_factors(solver)
        if not self.producer.is_queue():
            self.constrain_phased_stack(solver)

    def constrain_tm_sequence(self, solver: z3.Solver) -> None:
        """Constrains a single producer -> consumer connection, so that the TMs on
        that connection are compatible with each other (in terms of tensor shape).

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        # Tensor shape throughout tms has to match.
        input_rt = self.get_unpadded_input_rt()
        input_ct = self.get_unpadded_input_ct()
        input_t = self.producer.t_dim

        # Output of each TM must match its input modified by the TM operation.
        for tm_info in self.tms:
            solver.add(
                Implies(
                    tm_info.tm_type == TMS.c_broadcast.value,
                    And(
                        tm_info.post_tm_ct
                        == tm_info.tm_arg * input_ct,  # out_ct = broadcast_factor * in_ct
                        tm_info.post_tm_rt == input_rt,  # out_rt = in_rt
                        tm_info.post_tm_t == input_t,  # out_t  = in_t
                    ),
                )
            )

            solver.add(
                Implies(
                    tm_info.tm_type == TMS.r_broadcast.value,
                    And(
                        tm_info.post_tm_rt
                        == tm_info.tm_arg * input_rt,  # out_rt = broadcast_factor * in_rt
                        tm_info.post_tm_ct == input_ct,  # out_ct = in_ct
                        tm_info.post_tm_t == input_t,  # out_t  = in_t
                    ),
                )
            )

            solver.add(
                Implies(
                    tm_info.tm_type == TMS.t_broadcast.value,
                    And(
                        input_t == 1,  # in_t   = 1
                        tm_info.post_tm_t
                        == tm_info.tm_arg * input_t,  # out_t  = broadcast_factor * in_t
                        tm_info.post_tm_ct == input_ct,  # out_ct = in_ct
                        tm_info.post_tm_rt == input_rt,  # out_rt = in_rt
                    ),
                )
            )

            solver.add(
                Implies(
                    tm_info.tm_type == TMS.transpose.value,
                    And(
                        tm_info.post_tm_ct == input_rt,  # out_ct = in_rt
                        tm_info.post_tm_rt == input_ct,  # out_rt = in_ct
                        tm_info.post_tm_t == input_t,  # out_t  = in_t
                    ),
                )
            )

            solver.add(
                Implies(
                    tm_info.tm_type == TMS.hslice.value,
                    And(
                        tm_info.post_tm_t
                        == tm_info.tm_arg * input_t,  # out_t  = hslice_factor * in_t
                        input_ct
                        == tm_info.tm_arg
                        * tm_info.post_tm_ct,  # out_ct = in_ct / hslice_factor and in_ct % hslice_factor = 0
                        tm_info.post_tm_rt == input_rt,  # out_rt = in_rt
                    ),
                )
            )

            solver.add(
                Implies(
                    tm_info.tm_type == TMS.vslice.value,
                    And(
                        tm_info.post_tm_t
                        == tm_info.tm_arg * input_t,  # out_t  = vslice_factor * in_t
                        input_rt
                        == tm_info.tm_arg
                        * tm_info.post_tm_rt,  # out_rt = in_rt / vslice_factor and in_rt % vslice_factor = 0
                        tm_info.post_tm_ct == input_ct,  # out_ct = in_ct
                    ),
                )
            )

            solver.add(
                Implies(
                    tm_info.tm_type == TMS.hstack.value,
                    And(
                        tm_info.post_tm_ct
                        == tm_info.tm_arg * input_ct,  # out_ct = hstack_factor * in_ct
                        input_t
                        == tm_info.post_tm_t
                        * tm_info.tm_arg,  # out_t  = in_t / hstack_factor and in_t % hstack_factor == 0
                        tm_info.post_tm_rt == input_rt,  # out_rt = in_rt
                    ),
                )
            )

            solver.add(
                Implies(
                    tm_info.tm_type == TMS.vstack.value,
                    And(
                        tm_info.post_tm_rt
                        == tm_info.tm_arg * input_rt,  # out_rt = vstack_factor * in_rt
                        input_t
                        == tm_info.post_tm_t
                        * tm_info.tm_arg,  # out_t  = in_t / vstack_factor and in_t % vstack_factor == 0
                        tm_info.post_tm_ct == input_ct,  # out_ct = in_ct
                    ),
                )
            )

            input_rt = tm_info.post_tm_rt
            input_ct = tm_info.post_tm_ct
            input_t = tm_info.post_tm_t

    def constrain_padded_stack(self, solver: z3.Solver) -> None:
        """
        Constraints padding factors w.r.t. to the tiles being stacked in the corresponding dimension.

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        solver.add(
            If(
                self.effective_vstack_factor > 1,
                And(
                    self.pad_unpad_params.pad_rt % self.stacking_block_rt == 0,
                    self.post_padding_effective_vstack_factor
                    == self.effective_vstack_factor
                    + (self.pad_unpad_params.pad_rt / self.stacking_block_rt),
                    self.post_padding_stacking_block_rt == self.stacking_block_rt,
                ),
                And(
                    self.post_padding_stacking_block_rt
                    == self.stacking_block_rt + self.pad_unpad_params.pad_rt,
                    self.post_padding_effective_vstack_factor == self.effective_vstack_factor,
                ),
            )
        )

        solver.add(
            If(
                self.effective_hstack_factor > 1,
                And(
                    self.pad_unpad_params.pad_ct % self.stacking_block_ct == 0,
                    self.post_padding_effective_hstack_factor
                    == self.effective_hstack_factor
                    + (self.pad_unpad_params.pad_ct / self.stacking_block_ct),
                    self.post_padding_stacking_block_ct == self.stacking_block_ct,
                ),
                And(
                    self.post_padding_stacking_block_ct
                    == self.stacking_block_ct + self.pad_unpad_params.pad_ct,
                    self.post_padding_effective_hstack_factor == self.effective_hstack_factor,
                ),
            )
        )

    def constrain_phased_stack(self, solver: z3.Solver):
        """Defines stacking constraints for a single producer -> consumer connection, mostly
        related to the case where the producer does not have full t buffering.

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        self.constrain_point_to_point_stack_fork_buf_size_mb(solver)
        self.constrain_tm_ordering(solver)
        self.constrain_stacking_divisibility_rules(solver)
        self.constrain_padded_stack(solver)
        self.constrain_consumer_grid_blocking(solver)
        self.scan_order_constraints(solver)

    def calculate_total_tm_factors(self, solver: z3.Solver) -> None:
        """Calculates total cumulative factors for all the TMs on a single
        producer -> consumer connection.

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        # We don't compute any factor for transpose.
        tms_with_factor = [t for t in MULTI_TM_TESTS_VALID_TMS if t != TMS.transpose]
        # Initialize factors to 1 at the input of the first TM.
        input_tm_factors = {tm: 1 for tm in tms_with_factor}
        adjusted_slice_factor = 1
        multi_t_reach_stack = 0
        for tm_info in self.tms:
            # Update the adjusted_slice_factor and multi_t_reach_stack attributes.
            solver.add(
                If(
                    Or(tm_info.tm_type == TMS.hslice.value, tm_info.tm_type == TMS.vslice.value),
                    And(
                        tm_info.adjusted_slice_factor == adjusted_slice_factor * tm_info.tm_arg,
                        tm_info.multi_t_reach_stack == multi_t_reach_stack,
                    ),
                    If(
                        Or(
                            tm_info.tm_type == TMS.hstack.value, tm_info.tm_type == TMS.vstack.value
                        ),
                        If(
                            adjusted_slice_factor % tm_info.tm_arg == 0,
                            And(
                                tm_info.adjusted_slice_factor
                                == adjusted_slice_factor / tm_info.tm_arg,
                                tm_info.multi_t_reach_stack == multi_t_reach_stack,
                            ),
                            And(
                                tm_info.adjusted_slice_factor == 1, tm_info.multi_t_reach_stack == 1
                            ),
                        ),
                        And(
                            tm_info.adjusted_slice_factor == adjusted_slice_factor,
                            tm_info.multi_t_reach_stack == multi_t_reach_stack,
                        ),
                    ),
                )
            )
            for tm in tms_with_factor:
                # Increase appropriate factor based on the current TM type.
                solver.add(
                    If(
                        tm_info.tm_type == tm.value,
                        tm_info.tm_factor[tm] == input_tm_factors[tm] * tm_info.tm_arg,
                        tm_info.tm_factor[tm] == input_tm_factors[tm],
                    )
                )
            input_tm_factors = tm_info.tm_factor
            adjusted_slice_factor = tm_info.adjusted_slice_factor
            multi_t_reach_stack = tm_info.multi_t_reach_stack

    def constrain_point_to_point_stack_fork_buf_size_mb(self, solver: z3.Solver) -> None:
        """Constraints producer's buffer size, in the case where the producer
        is connected to the test op, and there are multiple TMs on that connection.

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        # When producer has full t buffering, it means buf_mb_size is 2*total_stack_factor
        solver.add(
            If(
                self.prod_fullt_buf == 1,
                And(
                    Or(
                        self.p2p_buf_size_mb == 2 * self.total_stack_factor,
                        self.multi_t_reach_stack == 0,
                    ),
                    # Now, combined buf_size_mb of the op node has to be such that all point-to-point
                    # buf_size_mb constraints are satisfied, meaning that it has to be LCM of all
                    # point-to-point buf_size_mbs. In order to avoid complicated LCM calculation, we
                    # enforce divisibility between combined buf_size_mb and point-to-point buf_size_mbs,
                    # which is basically >= LCM.
                    self.producer.buf_size_mb >= self.p2p_buf_size_mb,
                    self.producer.buf_size_mb % self.p2p_buf_size_mb == 0,
                ),
                # If no full-t buffering on producer, buf_mb_size can be either 1 or 2
                # We should also force net2pipe to use phased stack.
                self.get_need_phased_stack_constraints(),
            )
        )

    def get_need_phased_stack_constraints(self) -> z3.Var:
        """Get constraints which will force net2pipe to implement phased stack w.r.t
        producer's attributes. Another factor which is required for phased stack is that
        stack factor is grater than 1, but that is controlled by set in the guiding section.

        TODO - revisit if there is a nicer way to implemnt this when you refactor
               guiding options

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        return And(
            self.multi_t_reach_stack == 1,
            Or(self.producer.buf_size_mb == 1, self.producer.buf_size_mb == 2),
            self.producer.t_dim > 1,
            self.p2p_buf_size_mb == self.producer.buf_size_mb,
        )

    def constrain_tm_ordering(self, solver: z3.Solver) -> None:
        """Constraints ordering of TMs on a single producer -> consumer connection,
        for the case where producer does not have full t buffering.

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        num_tms = len(self.tms)
        prev_tm: Optional[TMInfo] = None

        for tm_idx, tm in enumerate(self.tms):
            # With single-t buffering on producer side,
            # there can be up to 1 hstack and up to 1 vstack in the sequence,
            # and those stack TMs must be the last TMs in the sequence.

            # Ensure that only the last two TMs in the sequence can be stack TMs.
            if tm_idx < num_tms - 2:
                solver.add(
                    Implies(
                        self.prod_fullt_buf == 0,
                        And(tm.tm_type != TMS.hstack.value, tm.tm_type != TMS.vstack.value),
                    )
                )

            if num_tms >= 2 and tm_idx == num_tms - 1:
                # Ensure that after hstack there can only be a vstack.
                solver.add(
                    Implies(
                        And(self.prod_fullt_buf == 0, prev_tm.tm_type == TMS.hstack.value),
                        tm.tm_type == TMS.vstack.value,
                    )
                )

                # Ensure that after vstack there can only be an hstack.
                solver.add(
                    Implies(
                        And(self.prod_fullt_buf == 0, prev_tm.tm_type == TMS.vstack.value),
                        tm.tm_type == TMS.hstack.value,
                    )
                )

                solver.add(
                    If(
                        Or(
                            And(
                                tm.tm_type == TMS.vstack.value,
                                prev_tm.tm_type == TMS.hstack.value,
                                prev_tm.tm_arg > 1,
                            ),
                            And(
                                tm.tm_type == TMS.hstack.value,
                                Or(prev_tm.tm_type != TMS.vstack.value, prev_tm.tm_arg == 1),
                            ),
                        ),
                        self.row_major_stack == 1,
                        self.row_major_stack == 0,
                    )
                )

            prev_tm = tm

    def constrain_stacking_divisibility_rules(self, solver: z3.Solver) -> None:
        """Constraints stacking factor divisibility rules on a single producer -> consumer
        connection.

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        solver.add(
            Implies(
                And(
                    self.total_stack_factor > 1,
                    self.prod_fullt_buf == 0,
                ),
                If(
                    self.row_major_stack == 1,
                    # row_major_stack=1
                    self.get_dim_major_stacking_constraints(Dimension.Horizontal),
                    # else: row_major_stack=0
                    self.get_dim_major_stacking_constraints(Dimension.Vertical),
                ),
            )
        )

    def get_dim_major_stacking_constraints(self, dim: Dimension) -> z3.Var:
        """Returns a z3 Expression which models stacking factor divisibility constraints for a
        given connection and a given spatial dimension.

        Parameters
        ----------
        dim:
            Spatial dimension which tells which type of stacking is being done on a given connection.
            Dimension.Horizontal -> row major stack (i.e. hstack is the first stack TM)
            Dimension.Vertical -> column major stack (i.e. vstack is the first stack TM)
        """
        opposite_dim = Dimension.opposite(dim)
        return And(
            divisible_either_direction(self.total_slice_factor, self.stack_factor[dim]),
            If(
                self.total_slice_factor % self.stack_factor[dim] == 0,
                And(
                    divisible_either_direction(
                        self.total_slice_stack_ratio[dim], self.stack_factor[opposite_dim]
                    ),
                    self.stacking_block_tiles[dim]
                    == self.producer_block_tiles_pre_stack[dim] * self.stack_factor[dim],
                    self.effective_stack_factor[dim] == 1,
                    If(
                        (self.total_slice_stack_ratio[dim]) % self.stack_factor[opposite_dim] == 0,
                        # In this case, one original producer t covers the total stacking factor
                        And(
                            self.stacking_block_tiles[opposite_dim]
                            == self.producer_block_tiles_pre_stack[opposite_dim]
                            * self.stack_factor[opposite_dim],
                            self.effective_stack_factor[opposite_dim] == 1,
                        ),
                        # else: stack_factor_opposite_dim % (total_slice_factor/stack_factor_dim) == 0
                        And(
                            self.stacking_block_tiles[opposite_dim]
                            == self.producer_block_tiles_pre_stack[opposite_dim]
                            * self.total_slice_stack_ratio[dim],
                            self.effective_stack_factor[opposite_dim]
                            == self.stack_factor[opposite_dim] / self.total_slice_stack_ratio[dim],
                        ),
                    ),
                ),
                # else: stack_factor_for_dim % total_slice_factor == 0
                # One dim of stacking takes tiles from multiple producer t's.
                And(
                    self.stacking_block_tiles[dim]
                    == self.producer_block_tiles_pre_stack[dim] * self.total_slice_factor,
                    self.stacking_block_tiles[opposite_dim]
                    == self.producer_block_tiles_pre_stack[opposite_dim],
                    self.effective_stack_factor[dim]
                    == self.stack_factor[dim] / self.total_slice_factor,
                    self.effective_stack_factor[opposite_dim] == self.stack_factor[opposite_dim],
                ),
            ),
        )

    def constrain_consumer_grid_blocking(self, solver: z3.Solver) -> None:
        """Constraints producer's stacking variables w.r.t. consumer's grid size.

        1) Stacking blocks must be aligned with consumer-side grid and macro-blocks.
        2) Stacking blocks must contain a whole number of consumer micro-blocks.

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """

        # Stacking blocks must be aligned with consumer-side grid and macro-blocks

        # In case of matmul, for the matmul inner dimension, we use "k" for
        # the block sizes in the checks below, and the consumer grid size should
        # be set to 1 (since we're gathering at only one core and broadcasting
        # to the whole row/column).

        solver.add(
            Implies(
                And(self.total_stack_factor > 1, self.prod_fullt_buf == 0),
                divisible_either_direction(
                    self.post_padding_effective_hstack_factor, self.consumer_config.grid_size_c
                ),
            )
        )

        solver.add(
            Implies(
                And(self.total_stack_factor > 1, self.prod_fullt_buf == 0),
                divisible_either_direction(
                    self.post_padding_effective_vstack_factor, self.consumer_config.grid_size_r
                ),
            )
        )

        solver.add(
            Implies(
                And(self.total_stack_factor > 1, self.prod_fullt_buf == 0),
                And(
                    divisible_either_direction(
                        self.post_padding_stacking_block_ct,
                        self.consumer_config.mblock_c * self.consumer_config.ublock_c,
                    ),
                    divisible_either_direction(
                        self.post_padding_stacking_block_rt,
                        self.consumer_config.mblock_r * self.consumer_config.ublock_r,
                    ),
                    # Stacking blocks must contain a whole number of consumer micro-blocks.
                    # Otherwise tile scan order inside mircro-blocks is a problem.
                    divisible_either_direction(
                        self.post_padding_stacking_block_ct, self.consumer_config.ublock_c
                    ),
                    divisible_either_direction(
                        self.post_padding_stacking_block_rt, self.consumer_config.ublock_r
                    ),
                ),
            )
        )

    def scan_order_constraints(self, solver: z3.Solver) -> None:
        """Constraints producer's stacking variables based on the consumer's microblock scan order.
        These constraints are relevant if multiple scan blocks (i.e. tiles from multiple producer t's)
        are needed to construct a single consumer-side macro-block. In this case, the scan
        order of micro-blocks insider the macro-block may require traversing tiles from different
        stacking blocks out of order.

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        solver.add(
            Implies(
                And(self.total_stack_factor > 1, self.prod_fullt_buf == 0),
                If(
                    # Stacking block within consumer micro-block
                    Or(
                        self.post_padding_stacking_block_ct < self.consumer_config.ublock_c,
                        self.post_padding_stacking_block_rt < self.consumer_config.ublock_r,
                    ),
                    # Multiple stacking blocks cover one consumer-side micro-block, so we need
                    # to follow row-major scan order within a micro-block
                    If(
                        self.post_padding_stacking_block_ct < self.consumer_config.ublock_c,
                        # We're stacking a micro-block along the c dimension, so it must be in
                        # row-major order with 1-tile tall blocks, and also consistent with the next
                        # level of scan order (between micro-block and macro-blocks)
                        And(
                            self.row_major_stack == 1,
                            self.post_padding_stacking_block_rt == 1,
                            Or(
                                self.consumer_config.mblock_c == 1,
                                And(
                                    self.consumer_config.ublock_r == 1,
                                    Or(
                                        self.consumer_config.ublock_scan_order
                                        == UblockOrder.r.value,
                                        self.consumer_config.mblock_r == 1,
                                    ),
                                ),
                            ),
                        ),
                        # else if
                        If(
                            self.post_padding_stacking_block_ct == self.consumer_config.ublock_c,
                            And(
                                self.row_major_stack == 0,
                                Or(
                                    self.consumer_config.mblock_c == 1,
                                    self.consumer_config.ublock_scan_order == UblockOrder.c.value,
                                ),
                            ),
                            # else: No other case of stacking within a micro-block will be
                            # consistent with the scan order
                            False,
                        ),
                    ),
                    If(
                        self.consumer_config.ublock_scan_order == UblockOrder.r.value,
                        # consumer has row-major ublock scan order
                        Implies(
                            self.post_padding_stacking_block_ct
                            < self.consumer_config.mblock_c * self.consumer_config.ublock_c,
                            And(
                                # we can't stack column-major if the consumer core requires input ublocks in row-major order
                                Or(
                                    self.row_major_stack == 1,
                                    self.post_padding_effective_vstack_factor == 1,
                                ),
                                # we must stack one row of ublocks at a time, otherwise
                                # scan order traverses tiles from producer t's out of order
                                self.post_padding_stacking_block_rt
                                == self.consumer_config.ublock_r,
                            ),
                        ),
                        # else: consumer has column-major ublock scan order
                        Implies(
                            self.post_padding_stacking_block_rt
                            < self.consumer_config.mblock_r * self.consumer_config.ublock_r,
                            And(
                                # we can't stack row-major if the consumer core requires input ublocks in column-major order
                                Or(
                                    self.row_major_stack == 0,
                                    self.post_padding_effective_hstack_factor == 1,
                                ),
                                # we must stack one column of ublocks at a time, otherwise
                                # scan order traverses tiles from producer t's out of order
                                self.post_padding_stacking_block_ct
                                == self.consumer_config.ublock_c,
                            ),
                        ),
                    ),
                ),
            )
        )


@dataclass
class KernelOpInputBase(OpInput):
    """
    Base class for all the Op inputs which are implemented by kernels and not by data movement stack.

    Attributes
    ----------
    producer:
        Producer node.
    """

    producer: Node


@dataclass
class ScheduledOpInputNoTMs(KernelOpInputBase):
    """
    Represents scheduled OP input inside the fused op with no TMs.
    """

    # @override
    def get_tms(self) -> List[TMInfo]:
        return []

    # @override
    def get_rt(self) -> z3.Var:
        return self.producer.get_vertical_size_var()

    # @override
    def get_ct(self) -> z3.Var:
        return self.producer.get_horizontal_size_var()

    # @override
    def get_t(self) -> z3.Var:
        return self.producer.t_dim

    # @override
    def constrain(self, solver: z3.Solver) -> None:
        pass


@dataclass
class ScheduledOpInputTMs(KernelOpInputBase):
    """
    Represents scheduled OP input inside the fused op with multiple TMs.

    Attributes
    ----------
    tms:
        List of all the TMs on this point-to-point connection.
    """

    tms: List[ScheduledOpTMInfo]

    # @override
    def get_tms(self) -> List[TMInfo]:
        return self.tms

    # @override
    def get_rt(self) -> z3.Var:
        return self.tms[-1].post_tm_rt

    # @override
    def get_ct(self) -> z3.Var:
        return self.tms[-1].post_tm_ct

    # @override
    def get_t(self) -> z3.Var:
        return self.producer.t_dim

    def constrain(self, solver: z3.Solver) -> None:
        """Constrains a single producer -> consumer connection, so that the TMs on
        that connection are compatible with each other (in terms of tensor shape).

        Parameters
        ----------
        solver:
            z3 Solver instance to which to add constraints.
        """
        # Tensor shape throughout tms has to match.
        input_rt = self.producer.get_vertical_size_var()
        input_ct = self.producer.get_horizontal_size_var()

        # Output of each TM must match its input modified by the TM operation.
        for tm_info in self.tms:
            if tm_info.tm_type == TMS.c_broadcast.value:
                solver.add(
                    And(
                        # out_ct == broadcast_factor * in_ct
                        tm_info.post_tm_ct == tm_info.tm_arg * input_ct,
                        tm_info.post_tm_rt == input_rt,
                        tm_info.tm_arg >= 1,
                    )
                )
            elif tm_info.tm_type == TMS.r_broadcast.value:
                solver.add(
                    And(
                        # out_rt == broadcast_factor * in_rt
                        tm_info.post_tm_rt == tm_info.tm_arg * input_rt,
                        tm_info.post_tm_ct == input_ct,
                        tm_info.tm_arg >= 1,
                    )
                )
            elif tm_info.tm_type == TMS.tile_broadcast.value:
                solver.add(
                    And(
                        tm_info.post_tm_rt == input_rt,
                        tm_info.post_tm_ct == input_ct,
                        tm_info.tm_arg >= TileBroadcastDimension.r.value,
                        tm_info.tm_arg <= TileBroadcastDimension.c.value,
                    )
                )

            input_rt = tm_info.post_tm_rt
            input_ct = tm_info.post_tm_ct
