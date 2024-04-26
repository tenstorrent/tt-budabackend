# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import hashlib
import os
import shutil
from dataclasses import dataclass
from enum import Enum
from logging import Logger
from typing import Dict, List, Tuple

from util import get_git_root

from verif.common.runner_blobgen import BLOBGEN_RB_PATH, run_blobgen
from verif.common.runner_net2pipe import run_net2pipe
from verif.common.runner_pipegen import run_pipegen
from verif.template_netlist.scripts.utility.generate_netlists_from_configs import (
    generate_netlists_from_configs,
)

PROCESS_DUMP_FOLDER_PREFIX = "test_dir_config"

NET2PIPE_BIN_PATH = "./build/bin/net2pipe"
PIPEGEN_BIN_PATH = "./build/bin/pipegen2"
BLOBGEN_BIN_PATH = "./src/overlay/blob_gen.rb"

RED = "\033[0;31m"
GRN = "\033[0;32m"
YLW = "\033[0;33m"
INFO = "\033[1;33m"
ENDL = "\033[0m"  # No color
PASS = f"{GRN}<PASSED>{ENDL}"
FAIL = f"{RED}<FAILED>{ENDL}"


class DeviceArchitecture(Enum):
    grayskull = "grayskull"
    wormhole = "wormhole"
    wormhole_b0 = "wormhole_b0"


@dataclass
class PipelineCompilationResult:
    passed: bool
    message: str = ""

    @staticmethod
    def passed():
        return PipelineCompilationResult(passed=True)

    @staticmethod
    def failed(message: str = ""):
        return PipelineCompilationResult(passed=False, message=message)

    def __bool__(self):
        return self.passed


