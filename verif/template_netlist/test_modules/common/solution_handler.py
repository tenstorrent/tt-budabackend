# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import os
from abc import ABC, abstractmethod
from dataclasses import dataclass
import json
import yaml
from typing import Dict, List, Optional

from z3 import CheckSatResult, parse_smt2_string

from test_modules.common.solution_search import SolutionSearchConfig
from test_modules.common.sweep import SweepCombination, SweepContext
from test_modules.common.test_module_wrapper import TestModuleWrapper
from test_modules.common.worker_manager import WorkerManager, WorkerSolutionHandler
from test_modules.common.worker_manager_utils import SolverWorkerConfig
from util import PerfOpSweepConfig, get_git_root

# Path where to eager dump configs if desired.
__DEFAULT_CONFIG_DUMP_BASE_PATH = os.path.join(
    get_git_root(), "build", "test", "verif", "config_dumps"
)
CONFIG_DUMP_BASE_PATH = os.getenv("CONFIG_DUMP_BASE_PATH", __DEFAULT_CONFIG_DUMP_BASE_PATH)


@dataclass(frozen=True)
class PostprocessedSolverSolution:
    """Simple class representing return value from `postprocess_solver_solution`.

    Attributes
    ----------
    valid:
        Whether solution passed valid_config_callback.
    postprocessed_solution:
        Dict mapping var names to values (either int or str) - final form
        after extra_config_callback processing (has some added vars which
        solver itself does not deal with, like buffer addresses and channels,
        data formats turned from int to str, etc.).
    solver_solution:
        Dict mapping var names to values solver has found.
    """

    valid: bool
    postprocessed_solution: Dict[str, int | str]
    solver_solution: Dict[str, int]


