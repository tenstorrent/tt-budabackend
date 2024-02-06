# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from abc import ABC
from dataclasses import dataclass
from enum import Enum
import os
import os.path
from typing import Callable, List, Dict, Tuple
from pathlib import Path

from z3 import *

from test_modules.common.template_netlist_test_base import TemplateNetlistTestBase
from test_modules.common.device_architecture import DeviceArchitecture
from test_modules.common.node import Node
from test_modules.common.enums import TMS, BufferLoc, Dimension, Untilize
from test_modules.common.op_input import NetlistTMConfiguration
from test_modules.common.sweep import SweepVarsGroup
from test_modules.common.constants import (
    MULTI_TM_TESTS_SWEEP_TMS,
    TEMPLATE_VAR_PREFIX,
)


class MultiTMTestParameters:
    """
    Exposes environment variables which are neded when using multi TM test modules in the Z3 generator through its static methods.
    """

    @staticmethod
    def get_num_determinism_loops() -> int:
        return int(os.getenv("NUM_DETERMINISM_LOOPS", 0))

    @staticmethod
    def get_num_tm_for_conn(input_index: int) -> int:
        return int(os.getenv(f"NUM_TMS_PER_CONN{input_index}", 2))

    @staticmethod
    def get_num_tms_per_conn(num_inputs: int) -> List[int]:
        return [MultiTMTestParameters.get_num_tm_for_conn(i) for i in range(num_inputs)]

    @staticmethod
    def get_num_configs_per_tms_combination() -> int:
        return int(os.getenv("NUM_CONFIGS_PER_TMS_COMBINATION", 10))

    @staticmethod
    def get_num_determinism_loops() -> int:
        return int(os.getenv("NUM_DETERMINISM_LOOPS", 0))

    @staticmethod
    def get_max_output_tensor_in_tiles() -> int:
        return int(os.getenv("MAX_OUTPUT_TENSOR_IN_TILES", 16 * 16 * 64))

    @staticmethod
    def get_fork_factor() -> int:
        return int(os.getenv("FORK_FACTOR", 2))

    @staticmethod
    def get_is_multichip_test() -> int:
        return int(os.getenv("MULTICHIP_TEST", 0))

    @staticmethod
    def get_is_matmul_bias_test() -> int:
        return int(os.getenv("MATMUL_BIAS", 0))

    @staticmethod
    def get_max_input_tensor_in_tiles(index: int) -> int:
        return int(os.getenv(f"MAX_INPUT{index}_TENSOR_IN_TILES", 16 * 16 * 64))

    @staticmethod
    def get_is_prologue_test() -> int:
        return os.getenv("PROLOGUE_TEST", 0)

    @staticmethod
    def get_force_pad_unpad_set() -> int:
        return int(os.getenv("FORCE_HAS_PAD_UNPAD_SET", 0))

    @staticmethod
    def get_force_keep_padding() -> int:
        return int(os.getenv("FORCE_KEEP_PADDING", 0))


