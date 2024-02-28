# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import hashlib
import os
import shutil
import subprocess
from dataclasses import dataclass
from enum import Enum
from logging import Logger
from typing import List, Optional, Dict, Tuple
import yaml

from scripts.utility.generate_netlists_from_configs import generate_netlists_from_configs
from util import get_git_root

# TODO: to be removed
# Used to prove that the import is working from another package
# Future changes will have useful imports
from verif.common.test_utils import print_success

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


@dataclass
class DescriptorPaths:
    """
    Attributes
    ----------
    default_soc_path:
        The default paths of .yaml files that describe the SoC architecture, in a form of a
        dictionary mapping the paths to the corresponding specific architectures.
    default_cluster_desc:
        The default paths of .yaml files that describe the clusters, in a form of a
        dictionary mapping the paths to the corresponding specific architectures.
    """

    default_soc_path = {
        DeviceArchitecture.grayskull: "./soc_descriptors/grayskull_10x12.yaml",
        DeviceArchitecture.wormhole: "./soc_descriptors/wormhole_8x10.yaml",
        DeviceArchitecture.wormhole_b0: "./soc_descriptors/wormhole_b0_8x10.yaml",
    }
    default_cluster_desc = {
        DeviceArchitecture.grayskull: "",
        DeviceArchitecture.wormhole: "./wormhole_2chip_cluster.yaml",
        DeviceArchitecture.wormhole_b0: "",
    }


class BlobgenRunner:
    """Class providing a wrapper around calls to blobgen"""

    @staticmethod
    def __get_l1_size(arch_name) -> int:
        device_architecture = DeviceArchitecture[arch_name]
        soc_path = DescriptorPaths.default_soc_path[device_architecture]
        with open(soc_path, "r") as yaml_file:
            soc_descriptor = yaml.safe_load(yaml_file)
            return soc_descriptor["worker_l1_size"]

    @staticmethod
    def __get_noc_translation_id_enabled(arch_name) -> bool:
        device_architecture = DeviceArchitecture[arch_name]
        soc_path = DescriptorPaths.default_soc_path[device_architecture]
        with open(soc_path, "r") as yaml_file:
            soc_descriptor = yaml.safe_load(yaml_file)
            if (
                "noc" in soc_descriptor["features"]
                and "translation_id_enabled" in soc_descriptor["features"]["noc"]
            ):
                return soc_descriptor["features"]["noc"]["translation_id_enabled"] == "True"
            else:
                return False

    @staticmethod
    def __get_physical_noc_size(arch_name) -> Tuple[int, int]:
        device_architecture = DeviceArchitecture[arch_name]
        soc_path = DescriptorPaths.default_soc_path[device_architecture]
        with open(soc_path, "r") as yaml_file:
            soc_descriptor = yaml.safe_load(yaml_file)
            return (soc_descriptor["grid"]["x_size"], soc_descriptor["grid"]["y_size"])

    @staticmethod
    def __get_overlay_version(arch_name) -> int:
        device_architecture = DeviceArchitecture[arch_name]
        soc_path = DescriptorPaths.default_soc_path[device_architecture]
        with open(soc_path, "r") as yaml_file:
            soc_descriptor = yaml.safe_load(yaml_file)
            return soc_descriptor["features"]["overlay"]["version"]

    @staticmethod
    def __get_blobgen_command(build_graph_dir, netlist_folder, arch_name) -> str:
        blobgen_cmd = "ruby " + os.path.join(get_git_root(), BLOBGEN_BIN_PATH)
        blobgen_cmd += " --blob_out_dir " + build_graph_dir
        blobgen_cmd += " --graph_yaml 1"
        blobgen_cmd += " --graph_input_file " + os.path.join(build_graph_dir, "blob.yaml")
        blobgen_cmd += " --graph_name pipegen_epoch0"
        blobgen_cmd += " --noc_x_size " + str(BlobgenRunner.__get_physical_noc_size(arch_name)[0])
        blobgen_cmd += " --noc_y_size " + str(BlobgenRunner.__get_physical_noc_size(arch_name)[1])
        blobgen_cmd += " --chip " + arch_name
        blobgen_cmd += " --noc_version " + str(BlobgenRunner.__get_overlay_version(arch_name))
        blobgen_cmd += " --tensix_mem_size " + str(BlobgenRunner.__get_l1_size(arch_name))
        blobgen_cmd += " --noc_translation_id_enabled " + (
            "1" if BlobgenRunner.__get_noc_translation_id_enabled(arch_name) else "0"
        )
        # Only works for single chip tests
        # TODO: make it work for multichip tests
        blobgen_cmd += " --chip-ids " + "0"
        blobgen_cmd += " --noc_x_logical_size " + str(
            BlobgenRunner.__get_physical_noc_size(arch_name)[0]
        )
        blobgen_cmd += " --noc_y_logical_size " + str(
            BlobgenRunner.__get_physical_noc_size(arch_name)[1]
        )
        return blobgen_cmd

    @staticmethod
    # TODO: make this function work for all temporal epochs
    def run_blobgen(netlist_folder, arch_name) -> subprocess.CompletedProcess:
        build_graph_dir = os.path.join(netlist_folder, "temporal_epoch_0/overlay")
        blobgen_cmd = BlobgenRunner.__get_blobgen_command(
            build_graph_dir, netlist_folder, arch_name
        )
        result = subprocess.run(
            blobgen_cmd,
            capture_output=True,
            shell=True,
            env={"ARCH_NAME": arch_name},
        )
        return result


