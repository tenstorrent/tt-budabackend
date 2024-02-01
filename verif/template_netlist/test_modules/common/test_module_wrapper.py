# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import importlib
import random
from dataclasses import dataclass
from logging import Logger
from typing import Callable, Dict, List, Optional

from z3 import Int, Solver, Then, CheckSatResult, sat, unsat, unknown

from test_modules.common.custom_logger import make_custom_logger, make_default_logger
from test_modules.common.solution_search import SolutionSearchConfig
from test_modules.common.sweep import SweepCombination, SweepContext, SweepVarsGroup
from util import PerfOpSweepConfig, TestType


@dataclass(frozen=True)
class TestModuleWrapper:
    """Wrapper around public functions used to access the module and an object
    in which instance of Solver is kept and accessed through.

    Attributes
    ----------
    solver:
        Instance of z3 Solver.
    solver_vars:
        Dictionary to store all created Z3 variables with their name: value pairs.
    logger:
        Instance of logging.Logger.
    sweep_vars_list:
        For solution search which uses sweep, this list contains all variables
        test modules defines as sweep vars.
    coverage_vars_list:
        List of variables for which solver will force different values between the solutions
        within the same sweep combination. If not provided in the module, list of names of all
        solver_vars will be used instead.
    extra_config_callback:
        Holding pointer to extra_config_callback function of test module.
    valid_config_callback:
        Holding pointer to valid_config_callback function of test module.
    push_sweep_context:
        Holding pointer to push_sweep_context function of test module.
    pop_sweep_context:
        Holding pointer to pop_sweep_context function of test module.
    """

    solver: Solver
    solver_vars: Dict
    logger: Logger
    progress: Optional[Logger] = None
    sweep_vars_list: Optional[List[SweepVarsGroup]] = None
    coverage_vars_list: Optional[List[str]] = None
    extra_config_callback: Optional[Callable] = None
    valid_config_callback: Optional[Callable] = None
    push_sweep_context: Optional[Callable] = None
    pop_sweep_context: Optional[Callable] = None
    # TODO: move these to SolverWrapper class, once it is implemented
    RANDOM_SEED_MIN = 0
    RANDOM_SEED_MAX = 100000

    @staticmethod
    def create_from_search_config(
        search_config: SolutionSearchConfig,
    ) -> TestModuleWrapper:
        """Based on SolutionSearchConfig settings, return TestModuleWrapper instance."""
        # Import module and public functions.
        test_module = importlib.import_module(search_config.test_module_name)

        constraint_model: Callable = getattr(test_module, "constraint_model", None)
        extra_config_callback: Callable = getattr(test_module, "extra_config_callback", None)
        valid_config_callback: Callable = getattr(test_module, "valid_config_callback", None)
        push_sweep_context: Callable = getattr(test_module, "push_sweep_context", None)
        pop_sweep_context: Callable = getattr(test_module, "pop_sweep_context", None)

        # Create solver instance.
        if search_config.enable_strategy:
            solver = Then(
                "simplify", "propagate-values", "elim-term-ite", "solve-eqs", "smt"
            ).solver()
        else:
            solver = Solver()

        TestModuleWrapper.set_solver_seed(solver, search_config)
        TestModuleWrapper.set_solver_timeout(solver, search_config)

        solver_vars = {x: Int(x) for x in getattr(test_module, "solver_var_names", [])}
        constraint_model(solver, solver_vars, search_config.arch, search_config.harvested_rows)

        # Sweep vars list is filled only after constraint_model is run.
        sweep_vars_list: List[SweepVarsGroup] = getattr(test_module, "sweep_var_names", None)
        coverage_vars_list: List[str] = getattr(test_module, "coverage_var_names", None)

        # Create logger to output info during generation if verbose or log_to_file enabled.
        logger = make_custom_logger(
            name=search_config.get_unique_logger_name(),
            verbose=search_config.verbose,
            log_to_file=search_config.log_to_file,
        )
        # Create a file logger which outputs to progress.log if log_to_file is enabled.
        progress = make_custom_logger(
            name=search_config.get_progress_logger_name(),
            verbose=False,
            log_to_file=search_config.log_to_file,
        )

        return TestModuleWrapper(
            solver,
            solver_vars,
            logger,
            progress,
            sweep_vars_list,
            coverage_vars_list,
            extra_config_callback,
            valid_config_callback,
            push_sweep_context,
            pop_sweep_context,
        )

    @staticmethod
    def create_from_free_search_config(
        search_config: SolutionSearchConfig, perf_config: PerfOpSweepConfig
    ) -> TestModuleWrapper:
        """
        Based on SolutionSearchConfig and PerfOpSweepConfig settings,
        return TestModuleWrapper instance.
        """
        # Import module and public functions.
        test_module = importlib.import_module(search_config.test_module_name)
        extra_config_callback = getattr(test_module, "extra_config_callback", None)
        valid_config_callback = getattr(test_module, "valid_config_callback", None)

        # Create solver instance.
        if search_config.enable_strategy:
            solver = Then(
                "simplify", "propagate-values", "elim-term-ite", "solve-eqs", "smt"
            ).solver()
        else:
            solver = Solver()

        # Initialize solver options.
        TestModuleWrapper.set_solver_seed(solver, search_config)
        TestModuleWrapper.set_solver_timeout(solver, search_config)

        if perf_config and perf_config.sweep_en:
            solver_var_names = test_module.get_solver_var_names(perf_config)
            solver_vars = {n: Int(n) for n in solver_var_names}
        else:
            solver_vars = {x: Int(x) for x in getattr(test_module, "solver_var_names", [])}

        if perf_config and perf_config.sweep_en:
            test_module.perf_sweep_constraint_model(
                solver=solver,
                svars=solver_vars,
                perf_config=perf_config,
                arch=search_config.arch,
            )
        elif getattr(test_module, "TEST_TYPE", TestType.TmTest) == TestType.GraphTest:
            test_module.constraint_model(
                solver, solver_vars, search_config.arch, search_config.harvested_rows
            )
        else:
            test_module.constraint_model(solver, solver_vars)

        logger = make_default_logger(
            name=search_config.get_unique_logger_name(), verbose=search_config.verbose
        )

        return TestModuleWrapper(
            solver=solver,
            solver_vars=solver_vars,
            logger=logger,
            extra_config_callback=extra_config_callback,
            valid_config_callback=valid_config_callback,
        )

    # TODO: move to solver wrapper
    @staticmethod
    def set_solver_seed(solver: Solver, search_config: SolutionSearchConfig = None):
        """If solver seed is passed, it will set it. Otherwise, random seed is chosen."""
        solver.set("smt.arith.random_initial_value", True)
        if search_config and search_config.random_seed:
            solver.set("random_seed", search_config.random_seed)
        else:
            rand_seed = random.randint(
                TestModuleWrapper.RANDOM_SEED_MIN, TestModuleWrapper.RANDOM_SEED_MAX
            )
            solver.set("random_seed", rand_seed)

    # TODO: move to solver wrapper
    @staticmethod
    def set_solver_timeout(solver: Solver, search_config: SolutionSearchConfig):
        if search_config.timeout > 0:
            solver.set("timeout", search_config.timeout * 1000)

    # TODO: move to solver wrapper
    @staticmethod
    def is_sat(result: CheckSatResult):
        return result == sat

    # TODO: move to solver wrapper
    @staticmethod
    def is_unsat(result: CheckSatResult):
        return result == unsat

    # TODO: move to solver wrapper
    @staticmethod
    def is_unknown(result: CheckSatResult):
        return result == unknown