@dataclass
class StackingGuidingOptions:
    """
    Defines variables which control guiding for better multi TMs coverage in the
    case of TM stacking when the producer does not have full t buffering.

    Attributes
    ----------
    guiding_enabled:
        Controls if the guiding is enabled, if not no constraints will be added.
        Controlled by GUIDING_ENABLED env variable.
    force_prod_fullt_buf_zero_and_stack_greater_than_one_condition:
        Controls if we want to force the condition that the producer op does not
        have full t buffering and at least one TM in TM sequence is a stack operation.
        Controlled by FORCE_PROD_FULLT_BUF_ZERO_AND_STACK_GREATER_THAN_ONE_CONDITION
        env variable.
    force_total_slice_factor_mod_stack_factor_is_zero:
        Controls if we want to make slice and stack factors divisible.
        Controlled by FORCE_TOTAL_SLICE_FACTOR_MOD_STACK_FACTOR_IS_ZERO env variable.
    force_total_slice_factor_div_stack_factor_mod_stack_factor_is_zero:
        Controls if we want to set (slice_factor / stack_factor) % stack_factor == 0.
        Controlled by FORCE_TOTAL_SLICE_FACTOR_DIV_STACK_FACTOR_MOD_STACK_FACTOR_IS_ZERO
        env variable.
    force_tm_args_greater_than_one:
        Controls if we want to force that all arguments of TMs are > 1.
        Controlled by FORCE_TM_ARGS_GREATER_THAN_ONE env variable.
    force_tm_args_equal_one:
        Controls if we want to set all the TM arguments to 1.
        Controlled by FORCE_TM_ARGS_EQUAL_ONE env variable.
    force_different_last_two_tm_args:
        Controls if we want to force that the last two TM arguments are different,
        assuming we have more than 1 TM.
        Controlled by FORCE_DIFFERENT_LAST_TWO_TM_ARGS env variable.
    force_stacking_within_consumer_ublock_c:
        Controls if we want fo force stacking within consumer ublock branch coverage
        in scan order constarints by forcing stacking_block_ct < ublock_c.
        Controlled by FORCE_STACKING_WITHIN_CONSUMER_UBLOCK_C env variable.
    force_stacking_within_consumer_ublock_r:
        Controls if we want fo force stacking within consumer ublock branch coverage
        in scan order constarints by forcing stacking_block_rt < ublock_r
        and stacking_block_ct == ublock_c.
        Controlled by FORCE_STACKING_WITHIN_CONSUMER_UBLOCK_R env variable.
    force_has_pad_unpad_set:
        Controls if we want to force padding and unpadding values to be greater than 0.
        Controlled by FORCE_HAS_PAD_UNPAD_SET env variable.
    force_keep_padding:
        Controls if the padding gets removed on the consumer op, i.e. the feeder op sets
        padding which is then not removed on the consumer op.
        Controlled by FORCE_KEEP_PADDING env variable.
    """

    guiding_enabled: int = 0
    force_prod_fullt_buf_zero_and_stack_greater_than_one_condition: int = 0
    force_total_slice_factor_mod_stack_factor_is_zero: int = 0
    force_total_slice_factor_div_stack_factor_mod_stack_factor_is_zero: int = 0
    force_tm_args_greater_than_one: int = 0
    force_tm_args_equal_one: int = 0
    force_different_last_two_tm_args: int = 0

    force_stacking_within_consumer_ublock_c: int = 0
    force_stacking_within_consumer_ublock_r: int = 0

    force_has_pad_unpad_set: int = 0
    force_keep_padding: int = 0

    @staticmethod
    def create_from_env() -> StackingGuidingOptions:
        """Factory method for creating an instance of GuidingOptions class from the
        enviroment variables. Variables are assumed to be set in the all-uppercase
        notation (e.g. GUIDING_ENABLED). Default value for all the variables (if
        they can't be read from the env) is 0.
        """
        return StackingGuidingOptions(
            guiding_enabled=int(os.getenv("GUIDING_ENABLED", 0)),
            force_prod_fullt_buf_zero_and_stack_greater_than_one_condition=int(
                os.getenv("FORCE_PROD_FULLT_BUF_ZERO_AND_STACK_GREATER_THAN_ONE_CONDITION", 0)
            ),
            force_total_slice_factor_mod_stack_factor_is_zero=int(
                os.getenv("FORCE_TOTAL_SLICE_FACTOR_MOD_STACK_FACTOR_IS_ZERO", 0)
            ),
            force_total_slice_factor_div_stack_factor_mod_stack_factor_is_zero=int(
                os.getenv("FORCE_TOTAL_SLICE_FACTOR_DIV_STACK_FACTOR_MOD_STACK_FACTOR_IS_ZERO", 0)
            ),
            force_different_last_two_tm_args=int(os.getenv("FORCE_DIFFERENT_LAST_TWO_TM_ARGS", 0)),
            force_tm_args_greater_than_one=int(os.getenv("FORCE_TM_ARGS_GREATER_THAN_ONE", 0)),
            force_tm_args_equal_one=int(os.getenv("FORCE_TM_ARGS_EQUAL_ONE", 0)),
            force_stacking_within_consumer_ublock_c=int(
                os.getenv("FORCE_STACKING_WITHIN_CONSUMER_UBLOCK_C", 0)
            ),
            force_stacking_within_consumer_ublock_r=int(
                os.getenv("FORCE_STACKING_WITHIN_CONSUMER_UBLOCK_R", 0)
            ),
            force_has_pad_unpad_set=MultiTMTestParameters.get_force_pad_unpad_set(),
            force_keep_padding=MultiTMTestParameters.get_force_keep_padding(),
        )