class SolutionHandler(ABC):
    def __init__(
        self, module_wrapper: TestModuleWrapper, search_config: SolutionSearchConfig
    ) -> None:
        self._module_wrapper = module_wrapper
        self._max_num_configs = search_config.max_num_configs
        self.__generated_configs = []
        self._config_dir_path = None

    @property
    def num_generated_configs(self) -> int:
        return len(self.__generated_configs)

    def handle_sat(self, perf_config: Optional[PerfOpSweepConfig] = None) -> None:
        """Handles case when solver.check() returned z3.sat."""
        # If this set of constraints passed check, pass the solution to additional postprocessing.
        postprocessed = self.__postprocess_solver_solution(perf_config)

        if postprocessed.valid:
            # Add generated config to the rest of generated configs and force solver to try
            # and find next solution that differs from this one in at least one variable
            # that is not sweep variable.
            self._handle_valid_config(postprocessed)
        else:
            # Discard generated config and force solver to try and find next solution that
            # differs from this one in at least one variable.
            self._handle_invalid_config(postprocessed)

    def find_solution(self) -> CheckSatResult:
        return self._module_wrapper.solver.check()

    def start(self) -> None:
        """Does the necessary steps when starting searching for a solution"""
        pass

    @abstractmethod
    def finish(self) -> None:
        """Does the necessary steps when finishing the handling of a specific solution"""
        pass

    @abstractmethod
    def handle_unsat(self) -> None:
        """Handles case when solver.check() returned z3.unsat."""
        pass

    @abstractmethod
    def handle_unknown(self) -> None:
        """Handles case when solver.check() returned z3.unknown."""
        pass

    def begin_sweep(self, combination: SweepCombination) -> None:
        # Save solver state on stack so that we can manually explore values for sweep vars and
        # later restore the saved state.
        self._module_wrapper.solver.push()

        # Add new constraints in this solver scope by giving sweep vars some concrete values
        # thus forcing the solver to find solutions for other vars.
        for sweep_var_name, sweep_var_val in combination.combination.items():
            self._module_wrapper.solver.add(
                self._module_wrapper.solver_vars[sweep_var_name] == sweep_var_val
            )

        # Push sweep context.
        if self._module_wrapper.push_sweep_context:
            self._module_wrapper.push_sweep_context(SweepContext(combination))

    def end_sweep(self) -> None:
        # Pop sweep context.
        if self._module_wrapper.pop_sweep_context:
            self._module_wrapper.pop_sweep_context()

        # Restore previous solver state thus excluding forced values for sweep vars from the
        # constraints.
        self._module_wrapper.solver.pop()

    @abstractmethod
    def more_solutions_needed(self) -> bool:
        """Returns true if we have not found the requested number of solutions."""
        pass

    def all_solutions_found(self) -> bool:
        """Returns true if we have found the requested number of solutions."""
        return (
            self._max_num_configs is not None
            and self.num_generated_configs == self._max_num_configs
        )

    def get_generated_configs(self) -> List[Dict[str, int | str]]:
        return self.__generated_configs

    def get_last_generated_config(self) -> Dict[str, int | str]:
        return self.get_generated_configs()[-1]

    @abstractmethod
    def _handle_valid_config(self, postprocessed: PostprocessedSolverSolution) -> None:
        """
        Append generated config to the list of generated configs and prevent solver from generating
        same values for variables in this scope.
        """
        self.__add_config(postprocessed.postprocessed_solution)
        self.__eager_dump_config(postprocessed.postprocessed_solution)
        self.__prevent_solver_from_generating_this_solution_again(
            postprocessed.solver_solution, self._get_valid_config_vars_to_prevent()
        )

    @abstractmethod
    def _handle_invalid_config(self, postprocessed: PostprocessedSolverSolution) -> None:
        """
        Prevent solver from generating same values for variables in this scope.
        """
        self.__prevent_solver_from_generating_this_solution_again(
            postprocessed.solver_solution, self._get_invalid_config_vars_to_prevent()
        )

    def _get_valid_config_vars_to_prevent(self) -> List[str]:
        """
        Returns list of var names for which solver will be forced to find new values in next search
        after current values passed valid callback.
        """
        return self._module_wrapper.coverage_vars_list or self._module_wrapper.solver_vars.keys()

    def _get_invalid_config_vars_to_prevent(self) -> List[str]:
        """
        Returns list of var names for which solver will be forced to find new values in next search
        after current values failed valid callback.
        """
        return self._module_wrapper.solver_vars.keys()

    def __prevent_solver_from_generating_this_solution_again(
        self, solver_solution: Dict[str, int], vars_names: List[str]
    ) -> None:
        """
        Prevent solver from generating same values for vars_names in this scope.
        """
        self._module_wrapper.solver.add(
            parse_smt2_string(
                SolutionHandler.__different_solution_constraint_smt2_str(
                    vars_values=solver_solution,
                    vars_names=vars_names,
                ),
                decls=self._module_wrapper.solver_vars,
            )
        )

    def __postprocess_solver_solution(
        self, perf_config: Optional[PerfOpSweepConfig] = None
    ) -> PostprocessedSolverSolution:
        """
        Function which obtains one set of solutions from the solver, passes it for
        further processing to extra_config_callback and valid_config_callback and
        returns back the results.

        Parameters
        ----------
        perf_config:
            Configuration object for perf tests.

        Returns
        -------
        PostprocessedSolverSolution:
            .valid:
                Whether solution passed valid_config_callback.
            .postprocessed_solution:
                Dict mapping var names to values (either int or str) - final form
                after extra_config_callback processing (has some added vars which
                solver itself does not deal with, like buffer addresses and channels,
                data formats turned from int to str, etc.).
            .solver_solution:
                Dict mapping var names to values solver has found.
        """
        # Obtain solutions generated by solver.
        module_wrapper: TestModuleWrapper = self._module_wrapper

        model = module_wrapper.solver.model()
        solver_vars_values = {}
        for name, z3_var in module_wrapper.solver_vars.items():
            try:
                solver_vars_values[name] = model[z3_var].as_long()
            except:
                raise RuntimeError(f"Failed to extract variable from model: '{name}', '{z3_var}'.")

        # Final config consists of solver-generated solutions plus solutions obtained
        # from extra_config_callback postprocessing.
        config = dict(solver_vars_values)

        if perf_config and perf_config.sweep_en:
            extra_config_valid = True

            if module_wrapper.extra_config_callback:
                config, extra_config_valid = module_wrapper.extra_config_callback(
                    config, perf_config
                )

            if (
                module_wrapper.valid_config_callback
                and not module_wrapper.valid_config_callback(config)
            ) or not extra_config_valid:
                return PostprocessedSolverSolution(False, config, solver_vars_values)
        else:
            if module_wrapper.valid_config_callback and not module_wrapper.valid_config_callback(
                config, module_wrapper.logger
            ):
                return PostprocessedSolverSolution(False, config, solver_vars_values)

            if module_wrapper.extra_config_callback:
                config = module_wrapper.extra_config_callback(config)

        return PostprocessedSolverSolution(True, config, solver_vars_values)

    def __add_config(self, config: Dict[str, int | str]) -> None:
        self.__generated_configs.append(config)

    @staticmethod
    def __different_solution_constraint_smt2_str(
        vars_values: Dict[str, int], vars_names: List[str]
    ) -> str:
        """
        Create smt2 string which forces solver to find other solutions.

        Given the lists of names and values of z3 variables, this function creates
        smt2 constraint string in the format:

        (assert (or (distinct var1 val1) ... distinct (varN valN)))

        which is equal to

        Or(var1 != val1, var2 != val2, ..., varN != valN).

        This string when passed to 'parse_smt2_string' function will turn it into a
        set of constraints that when added to the solver through solver.add(...)
        force it to find other set of solutions for z3 vars in vars_names (even if
        just one of them is different).

        For the edge case of an empty list of names, the generated constraint is
        neutral (always satisfied).

        Parameters
        ----------
        vars_names:
            List of solver vars names.
        vars_values:
            Dict of values for vars in vars_names which should not repeat again as
            a solution (at least one of them must be different).

        Returns
        -------
        str:
            smt2 string as explained above.
        """
        if not vars_names:
            return "(assert true)"

        smt2_string = "(assert (or"

        for var_name in vars_names:
            smt2_string = f"{smt2_string} (distinct {var_name} {vars_values[var_name]})"

        smt2_string = f"{smt2_string}))"

        return smt2_string

    def __eager_dump_config(self, config: Dict) -> None:
        """Potentially saves the config dict to the dump config dir.

        Parameters
        ----------
        config:
            Config dict, i.e. processed solver solution.
        """
        if not self._config_dir_path:
            return

        config_file_path = os.path.join(
            self._config_dir_path, f"config_{self.num_generated_configs}.yaml"
        )

        with open(config_file_path, "w") as config_file:
            yaml.dump(config, config_file)

    @staticmethod
    def _make_config_dump_dir(search_config: SolutionSearchConfig) -> Optional[str]:
        """
        Creates a folder where the configs will be stored and returns path to the created dir.
        """
        if not search_config.eager_dump_configs:
            return None

        config_dir_path = os.path.join(
            CONFIG_DUMP_BASE_PATH, search_config.get_unique_dump_dir_name()
        )

        if not os.path.exists(config_dir_path):
            os.makedirs(config_dir_path)

        return config_dir_path


