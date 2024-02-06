# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import itertools
import time
from typing import Dict, List, Optional, Tuple, Union

import z3


class SweepContext:
    """
    Models current sweep context.

    Attributes
    ----------
    _sweep_var_values
        Key value pairs of sweep variable names and values for the current context.
    """

    def __init__(self, sweep_var_values: SweepCombination) -> None:
        """Initializes sweep context with var name and value pairs."""
        self._sweep_var_values = sweep_var_values.combination

    def get_sweep_var_value(self, sweep_var: Union[z3.Var, str]) -> int:
        """Gets variables value in the current sweep context. Throws RuntimeError if the variable
        is not defined in this context.

        Parameters
        ----------
        sweep_var
            z3 variable or variable name for which to get value.
        """
        sweep_var_name = SweepContext._get_sweep_var_name(sweep_var)
        if sweep_var_name not in self._sweep_var_values:
            raise RuntimeError(f"Sweep variable: {sweep_var_name} is not present in this context.")
        return self._sweep_var_values[sweep_var_name]

    @staticmethod
    def _get_sweep_var_name(sweep_var: Union[z3.Var, str]) -> str:
        """Returns variable name. If the sweep_var parameter is already a variable name (i.e.
        a string), then this is a no-op.

        Parameters
        ----------
        sweep_var
            z3 varaible of variable name for which to return name.
        """
        if isinstance(sweep_var, str):
            return sweep_var
        return sweep_var.sexpr()


class SweepCombination:
    """
    Simple class that holds info about combinations. When solver starts search for combination, it
    calls record_start_time() method. When solver finds solution, it calls record_end_time() and
    corresponding start time is found (the one from same process) and accordingly recorded.

    Attributes
    ----------
    __name: str
        String representation of combination, a.k.a. it's name.

    __combination: Dict[str, int]
        An actual sweep combination dictionary, with keys and vals corresponding to
        sweep variables and their values.

    __max_configs_per_combination: int
        Maximum number of configs to generate for current combination.

    __start_times: float
        Captures timestamp when individual solver started solving the combination. Updating this
        list has to be through assignment operator because it is encapsulated in Proxy objects
        (like SolutionDictProxy and TodoDictProxy).

    __end_times: float
        Captures timestamps when solvers found the configs for this combination. Updating this
        list has to be through assignment operator because it is encapsulated in Proxy objects
        (like SolutionDictProxy and TodoDictProxy).


    __generated_configs: List[Dict]
        If solutions are found, solution models are to be stored in models property
        by simply adding to the end of the list. Updating this list has to be through assignment
        operator because it is encapsulated in Proxy objects (like SolutionDictProxy
        and TodoDictProxy).

    __potential_start_times: List[Tuple[float, int]]
        When solver starts solving combination, it records start time with corresponding process id
        in potential start times list. It is used to be paired up with end times to find duration.
        Updating this list has to be through assignment operator because it is encapsulated in
        Proxy objects (like SolutionDictProxy and TodoDictProxy).

    ____found_times: List[Tuple[float, float]]
        When end times are found, they are save with corresponding start times to found times
        list. Updating this list has to be through assignment operator because it is encapsulated
        in Proxy objects (like SolutionDictProxy and TodoDictProxy).

    Properties
    -------------
    name: str
        Getter for __name field.

    combination: Dict[str, int]
        Getter for __combination field.

    max_configs_per_combination: int
        Getter for __max_configs_per_combination field.

    start_times: float
        Getter for __start_times field.

    end_times: float
        Getter for __end_times field.

    durations: float
        Calculates differences between __end_times and __start_times elementwise.

    generated_configs: List[Dict]
        Getter for __generated_configs field.
    """

    __name: str = None
    __combination: Dict[str, int]
    __max_configs_per_combination: int = None
    __potential_start_times: List[Tuple[float, int]] = []
    __found_times: List[Tuple[float, float]] = []
    __generated_configs: List[Dict] = []

    def __init__(self, combination: Dict[str, int], max_configs_per_combination) -> None:
        self.__combination = combination
        self.__name = str(combination)
        self.__max_configs_per_combination = max_configs_per_combination

    def __str__(self) -> str:
        return self.__name

    @property
    def name(self):
        return self.__name

    @property
    def combination(self):
        return self.__combination

    @property
    def max_configs_per_combination(self):
        return self.__max_configs_per_combination

    @property
    def start_times(self):
        return [x for x in self.__potential_start_times]

    @property
    def end_times(self):
        return [x[1] for x in self.__found_times]

    @property
    def durations(self):
        return [x[1] - x[0] if x[1] != -1 else -1 for x in self.__found_times]

    @property
    def generated_configs(self):
        return self.__generated_configs

    def record_start_time(self, id: int = 0) -> SweepCombination:
        """
        Capures current start time as a potental start time for solver (if this turns out to
        be the solver that finds solution) along with it's unique id.
        """
        start_time = time.time()
        self.__potential_start_times = self.__potential_start_times + [(start_time, id)]
        return self

    def record_end_time(self, id: int = 0) -> SweepCombination:
        """
        Capure current time as end time, then find the start time that corresponds
        to this end time (lookup by unique id).
        """
        end_time = time.time()
        start_time = [el[0] for el in self.__potential_start_times if el[1] == id][-1]

        self.__found_times = self.__found_times + [(start_time, end_time)]
        return self

    def record_unsat(self) -> SweepCombination:
        """Record that this combination does not have any solutions left, indicated by -1."""
        self.__found_times = self.__found_times + [(-1, -1)]
        return self

    def add_new_config(self, config: Dict[str, int]) -> SweepCombination:
        """
        When new config is created, this is triggered to add it to generated configs list.
        New config is added only if the same one doesn't already exist and if number of configs
        per combination has not been reached.
        """
        # Append() cannot be used since __generated_configs will not be updated in its manager, but
        # has to be rewritten
        self.__generated_configs = self.__generated_configs + [config]

        return self

    def can_add_new_config(self, config) -> bool:
        """Checks if we can add new config to generated configs"""
        return (
            not config in self.__generated_configs
            and len(self.__generated_configs) < self.__max_configs_per_combination
        )


