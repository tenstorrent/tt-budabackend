# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from typing import Dict, List, Optional

from test_modules.common.constants import ARCH_VAR_NAME
from test_modules.common.solution_handler import (
    FreeSearchSolutionHandler,
    ParallelSweepSolutionHandler,
    SerialSweepSolutionHandler,
)
from test_modules.common.solution_search import (
    DefaultSearchConfig,
    ParallelSweepSearchConfig,
    SerialSweepSearchConfig,
    SolutionSearchConfig,
)
from test_modules.common.test_module_wrapper import TestModuleWrapper
from test_modules.common.worker_manager import WorkerManager, WorkerSolutionHandler
from test_modules.common.worker_manager_utils import SolverWorkerConfig
from util import PerfOpSweepConfig


def parallel_sweep_worker(
    worker_config: SolverWorkerConfig, worker_solution_handler: WorkerSolutionHandler
):
    """Starts a processs which handles single sweep combination config generation. For more details,
    see sweep_vars function.
    Performs solving untill wanted number of configs for given combination have been generated,
    saves those solutions.

    Parameters
    ----------
    worker_config:
        Configuration of the parallel sweep worker process.

    worker_manager:
        Starts and orchestrates the processes which control the threads that solve combinations.
    """
    module_wrapper: TestModuleWrapper = TestModuleWrapper.create_from_search_config(
        worker_config.search_config
    )
    solution_handler: ParallelSweepSolutionHandler = ParallelSweepSolutionHandler(
        module_wrapper, worker_config, worker_solution_handler
    )
    solution_handler.start()

    # Setup counters to keep track of solutions generated for this sweep combination.
    solution_handler.setup_for_new_sweep_combination(worker_config.combination)

    # Save current solver state on stack and add new constraints for sweep combination.
    solution_handler.begin_sweep(worker_config.combination)

    # Force solver to try and find new solutions with these newly added constraints for sweep vars.
    while solution_handler.more_solutions_needed():
        # Begin recording start time for this solver run.
        solution_handler.setup_for_new_solver_run(worker_config.combination)

        # Try finding a solution.
        result = solution_handler.find_solution()

        # Solver couldn't find solution. Discard this combination.
        if TestModuleWrapper.is_unsat(result):
            solution_handler.handle_unsat()
            break

        # Search timed out.
        if TestModuleWrapper.is_unknown(result):
            if solution_handler.too_many_retries():
                solution_handler.handle_unsat()
                break

            solution_handler.handle_unknown()
            continue

        # Solver managed to find solution which satisfies constraints.
        solution_handler.handle_sat()

    # Return to the saved solver state now that sweeping this combination is done.
    solution_handler.end_sweep()
    solution_handler.finish()


def parallel_sweep(
    search_configs: SolutionSearchConfig | List[SolutionSearchConfig],
) -> None:
    """Parallel implementation of sweep mechanism. For every possible sweep combination,
    at least one process is created. It is important to note that this function
    DOES NOT GUARANTEE that we reach the exact number of solutions specified by max_configs
    parameter. This number actually represents a lower bound on the number of solutions
    (assuming that we can actually generated that number of solutions just by sweeping and not
    by letting the solver find solutions on it's own).

    Parameters
    ----------
    search_configs:
        List of SolutionSearchConfig objects containing all info about desired
        search type and efects. One SolutionSearchConfig is tied to one test
        module/arch/seed combination. Only in multi module test generator will
        we have multiple SolutionSearchConfigs.
    """
    # Wrap search_configs arg in list if it is not already, in order for for loop to work in
    # general case.
    search_configs = search_configs if isinstance(search_configs, list) else [search_configs]
    config_dir_paths = [
        ParallelSweepSolutionHandler.make_config_dump_dir(search_config)
        for search_config in search_configs
    ]
    # Initializing WorkerManager class that internally runs parallel_sweep_workers and
    # orchestrates them, while capturing found solutions, solution durations and end times,
    # as well as statistics such as number of generated tests and number of failed tests.
    worker_manager = WorkerManager(search_configs, config_dir_paths)
    worker_manager.run(parallel_sweep_worker)
    return worker_manager.generated_configs