class MultiTmTestBase(TemplateNetlistTestBase, ABC):
    """
    Base class for multi TMs tests generators.

    Individual multi TM tests should extend this class and override
    function add_additional_constraints() to add specific constraints.

    Additionally, additional sweep vars can also be exported by overriding
    function export_additional_sweep_vars().

    One can control the coverage for multi TM tests with stacking TMs in the
    case where producer does not have full t buffering, for more information
    see the docstring of StackingGuidingOptions class.
    """

    def __init__(
        self,
        solver: z3.Solver,
        svars: Dict[str, z3.Var],
        template_path: str,
        num_tms_per_conn: List[int],
        num_configs_per_tms: int,
        arch: DeviceArchitecture,
        guiding_options: StackingGuidingOptions = StackingGuidingOptions.create_from_env(),
        verbose: bool = True,
    ) -> None:
        """
        Initializes MultiTmTestBase.

        Parameters
        ----------
        solver:
            Z3 solver to add constraints to.
        svars:
            Dictionary to store all created Z3 variables with their name: value pairs.
        template_path:
            Template file location.
        num_tms_per_conn:
            Number of TMs for all the input connections.
        num_configs_per_tms:
            How many configuration for a single TM combination to generate.
        arch:
            Device architecture configuration.
        guiding_options:
            See docstring of StackingGuidingOptions for more details.
        verbose:
            Print logs to stdout.
        """
        super().__init__(
            solver=solver, svars=svars, template_path=template_path, arch=arch, verbose=verbose
        )
        self.num_tms_per_conn = num_tms_per_conn
        self.num_configs_per_tms = num_configs_per_tms
        self.guiding_options = guiding_options

        self.op_input_has_tms: Dict[str, Dict[str, bool]] = {}

    def log(self, *args, **kwargs) -> None:
        """
        Prints output to stdout if verbose attribute has been set to True. Simply forwards
        arguments to built-in print function.
        """

        if self.verbose:
            print(*args, **kwargs)

    # @override
    def export_op_input_tm_configuration(
        self, op_node: Node, input_idx: int
    ) -> List[NetlistTMConfiguration]:
        input_name = op_node.inputs[input_idx]
        if not self.op_input_has_tms[op_node.name][input_name]:
            return []

        # We want to randomize both TM args and TM params.
        return [
            NetlistTMConfiguration.randomized() for _ in range(self.num_tms_per_conn[input_idx])
        ]

    # @override
    def parse_op_tms(self, op_node: Node, op_params: Dict[str, any]) -> None:
        self.op_input_has_tms[op_node.name] = {input_name: False for input_name in op_node.inputs}
        for input_idx, input_name in enumerate(op_node.inputs):
            key_name = f"input_{input_idx}_tms"
            if TemplateNetlistTestBase.is_field_template_var(op_params.get(key_name, "")):
                setattr(
                    op_node,
                    f"input_{input_idx}_tms_template_str",
                    op_params[key_name][len(TEMPLATE_VAR_PREFIX) :],
                )
                self.op_input_has_tms[op_node.name][input_name] = True

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        tm_value_range = [tm.value for tm in MULTI_TM_TESTS_SWEEP_TMS]
        pad_r_range = list(range(0, 20))
        pad_c_range = list(range(0, 20))

        sweep_vars = []
        for consumer_op in self.ops:
            for input_name in consumer_op.inputs:
                if self.op_input_has_tms[consumer_op.name][input_name]:
                    var_names_range_dict = {}

                    for tm in self.op_input_tm_map[consumer_op.name][input_name].get_tms():
                        var_names_range_dict[tm.tm_type.sexpr()] = tm_value_range
                    # TODO: Uncomment this to force better padding coverage.
                    # op_input_padding = self.op_input_pad_unpad[consumer_op.name][input_name]

                    # def __add_to_sweep_action(var, sweep_range):
                    #     var_names_range_dict[var.sexpr()] = sweep_range

                    # if (
                    #     self.guiding_options.force_has_pad_unpad_set
                    #     or self.guiding_options.force_keep_padding
                    # ):
                    #     self.do_action_if_z3_var(
                    #         op_input_padding.pad_ct, lambda x: __add_to_sweep_action(x, pad_c_range)
                    #     )
                    #     self.do_action_if_z3_var(
                    #         op_input_padding.pad_rt, lambda x: __add_to_sweep_action(x, pad_r_range)
                    #     )
                    sweep_vars.append(
                        SweepVarsGroup(
                            var_names_range_dict=var_names_range_dict,
                            max_num_configs_per_combination=self.num_configs_per_tms,
                        )
                    )

        additional_sweep_vars = self.export_additional_sweep_vars()

        if additional_sweep_vars:
            sweep_vars.extend(additional_sweep_vars)

        return sweep_vars

    # @override
    def export_coverage_vars(self) -> List[str]:
        coverage_vars = []
        add_to_coverage_vars_action = lambda var: coverage_vars.append(var.sexpr())

        if self.guiding_options.force_has_pad_unpad_set or self.guiding_options.force_keep_padding:
            for consumer in self.ops:
                for producer_name in consumer.inputs:
                    pad_unpad_params = self.op_input_pad_unpad[consumer.name][producer_name]
                    self.do_action_if_z3_var(pad_unpad_params.unpad_rt, add_to_coverage_vars_action)
                    self.do_action_if_z3_var(pad_unpad_params.unpad_ct, add_to_coverage_vars_action)
                    self.do_action_if_z3_var(pad_unpad_params.pad_rt, add_to_coverage_vars_action)
                    self.do_action_if_z3_var(pad_unpad_params.pad_ct, add_to_coverage_vars_action)

        return coverage_vars

    # @override
    def constrain_ops(self) -> None:
        super().constrain_ops()

        # In TM tests we force all untilize attributes to be false.
        for op_node in self.ops:
            self.solver.add(op_node.untilize == Untilize.false.value)

    # @override
    def constrain_queue_location(self, q_node: Node) -> None:
        super().constrain_queue_location(q_node)

        # In TM tests we force all the queues to be located in DRAM.
        self.solver.add(q_node.loc == BufferLoc.dram.value)

    # @override
    def constrain_point_to_point_connection(self, producer: Node, consumer: Node) -> None:
        super().constrain_point_to_point_connection(producer, consumer)

        self.multi_tm_guiding_constraints(producer, consumer)

    # @override
    def constrain_matmul_op(self, op_node: Node) -> None:
        super().constrain_matmul_op(op_node)

        # Constrain stacking along matmul outer dim to prevent deadlock.
        in0 = self.nodes[op_node.inputs[0]]
        in1 = self.nodes[op_node.inputs[1]]

        op_input0 = self.op_input_tm_map[op_node.name][in0.name]
        op_input1 = self.op_input_tm_map[op_node.name][in1.name]

        for tm in op_input0.get_tms():
            self.solver.add(
                Implies(
                    And(
                        op_input0.prod_fullt_buf == 0, tm.tm_type == TMS.vstack.value, tm.tm_arg > 1
                    ),
                    op_node.grid_size_y == 1,
                )
            )

        for tm in op_input1.get_tms():
            self.solver.add(
                Implies(
                    And(
                        op_input1.prod_fullt_buf == 0, tm.tm_type == TMS.hstack.value, tm.tm_arg > 1
                    ),
                    op_node.grid_size_x == 1,
                )
            )

    # @override
    def postprocess(self, model_vars: Dict[str, any]) -> None:
        super().postprocess(model_vars)

        self.create_tms_strings(model_vars)

    def get_consumer_ops_with_tms(self) -> List[Node]:
        """Returns the list of all OP nodes whose at least one input has TMs."""
        return [
            self.nodes[consumer_op_name]
            for (consumer_op_name, inputs_have_tms) in self.op_input_has_tms.items()
            if any(inputs_have_tms.values())
        ]

    def multi_tm_guiding_constraints(self, producer: Node, consumer: Node) -> None:
        """Applies guiding constraints to a single producer -> consumer connection
        to get better coverage for multi TM tests with stacking where the producer
        does not have full t buffering.

        For more details, see docstring of StackingGuidingOptions.

        Parameters
        ---------
        producer:
            Producer.
        consumer:
            Consumer.
        """
        # We always guide pad and unpad constraints in order to keep backward compatibility, by default they will
        # be constrained to 0.
        self.guide_pad_unpad_constraints(producer, consumer)

        if self.guiding_options.guiding_enabled == 0:
            self.log(
                f"No guiding constraints have been added for point-to-point connection {producer.name} -> {consumer.name}."
            )
            return

        # If there are no any TMs on producer -> consumer connection, then there is nothing else to guide.
        if not self.op_input_has_tms[consumer.name][producer.name]:
            return

        if not producer.is_queue():
            self.phased_stack_coverage_guiding_constraints(producer, consumer)

        self.guide_tm_args_constraints(producer, consumer)

    def guide_pad_unpad_constraints(self, producer: Node, consumer: Node):
        """
        Guide values of pad/unpad attributes on a producer consumer connection.

        Parameters
        ----------
        producer:
            Producer node.
        consumer:
            Consumer node.
        """
        if (
            consumer.name not in self.op_input_pad_unpad
            or producer.name not in self.op_input_pad_unpad[consumer.name]
        ):
            self.log(
                f"Did not found {producer.name} -> {consumer.name} connection in pad/unpad mapping."
            )
            return

        input_idx = self.get_consumers_producer_input_index(producer, consumer)
        supports_r_padding, supports_c_padding = self.op_input_supports_padding(consumer, input_idx)
        pad_unpad_params = self.op_input_pad_unpad[consumer.name][producer.name]
        is_feeder_op = consumer.name.startswith("feeder") and producer.is_queue()
        force_padding = (self.guiding_options.force_has_pad_unpad_set and not is_feeder_op) or (
            self.guiding_options.force_keep_padding and is_feeder_op
        )

        if self.guiding_options.force_has_pad_unpad_set:
            self.log("Adding constraints for forcing unpadding > 0.")
            self.try_apply_constraints_if_z3_var(pad_unpad_params.unpad_ct, lambda x: x > 0)
            self.try_apply_constraints_if_z3_var(pad_unpad_params.unpad_rt, lambda x: x > 0)
        else:
            self.try_apply_constraints_if_z3_var(pad_unpad_params.unpad_ct, lambda x: x == 0)
            self.try_apply_constraints_if_z3_var(pad_unpad_params.unpad_rt, lambda x: x == 0)

        if force_padding:
            if supports_r_padding:
                self.try_apply_constraints_if_z3_var(pad_unpad_params.pad_rt, lambda x: x > 0)
            if supports_c_padding:
                self.try_apply_constraints_if_z3_var(pad_unpad_params.pad_ct, lambda x: x > 0)
        else:
            self.try_apply_constraints_if_z3_var(pad_unpad_params.pad_ct, lambda x: x == 0)
            self.try_apply_constraints_if_z3_var(pad_unpad_params.pad_rt, lambda x: x == 0)

    def guide_tm_args_constraints(self, producer: Node, consumer: Node) -> None:
        """Applies guiding constraints which force specific TM arguments values/relationships on the
        point-to-point connection between producer and consumer.

        Paramters
        ---------
        producer:
            Producer.
        consumer:
            Consumer.
        """
        op_input_tms = self.op_input_tm_map[consumer.name][producer.name]

        num_tms = len(op_input_tms.get_tms())

        if self.guiding_options.force_tm_args_greater_than_one == 1:
            for tm in op_input_tms.get_tms():
                self.log("\tAdding constraint for all coverage cases:")
                self.log(f"\t\t{tm.tm_arg.sexpr()} > 1")

                self.solver.add(tm.tm_arg > 1)

        if self.guiding_options.force_tm_args_equal_one == 1:
            for tm in op_input_tms.get_tms():
                self.log("\tAdding constraint for all coverage cases:")
                self.log(f"\t\t{tm.tm_arg.sexpr()} == 1")

                self.solver.add(tm.tm_arg == 1)

        if self.guiding_options.force_different_last_two_tm_args == 1 and num_tms > 1:
            tms = op_input_tms.get_tms()
            self.log("\t\t\tAdding constraint for coverage case 111: ")
            self.log("\t\t\t\tsolver.add(")
            self.log(f"\t\t\t\t\t {tms[-1].tm_arg.sexpr()} != {tms[-2].tm_arg.sexpr()}")
            self.log("\t\t\t\t)")

            self.solver.add(tms[-1].tm_arg != tms[-2].tm_arg)

    def phased_stack_coverage_guiding_constraints(self, producer: Node, consumer: Node) -> None:
        """Applies guiding constraints which force the coverage of a specific case of
        net2pipe::tile_maps::check_phased_stack for a point-to-point connection
        between producer and consumer nodes.

        Parameters
        ----------
        producer:
            Producer.
        consuemr:
            Consumer.
        """
        op_input_tms = self.op_input_tm_map[consumer.name][producer.name]

        h = Dimension.Horizontal
        v = Dimension.Vertical

        input_idx = self.get_consumers_producer_input_index(producer, consumer)
        consumer_config = self.get_op_consumer_config(consumer, input_idx)

        self.log(
            f"Adding the following guiding constraints for point-to-point connection {producer.name} -> {consumer.name}:"
        )

        if self.guiding_options.force_prod_fullt_buf_zero_and_stack_greater_than_one_condition == 1:
            self.log("\tAdding constraints for coverage cases 1xx:")
            self.log(
                f"\t\tAnd({op_input_tms.total_stack_factor.sexpr()} > 1, {op_input_tms.prod_fullt_buf.sexpr()} == 0)"
            )
            self.solver.add(
                And(op_input_tms.total_stack_factor > 1, op_input_tms.prod_fullt_buf == 0)
            )

            # Add constraints for stacking within consumer ublock
            if self.guiding_options.force_stacking_within_consumer_ublock_c:
                self.log(f"\t\tAdding constraints for coverage cases 1xx C:")
                self.log(
                    f"\t\t\t {op_input_tms.stacking_block_ct.sexpr()} < {consumer_config.ublock_c.sexpr()}"
                )
                self.solver.add(op_input_tms.stacking_block_ct < consumer_config.ublock_c)

            if self.guiding_options.force_stacking_within_consumer_ublock_r:
                self.log(f"\t\tAdding constraints for coverage cases 1xx R:")
                self.log(
                    f"\t\t\t {op_input_tms.stacking_block_rt.sexpr()} < {consumer_config.ublock_r.sexpr()},"
                )
                self.log(
                    f"\t\t\t {op_input_tms.stacking_block_ct.sexpr()} == {consumer_config.ublock_c.sexpr()}"
                )
                self.solver.add(
                    op_input_tms.stacking_block_rt < consumer_config.ublock_r,
                    op_input_tms.stacking_block_ct == consumer_config.ublock_c,
                )

            # These cases are only relevant for TM combinations with at least one stack
            if self.guiding_options.force_total_slice_factor_mod_stack_factor_is_zero == 1:
                self.log("\t\tAdding constraint for coverage case 11x: ")
                self.log("\t\t\tIf(")
                self.log(f"\t\t\t\t\t{op_input_tms.row_major_stack.sexpr()} == 1,")
                self.log(
                    f"\t\t\t\t\t{op_input_tms.total_slice_factor.sexpr()} % {op_input_tms.stack_factor[h].sexpr()} == 0,"
                )
                self.log(
                    f"\t\t\t\t\t{op_input_tms.total_slice_factor.sexpr()} % {op_input_tms.stack_factor[v].sexpr()}] == 0"
                )
                self.log("\t\t\t)")
                self.solver.add(
                    If(
                        op_input_tms.row_major_stack == 1,
                        op_input_tms.total_slice_factor % op_input_tms.stack_factor[h] == 0,
                        op_input_tms.total_slice_factor % op_input_tms.stack_factor[v] == 0,
                    )
                )

                if (
                    self.guiding_options.force_total_slice_factor_div_stack_factor_mod_stack_factor_is_zero
                    == 1
                ):  # coverage case 111
                    self.log("\t\t\tAdding constraint for coverage case 111: ")
                    self.log("\t\t\t\tIf(")
                    self.log(f"\t\t\t\t\t\t{op_input_tms.row_major_stack.sexpr()} == 1,")
                    self.log(
                        f"\t\t\t\t\t\t{op_input_tms.total_slice_stack_ratio[h].sexpr()} % {op_input_tms.stack_factor[v].sexpr()} == 0,"
                    )
                    self.log(
                        f"\t\t\t\t\t\t{op_input_tms.total_slice_stack_ratio[v].sexpr()} % {op_input_tms.stack_factor[h].sexpr()} == 0"
                    )
                    self.log("\t\t\t\t)")
                    self.solver.add(
                        If(
                            op_input_tms.row_major_stack == 1,
                            op_input_tms.total_slice_stack_ratio[h] % op_input_tms.stack_factor[v]
                            == 0,
                            op_input_tms.total_slice_stack_ratio[v] % op_input_tms.stack_factor[h]
                            == 0,
                        )
                    )
                else:  # coverage case 110
                    self.log("\t\t\tAdding constraint for coverage case 110:")
                    self.log("\t\t\t\tIf(")
                    self.log(f"\t\t\t\t\t\t{op_input_tms.row_major_stack.sexpr()} == 1,")
                    self.log(
                        f"\t\t\t\t\t\t{op_input_tms.total_slice_stack_ratio[h].sexpr()} % {op_input_tms.stack_factor[v].sexpr()} != 0,"
                    )
                    self.log(
                        f"\t\t\t\t\t\t{op_input_tms.total_slice_stack_ratio[v].sexpr()} % {op_input_tms.stack_factor[h].sexpr()} != 0"
                    )
                    self.log("\t\t\t\t)")
                    self.solver.add(
                        If(
                            op_input_tms.row_major_stack == 1,
                            op_input_tms.total_slice_stack_ratio[h] % op_input_tms.stack_factor[v]
                            != 0,
                            op_input_tms.total_slice_stack_ratio[v] % op_input_tms.stack_factor[h]
                            != 0,
                        )
                    )

            else:  # coverage cases 10x
                self.log("\t\tAdding constraint for coverage cases 10x:")
                self.log("\t\t\tIf(")
                self.log(f"\t\t\t\t\t{op_input_tms.row_major_stack.sexpr()} == 1,")
                self.log(
                    f"\t\t\t\t\t{op_input_tms.total_slice_factor.sexpr()} % {op_input_tms.stack_factor[h].sexpr()} != 0,"
                )
                self.log(
                    f"\t\t\t\t\t{op_input_tms.total_slice_factor.sexpr()} % {op_input_tms.stack_factor[v].sexpr()} != 0"
                )
                self.log("\t\t\t)")
                self.solver.add(
                    If(
                        op_input_tms.row_major_stack == 1,
                        op_input_tms.total_slice_factor % op_input_tms.stack_factor[h] != 0,
                        op_input_tms.total_slice_factor % op_input_tms.stack_factor[v] != 0,
                    )
                )

        else:  # coverage case 000 (0xx)
            # Only do this if there is TMs on the input.
            if len(op_input_tms.get_tms()) != 0:
                self.log("\tAdding constraint for coverage cases 0xx:")
                self.log(f"\t\t{op_input_tms.prod_fullt_buf.sexpr()} == 1")
                self.solver.add(op_input_tms.prod_fullt_buf == 1)

    def create_tms_strings(self, model_vars: Dict[str, any]) -> None:
        """Updates `model_vars` TM string created for every connection where
        the TMs should be presented. The strings are constructed based on the
        TM type and TM argument variable values.

        Parameters
        ----------
        model_vars:
            Dictionary of variable names and their generated values.
        """
        tm_formats = {
            TMS.c_broadcast.value: "broadcast: {{c: {}}}",
            TMS.r_broadcast.value: "broadcast: {{r: {}}}",
            TMS.t_broadcast.value: "broadcast: {{z: {}}}",
            TMS.transpose.value: "transpose",
            TMS.hstack.value: "hstack: {}",
            TMS.vstack.value: "vstack: {}",
            TMS.hslice.value: "hslice: {}",
            TMS.vslice.value: "vslice: {}",
        }

        for consumer_op in self.ops:
            for input_idx, input_name in enumerate(consumer_op.inputs):
                tm_str = ""
                # If we do have any TMs on the input connection.
                op_input = self.op_input_tm_map[consumer_op.name][input_name]
                op_input_tms = op_input.get_tms()
                if len(op_input_tms) > 0:
                    tm_str = "[{}]".format(
                        ", ".join(
                            tm_formats[model_vars[tm.tm_type.sexpr()]].format(
                                model_vars[tm.tm_arg.sexpr()]
                            )
                            for tm in op_input_tms
                        )
                    )

                input_tms_template_str = getattr(
                    consumer_op, f"input_{input_idx}_tms_template_str", None
                )
                if input_tms_template_str:
                    model_vars[input_tms_template_str] = tm_str

    def make_sweep_var_groups_from_coverage_report(
        self,
        tm_strings: List[str],
        tm_vars: List[z3.Var],
        delimiter: str = ",",
    ) -> List[SweepVarsGroup]:
        """Create list of sweep vars group required to get full coverage.

        Parameters
        ----------
        tm_strings:
            List of TM string representing sweep combinations. Required format:
            [[vslice, hstack], [r_broadcast, transpose]].
        tm_vars:
            List of TM type variables which will be assigned the values from TM strings.
        delimiter:
            Delimiter by which to split the TM strings.
        """
        sweep_vars = []
        for tm_string in tm_strings:
            tm_names = [tm_name.strip() for tm_name in tm_string.split(delimiter)]
            var_dict = {
                tm_var.sexpr(): [TMS[tm_name].value] for tm_var, tm_name in zip(tm_vars, tm_names)
            }
            sweep_vars.append(
                SweepVarsGroup(
                    var_names_range_dict=var_dict,
                    max_num_configs_per_combination=self.num_configs_per_tms,
                )
            )
        return sweep_vars

    def try_apply_constraints_if_z3_var(
        self, var: z3.Var | int, constraint_provider: Callable[[any], z3.Var]
    ) -> None:
        """
        Try to apply constraints on a variable if variable is z3.Var. If var is of any other type, then
        this function is a no-op.

        Parameters
        ----------
        var:
            Variable on which to try to apply constraint.
        constraint_provider:
            Constraint provider, takes var as argument and returns z3 constraint.
        """
        self.do_action_if_z3_var(var, lambda x: self.solver.add(constraint_provider(x)))

    def do_action_if_z3_var(self, var: z3.Var | int, var_action: Callable[[z3.Var], None]) -> None:
        """
        Perform action on a given variable if it is a z3.Var.

        Parameters
        ----------
        var:
            Variable on which to try to perform action.
        var_action:
            Action to perform.
        """
        if isinstance(var, z3.ArithRef):
            var_action(var)

    @staticmethod
    def get_test_module_names() -> List[str]:
        return [
            "test_modules.multi_tm_tests." + filename.strip(".py")
            for filename in os.listdir(Path(__file__).parent.resolve())
            if filename.startswith("test_") and filename.endswith(".py")
        ]