class Net2PipeRunner:
    @staticmethod
    def __get_net2pipe_command(
        netlist_path: str,
        out_dir: str,
        arch: str,
        global_epoch_start: Optional[int],
        soc_path: Optional[str],
        cluster_desc: Optional[str],
    ) -> str:
        device_architecture = DeviceArchitecture[arch]
        soc_path = soc_path or DescriptorPaths.default_soc_path[device_architecture]
        cluster_desc = cluster_desc or DescriptorPaths.default_cluster_desc[device_architecture]
        return " ".join(
            [
                NET2PIPE_BIN_PATH,
                netlist_path,
                out_dir,
                str(global_epoch_start),
                soc_path,
                cluster_desc,
            ]
        )

    @staticmethod
    def run_net2pipe(
        netlist_path: str,
        out_dir: str,
        arch: str,
        global_epoch_start: Optional[int] = 0,
        soc_path: Optional[str] = None,
        cluster_desc: Optional[str] = None,
    ) -> subprocess.CompletedProcess:
        command = Net2PipeRunner.__get_net2pipe_command(
            netlist_path, out_dir, arch, global_epoch_start, soc_path, cluster_desc
        )
        result = subprocess.run(command, capture_output=True, shell=True, env={"ARCH_NAME": arch})
        return result


class PipegenRunner:
    @staticmethod
    def __get_pipegen_command(
        pipegen_yaml_path: str,
        output_blob_yaml_path: str,
        arch: str,
        soc_path: Optional[str],
        epoch: int,
    ) -> str:
        device_architecture = DeviceArchitecture[arch]
        soc_path = soc_path or DescriptorPaths.default_soc_path[device_architecture]

        return " ".join(
            [
                PIPEGEN_BIN_PATH,
                pipegen_yaml_path,
                soc_path,
                output_blob_yaml_path,
                str(epoch),
                "0",
            ]
        )

    @staticmethod
    def run_pipegen(
        pipegen_yaml_path: str,
        output_blob_yaml_path: str,
        arch: str,
        soc_path: Optional[str] = None,
        epoch: int = 0,
    ) -> subprocess.CompletedProcess:
        command = PipegenRunner.__get_pipegen_command(
            pipegen_yaml_path, output_blob_yaml_path, arch, soc_path, epoch
        )
        result = subprocess.run(command, capture_output=True, shell=True, env={"ARCH_NAME": arch})
        return result


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

        # Run blobgen on the results
        netlist_folder = os.path.join(out_dir, f"netlist_{test_id}")
        blobgen_result = BlobgenRunner.run_blobgen(netlist_folder, arch)
        if blobgen_result.returncode:
            return PipelineCompilationResult.failed(
                "pipegen failed: \n" + blobgen_result.stderr.decode("utf-8")
            )
        else:
            return PipelineCompilationResult.passed()

    @staticmethod
    def run_net2pipe_pipegen_single_test(
        test_dir: str, netlist_dir: str, out_dir: str, arch: str
    ) -> PipelineCompilationResult:
        netlist_id = netlist_dir.split("_")[1]
        netlist_path = os.path.join(test_dir, netlist_dir, f"netlist_{netlist_id}.yaml")
        netlist_out_dir = os.path.join(out_dir, f"netlist_{netlist_id}")
        os.makedirs(netlist_out_dir, exist_ok=True)
        # Run net2pipe.
        result = Net2PipeRunner.run_net2pipe(
            netlist_path=netlist_path, out_dir=netlist_out_dir, arch=arch
        )
        if result.returncode:
            return PipelineCompilationResult.failed(
                "net2pipe failed: \n" + result.stderr.decode("utf-8")
            )

        # Run pipegen.
        result = PipegenRunner.run_pipegen(
            pipegen_yaml_path=os.path.join(
                netlist_out_dir, "temporal_epoch_0", "overlay", "pipegen.yaml"
            ),
            output_blob_yaml_path=os.path.join(
                netlist_out_dir, "temporal_epoch_0", "overlay", "blob.yaml"
            ),
            arch=arch,
        )
        shutil.copy2(
            netlist_path,
            os.path.join(netlist_out_dir, "temporal_epoch_0", "overlay", "netlist.yaml"),
        )
        if result.returncode:
            return PipelineCompilationResult.failed(
                "pipegen failed: \n" + result.stderr.decode("utf-8")
            )

        return PipelineCompilationResult.passed()

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