class SweepVarsGroup:
    """
    Simple class to conveniently wrap desired z3 vars and values to explicitly
    assign to those vars while solver searches for solutions.

    Test modules can define a list of one or more SweepVarsGroup objects, with
    one or more variable names per object, depending on the test module goals.
    For each variable in group, a range of values to try (sweep range) must be
    provided, as well as how many configs should be generated in total for the
    group, and per one sweep var combination.

    If the scenario is to sweep K variables independently, sweep_vars_list
    should contain K SweepVarsGroup objects with a single variable in the list.
    Examples test modules are: test_feedforward.py and test_add_and_norm.py.

    If the scenario is to sweep all combinations of M variables, sweep_vars_list
    should contain 1 SweepVarsGroup object with M variables in the list. Example
    test module is: test_dram_input_datacopy_multiple_tms_and_reblock.py.

    If the scenario is a mix of the above two, sweep_vars_list should contain
    multiple SweepVarsGroup objects with lists of one or more names of sweep
    variables.

    Writer of the class is responsible for making sure sweep ranges and var
    combinations make sense and are limited to a practical range.

    Example:
    sweep_vars_list = [
        SweepVarsGroup(
            {
                var1 : [val11, val12,... val1m],
                var2 : [val21, val22,... val2n],
                var3 : [val31, val32,... val3p]
            },
            max_num_configs_per_combination=5
        ),
        SweepVarsGroup(
            {
                var4 : [0, 1],
                var5 : list(range(1, 9))
            },
            max_num_configs_per_combination=10
        )
    ]

    Attributes
    ----------
    var_names_range_dict: dict
        Dict mapping z3_var_name to list of values it should take during search
        for solution.

    max_num_configs_per_combination: int
        For one combination of values for vars in this sweep froup, no more
        than max_num_configs_per_combination solutions should be generated
        (since it often happens solver gets stuck with one combination which
        allows it to easily find solutions by tweaking some simpler vars).

    drop_symmetric_tm_configurations: bool
        Governs if the sweep var group should drop symmetric TM confgurations
        on forking netlists, preventing generation of multiple netlists with
        the same branch-TM configuration. Do not use when sweeping for var values,
        as there is no such symmetry in that regard.

    Methods
    -------
    get_var_names_list(self) -> list
        Returns list of names of z3 vars to sweep.
    get_var_sweep_ranges_list(self) -> list
        Returns list of lists where each internal list is a range of values
        corresponding sweep var should take. Used to generate all possible
        combinations of values for sweep vars.
    get_num_configs_per_combination(self) -> int
        Returns max_num_configs_per_combination.
    get_max_valid_config_callback_fails(self) -> Int
        Returns max_valid_config_callback_fails.
    get_next_combination(self) -> dict
        Generator function which yields dict in form {'var1': val1, 'var2': val2 ...}
        which represents one combination of sweep vars.
    """

    def __init__(
        self,
        var_names_range_dict: Dict,
        max_num_configs_per_combination: Optional[int] = None,
        drop_symmetric_tm_configurations: Optional[bool] = None,
        num_tms_per_conn: Optional[List[int]] = None,
    ) -> None:
        self.var_names_range_dict = var_names_range_dict
        self.max_num_configs_per_combination = max_num_configs_per_combination
        self.drop_symmetric_tm_configurations = drop_symmetric_tm_configurations
        self.num_tms_per_conn = num_tms_per_conn

    def get_var_names_list(self) -> List:
        return list(self.var_names_range_dict.keys())

    def get_var_sweep_ranges_list(self) -> List:
        return list(self.var_names_range_dict.values())

    def get_num_configs_per_combination(self) -> Optional[int]:
        return self.max_num_configs_per_combination

    def __get_tm_configurations_grouped_by_fork_branch(self, configuration):
        # We assume that this function will be used when eliminating symmetric forking
        # configurations regarding TMs.
        tm_configurations_per_fork_branch = []
        start = 0
        for num_tms_on_current_fork in self.num_tms_per_conn:
            tm_config_on_current_fork = tuple(configuration.values())[
                start : start + num_tms_on_current_fork
            ]
            tm_configurations_per_fork_branch.append(tm_config_on_current_fork)
            start += num_tms_on_current_fork
        # We are sorting here, as opposed to in tm_config_on_current_fork, because the ordering on
        # one fork path matters, while the ordering between different paths does not
        return tuple(sorted(tm_configurations_per_fork_branch))

    def get_next_combination(self) -> SweepCombination:
        # 'all_combinations' is a generator object, iterate through it and yield
        # the result, making this function also a generator.
        all_combinations = itertools.product(*self.get_var_sweep_ranges_list())

        # If we want to not yield symetric configurations,
        # we use the this set to keep track of sorted configurations
        seen_tm_configurations = set()

        # Pair up each var_name with appropriate value in this combination and
        # yield it back to the caller.
        for combination in all_combinations:
            configuration = dict(zip(self.get_var_names_list(), combination))

            if self.drop_symmetric_tm_configurations:
                tm_configuration = self.__get_tm_configurations_grouped_by_fork_branch(
                    configuration
                )
                if tm_configuration in seen_tm_configurations:
                    continue
                seen_tm_configurations.add(tm_configuration)
            sweep_combination = SweepCombination(
                configuration, self.max_num_configs_per_combination
            )
            yield sweep_combination