class MultiTMForkingTestBase(MultiTmTestBase, ABC):
    """
    Base class for all the TM tests regarding forking producer buffers.
    """

    # @overide
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        tm_value_range = [
            TMS.transpose.value,
            TMS.r_broadcast.value,
            TMS.hstack.value,
            TMS.hslice.value,
        ]

        consumer_ops = self.get_consumer_ops_with_tms()
        assert len(consumer_ops) > 0, "No consumer ops with TMs found."

        n_inputs = len(consumer_ops[0].inputs)
        for consumer_op in consumer_ops:
            assert (
                len(consumer_op.inputs) == n_inputs
            ), "All consumer ops must have the same number of inputs."

        sweep_vars = [
            SweepVarsGroup(
                var_names_range_dict={
                    tm.tm_type.sexpr(): tm_value_range
                    for consumer_op in consumer_ops
                    for tm in self.op_input_tm_map[consumer_op.name][
                        consumer_op.inputs[input_idx]
                    ].get_tms()
                },
                max_num_configs_per_combination=self.num_configs_per_tms,
                drop_symmetric_tm_configurations=True,
                num_tms_per_conn=self.num_tms_per_conn,
            )
            for input_idx in range(n_inputs)
        ]
        additional_sweep_vars = self.export_additional_sweep_vars()

        if additional_sweep_vars:
            sweep_vars.extend(additional_sweep_vars)

        return sweep_vars


