# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import json
import multiprocessing as mp
from multiprocessing.managers import BaseManager, DictProxy, ListProxy, SyncManager
import os
import random
import sys
import threading
import time
from typing import Callable, Dict, List, Optional

from test_modules.common.custom_logger import get_logger
from test_modules.common.solution_search import SolutionSearchConfig
from test_modules.common.sweep import SweepCombination
from test_modules.common.test_module_wrapper import TestModuleWrapper
from test_modules.common.worker_manager_utils import (
    CustomSharedObjectsManager,
    SolutionDict,
    SolutionDictProxy,
    SolverWorkerConfig,
    TodoDict,
    TodoDictProxy,
)


class WorkerSolutionHandler:
    """
    Class which stored all the shared objects needed for the different worker threads, as well
    as locking access to the critical parts to prevent racing conditions.
     Attributes
    ----------
    __shared_objects_manager: mp.Manager
        Used to create and mp.Manager.dict which is shared across processes.

    __mp_num_failed_tests: mp.Array
        counter for number of failed tests.

    __mp_lock: mp.Lock
        Lock object used to synchronize access and writing to mp structures.

    __mp_solutions: SolutionDictProxy
        Proxy for SolutionDict used to map combinations to their SweepCombination objects.
        Keeps track of solution start time, end time, duration and generated configs for
        each combination. Should NOT be used inside mp_lock context, as it already has internal
        lock for access and writing, and it can lead to deadlock.

    __mp_todo: TodoDictProxy
        Dictionary used to keep track of unsolved combinations.
        Should NOT be used inside mp_lock context, as it already has internal lock for access and
        writing, and it can lead to deadlock.

    __worker_configs: List[SolverWorkerConfig]
        Configs that give neccessary info to every worker.

    __search_configs: List[SolutionSearchConfig]
        Give info about different search configurations that need to be performed.

    __max_num_configs: int
        Maximum number of configs needed to generate. It is minimum of number of all combinations
        and max number of configs as indicated by command line argument.
    """

    __shared_objects_manager: BaseManager
    __mp_num_failed_tests: ListProxy
    __mp_lock: mp.Lock
    __mp_solutions: SolutionDictProxy
    __mp_todo: TodoDictProxy
    __worker_configs: List[SolverWorkerConfig]
    __search_configs: List[SolutionSearchConfig]
    __semaphore_process_dictionary: DictProxy[int, mp.BoundedSemaphore]
    __max_num_configs: int

    def __init__(self, search_configs: List[SolutionSearchConfig], config_dir_paths: List[str]):
        CustomSharedObjectsManager.register("TodoDict", TodoDict, TodoDictProxy)
        CustomSharedObjectsManager.register("SolutionDict", SolutionDict, SolutionDictProxy)
        self.__mp_lock = mp.Lock()
        self.__search_configs = search_configs
        self.__config_dir_paths = config_dir_paths

        self.__worker_configs = []

        for idx, search_config in enumerate(search_configs):
            self.__worker_configs.extend(
                self.__create_worker_configs_from_search_config(search_config, idx)
            )
        self.__max_num_configs = self.__search_configs[-1].max_num_configs

    def __create_worker_configs_from_search_config(
        self, search_config: SolutionSearchConfig, worker_config_id: int
    ) -> List[SolverWorkerConfig]:
        # Dummy object needed to extract sweep_vars_list from it, since list is filled during
        # constraint_model call. Each process from the pool creates its own TestModuleWrapper with
        # brand new solver and callables, due to the problems of serializing modules in Python.
        # Thus, this module wrapper won't be used anywhere but here.
        dummy_module_wrapper = TestModuleWrapper.create_from_search_config(search_config)

        comb = [
            SolverWorkerConfig(
                worker_config_id=worker_config_id,
                search_config=search_config,
                combination=combination,
                num_configs=sweep_vars_group.get_num_configs_per_combination(),
                config_dir_path=self.__config_dir_paths[worker_config_id],
            )
            for sweep_vars_group in dummy_module_wrapper.sweep_vars_list
            for combination in sweep_vars_group.get_next_combination()
        ]

        assert len(comb), "Expecting at least one sweep combination"

        # Shuffle 'comb' in order to randomize parallel workers' access to worker configs.
        random.shuffle(comb)

        # If user requested, clamp 'comb'. This will result in less or equal than `max_num_configs`
        # solutions generated.
        if search_config.clamp_to_max_num_configs:
            if len(comb) > search_config.max_num_configs:
                comb = comb[0 : search_config.max_num_configs]

        # Make sure 'max_num_configs' is properly set if not set by user or if larger than number of
        # possible combinations.
        for worker_config in comb:
            if (
                worker_config.search_config.max_num_configs == None
                or worker_config.search_config.max_num_configs
                > len(comb) * worker_config.num_configs
            ):
                worker_config.search_config.max_num_configs = len(comb) * worker_config.num_configs

        return comb

    def init_run(self) -> None:
        """Necessary initializations before the run."""
        self.__shared_objects_manager = CustomSharedObjectsManager()
        self.__shared_objects_manager.__enter__()
        self.__mp_solutions = self.__shared_objects_manager.SolutionDict()
        self.__mp_todo = self.__shared_objects_manager.TodoDict()
        self.__mp_num_failed_tests = self.__shared_objects_manager.list(
            [0] * len(self.__search_configs)
        )
        self.__semaphore_process_dictionary = self.__shared_objects_manager.dict()

        for worker_conf in self.__worker_configs:
            self.__mp_solutions[worker_conf.combination.name] = worker_conf.combination
            self.__mp_todo[worker_conf.combination.name] = worker_conf

    def __create_semaphore_if_not_exists(self, pid: int) -> None:
        # Because lookup in the dictionary and its updating is not one atomic operation, it is
        # nececessary to lock these two operations with an atomic lock
        with self.__mp_lock:
            if pid not in self.__semaphore_process_dictionary:
                self.__semaphore_process_dictionary[
                    pid
                ] = self.__shared_objects_manager.BoundedSemaphore(value=1)

    def release_semaphore(self, pid: int) -> None:
        self.__semaphore_process_dictionary[pid].release()

    def acquire_semaphore(self, pid: int) -> None:
        self.__create_semaphore_if_not_exists(pid)
        self.__semaphore_process_dictionary[pid].acquire()

    def remove_from_todo(self, combination_name: str) -> None:
        self.__mp_todo.pop(combination_name, None)

    def process_unsat(self, worker_id: int, combination_name: str) -> None:
        """
        Increments the failed test counter, indicates that given
        SweepCombination is unsatisfiable and logs unsat combination.
        """
        self.__mp_num_failed_tests[worker_id] = self.__mp_num_failed_tests[worker_id] + 1
        self.__mp_solutions.record_unsat(combination_name)

        num_failed_tests = self.num_failed_tests(worker_id)

        self.log_warning(f"Couldn't find solution for combination {combination_name}.")
        self.log_progress(
            f"Progress: [combination {combination_name}] failed test {num_failed_tests}"
            f"\n{'-'*40}"
        )

        self.remove_from_todo(combination_name)

    def process_sat(self, config: Dict[str, int | str], combination_name: str) -> None:
        """
        Processes satisfiable combination. Saves found config, records end time,
        logs the found solution and checks if all configs have been found for given combination.
        """
        if self.__mp_solutions.already_found(combination_name, config):
            return

        self.__mp_solutions.add_new_config(combination_name, config)
        self.__mp_solutions.record_end_time(combination_name, os.getpid())

        self.__log_sat_solution_progress(combination_name)

    def __log_sat_solution_progress(self, combination_name) -> None:
        curr_generated_tests = self.num_generated_tests()
        total_num_tests_to_generate = self.__max_num_configs * len(self.__search_configs)

        duration = self.get_found_sol_duration(combination_name)
        end_time = self.get_found_sol_endtime(combination_name)
        self.log_progress(
            f"Progress: [combination {combination_name}]"
            f"{curr_generated_tests}/{total_num_tests_to_generate} = "
            f"{100 * curr_generated_tests/total_num_tests_to_generate:.3f}% \n"
            f"|Duration: {duration}"
            f"|End time: {end_time}"
            f"\n{'-'*40}"
        )
        self.log_info(
            f"Found solution for combination {combination_name}. "
            f"Generated config num "
            f"{curr_generated_tests}."
        )

    def get_generated_configs(self) -> List[Dict[str, int | str]]:
        """Returns generated configs after simple parsing."""
        configs = [
            x.generated_configs
            for x in self.__mp_solutions.values()
            if len(x.generated_configs) > 0
        ]
        # flattens the list (e.g. from [[1,2,3], [4,5]] to [1,2,3,4,5])
        configs = [item for sol_list in configs for item in sol_list]
        return configs

    def get_solution_times(self) -> str:
        """Returns formated string containing solution times for each combination."""
        sol_times_dict = {
            k: [self.__mp_solutions[k].durations, self.__mp_solutions[k].end_times]
            for k in self.__mp_solutions.keys()
        }
        return json.dumps(sol_times_dict, indent=4)

    def record_new_run(self, combination_name: str) -> None:
        """Records the current time as start time for this solver."""
        self.__mp_solutions.record_start_time(combination_name, os.getpid())

    def num_generated_tests(self) -> int:
        return self.__mp_solutions.num_generated_tests()

    def num_failed_tests(self, id: int) -> int:
        """Get number of failed tests by search config id."""
        return self.__mp_num_failed_tests[id]

    def get_found_sol_duration(self, combination_name: str) -> float:
        return self.__mp_solutions[combination_name].durations

    def get_found_sol_endtime(self, combination_name: str) -> float:
        return self.__mp_solutions[combination_name].end_times

    def more_configs_per_combination_needed(self, combination_name: str) -> List[Dict]:
        curr_num_configs = len(self.__mp_solutions[combination_name].generated_configs)
        return curr_num_configs < self.__mp_solutions[combination_name].max_configs_per_combination

    def more_solutions_needed(self, combination: SweepCombination) -> None:
        return (
            self.num_generated_tests() < self.__max_num_configs
            and self.more_configs_per_combination_needed(combination.name)
        )

    # TODO: Find a simpler solution to get the loggers, not by using dummy_search_config
    def log_info(self, info_message: str) -> None:
        dummy_search_config = self.__search_configs[-1]
        logger = get_logger(dummy_search_config.get_unique_logger_name())
        with self.__mp_lock:
            logger.info(info_message)

    def log_warning(self, warning_message: str) -> None:
        dummy_search_config = self.__search_configs[-1]
        logger = get_logger(dummy_search_config.get_unique_logger_name())
        with self.__mp_lock:
            logger.warning(warning_message)

    def log_progress(self, progress_message: str) -> None:
        dummy_search_config = self.__search_configs[-1]
        progress = get_logger(dummy_search_config.get_progress_logger_name())
        with self.__mp_lock:
            progress.info(progress_message)

    def finish(self) -> None:
        """Performs logging at the end and manager shutdown."""
        self.log_info(f"Done. Generated {self.num_generated_tests()} configs. \n \n")
        self.log_info(self.get_solution_times())
        self.__shared_objects_manager.__exit__(*sys.exc_info())

    def still_needs_generating(self, combination_name: str) -> bool:
        return combination_name in self.__mp_todo.keys() and self.more_configs_to_generate()

    def more_configs_to_generate(self) -> bool:
        return self.num_generated_tests() < self.__max_num_configs

    def get_worker_config(self) -> Optional[SolverWorkerConfig]:
        return self.__mp_todo.get_random_worker_config_if_exists()


