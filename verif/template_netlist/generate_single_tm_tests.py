# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
import argparse

from util import get_git_root
from generate_ci_tests import generate_ci_tests, compress_netlists, erase_netlists

ROOT_FILE_PATH = get_git_root()
DEFAULT_OUTPUT_DIR = f"."


def set_num_tms_per_connection(connection_number, num_tms) -> None:
    """
    Set the environment variables that govern the number of TMs on the connections

    Parameters
    ----------
    connection_number:
        The number of the connection in question
    num_tms:
        The number of TMs to set on that specific connection
    """
    os.environ[f"NUM_TMS_PER_CONN"] = str(num_tms)
    os.environ[f"NUM_TMS_PER_CONN{connection_number}"] = str(num_tms)


def generate_test_netlist_for_tm(
    folder_name,
    harvested_rows,
    module_name,
    zip_name,
    number_of_connections,
    list_of_tm_numbers,
    arch_name,
    clamp_num_configs=False,
    run_compiler=False,
) -> None:
    """
    Generates a set of netlists given a set of parameters

    Parameters
    ----------
    folder_name:
        Name of the folder where the .zip files will be created.
    harvested_rows:
        Number of targeted rows in the harvested architecture.
    module_name:
        Name of the test module from which the tests are generated.
    zip_name:
        Name of the .zip file which will be generated.
    determinism_tests:
        Are the generated tests determinism in type.
    number_of_connections:
        Number of connections on which there are TMs in the test module
    list_of_tm_numbers:
        List of integers representing how many TMs are going to be generated on each connection
    arch_name:
        Target architecture, currently either "grayskull" or "wormhole_b0".
    clamp_num_configs:
        Clamp number of solutions
    run_compiler:
        Run the compiler pipeline as part of the test generation
    """
    # We generate both single-tm and no-tm netlists.
    for num_tms in list_of_tm_numbers:
        # Same number of tms on all connections.
        for connection_number in range(number_of_connections):
            set_num_tms_per_connection(connection_number, num_tms)
        output_dir = f"{folder_name}/{zip_name}_single_tm_{arch_name}"
        generate_ci_tests(
            test_module_name=module_name,
            output_dir=output_dir,
            arch=arch_name,
            search_type="parallel-sweep",
            log_to_file=True,
            harvested_rows=harvested_rows,
            timeout=6000,
            skip_sh_files_gen=True,
            verbose=True,
            clamp_num_configs=clamp_num_configs,
            run_compiler=run_compiler,
        )
    # TODO: Check if the netlists are valid on silicon
    # Compress into testlist-compatible mode.
    compress_netlists(
        output_dir,
        os.path.join("verif", "template_netlist", "netlists", "single_tm_tests", "push", arch_name),
        zip_name,
    )

    # Erase the uncompressed tests.
    erase_netlists(output_dir)


def generate_basic_configuration_tests(
    output_dir, harvested_rows, is_padding=False, arch_name="grayskull", run_compiler=False
) -> None:
    """
    Generates the basic single-TM configurations (dram_input -> datacopy, dram_input -> matmul,
    datacopy -> datacopy, datacopy -> matmul)

    Parameters
    ----------
    output_dir:
        The output directory for the .zip files
    harvested_rows:
        Number of harvested rows on the target architecture
    is_padding:
        Are the tests generated with padding
    arch_name:
        Target architecture, currently either "grayskull" or "wormhole_b0".
    """
    padding_suffix = "_padding" if is_padding else ""
    generate_test_netlist_for_tm(
        output_dir,
        harvested_rows,
        "test_modules.multi_tm_tests.test_dram_input_datacopy_multiple_tms_and_reblock",
        f"test_dram_input_datacopy{padding_suffix}",
        number_of_connections=1,
        list_of_tm_numbers=[0, 1],
        arch_name=arch_name,
        run_compiler=run_compiler,
    )
    generate_test_netlist_for_tm(
        output_dir,
        harvested_rows,
        "test_modules.multi_tm_tests.test_dram_input_matmul_multiple_tms_and_reblock",
        f"test_dram_input_matmul{padding_suffix}",
        number_of_connections=2,
        list_of_tm_numbers=[0, 1],
        arch_name=arch_name,
        run_compiler=run_compiler,
    )
    generate_test_netlist_for_tm(
        output_dir,
        harvested_rows,
        "test_modules.multi_tm_tests.test_datacopy_datacopy_multiple_tms_and_reblock",
        f"test_datacopy_datacopy{padding_suffix}",
        number_of_connections=1,
        list_of_tm_numbers=[0, 1],
        arch_name=arch_name,
        run_compiler=run_compiler,
    )
    generate_test_netlist_for_tm(
        output_dir,
        harvested_rows,
        "test_modules.multi_tm_tests.test_datacopy_matmul_multiple_tms_and_reblock",
        f"test_datacopy_matmul{padding_suffix}",
        number_of_connections=2,
        list_of_tm_numbers=[0, 1],
        arch_name=arch_name,
        run_compiler=run_compiler,
    )