class ForceReblockingOptions(Enum):
    force_grid_size_x_reblocking = 0
    force_grid_size_y_reblocking = 1
    force_mblock_r_reblocking = 2
    force_mblock_c_reblocking = 3
    force_ublock_r_reblocking = 4
    force_ublock_c_reblocking = 5
    force_scan_order_reblocking = 6


class MultiTMKernelBroadcastTestBase(MultiTmTestBase, ABC):
    """
    Base class for all the TM tests regarding kernel broadcast. The difference between 'regular' and
    kernel broadcasts is that the regular broadcast is implemented by pipegen stack, whereas kernel
    broadcast is implemented by OPs, so they test different code paths.
    """

    def __init__(
        self,
        solver: z3.Solver,
        svars: Dict[str, z3.Var],
        template_path: str,
        num_tms_per_conn: List[int],
        num_configs_per_tms: int,
        arch: DeviceArchitecture,
        verbose: bool = True,
    ) -> None:
        # Kernel broadcast is now allowed for stacking pipes so we only test for the case where we have
        # full-t buffering on producer side.
        guiding_options = StackingGuidingOptions().create_from_env()
        guiding_options.guiding_enabled = 1
        super().__init__(
            solver,
            svars,
            template_path,
            num_tms_per_conn,
            num_configs_per_tms,
            arch,
            guiding_options=guiding_options,
            verbose=verbose,
        )

        self.max_tms_per_conn = max(self.num_tms_per_conn)
        assert self.max_tms_per_conn <= 3, "Expecting max 3 TMs per connection."

        self._bcast_min_value = 2
        self._bcast_max_value = 20 if max(self.num_tms_per_conn) == 1 else 10

        self.kernel_bcast_idx = self.add_var("kernel_bcast_idx")
        self.reblocking_option_idx = self.add_var("reblocking_option_idx")

    # @override
    def constrain_point_to_point_connections(self) -> None:
        super().constrain_point_to_point_connections()

        for consumer_op in self.get_consumer_ops_with_tms():
            for input_idx, input_name in enumerate(consumer_op.inputs):
                input_node = self.nodes[input_name]

                op_input_tms = self.op_input_tm_map[consumer_op.name][input_name]

                self.solver.add(
                    Implies(
                        self.is_op_input_kernel_broadcasted(input_idx),
                        And(
                            input_node.t_dim == 1,
                            # For kernel broadcast, producer buf_size_mb needs to be 1 or 2
                            Implies(
                                self.max_tms_per_conn <= 2,
                                Or(input_node.buf_size_mb == 1, input_node.buf_size_mb == 2),
                            ),
                            # Force reblocking between producer and consumer.
                            Implies(
                                self.reblocking_option_idx
                                == ForceReblockingOptions.force_grid_size_x_reblocking.value,
                                input_node.grid_size_x != consumer_op.grid_size_x,
                            ),
                            Implies(
                                self.reblocking_option_idx
                                == ForceReblockingOptions.force_grid_size_y_reblocking.value,
                                input_node.grid_size_y != consumer_op.grid_size_y,
                            ),
                            Implies(
                                self.reblocking_option_idx
                                == ForceReblockingOptions.force_mblock_r_reblocking.value,
                                input_node.mb_m != consumer_op.mb_m,
                            ),
                            Implies(
                                self.reblocking_option_idx
                                == ForceReblockingOptions.force_mblock_c_reblocking.value,
                                input_node.mb_n != consumer_op.mb_m,
                            ),
                            Implies(
                                self.reblocking_option_idx
                                == ForceReblockingOptions.force_ublock_r_reblocking.value,
                                input_node.ub_r != consumer_op.ub_r,
                            ),
                            Implies(
                                self.reblocking_option_idx
                                == ForceReblockingOptions.force_ublock_c_reblocking.value,
                                input_node.ub_c != consumer_op.ub_c,
                            ),
                            Implies(
                                self.reblocking_option_idx
                                == ForceReblockingOptions.force_scan_order_reblocking.value,
                                input_node.ublock_order != consumer_op.ublock_order,
                            ),
                            # If we have slice and stack before kernel broadcast their factors have to match and have
                            # to be greater than one.
                            Implies(
                                self.max_tms_per_conn == 3,
                                And(
                                    op_input_tms.get_tms()[0].tm_arg
                                    == op_input_tms.get_tms()[
                                        min(1, self.max_tms_per_conn - 1)
                                    ].tm_arg,
                                    op_input_tms.get_tms()[0].tm_arg > 1,
                                ),
                            ),
                        ),
                    )
                )

    def get_reblocking_option_idx(self) -> int:
        """Returns index of the reblocking option for the current sweep context."""
        if not self.sweep_context:
            return 0
        return self.sweep_context.get_sweep_var_value(self.reblocking_option_idx)

    def is_op_input_kernel_broadcasted(self, input_idx: int) -> bool:
        """Returns true if the input on a given index is operating in kernel broadcast mode in
        the current sweep context."""
        if not self.sweep_context:
            return True
        return self.sweep_context.get_sweep_var_value(self.kernel_bcast_idx) == input_idx

    # @override
    def postprocess(self, model_vars: Dict[str, any]) -> None:
        super().postprocess(model_vars)

        for consumer_op in self.get_consumer_ops_with_tms():
            for input_idx, input_name in enumerate(consumer_op.inputs):
                if self.is_op_input_kernel_broadcasted(input_idx):
                    producer_node = self.nodes[input_name]

                    # This is a hack arround buf_size_mb and stack factor relation. In op_input.py we define that
                    # buf_size_mb == 2 * total_stack_factor, however the precise constraint would be that buf_size_mb
                    # == 2 * total_effective_stack_factor, where total_effective_stack_factor depends on
                    # total_stack_factor and total_slice_factor. In the order to be able to use kernel_broadcast
                    # feature these factors have to match (they cancel each other out) so effective_stack_factor == 1.
                    # buf_size_mb can therefore be either 1 or 2 (depends if we want to add double buffering). However,
                    # since for queues buf_size_mb == t_dim, and t_dim == 1 is a constraint for kernel_broadcast, we
                    # set buf_size_mb == 1 for uniformity.
                    model_vars[producer_node.buf_size_mb.sexpr()] = 1

    # @override
    def export_sweep_vars(self) -> List[SweepVarsGroup]:
        sweep_vars = []
        for consumer_op in self.get_consumer_ops_with_tms():
            for input_idx, input_name in enumerate(consumer_op.inputs):
                op_input_tms = self.op_input_tm_map[consumer_op.name][input_name].get_tms()

                var_names_range_dict = {}
                tm_sweep_generator = self.tm_sweep_vars_generator()
                for tm_var, (tm_type_values, tm_arg_values) in zip(
                    op_input_tms, tm_sweep_generator
                ):
                    if tm_type_values:
                        var_names_range_dict[tm_var.tm_type.sexpr()] = tm_type_values
                    if tm_arg_values:
                        var_names_range_dict[tm_var.tm_arg.sexpr()] = tm_arg_values

                var_names_range_dict[self.kernel_bcast_idx.sexpr()] = [input_idx]

                if self.max_tms_per_conn <= 2:
                    var_names_range_dict[self.reblocking_option_idx.sexpr()] = [
                        fro.value for fro in ForceReblockingOptions
                    ]
                else:
                    var_names_range_dict[self.reblocking_option_idx.sexpr()] = [
                        ForceReblockingOptions.force_grid_size_x_reblocking.value,
                        ForceReblockingOptions.force_grid_size_y_reblocking.value,
                    ]

                sweep_vars.append(
                    SweepVarsGroup(
                        var_names_range_dict=var_names_range_dict,
                        max_num_configs_per_combination=self.num_configs_per_tms,
                    )
                )

        additional_sweep_vars = self.export_additional_sweep_vars()

        if additional_sweep_vars:
            sweep_vars.extend(additional_sweep_vars)

        return sweep_vars

    def tm_sweep_vars_generator(self) -> List[Tuple]:
        """Generate TM arg and type value ranges based on the max number of TMs per connection. The logic is:
        1) max_tms_per_conn == 1 -> generate kernel_broadcast only
        2) max_tms_per_conn == 2 -> transpose, kernel_broadcast
        3) max_tms_per_conn == 3 -> slice, stack, kernel_broadcast (slice and stack must have the same TM args)
        """
        tm_values = [TMS.t_broadcast.value, TMS.c_broadcast.value, TMS.r_broadcast.value]
        bcast_factors = list(range(self._bcast_min_value, self._bcast_max_value + 1))

        if self.max_tms_per_conn == 1:
            return [(tm_values, bcast_factors)]
        elif self.max_tms_per_conn == 2:
            return [([TMS.transpose.value], None), (tm_values, bcast_factors)]
        else:
            return [
                ([TMS.hslice.value, TMS.vslice.value], None),
                ([TMS.hstack.value, TMS.vstack.value], None),
                (tm_values, bcast_factors),
            ]


class MultiTmFusedOpTestBase(MultiTmTestBase, ABC):
    """
    Base class for all TM tests regarding fused ops.
    """

    # @override
    def constrain_fused_op_input_dims(self, op_node: Node) -> None:
        super().constrain_fused_op_input_dims(op_node)

        # Force constant ublock size accross schedules ops.
        for scheduled_op in op_node.scheduled_ops:
            self.solver.add(op_node.ub_r == scheduled_op.ub_r, op_node.ub_c == scheduled_op.ub_c)


class MultiTMFusedOpKernelBroadcastTestBase(MultiTMKernelBroadcastTestBase, ABC):
    def constrain_fused_op_input_dims(self, op_node: Node) -> None:
        """Forces constant ublock size accross all the scheduled ops in the given fused op.

        op_node:
            Fused op node.
        """
        super().constrain_fused_op_input_dims(op_node)

        # Force constant ublock size accross schedules ops.
        for scheduled_op in op_node.scheduled_ops:
            self.solver.add(op_node.ub_r == scheduled_op.ub_r, op_node.ub_c == scheduled_op.ub_c)
