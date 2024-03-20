# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

import datetime
from abc import ABC
from dataclasses import dataclass
from enum import Enum


class SearchType(Enum):
    """Supported search types."""

    free_search = 0
    serial_sweep = 1
    parallel_sweep = 2

    @staticmethod
    def create_from_string(search_type: str) -> SearchType:
        if search_type == "serial-sweep":
            return SearchType.serial_sweep
        elif search_type == "parallel-sweep":
            return SearchType.parallel_sweep
        else:
            return SearchType.free_search


@dataclass
class SolutionSearchConfig(ABC):
    """Simple class to wrap all info needed for search for solutions.

    Attributes
    ----------
    test_module_name:
        Name of test module for which we want to find solutions.
    random_seed:
        Seed with which solver is initialized.
    max_num_configs:
        (Approximate) number of solutions that will be generated. There is no
        strict way to know in advance how many will be generated, since some
        may fail valid_config_callback or, if sweep is performed, solver may
        return unsat for some sweep combinations.
    arch:
        Device architecture for which we are generatin solutions.
    verbose:
        Do we want output printed or not.
    eager_dump_configs:
        Flag which indicates we want to dump configs as soon as they are
        generated, since netlists are created only after the complete
        search is done.
    clamp_to_max_num_configs:
        Flag which indicates that for each (test_module_name, arch, random_seed)
        triplet we clamp sweep vars combinations to `max_num_configs`, therefore
        we will get anything between 0 and `max_num_configs` solutions. This is
        done to make more sense of `max` prefix, which, if this flag is False,
        doesn't make much sense considering it is only approximate (it often
        happens you will get >120 configs if you passed --max_num_configs 100).
    """

    test_module_name: str
    random_seed: int
    max_num_configs: int
    arch: str
    verbose: bool
    log_to_file: bool
    eager_dump_configs: bool
    clamp_to_max_num_configs: bool
    enable_strategy: bool
    timeout: int
    num_retries_on_timeout: int
    harvested_rows: int
    run_compiler: bool

    def __get_unique_id(self, with_timestamp=False) -> str:
        """Returns string composed of current time, module name, arch and seed."""
        id_str = f"{self.test_module_name.split('.')[-1]}_{self.arch}_seed_{self.random_seed}"

        if with_timestamp:
            return f'{id_str}_{datetime.datetime.now().strftime("%Y%m%d_%H%M%S")}'
        else:
            return f"{id_str}"

    @staticmethod
    def get_progress_logger_name() -> str:
        return "progress"

    def get_unique_logger_name(self) -> str:
        return self.__get_unique_id()

    def get_unique_dump_dir_name(self) -> str:
        return self.__get_unique_id(with_timestamp=True)

    @staticmethod
    def create_from_search_type(search_type: SearchType, *args, **kwargs) -> SolutionSearchConfig:
        if search_type == SearchType.serial_sweep:
            return SerialSweepSearchConfig(*args, **kwargs)
        elif search_type == SearchType.parallel_sweep:
            return ParallelSweepSearchConfig(*args, **kwargs)
        else:
            return DefaultSearchConfig(*args, **kwargs)


@dataclass(init=False)
class DefaultSearchConfig(SolutionSearchConfig):
    def __init__(
        self,
        test_module_name,
        random_seed,
        max_num_configs,
        arch,
        verbose,
        log_to_file,
        clamp_to_max_num_configs,
        timeout=0,
        num_retries_on_timeout=0,
        enable_strategy=False,
        harvested_rows=0,
        run_compiler=False,
    ):
        super().__init__(
            test_module_name=test_module_name,
            random_seed=random_seed,
            max_num_configs=max_num_configs,
            arch=arch,
            verbose=verbose,
            log_to_file=False,
            eager_dump_configs=False,
            clamp_to_max_num_configs=False,
            enable_strategy=False,
            timeout=0,
            num_retries_on_timeout=0,
            harvested_rows=harvested_rows,
            run_compiler=run_compiler,
        )


@dataclass(init=False)
class SerialSweepSearchConfig(SolutionSearchConfig):
    def __init__(
        self,
        test_module_name,
        random_seed,
        max_num_configs,
        arch,
        verbose,
        log_to_file,
        clamp_to_max_num_configs=False,
        timeout=0,
        num_retries_on_timeout=0,
        enable_strategy=False,
        harvested_rows=0,
        run_compiler=False,
    ):
        super().__init__(
            test_module_name=test_module_name,
            random_seed=random_seed,
            max_num_configs=max_num_configs,
            arch=arch,
            verbose=verbose,
            log_to_file=log_to_file,
            eager_dump_configs=True,
            clamp_to_max_num_configs=clamp_to_max_num_configs,
            timeout=timeout,
            num_retries_on_timeout=num_retries_on_timeout,
            enable_strategy=enable_strategy,
            harvested_rows=harvested_rows,
            run_compiler=run_compiler,
        )


@dataclass(init=False)
class ParallelSweepSearchConfig(SolutionSearchConfig):
    def __init__(
        self,
        test_module_name,
        random_seed,
        max_num_configs,
        arch,
        verbose,
        log_to_file,
        clamp_to_max_num_configs=False,
        timeout=0,
        num_retries_on_timeout=0,
        enable_strategy=False,
        harvested_rows=0,
        run_compiler=False,
    ):
        super().__init__(
            test_module_name=test_module_name,
            random_seed=random_seed,
            max_num_configs=max_num_configs,
            arch=arch,
            verbose=verbose,
            log_to_file=log_to_file,
            eager_dump_configs=True,
            clamp_to_max_num_configs=clamp_to_max_num_configs,
            timeout=timeout,
            num_retries_on_timeout=num_retries_on_timeout,
            enable_strategy=enable_strategy,
            harvested_rows=harvested_rows,
            run_compiler=run_compiler,
        )