def generate_forking_tests(output_dir, harvested_rows, fork_factor, arch_name):
    """
    Generates the basic single-TM configurations with forking 
    (dram_input -> datacopy, dram_input -> matmul, datacopy -> datacopy, datacopy -> matmul)

    Parameters
    ----------
    output_dir:
        The output directory for the .zip files
    harvested_rows:
        Number of harvested rows on the target architecture
    fork_factor:
        The forking factor of the generated tests
    arch_name:
        Target architecture, currently either "grayskull" or "wormhole_b0".
    """
    os.environ["FORK_FACTOR"] = str(fork_factor)
    generate_test_netlist_for_tm(
        output_dir,
        harvested_rows,
        "test_modules.multi_tm_tests.test_dram_input_fork_datacopy_multiple_tms_and_reblock",
        f"test_dram_input_fork{fork_factor}_datacopy",
        number_of_connections=1 * fork_factor,
        list_of_tm_numbers=[0, 1],
        arch_name=arch_name,
    )
    generate_test_netlist_for_tm(
        output_dir,
        harvested_rows,
        "test_modules.multi_tm_tests.test_dram_input_fork_matmul_multiple_tms_and_reblock",
        f"test_dram_input_fork{fork_factor}_matmul",
        number_of_connections=2 * fork_factor,
        list_of_tm_numbers=[0, 1],
        arch_name=arch_name,
    )
    generate_test_netlist_for_tm(
        output_dir,
        harvested_rows,
        "test_modules.multi_tm_tests.test_datacopy_fork_datacopy_multiple_tms_and_reblock",
        f"test_datacopy_fork{fork_factor}_datacopy",
        number_of_connections=1 * fork_factor,
        list_of_tm_numbers=[0, 1],
        arch_name=arch_name,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default=DEFAULT_OUTPUT_DIR, type=str)
    parser.add_argument("--harvested-rows", default=0, type=int)
    parser.add_argument(
        "--arch",
        required=True,
        type=str,
        help="Name of the target architecture: [grayskull, wormhole_b0]",
    )
    parser.add_argument(
        "--run-compiler",
        action="store_true",
        help="Run the compiler pipeline as part of the test generation",
    )
    parser.add_argument("--forking-tests", action="store_true", help="Generate forking tests")

    os.environ["GUIDING_ENABLED"] = "1"
    os.environ["FORCE_TM_ARGS_GREATER_THAN_ONE"] = "1"
    os.environ["NUM_CONFIGS_PER_TMS_COMBINATION"] = "1"

    args = parser.parse_args()
    os.makedirs(f"{ROOT_FILE_PATH}/{args.output_dir}", exist_ok=True)

    # Generate test cases for datacopy and matmul (no padding, no forking)
    generate_basic_configuration_tests(
        args.output_dir, args.harvested_rows, arch_name=args.arch, run_compiler=args.run_compiler
    )

    # Generate datacopy->dram_output tests without TMs but with reblocking.
    generate_test_netlist_for_tm(
        args.output_dir,
        args.harvested_rows,
        "test_modules.multi_tm_tests.test_op_dram_multiple_tms_and_reblock",
        "test_datacopy_dram_output",
        number_of_connections=1,
        list_of_tm_numbers=[0],
        arch_name=args.arch,
        clamp_num_configs=True,
        run_compiler=args.run_compiler,
    )

    # Generate test cases for datacopy and matmul (padding, no forking).
    os.environ["FORCE_HAS_PAD_UNPAD_SET"] = "1"
    os.environ["FORCE_KEEP_PADDING"] = "1"
    generate_basic_configuration_tests(
        args.output_dir,
        args.harvested_rows,
        is_padding=True,
        arch_name=args.arch,
        run_compiler=args.run_compiler,
    )

    if args.forking_tests:
        generate_forking_tests(
            args.output_dir,
            args.harvested_rows,
            fork_factor=2,
            arch_name=args.arch,
        )

        # Generate dram_input -> fork4 -> datacopy single TM test 
        # (rest of the fork4 tests would take too much time to generate).
        os.environ["FORK_FACTOR"] = "4"
        generate_test_netlist_for_tm(
            args.output_dir,
            args.harvested_rows,
            "test_modules.multi_tm_tests.test_dram_input_fork_datacopy_multiple_tms_and_reblock",
            "test_dram_input_fork4_datacopy",
            number_of_connections=4,
            list_of_tm_numbers=[0, 1],
            arch_name=args.arch,
        )
