#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import os
import argparse
import importlib
import random
import shutil

from util import *
from z3_config_generator import generate_z3_configs
from test_modules.common.solution_search import SearchType, SolutionSearchConfig


def generate_ci_tests(
    test_module_name,
    output_dir,
    random_seed=0,
    num_tests=50,
    num_loops=1,
    determinism_tests=False,
    arch="grayskull",
    search_type="parallel-sweep",
    clamp_num_configs=False,
    skip_sh_files_gen=False,
    verbose=False,
    log_to_file=False,
    harvested_rows=0,
    timeout=0,
    num_retries_on_timeout=0,
    enable_strategy=False,
    run_compiler=False,
) -> None:
    """
    Generates a set of netlists given a set of parameters

    Parameters
    ----------
    test_module_name:
        Name of the module that was used for test generation, in the form of a relative path from
        the Budabackend repository.
    output_dir:
        Output directory, relative path from the Budabackend repository.
    random_seed:
        Seed from which the z3 solver starts the search.
    num_tests:
        Number of loops a program is run for each test.
    determinism_tests:
        Are the generated tests determinism in type.
    arch:
        Target architecture, currently either "grayskull" or "wormhole_b0".
    search_type:
        Type of search z3 utilizes, can be "parallel-sweep", "serial-sweep" or "free-search".
    clamp_num_configs:
        Clamp sweep combinations to max_num_configs.
    skip_sh_files_gen:
        Controls the generation of .sh files
    verbose:
        Print out progress.
    log_to_file:
        Controls logging to file
    harvested_rows:
        Number of harvested rows of the target architecture
    timeout:
        Timeout in seconds for z3 solver, zero==inf
    num_retries_on_timeout:
        Enables retry with different seed if z3 solver timed out
    enable_strategy:
        Instantiate z3 solver with predefined strategy (as it can improve z3 solver performance)
    run_compiler:
        Should the solver run the compilation pipeline as part of the tests generation process
    """
    cmd = (
        "SEED="
        + str(random_seed)
        + " ARCH_NAME="
        + arch
        + " verif/template_netlist/run_generate_ci_tests.sh "
        + test_module_name
        + " "
        + output_dir
        + " "
        + str(num_tests)
        + " "
        + str(num_loops)
    )

    if determinism_tests:
        cmd += " determinism-tests"

    # Get abs path to output dirs
    git_root = get_git_root()
    output_dir = f"{git_root}/{output_dir}"

    # make output dir
    os.makedirs(output_dir, exist_ok=True)

    # Import test module that contains solver constraints and test specific config
    test_module = importlib.import_module(test_module_name)

    # Generate configs
    random.seed(random_seed)
    search_type = SearchType.create_from_string(search_type)
    search_config = SolutionSearchConfig.create_from_search_type(
        search_type=search_type,
        test_module_name=test_module_name,
        random_seed=random_seed,
        max_num_configs=num_tests,
        arch=arch,
        verbose=verbose,
        log_to_file=log_to_file,
        clamp_to_max_num_configs=clamp_num_configs,
        harvested_rows=harvested_rows,
        timeout=timeout,
        num_retries_on_timeout=num_retries_on_timeout,
        enable_strategy=enable_strategy,
        run_compiler=run_compiler,
    )

    # Generate configs
    config_list = generate_z3_configs(search_config=search_config)

    # Possibly dump the generated configs to a yaml file
    with open(f"{output_dir}/test_configs.yaml", "w") as f:
        yaml.dump(config_list, f)

    # Generate netlists and test dirs
    tests = create_netlists(
        template_yaml=test_module.template_yaml,
        output_dir=output_dir,
        configs=config_list,
        command=cmd,
    )

    test_type = getattr(test_module, "TEST_TYPE", TestType.TmTest)
    cmds = generate_test_commands(
        tests, test_type, determinism_tests=determinism_tests, num_loops=num_loops
    )
    print("cmd=", cmd)
    if not skip_sh_files_gen:
        for (odir, test_yaml), cmd in zip(tests, cmds):
            shfile = f"{test_yaml[:-5]}.sh"
            script_contents = (
                f"#!/bin/bash\ncat {odir}/{test_yaml}\n{cmd} > >(tee {odir}/run.log) 2>&1"
            )
            print(script_contents)
            shfile_full_path = f"{output_dir}/{shfile}"
            with open(shfile_full_path, "w") as f:
                f.write(script_contents)

            os.chmod(shfile_full_path, 0o755)