class WorkerSolutionHandlerManager(SyncManager):
    """
    The multiprocessing module programming paradigm needs a Manager for every on of its proxy
    (shared) class objects. The WorkerSolutionHandlerManager class manages the WorkerSolutionHandler
    objects, locking them as necessary. As per
    https://docs.python.org/3/library/multiprocessing.html#customized-managers, it only needs to be
    an empty class derived from Multiprocessing.managers.BaseManager. WorkerSolutionHandlerManager
    inherits from SyncManager (which itself inherits from BaseManager) for consistency with
    CustomSharedObjectsManager
    """

    pass


class WorkerManager:
    """
    Class used to run threads that monitor parallel sweep worker excecution.
    It is initialized with worker and search configs for combinations that need to be solver.
    When run method is performed, workers are managed like this:
    A number of threads (n_threads) is created, each solving a single combination. Each thread
    creates one process. Process is a worker - it solves the given combination.
    When enough configs are found for a given combinations, all processes that are working on that
    combination are to be terminated (that is the responsibility of thread).

    Attributes
    ----------
    __worker_solution_handler: WorkerSolutionHandler
        Instance of a WorkerSolutionHandler class which handles all of the shared objects needed
        for the worker threads.

    __worker_solution_handler_manager: WorkerSolutionHandlerManager
        Manager of the __worker_solution_handler, doing arbitrage of shared memory access, in
        accordance with the paradigm in the Python Multiprocessing module.

    __generated_configs: list[Dict]
        List of the final generated configs
    """

    __worker_solution_handler: WorkerSolutionHandler
    __worker_solution_handler_manager: WorkerSolutionHandlerManager
    __generated_configs: list[Dict]

    # Max number of processes to be used by parallel sweep thread pool.
    NUMBER_OF_THREADS = int(os.getenv("PARALLEL_SWEEP_PROCESS_COUNT", os.cpu_count()))
    POLLING_DELAY = 1

    def __init__(self, search_configs: List[SolutionSearchConfig], config_dir_paths: List[str]):
        WorkerSolutionHandlerManager.register("WorkerSolutionHandler", WorkerSolutionHandler)
        self.__worker_solution_handler_manager = WorkerSolutionHandlerManager()
        self.__worker_solution_handler_manager.__enter__()
        self.__worker_solution_handler = (
            self.__worker_solution_handler_manager.WorkerSolutionHandler(
                search_configs, config_dir_paths
            )
        )

    def __finish(self) -> None:
        self.__generated_configs = self.__worker_solution_handler.get_generated_configs()
        self.__worker_solution_handler.finish()
        self.__worker_solution_handler_manager.__exit__(*sys.exc_info())

    def run(self, worker_func: Callable) -> None:
        """
        Main function used to run threads in parallel.
        Each thread goes to monitor function, which starts and ends procesess (workers).
        """

        self.__worker_solution_handler.init_run()
        threads = [
            threading.Thread(target=self.__run_and_monitor, args=(worker_func,))
            for _ in range(WorkerManager.NUMBER_OF_THREADS)
        ]
        for t in threads:
            t.start()

        for t in threads:
            t.join()

        self.__finish()

    def __terminate(self, process: mp.Process) -> None:
        """
        Because the process might be holding shared objects at the point in time when the manager is
        trying to terminate it, we need to proctect the termination with semaphores, which the
        worker then acquires when it handles shared objects.
        """
        process_pid = int(process.pid)
        self.__worker_solution_handler.acquire_semaphore(process_pid)
        process.terminate()
        self.__worker_solution_handler.release_semaphore(process_pid)

    def __monitor(self, process: mp.Process, combination_name: str) -> None:
        """
        Polls process and decides when to kill it. If current combination has been completely
        solved by processes working on it, or if the maximum number of configs has been
        reached, kill the process.
        """
        while self.__worker_solution_handler.still_needs_generating(combination_name):
            time.sleep(WorkerManager.POLLING_DELAY)

        # We can only terminate the process when it explicitely allows to do so, which is when
        # it is currently solving a configuration
        if process.is_alive():
            self.__terminate(process)

    def __run_and_monitor(self, worker_func: Callable) -> None:
        """Creates and destroys new processes which run worker function."""
        while self.__worker_solution_handler.more_configs_to_generate():
            # if there are no new worker configs for todo, then finish with the thread
            worker_config = self.__worker_solution_handler.get_worker_config()
            if worker_config is None:
                break

            process = mp.Process(
                target=worker_func, args=(worker_config, self.__worker_solution_handler)
            )
            process.start()

            self.__monitor(process, worker_config.combination.name)

    @property
    def generated_configs(self) -> list[Dict]:
        return self.__generated_configs