class CompilationPipelineRunner:
    """Class designated for running the compilation pipeline (net2pipe, pipegen and blobgen) on
    generated netlists (or configurations directly generated from the test generation pipeline) as
    part of test generation. Currently, used primarily in the generation of kernel broadcast tests,
    as well as in checking the validity of the generated tests.
    """

    @staticmethod
    def __get_test_directory_and_id(netlist_test_infos: List[List[Tuple]]) -> (str, str):
        """
        generate_netlists_from_configs() function returns a list of lists, where each element of
        the outer list corresponds to one directory of configs, and each element of the inner list
        is a tuple of two elements, first element being a directory where the netlist is stored,
        and the second the name of the netlist, with the .yaml extension.
        """
        test_directory = netlist_test_infos[0][0][0]
        generated_netlist_name = netlist_test_infos[0][0][1].split(".")[0]
        test_id = generated_netlist_name.split("_")[1]
        return test_directory, test_id

    @staticmethod
    def __create_netlist_from_single_config(
        config_file_path: str, config: str, relative_dump_folder: str, template_yaml: str
    ) -> (str, str):
        with open(config_file_path, "w") as config_file:
            yaml.dump(config, config_file)
        relative_dir_path = os.path.join(
            relative_dump_folder,
            f"config_{hashlib.md5(str(config).encode()).hexdigest()}",
        )
        template_yaml_relative = os.path.join(
            "verif", "template_netlist", "templates", os.path.basename(template_yaml)
        )
        netlist_test_infos = generate_netlists_from_configs(
            template_yaml_relative, relative_dir_path, relative_dump_folder
        )
        return CompilationPipelineRunner.__get_test_directory_and_id(netlist_test_infos)

    @staticmethod
    def run_compiler_pipeline_on_configs(
        template_yaml: str, config: Dict, arch_name: str, logger: Logger
    ) -> PipelineCompilationResult:
        # Setup the necessary file paths for writing the netlists to
        git_root = get_git_root()
        relative_compile_dump_folder = os.path.join(
            "build", "test", "verif", f"{PROCESS_DUMP_FOLDER_PREFIX}_{os.getpid()}"
        )
        compile_dump_folder = os.path.join(git_root, relative_compile_dump_folder)
        dir_path = os.path.join(
            git_root,
            relative_compile_dump_folder,
            f"config_{hashlib.md5(str(config).encode()).hexdigest()}",
        )
        if not os.path.exists(dir_path):
            os.makedirs(dir_path)
        config_file_path = os.path.join(
            dir_path,
            f"config_{hashlib.md5(str(config).encode()).hexdigest()}.yaml",
        )

        test_directory, test_id = CompilationPipelineRunner.__create_netlist_from_single_config(
            config_file_path, config, relative_compile_dump_folder, template_yaml
        )

        logger.info("Running the compilation pipeline on the generated config")

        compilation_result = CompilationPipelineRunner.run_net2pipe_pipegen_blobgen_single_test(
            test_dir=os.path.dirname(test_directory),
            netlist_dir=os.path.basename(test_directory),
            out_dir=compile_dump_folder,
            arch=arch_name,
            test_id=test_id,
        )

        # Delete the folder
        if os.path.exists(compile_dump_folder):
            shutil.rmtree(compile_dump_folder)
        if not compilation_result:
            logger.warning(f"Config failed compilation.\n {compilation_result.message}")
        return compilation_result

    @staticmethod
    def run_net2pipe_pipegen_blobgen_single_test(
        test_dir: str, netlist_dir: str, out_dir: str, arch: str, test_id: str
    ) -> PipelineCompilationResult:
        test_result = CompilationPipelineRunner.run_net2pipe_pipegen_single_test(
            test_dir, netlist_dir, out_dir, arch
        )
        if not test_result.passed:
            return test_result

        try:
            # Run blobgen on the results
            netlist_folder = os.path.join(out_dir, f"netlist_{test_id}")
            build_graph_dir = os.path.join(netlist_folder, "temporal_epoch_0/overlay")
            run_blobgen(
                BLOBGEN_RB_PATH,
                build_graph_dir,
                os.path.join(build_graph_dir, "blob.yaml"),
                0,
                arch,
                throw_if_error=True,
            )
            return PipelineCompilationResult.passed()
        except RuntimeError as e:
            return PipelineCompilationResult.failed(str(e))

    @staticmethod
    def run_net2pipe_pipegen_single_test(
        test_dir: str, netlist_dir: str, out_dir: str, arch: str
    ) -> PipelineCompilationResult:
        try:
            netlist_id = netlist_dir.split("_")[1]
            netlist_path = os.path.join(test_dir, netlist_dir, f"netlist_{netlist_id}.yaml")
            netlist_out_dir = os.path.join(out_dir, f"netlist_{netlist_id}")
            os.makedirs(netlist_out_dir, exist_ok=True)

            run_net2pipe(
                netlist_path=netlist_path, out_dir=netlist_out_dir, arch=arch, throw_if_error=True
            )

            run_pipegen(
                pipegen_yaml_path=os.path.join(
                    netlist_out_dir, "temporal_epoch_0", "overlay", "pipegen.yaml"
                ),
                blob_yaml_path=os.path.join(
                    netlist_out_dir, "temporal_epoch_0", "overlay", "blob.yaml"
                ),
                arch=arch,
                epoch_id="0",
                throw_if_error=True
            )
            shutil.copy2(
                netlist_path,
                os.path.join(netlist_out_dir, "temporal_epoch_0", "overlay", "netlist.yaml"),
            )

            return PipelineCompilationResult.passed()
        except RuntimeError as e:
            return PipelineCompilationResult.failed(str(e))

    @staticmethod
    def run_compilation(
        test_dir: str,
        out_dir: str,
        arch: str,
        clear_output: bool = False,
        run_blobgen: bool = False,
    ) -> None:
        os.makedirs(out_dir, exist_ok=True)
        total_fail = 0
        total_pass = 0
        for netlist_dir in os.listdir(test_dir):
            # Skip everything which is not a directory.
            if not netlist_dir.startswith("test_") or not os.path.isdir(
                os.path.join(test_dir, netlist_dir)
            ):
                continue

            if run_blobgen:
                test_id = netlist_dir.split("_")[1]
                result = CompilationPipelineRunner.run_net2pipe_pipegen_blobgen_single_test(
                    test_dir, netlist_dir, out_dir, arch, test_id
                )
            else:
                result = CompilationPipelineRunner.run_net2pipe_pipegen_single_test(
                    test_dir, netlist_dir, out_dir, arch
                )
            print(f"{netlist_dir}: {PASS if result.passed else FAIL}")
            if not result.passed:
                # If the test failed, then we want to know why it failed.
                print(result.message)
                total_fail += 1
            else:
                total_pass += 1

        if clear_output:
            shutil.rmtree(out_dir)

        print("\n\nSummary:")
        print(f"{GRN}\tPassed: {total_pass} / {total_fail + total_pass}{ENDL}")
        print(f"{RED}\tFailed: {total_fail} / {total_fail + total_pass}{ENDL}")
