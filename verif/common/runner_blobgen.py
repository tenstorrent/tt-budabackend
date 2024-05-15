#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Functions for running net2pipe commands.
"""
from __future__ import annotations

import os
from dataclasses import dataclass

import yaml

from verif.common.runner_utils import (
    WorkerResult,
    execute_in_parallel,
    get_soc_file,
    run_cmd,
)
from verif.common.test_utils import (
    extract_arch_from_path,
    extract_chip_ids_from_blob_yaml,
    find_all_files_in_dir,
    get_logger,
)

logger = get_logger(__name__)

BLOBGEN_RB_PATH = "./src/overlay/blob_gen.rb"


@dataclass
class BlobgenWorkerConfig:
    blobgen_rb_path: str
    blob_yaml_path: str
    hex_output_path: str
    epoch_id: str
    arch: str


class BlobgenRunner:
    def generate_blobgen_outputs(
        blob_yaml_path: str,
        blobgen_out_dir: str,
        blobgen_rb_path: str = BLOBGEN_RB_PATH,
        num_samples: int = -1,
        overwrite: bool = False,
    ) -> bool:
        """Runs blobgen in parallel on all blob.yamls in blob_yaml_path dir.

        Parameters
        ----------
        blob_yaml_path: str
            Directory with blob.yamls to run blobgen on.
        blobgen_out_dir: str
            Output directory.
        blobgen_rb_path: str
            Path for blobgen ruby script to run.
        num_samples: int
            Number of blob.yamls to process.
            -1 will process all yamls.
        overwrite: bool
            Overwrite existing blobgen outputs, even if the output folder exists.

        Returns
        -------
        bool
            True if all the workers succeeded, False otherwise.

        Example of expected input and directory structures
        --------------------------------------------------
        .
        |-- blob_yaml_path
        |   |-- filtered_yamls_Everything
        |   |   |-- grayskull
        |   |   |   |-- 1xlink
        |   |   |   |   `-- blob_0.yaml
        |   |   |   `-- netlist_programs1_nested_parallel_loops_single_epoch
        |   |   |       |-- blob_0.yaml
        |   |   |       `-- blob_1.yaml
        |   |   `-- wormhole_b0
        |   |       `-- netlist_programs1_nested_parallel_loops_single_epoch
        |   |           |-- blob_0.yaml
        |   |           `-- blob_1.yaml
        |   |-- filtered_yamls_UnicastPipe
        |   |   `-- grayskull
        |   |       `-- 1xlink
        |   |           `-- blob_0.yaml
        |   `-- pipegen.log
        `-- blobgen_out_dir
            |-- blobgen.log
            |-- filtered_yamls_Everything
            |   |-- grayskull
            |   |   |-- 1xlink
            |   |   |   |-- pipegen_epoch0_0_0_0.hex
            |   |   |   |-- ... 12 more items
            |   |   |   |-- pipegen_epoch0_0_0_13.hex
            |   |   |   |-- pipegen_epoch0_0_1_0.hex
            |   |   |   |-- ... 166 more items
            |   |   |   |-- pipegen_epoch0_0_12_13.hex
            |   |   `-- netlist_programs1_nested_parallel_loops_single_epoch
            |   |       |-- pipegen_epoch0_0_0_0.hex
            |   |       |-- ... 180 more items
            |   |       |-- pipegen_epoch0_0_12_13.hex
            |   |       |-- pipegen_epoch1_0_0_0.hex
            |   |       |-- ... 180 more items
            |   |       `-- pipegen_epoch1_0_12_13.hex
            |   `-- wormhole_b0
            |       `-- netlist_programs1_nested_parallel_loops_single_epoch
            |           |-- pipegen_epoch0_0_0_0.hex
            |           |-- ... 141 more items
            |           |-- pipegen_epoch0_0_12_10.hex
            |           |-- pipegen_epoch1_0_0_0.hex
            |           |-- ... 141 more items
            |           `-- pipegen_epoch1_0_12_10.hex
            `-- filtered_yamls_UnicastPipe
                `-- grayskull
                    `-- 1xlink
                        |-- pipegen_epoch0_0_0_0.hex
                        |-- ... 180 more items
                        `-- pipegen_epoch0_0_12_13.hex
        """
        if os.path.exists(blobgen_out_dir) and not overwrite:
            # Optimization to reuse generated blobgen outputs.
            logger.warning(
                f"Skipping blobgen, outputs already exist at {blobgen_out_dir}"
            )
            return True
        logger.info("Running blobgen workers")

        worker_configs = []

        all_blob_yamls = find_all_files_in_dir(blob_yaml_path, "*/blob_[0-9]*.yaml")
        for blob_relpath in all_blob_yamls:
            # Split into dirname and basename
            blob_reldir, blob_yaml_name = os.path.split(blob_relpath)

            epoch_id = int(blob_yaml_name[:-5].split("_")[1])
            arch = extract_arch_from_path(blob_reldir)

            output_folder = os.path.join(blobgen_out_dir, blob_reldir)
            os.makedirs(output_folder, exist_ok=True)

            worker_configs.append(
                BlobgenWorkerConfig(
                    blobgen_rb_path=blobgen_rb_path,
                    blob_yaml_path=os.path.join(blob_yaml_path, blob_relpath),
                    hex_output_path=output_folder,
                    arch=arch,
                    epoch_id=epoch_id,
                )
            )

        if num_samples >= 0:
            worker_configs = worker_configs[:num_samples]
            logger.info(
                f"Choosing first {num_samples} pipegen.yamls to run pipegen on."
            )

        results = execute_in_parallel(
            BlobgenRunner.run_blobgen_worker,
            worker_configs,
            log_file=f"{blobgen_out_dir}/blobgen.log",
            heavy_workload=True,
        )

        return all(result.error_code == 0 for result in results)

    def run_blobgen(
        blob_rb_path: str,
        blob_out_dir: str,
        blob_yaml_path: str,
        epoch_id: str,
        arch: str,
        throw_if_error: bool = False,
    ):
        with open(get_soc_file(arch), "r") as yaml_file:
            soc_desc = yaml.safe_load(yaml_file)

        blobgen_cmd = BlobgenRunner.__get_blobgen_cmd(
            blob_rb_path,
            blob_out_dir,
            blob_yaml_path,
            epoch_id,
            noc_x_size=(
                soc_desc["physical"]["x_size"]
                if "physical" in soc_desc
                else soc_desc["grid"]["x_size"]
            ),
            noc_y_size=(
                soc_desc["physical"]["y_size"]
                if "physical" in soc_desc
                else soc_desc["grid"]["y_size"]
            ),
            chip=soc_desc["arch_name"].lower(),
            noc_version=soc_desc["features"]["overlay"]["version"],
            tensix_mem_size=soc_desc["worker_l1_size"],
            chip_ids=extract_chip_ids_from_blob_yaml(blob_yaml_path),
            noc_translation_id_enabled=int(
                bool(soc_desc["features"]["noc"]["translation_id_enabled"])
                if "features" in soc_desc and "noc" in soc_desc["features"]
                else False
            ),
            noc_x_logical_size=soc_desc["grid"]["x_size"],
            noc_y_logical_size=soc_desc["grid"]["y_size"],
            tensix_mem_size_eth=soc_desc["eth_l1_size"],
            eth_cores_comma_str=BlobgenRunner.__convert_cores_from_soc_format(
                soc_desc["eth"]
            ),
            worker_cores_comma_str=BlobgenRunner.__convert_cores_from_soc_format(
                soc_desc["functional_workers"]
            ),
            blob_section_start=143488,
            blob_section_start_eth=131200,
            data_buffer_space_base_eth=163840,
        )

        blobgen_cmd.replace("\n", " ")
        result = run_cmd(blobgen_cmd, "", timeout=300)
        if result.returncode != 0:
            # Remove files produced for this epoch since they are not valid.
            files_relative = find_all_files_in_dir(
                blob_out_dir, f"*/pipegen_epoch{epoch_id}_*.hex"
            )

            for file in files_relative:
                os.remove(os.path.join(blob_out_dir, file))

        if result.returncode != 0 and throw_if_error:
            raise RuntimeError(f"Running blobgen on {blob_yaml_path} failed: {result}")
        return result.returncode, blobgen_cmd

    def run_blobgen_worker(blobgen_worker_config: BlobgenWorkerConfig):
        err, cmd = BlobgenRunner.run_blobgen(
            blob_rb_path=blobgen_worker_config.blobgen_rb_path,
            blob_out_dir=blobgen_worker_config.hex_output_path,
            blob_yaml_path=blobgen_worker_config.blob_yaml_path,
            epoch_id=blobgen_worker_config.epoch_id,
            arch=blobgen_worker_config.arch,
        )
        return WorkerResult(cmd, err)

    def __convert_cores_from_soc_format(cores: list[str]):
        # transpose cores so that we first put y coord and then x coord
        cores = [core.split("-")[::-1] for core in cores]
        cores = [f"{core[0]}-{core[1]}" for core in cores]
        cores_comma_str = ",".join(cores)

        return cores_comma_str

    def __get_blobgen_cmd(
        blob_rb_path: str,
        blob_out_dir: str,
        blob_yaml_path: str,
        epoch_id: str,
        noc_x_size: int,
        noc_y_size: int,
        chip: str,
        noc_version: int,
        tensix_mem_size: int,
        chip_ids: list[str],
        noc_translation_id_enabled: int,
        noc_x_logical_size: int,
        noc_y_logical_size: int,
        tensix_mem_size_eth: int,
        eth_cores_comma_str: str,
        worker_cores_comma_str: str,
        blob_section_start: int,
        blob_section_start_eth: int,
        data_buffer_space_base_eth: int,
    ):
        """Returns blobgen command.
        Example command
        ---------------
        ruby ./src/overlay/blob_gen.rb
            --blob_out_dir out/a/output_blobgen/filtered_yamls_Everything/wormhole_b0/netlist_softmax_single_tile
            --graph_yaml 1
            --graph_input_file out/a/output_pipegen/filtered_yamls_Everything/wormhole_b0/netlist_softmax_single_tile/blob_0.yaml
            --graph_name pipegen_epoch0
            --noc_x_size 10
            --noc_y_size 12
            --chip wormhole_b0
            --noc_version 2
            --tensix_mem_size 1499136
            --chip_ids 0
            --noc_translation_id_enabled 0
            --noc_x_logical_size 10
            --noc_y_logical_size 12
            --tensix_mem_size_eth 262144
            --eth_cores 0-9,0-1,0-8,0-2,0-7,0-3,0-6,0-4,6-9,6-1,6-8,6-2,6-7,6-3,6-6,6-4
            --worker_cores 1-1,1-2,1-3,1-4,1-6,1-7,1-8,1-9,2-1,2-2,2-3,2-4,2-6,2-7,2-8,2-9,3-1,3-2,3-3,3-4,3-6,3-7,3-8,3-9,4-1,4-2,4-3,4-4,4-6,4-7,4-8,4-9,5-1,5-2,5-3,5-4,5-6,5-7,5-8,5-9,7-1,7-2,7-3,7-4,7-6,7-7,7-8,7-9,8-1,8-2,8-3,8-4,8-6,8-7,8-8,8-9,9-1,9-2,9-3,9-4,9-6,9-7,9-8,9-9,10-1,10-2,10-3,10-4,10-6,10-7,10-8,10-9,11-1,11-2,11-3,11-4,11-6,11-7,11-8,11-9
            --blob_section_start 143488
            --blob_section_start_eth 131200
            --data_buffer_space_base_eth 163840
            --dump_phase_info 1
        """
        return (
            f"ruby {blob_rb_path} "
            f"--blob_out_dir {blob_out_dir} "
            f"--graph_yaml 1 "
            f"--graph_input_file {blob_yaml_path} "
            f"--graph_name pipegen_epoch{str(epoch_id)} "
            f"--noc_x_size {str(noc_x_size)} "
            f"--noc_y_size {str(noc_y_size)} "
            f"--chip {chip} "
            f"--noc_version {str(noc_version)} "
            f"--tensix_mem_size {str(tensix_mem_size)} "
            f"--chip_ids {','.join(chip_ids)} "
            f"--noc_translation_id_enabled {str(noc_translation_id_enabled)} "
            f"--noc_x_logical_size {','.join([str(noc_x_logical_size)]*len(chip_ids))} "
            f"--noc_y_logical_size {','.join([str(noc_y_logical_size)]*len(chip_ids))} "
            f"--tensix_mem_size_eth {str(tensix_mem_size_eth)} "
            f"--eth_cores {eth_cores_comma_str} "
            f"--worker_cores {worker_cores_comma_str} "
            f"--blob_section_start {str(blob_section_start)} "
            f"--blob_section_start_eth {str(blob_section_start_eth)} "
            f"--data_buffer_space_base_eth {str(data_buffer_space_base_eth)} "
            f"--dump_phase_info 1"
        )