def serial_sweep(search_config: SolutionSearchConfig) -> List[Dict[str, int | str]]:
    """
    Function which performs var sweeping - giving vars some concrete values from desired range and
    letting solver find solution for other vars.

    This function is implemented in order to force the solver to find valid solutions for various
    desired combinations of sweep vars determined by the creator of concrete GraphTestBase subclass
    (i.e. test module). By giving sweep vars some concrete values, search space for solver is
    narrowed and solver has less variables to deal with, and at the same time, it prevents it
    from getting stuck in local optimums.

    Parameters
    ----------
    search_config:
        SolutionSearchConfig object containing all info about desired search type and efects.
    """
    module_wrapper: TestModuleWrapper = TestModuleWrapper.create_from_search_config(search_config)
    solution_handler: SerialSweepSolutionHandler = SerialSweepSolutionHandler(
        module_wrapper, search_config
    )

    module_wrapper.logger.info("Serial sweep.")

    for sweep_vars_group in module_wrapper.sweep_vars_list:
        for combination in sweep_vars_group.get_next_combination():
            # Setup counters to keep track of solutions generated for this sweep combination.
            solution_handler.setup_for_new_sweep_combination(combination)

            # Save current solver state on stack and add new constraints for sweep combination.
            solution_handler.begin_sweep(combination)

            # Force solver to try and find new solutions with these newly added constraints for
            # sweep vars.
            while solution_handler.more_solutions_needed():
                # Begin recording start time for this solver run
                solution_handler.setup_for_new_solver_run(combination)
                # Try finding a solution.
                result = module_wrapper.solver.check()

                # Solver couldn't find solution. Discard this combination.
                if TestModuleWrapper.is_unsat(result):
                    solution_handler.handle_unsat()
                    break

                # Search timed out.
                if TestModuleWrapper.is_unknown(result):
                    if solution_handler.too_many_retries():
                        solution_handler.handle_unsat()
                        break

                    solution_handler.handle_unknown()
                    continue

                # Solver managed to find solution which satisfies constraints.
                solution_handler.handle_sat()

            # Return to the saved solver state now that sweeping this combination is done.
            solution_handler.end_sweep()

            # Done with this sweep var combination. Examine why the while loop has been exited.
            if solution_handler.all_solutions_found():
                module_wrapper.logger.info(
                    f"Requested number of configs ({solution_handler.num_generated_configs}) "
                    f"generated. Ending search."
                )
                module_wrapper.progress.info(
                    f"Done. Generated {solution_handler.num_generated_configs} configs."
                )
                return solution_handler.get_generated_configs()

            elif solution_handler.all_solutions_found_for_combination():
                module_wrapper.logger.info(
                    f"Maximum number of configs "
                    f"({solution_handler.max_num_configs_per_combination}) generated for this sweep"
                    f" var combination. Trying next combination."
                )
                continue

            else:
                module_wrapper.logger.info(
                    f"Solver can't find any more solutions for this sweep combination. "
                    f"Trying next combination."
                )
                continue

    solution_handler.finish()
    return solution_handler.get_generated_configs()


def free_search(
    search_config: SolutionSearchConfig,
    perf_config: Optional[PerfOpSweepConfig] = None,
) -> List[Dict[str, int | str]]:
    """
    Search for configs in such a way that we let solver generate whatever it finds z3.sat, and if it
    passes validation callback, we accept it as a solution. Note that this isn't really the best way
    to find solutions, since solver will generate whatever is the easiest to find, probably
    solutions in epsilon-neighborhood of previous solution but it will keep generating until
    max_num_configs is reached or no more z3.sat solutions can be found.

    Parameters
    ----------
    search_config:
        SolutionSearchConfig object containing all info about desired search type and efects.
    perf_config:
        Configuration object for perf tests, used only if perf_config.sweep_en == True.
    """
    module_wrapper: TestModuleWrapper = TestModuleWrapper.create_from_free_search_config(
        search_config, perf_config
    )
    solution_handler: FreeSearchSolutionHandler = FreeSearchSolutionHandler(
        module_wrapper, search_config
    )

    module_wrapper.logger.info("Free search.")

    # Let solver search for configs under condition that they differ in at least one variable.
    while solution_handler.more_solutions_needed():
        # Try finding a solution.
        result = module_wrapper.solver.check()

        if TestModuleWrapper.is_unsat(result):
            solution_handler.handle_unsat()
            break

        # Solver managed to find solution which satisfies constraints.
        solution_handler.handle_sat(perf_config)

    # Examine why the while loop has been exited.
    if solution_handler.all_solutions_found():
        module_wrapper.logger.info(
            f"Requested number of configs ({solution_handler.num_generated_configs}) generated. "
            f"Ending search."
        )
    else:
        module_wrapper.logger.info(
            f"Solver can not find any more solutions in free search. "
            f"Total num of generated solutions: {solution_handler.num_generated_configs}. "
            f"Ending search."
        )

    return solution_handler.get_generated_configs()


# TODO Remove multiple search configs completely.
def generate_z3_configs(
    search_config: SolutionSearchConfig | List[SolutionSearchConfig],
    perf_config: Optional[PerfOpSweepConfig] = None,
) -> List[Dict]:
    """
    Main function which decides what type of search to perform based of search_config.
    Calls appropriate functions and returns list of generated configs.
    """
    # Sweep through sweep values in a serial manner, one by one.
    if isinstance(search_config, SerialSweepSearchConfig):
        generated_configs = serial_sweep(search_config)

    # Sweep through sweep values in a parallel manner, by distributing search on multiple cores.
    if isinstance(search_config, (ParallelSweepSearchConfig, list)):
        generated_configs = parallel_sweep(search_config)

    # If free search is desired and there are more solutions to be found, then free_search.
    if isinstance(search_config, DefaultSearchConfig):
        generated_configs = free_search(search_config, perf_config)

    # Add info about arch and enumerate configs.
    for config_id, config in enumerate(generated_configs):
        if ARCH_VAR_NAME not in config.keys():
            config[ARCH_VAR_NAME] = search_config.arch
        config["test_config_id"] = config_id

    return generated_configs
