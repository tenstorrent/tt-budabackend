# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import random
from dataclasses import dataclass
from multiprocessing.managers import MakeProxyType, SyncManager
from typing import Dict, Optional

from test_modules.common.solution_search import SolutionSearchConfig
from test_modules.common.sweep import SweepCombination


@dataclass
class SolverWorkerConfig:
    """Simple class to wrap all info needed for idividual parallel workers.

    Attributes
    ----------
    worker_config_id:
        Unique ID for group of workers this worker belongs to.
        Separate groups have different search configs.
    search_config:
        Described in `class SolutionSearchConfig`.
    combination:
        SweepCombination object, one SweepVarGroup combination.
    num_configs:
        Number of configs we want to generate for this combination.
    config_dir_path:
        Path to folder where to eager dump configs.
    """

    worker_config_id: int
    search_config: SolutionSearchConfig
    combination: SweepCombination
    num_configs: int
    config_dir_path: Optional[str]


class TodoDict(dict):
    """
    A derived class from python dict. Used to better encapsulate behavior needed for todo
    dictionary in WorkerManager. TodoDictProxy is used to access this dictionary, that is
    used as a shared object among processes. Encapsulating needed manipulations of this dictionary
    enables one to avoid race conditions.
    """

    def __init__(self):
        super().__init__()

    def get_random_worker_config_if_exists(self) -> Optional[SolverWorkerConfig]:
        """
        If there are any leftover combinations to solve, a random worker config with that
        combination is returned. Otherwise, None is returned.
        """
        return None if len(self) == 0 else random.choice(list(self.values()))


class SolutionDict(dict):
    """
    A derived class from python dict. Used to better encapsulate behavior needed for todo
    dictionary in WorkerManager. SolutionDictProxy is used to access this dictionary, that is
    used as a shared object among processes. Encapsulating needed manipulations of this dictionary
    enables one to avoid race conditions.
    """

    def __init__(self):
        super().__init__()

    def record_unsat(self, combination_name: str) -> None:
        """Wrapper for SweepCombination record_unsat."""
        self[combination_name] = self[combination_name].record_unsat()

    def record_start_time(self, combination_name: str, id: int) -> None:
        """Wrapper for SweepCombination record_start_time."""
        self[combination_name] = self[combination_name].record_start_time(id)

    def record_end_time(self, combination_name: str, id: int) -> None:
        """Wrapper for SweepCombination record_end_time."""
        self[combination_name] = self[combination_name].record_end_time(id)

    def add_new_config(self, combination_name: str, config: Dict[str, int]) -> None:
        """Wrapper for SweepCombination add_new_config."""
        self[combination_name] = self[combination_name].add_new_config(config)

    def already_found(self, combination_name: str, config: Dict[str, int]) -> bool:
        """Checks if this exact same config has already been found."""
        return self[combination_name].already_found(config)

    def num_generated_tests(self) -> int:
        """Returns total number of tests generated at current moment."""
        return sum([len(comb.generated_configs) for comb in self.values()])


# Creates proxy for using these derived dictionaries as shared objects among processes.
# Below are methods exposed for usage with Proxies for these classes.


class CustomSharedObjectsManager(SyncManager):
    """
    The multiprocessing module programming paradigm needs a Manager for every on of its proxy
    (shared) class objects. The CustomSharedObjectsManager class manages the two dictionary objects
    that the solver processes share, TodoDict and SolutionDict, locking them as necessary. As per
    https://docs.python.org/3/library/multiprocessing.html#customized-managers, it only needs to be
    an empty class derived from Multiprocessing.managers.BaseManager. Here, we derive from
    SyncManager, in order to utilize its list() method, which return ListProxy.
    """

    pass


TodoDictProxy = MakeProxyType(
    "TodoDict",
    (
        "__contains__",
        "__delitem__",
        "__getitem__",
        "__iter__",
        "__len__",
        "__setitem__",
        "clear",
        "copy",
        "get",
        "items",
        "keys",
        "pop",
        "popitem",
        "setdefault",
        "update",
        "values",
        "get_random_worker_config_if_exists",
    ),
)

SolutionDictProxy = MakeProxyType(
    "SolutionDict",
    (
        "__contains__",
        "__delitem__",
        "__getitem__",
        "__iter__",
        "__len__",
        "__setitem__",
        "clear",
        "copy",
        "get",
        "items",
        "keys",
        "pop",
        "popitem",
        "setdefault",
        "update",
        "values",
        "record_unsat",
        "record_start_time",
        "record_end_time",
        "add_new_config",
        "already_found",
        "num_generated_tests",
    ),
)