class FreeSearchSolutionHandler(SolutionHandler):
    def __init__(
        self, module_wrapper: TestModuleWrapper, search_config: SolutionSearchConfig
    ) -> None:
        super().__init__(module_wrapper, search_config)
        self._config_dir_path = self._make_config_dump_dir(search_config)

    # @override
    def finish(self) -> None:
        pass

    # @override
    def handle_unsat(self) -> None:
        self._module_wrapper.logger.warning(
            f"Solver can't find more solutions in this search space."
        )

    # @override
    def handle_unknown(self) -> None:
        # Not handled in free search since not timeout mechanism is used.
        pass

    # @override
    def more_solutions_needed(self) -> bool:
        return self._max_num_configs is None or self.num_generated_configs < self._max_num_configs

    # @override
    def _handle_valid_config(self, postprocessed: PostprocessedSolverSolution) -> None:
        super()._handle_valid_config(postprocessed)

        self._module_wrapper.logger.info(
            f"Generated config num {self.num_generated_configs} in free search."
        )

    # @override
    def _handle_invalid_config(self, postprocessed: PostprocessedSolverSolution) -> None:
        super()._handle_invalid_config(postprocessed)

        self._module_wrapper.logger.warning(
            f"Combination failed valid_config_callback in free search."
        )


class SerialSweepSolutionHandler(SolutionHandler):
    """
    This handler has additional concepts of group and combination, due to the way serial sweep is
    used.

    __solution_logs: Dict[str, SweepCombination]
        SweepCombination objects will hold info about start times and end times
    """

    __solution_logs: Dict[str, SweepCombination]

    def __init__(
        self, module_wrapper: TestModuleWrapper, search_config: SolutionSearchConfig
    ) -> None:
        super().__init__(module_wrapper, search_config)
        self._config_dir_path = self._make_config_dump_dir(search_config)
        self._initial_random_seed = search_config.random_seed
        self.__max_num_retries_on_timeout = search_config.num_retries_on_timeout
        self.__sweep_vars_combination = None
        self.__max_num_configs_per_combination = None
        self.__solution_logs = {}

    @property
    def max_num_configs_per_combination(self) -> int:
        return self.__max_num_configs_per_combination

    # @override
    def handle_unsat(self) -> None:
        self._module_wrapper.logger.warning(
            f"Couldn't find solution for combination {self.__sweep_vars_combination}."
        )

    # @override
    def handle_unknown(self) -> None:
        # Setup new solver seed and try again.
        TestModuleWrapper.set_solver_seed(self._module_wrapper.solver)
        self.__num_retries_on_timeout += 1

        # Log failure.
        self._module_wrapper.logger.warning(
            f"Solver failed with timeout for combination {self.__sweep_vars_combination}, "
            f"retry number {self.__num_retries_on_timeout}."
        )

    # @override
    def more_solutions_needed(self) -> bool:
        return (
            self._max_num_configs is None or self.num_generated_configs < self._max_num_configs
        ) and (
            self.__max_num_configs_per_combination is None
            or self.__num_configs_per_combination < self.__max_num_configs_per_combination
        )

    def all_solutions_found_for_combination(self) -> bool:
        return (
            self.__max_num_configs_per_combination is not None
            and self.__num_configs_per_combination == self.__max_num_configs_per_combination
        )

    def too_many_retries(self) -> bool:
        return self.__num_retries_on_timeout >= self.__max_num_retries_on_timeout

    def setup_for_new_solver_run(self, combination: SweepCombination) -> None:
        """Neccessary setup for logging before each run."""
        if combination.name not in self.__solution_logs.keys():
            self.__solution_logs[combination.name] = combination

        self.__solution_logs[combination.name].record_start_time()

    def setup_for_new_sweep_combination(self, combination: SweepCombination) -> None:
        self._module_wrapper.logger.info(f"Trying combination: {combination.name}")
        self.__sweep_vars_combination = combination
        self.__max_num_configs_per_combination = combination.max_configs_per_combination
        self.__num_configs_per_combination = 0
        self.__num_retries_on_timeout = 0

    # @override
    def finish(self) -> None:
        """Performs logging at the end."""
        solution_times_dict = {
            k: [self.__solution_logs[k].durations, self.__solution_logs[k].end_times]
            for k in self.__solution_logs.keys()
        }
        solution_times_str = json.dumps(solution_times_dict, indent=4)
        self._module_wrapper.progress.info(solution_times_str)

    # @override
    def _handle_valid_config(self, postprocessed: PostprocessedSolverSolution) -> None:
        super()._handle_valid_config(postprocessed)

        combination_name = self.__sweep_vars_combination.name
        self.__solution_logs[combination_name].record_end_time()
        duration = self.__solution_logs[combination_name].durations[-1]
        end_time = self.__solution_logs[combination_name].end_times[-1]

        self._module_wrapper.progress.info(
            f"Progress: [combination {combination_name}]"
            f"{self.num_generated_configs}/{self._max_num_configs} = "
            f"{100 * self.num_generated_configs/self._max_num_configs:.3f}% \n"
            f"|Duration: {duration}"
            f"|End time: {end_time}"
            f"\n{'-'*40}"
        )

        self._module_wrapper.logger.info(
            f"Found solution for combination {self.__sweep_vars_combination}. "
            f"Generated config num {self.num_generated_configs}."
        )
        # Keep count of generated configs for current sweep combination.
        self.__num_configs_per_combination += 1

    # @override
    def _handle_invalid_config(self, postprocessed: PostprocessedSolverSolution) -> None:
        super()._handle_invalid_config(postprocessed)

        self._module_wrapper.logger.warning(
            f"Combination {self.__sweep_vars_combination} failed valid_config_callback."
        )

    # @override
    def _get_valid_config_vars_to_prevent(self) -> List[str]:
        # If test module provides `coverage_var_names` we make sure those vars are covered.
        # Otherwise, we force solver to find different solutions for all vars except sweep vars.
        all_solver_vars_except_sweep_vars = list(
            set(self._module_wrapper.solver_vars.keys())
            - set(self.__sweep_vars_combination.combination.keys())
        )
        return self._module_wrapper.coverage_vars_list or all_solver_vars_except_sweep_vars

    # @override
    def _get_invalid_config_vars_to_prevent(self) -> List[str]:
        # We force solver to find different solutions for all vars except sweep vars.
        all_solver_vars_except_sweep_vars = list(
            set(self._module_wrapper.solver_vars.keys())
            - set(self.__sweep_vars_combination.combination.keys())
        )
        return all_solver_vars_except_sweep_vars