def compress_netlists(root_directory, zip_destination, zip_file_name) -> None:
    """
    Compresses the netlists in the given directory

    Parameters
    ----------
    root_directory:
        The directory path (relative to the repository root) where the netlists are located
    zip_destination:
        The destination of the .zip file (relative to the repository root)
    zip_file_name:
        Name of the .zip file to be generated
    """
    # Get abs path to output dirs
    git_root = get_git_root()
    root_directory = f"{git_root}/{root_directory}"
    zip_destination = f"{git_root}/{zip_destination}"
    os.system(f"mkdir -p {zip_destination}")
    os.system(
        f"cd {root_directory}; rm -r logs/; rm *.sh; rm *.log;"
        f"rm {zip_destination}/{zip_file_name}.zip;"
        f"zip -rT {zip_destination}/{zip_file_name}.zip ."
    )


def erase_netlists(output_dir_path) -> None:
    """
    Deletes the directory which contains the unzipped netlists

    Parameters
    ----------
    output_dir_path:
        The directory path (relative to the repository root) where the netlists are located
    """
    git_root = get_git_root()
    root_directory = f"{git_root}/{output_dir_path}"
    shutil.rmtree(root_directory)


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument("--test-module", required=True, type=str)
    parser.add_argument("--output-dir", required=True, type=str)
    parser.add_argument("--random-seed", default=0, type=int)
    parser.add_argument("--num-tests", default=50, type=int)
    parser.add_argument("--num-loops", default=1, type=int)
    parser.add_argument("--determinism-tests", default=False, action="store_true")
    parser.add_argument("--arch", default="grayskull", type=str)
    parser.add_argument(
        "--search-type",
        default="default",
        type=str,
        help="One of: ['default', 'serial-sweep', 'parallel-sweep']",
    )
    parser.add_argument(
        "--clamp-num-configs",
        default=False,
        action="store_true",
        help="Clamp sweep combinations to max_num_configs",
    )
    parser.add_argument(
        "--skip-sh-files-gen",
        default=False,
        action="store_true",
        help="Skip the generation of .sh files",
    )
    parser.add_argument("--verbose", default=False, action="store_true")
    parser.add_argument("--log-to-file", default=False, action="store_true")
    parser.add_argument("--harvested-rows", default=0, type=int)
    parser.add_argument(
        "--timeout", default=0, type=int, help="Timeout in seconds for z3 solver, zero==inf"
    )
    parser.add_argument(
        "--num-retries-on-timeout",
        default=0,
        type=int,
        help="Enables retry with different seed if z3 solver timed out",
    )
    parser.add_argument(
        "--enable-strategy",
        default=False,
        action="store_true",
        help="Instantiate z3 solver with predefined strategy (it improves z3 solver performance)",
    )
    parser.add_argument(
        "--run-compiler",
        action="store_true",
        default=False,
        help="Run compiler to verify tests as part of test generation",
    )

    args = parser.parse_args()
    generate_ci_tests(
        args.test_module,
        args.output_dir,
        args.random_seed,
        args.num_tests,
        args.num_loops,
        args.determinism_tests,
        args.arch,
        args.search_type,
        args.clamp_num_configs,
        args.skip_sh_files_gen,
        args.verbose,
        args.log_to_file,
        args.harvested_rows,
        args.timeout,
        args.num_retries_on_timeout,
        args.enable_strategy,
        args.run_compiler,
    )


if __name__ == "__main__":
    main()