class ParallelSweepSolutionHandler(SolutionHandler):
    """
    This handler has additional concept of combination, worker config and multiprocessing objects,
    due to the way parallel sweep is used. The work on shared memory is done through a
    WorkerSolutionHandler objects, which controls accesses to shared memory.
    """

    def __init__(
        self,
        module_wrapper: TestModuleWrapper,
        worker_config: SolverWorkerConfig,
        worker_solution_handler: WorkerSolutionHandler,
    ) -> None:
        super().__init__(module_wrapper, worker_config.search_config)
        self._config_dir_path = worker_config.config_dir_path
        self._initial_random_seed = worker_config.search_config.random_seed
        self.__max_num_retries_on_timeout = worker_config.search_config.num_retries_on_timeout
        self.__sweep_vars_combination = None
        self.__worker_config = worker_config
        self.__worker_solution_handler = worker_solution_handler

    # @override
    def find_solution(self) -> CheckSatResult:
        self.__worker_solution_handler.release_semaphore(os.getpid())
        result = super().find_solution()
        self.__worker_solution_handler.acquire_semaphore(os.getpid())
        return result

    # @override
    def handle_unsat(self) -> None:
        self.__worker_solution_handler.process_unsat(
            self.__worker_config.worker_config_id, str(self.__sweep_vars_combination)
        )

    # @override
    def handle_unknown(self) -> None:
        # Setup new solver seed and try again.
        TestModuleWrapper.set_solver_seed(self._module_wrapper.solver)
        self.__num_retries_on_timeout += 1

        # Log failure.
        self.__worker_solution_handler.log_warning(
            f"Solver failed with timeout for combination {self.__sweep_vars_combination}, "
            f"retry number {self.__num_retries_on_timeout}"
        )

    # @override
    def more_solutions_needed(self) -> bool:
        return self.__worker_solution_handler.more_solutions_needed(self.__sweep_vars_combination)

    def too_many_retries(self) -> bool:
        return self.__num_retries_on_timeout >= self.__max_num_retries_on_timeout

    def setup_for_new_sweep_combination(self, combination: SweepCombination) -> None:
        # No sweep group is passed to parallel worker config, thus we get info about max num configs
        # per combination directly from worker config, along with the combination.

        self.__worker_solution_handler.log_info(f"Trying combination: {combination.name}.")

        self.__sweep_vars_combination = combination
        self.__num_retries_on_timeout = 0

    def setup_for_new_solver_run(self, combination: SweepCombination) -> None:
        """Neccessary setup for worker manager before each run."""
        self.__worker_solution_handler.record_new_run(combination.name)

    # @override
    def _handle_valid_config(self, postprocessed: PostprocessedSolverSolution) -> None:
        super()._handle_valid_config(postprocessed)
        self.__worker_solution_handler.process_sat(
            self.get_last_generated_config(), str(self.__sweep_vars_combination)
        )

    # @override
    def _handle_invalid_config(self, postprocessed: PostprocessedSolverSolution) -> None:
        super()._handle_invalid_config(postprocessed)
        self.__worker_solution_handler.log_warning(
            f"Combination {self.__sweep_vars_combination} failed valid_config_callback."
        )

    # @override
    def _get_valid_config_vars_to_prevent(self) -> List[str]:
        # If test module provides `coverage_var_names` we make sure those vars are covered.
        # Otherwise, we force solver to find different solutions for all vars except sweep vars.
        all_solver_vars_except_sweep_vars = list(
            set(self._module_wrapper.solver_vars.keys())
            - set(self.__sweep_vars_combination.combination.keys())
        )
        return self._module_wrapper.coverage_vars_list or all_solver_vars_except_sweep_vars

    # @override
    def _get_invalid_config_vars_to_prevent(self) -> List[str]:
        # We force solver to find different solutions for all vars except sweep vars.
        all_solver_vars_except_sweep_vars = list(
            set(self._module_wrapper.solver_vars.keys())
            - set(self.__sweep_vars_combination.combination.keys())
        )
        return all_solver_vars_except_sweep_vars

    # @override
    def start(self) -> None:
        self.__worker_solution_handler.acquire_semaphore(os.getpid())

    # @override
    def finish(self) -> None:
        # We remove the combination from ToDo dictionary at the very end, since the process can be
        # killed at any point after this.
        self.__worker_solution_handler.remove_from_todo(self.__worker_config.combination.name)
        self.__worker_solution_handler.release_semaphore(os.getpid())

    # @override
    @property
    def num_generated_configs(self) -> int:
        return self.__worker_solution_handler.num_generated_tests()

    @staticmethod
    def make_config_dump_dir(search_config: SolutionSearchConfig) -> Optional[str]:
        return SolutionHandler._make_config_dump_dir(search_config)
