# !/usr/bin/env python
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import argparse
from itertools import repeat
import math
import os
import yaml

from test_modules.data_movement.perf_tests.data_movement_perf_test_base import (
    DataMovementPerfTestType,
)
from test_modules.data_movement.perf_tests.data_movement_perf_test_modules import (
    get_perf_test_module_name,
)

from test_modules.common.test_module_wrapper import TestModuleWrapper
from test_modules.common.solution_search import SolutionSearchConfig, SearchType

from dataclasses import dataclass, field
import multiprocessing as mp
from typing import Any, List, Optional, Tuple, Dict, Union
import sys
import csv

sys.path.append(f"{os.getcwd()}/perf_lib")
from perf_test_base import *
from perf_sweep import (
    run_single_test,
    check_test_pass,
    get_perf_results_from_test,
    PerfResults,
)
from logger_utils import print_progress_bar

processed_perf_test_count: Optional[mp.Value]


def init_processed_perf_test_count(processed_perf_test_count_local: mp.Value):
    global processed_perf_test_count
    processed_perf_test_count = processed_perf_test_count_local


def match_test_id_to_sweep_vars(test_configs_yaml, all_sweep_vars):
    """Returns a dictionary of test_id to sweep_vars_combination."""

    if not os.path.exists(test_configs_yaml):
        raise FileNotFoundError(f"Test configs file {test_configs_yaml} does not exist")

    with open(test_configs_yaml, "r") as f:
        test_configs = yaml.safe_load(f)
        res = {}
        for test_config in test_configs:
            test_id = test_config["test_config_id"]
            res[test_id] = {}
            for sv in all_sweep_vars:
                if sv not in test_config:
                    raise ValueError(f"Test config {test_id} does not contain sweep variable {sv}")
                res[test_id][sv] = test_config[sv]
    return res


@dataclass
class PipeGraphOpData:
    # Epoch tiles of the target op.
    epoch_tiles: int

    # Tile clear granularity of the target op.
    kernel_clear_granularity: int

    # Chunk of data that is sent to the target op.
    producer_scatter_gather_num_tiles: int = 0


@dataclass
class StreamInfo:
    # List of phase configs.
    phase_configs: List[Dict[str, Union[int, bool]]] = field(default_factory=list)

    def __safe_get_common_phase_property(self, property_key: str, default_value: Union[int, bool]) -> Union[int, bool]:
        if not self.phase_configs or not self.phase_configs[0]:
            return default_value

        return self.phase_configs[0].get(property_key, default_value)

    @property
    def scatter_pack(self):
        return self.__safe_get_common_phase_property("is_scatter_pack", default_value=False)

    @property
    def num_msgs_in_block(self):
        return self.__safe_get_common_phase_property("num_msgs_in_block", default_value=-1)

    @property
    def num_iters_in_epoch(self):
        return self.__safe_get_common_phase_property("num_iters_in_epoch", default_value=1)

    @property
    def num_unroll_iter(self):
        return self.__safe_get_common_phase_property("num_unroll_iter", default_value=1)

    @property
    def num_phases(self):
        if not self.phase_configs:
            return 0

        return len(self.phase_configs) / self.num_unroll_iter

    @property
    def get_buffer_size_bytes(self):
        full_size = self.__safe_get_common_phase_property("buf_full_size_bytes", default_value=0)
        if full_size:
            return full_size

        return self.__safe_get_common_phase_property("buf_size", default_value=0)

    @property
    def get_tile_size(self):
        return self.__safe_get_common_phase_property("msg_size", default_value=0)

    @property
    def get_buf_space_available_ack_thr(self):
        return self.__safe_get_common_phase_property("buf_space_available_ack_thr", default_value=0)


@dataclass
class NcriscInfo:
    ncrisc_configs: Dict[str, Dict[int, Dict[str, Union[int, bool]]]] = field(default_factory=dict)

    is_merged_dram_unpacker_stream: bool = False

    def __safe_get_common_ncrisc_property(self, property_key: str, default_value: Union[int, bool]) -> Union[int, bool]:
        if not self.ncrisc_configs or not self.ncrisc_configs[0]:
            return default_value

        return self.ncrisc_configs[0].get(property_key, default_value)


    @property
    def dram_buf_read_chunk_size_tiles(self):
        return self.__safe_get_common_ncrisc_property("dram_buf_read_chunk_size_tiles", default_value=0)

    @property
    def dram_scatter_chunk_size_tiles(self):
        return self.__safe_get_common_ncrisc_property("dram_scatter_chunk_size_tiles", default_value=0)

    @property
    def dram_scatter_offsets_full_size(self):
        return self.__safe_get_common_ncrisc_property("dram_scatter_offsets_full_size", default_value=0)

    @property
    def total_readers(self):
        return self.__safe_get_common_ncrisc_property("total_readers", default_value=1)


@dataclass
class PipePerf:
    source_actual_bw: float
    source_required_bw: float
    dest_actual_bw: float
    dest_required_bw: float


@dataclass
class TestResults:
    # Id of the perf test.
    test_id: int

    # Performance results of the pipe between the target op and the feeder.
    perf_result: PipePerf

    # Target op data extracted from pipe grpah.
    pipe_graph_op_data: PipeGraphOpData

    packer_info: StreamInfo

    unpacker_info: StreamInfo

    # We assume all dram forks will have the same configuration.
    ncrisc_info: NcriscInfo


def extract_target_op_coords(list_of_cores) -> list:
    """Extracts the target op coordinates from the list of cores. The target op coordinates are the ones that have
    target_op in them. The coordinates are in the format of "x,y-target_op"."""

    target_op_coords = []
    for core in list_of_cores:
        core = core.split("-")[0]
        x, y = map(int, core.split(","))
        target_op_coords.append((x, y))

    return target_op_coords


def get_stream_phases(overlay_blob_path: str, test_id: int, cores: list, stream_id: int) -> list:
    """Returns list of phases of the unpacker stream of the target op."""
    phase_configs = []

    harvested_offset = 18  # just a hack to translate from netlist coordinates to NOC coordinates in harvested space
    # Assume single core is given for now
    stream_key = (
        f"chip_0__y_{harvested_offset + cores[0][0]}__x_{harvested_offset + cores[0][1]}__stream_id_{stream_id}"
    )
    try:
        with open(overlay_blob_path, "r") as f:
            overlay_blob = yaml.safe_load(f)

            # Go through blob and fill in stream_phase_configs
            for phase in overlay_blob.keys():
                if not phase.startswith("phase_"):
                    # we just want stream phases
                    continue
                phase_content = overlay_blob[phase]
                if stream_key in phase_content:
                    phase_configs.append(phase_content[stream_key])

    except:
        print(f"Failed to get phase configs for test {test_id}")

    return phase_configs


def get_stream_info(perf_results_dir: str, test_dir: str, test_id: int, cores: list, stream_id: int = 4) -> StreamInfo:
    """Returns StreamInfo for stream id on all given cores."""

    overlay_blob_path = os.path.join(perf_results_dir, test_dir, "temporal_epoch_0/overlay/blob.yaml")
    if not os.path.exists(overlay_blob_path):
        print(f"Overlay blob not found for test {test_id}")
        return StreamInfo()

    if not cores:
        return StreamInfo()

    # TODO read blob yaml once and pass it to all functions

    # for gather/fork we probably will want to keep StreamInfo for each core
    return StreamInfo(
        phase_configs=get_stream_phases(overlay_blob_path, test_id, cores, stream_id)
    )


def get_ncrisc_info(perf_results_dir: str, test_dir: str, test_id: int, cores: list, stream_ids: List[int]) -> StreamInfo:
    """Returns NcriscInfo fo the ncrisc reader on a given cores"""
    overlay_blob_path = os.path.join(perf_results_dir, test_dir, "temporal_epoch_0/overlay/blob.yaml")
    if not os.path.exists(overlay_blob_path):
        print(f"Overlay blob not found for test {test_id}")
        return NcriscInfo()

    if not cores:
        return NcriscInfo()

    harvested_offset = 18  # just a hack to translate from netlist coordinates to NOC coordinates in harvested space
    try:
        with open(overlay_blob_path, "r") as f:
            overlay_blob = yaml.safe_load(f)

            # Go through blob and fill in stream_phase_configs
            dram_blob = overlay_blob["dram_blob"]
            for stream_id in stream_ids:
                # Assume single core is given for now
                stream_key = (
                    f"chip_0__y_{harvested_offset + cores[0][0]}__x_{harvested_offset + cores[0][1]}__stream_id_{stream_id}"
                )

                if stream_key not in dram_blob:
                    continue

                is_merged_dram_unpacker = stream_id < 40 # if stream ID is not general purpose, then the two streams are merged
                return NcriscInfo(
                    is_merged_dram_unpacker_stream=is_merged_dram_unpacker,
                    ncrisc_configs=dram_blob[stream_key]
                )

    except:
        print(f"Failed to get ncrisc info for test {test_id}")

    return NcriscInfo()


def get_target_op_pipe_graph_properties(pipegen_yaml_path: str) -> int:
    """Returns epoch tiles of the unpacker stream of target op."""
    with open(pipegen_yaml_path, "r") as f:
        pipegen_yaml = yaml.load_all(f, yaml.FullLoader)

        for pipegen_yaml_segment in pipegen_yaml:
            for pg_entry_name, pg_entry_spec in pipegen_yaml_segment.items():
                if not pg_entry_name.startswith("buffer_"):
                    continue

                if "target_op" not in pg_entry_spec["md_op_name"]:
                    continue

                if pg_entry_spec["buffer_type"] != "unpacker":
                    continue

                return PipeGraphOpData(
                    epoch_tiles=pg_entry_spec["epoch_tiles"],
                    kernel_clear_granularity=pg_entry_spec["tile_clear_granularity"],
                    producer_scatter_gather_num_tiles=find_producer_scatter_gather_num_tiles(pipegen_yaml_path),
                )


def find_producer_scatter_gather_num_tiles(pipegen_yaml_path) -> int:
    """Returns the scatter gather tiles of the producer."""
    with open(pipegen_yaml_path, "r") as f:
        pipegen_yaml = yaml.load_all(f, yaml.FullLoader)

        for pipegen_yaml_segment in pipegen_yaml:
            for pg_entry_name, pg_entry_spec in pipegen_yaml_segment.items():
                if not pg_entry_name.startswith("buffer_"):
                    continue

                if "feeder_op" not in pg_entry_spec["md_op_name"]:
                    continue

                if pg_entry_spec["buffer_type"] != "packer":
                    continue

                return pg_entry_spec["scatter_gather_num_tiles"]


def get_target_op_pipe_graph_data(perf_results_dir: str, test_dir: str, test_id: int) -> int:
    """Returns epoch tiles attribute of the unpacker buffer of target op."""
    pipegen_yaml_path = os.path.join(perf_results_dir, test_dir, "temporal_epoch_0/overlay/pipegen.yaml")
    if not os.path.exists(pipegen_yaml_path):
        print(f"Pipegen yaml not found for test {test_id}")
        return PipeGraphOpData()

    return get_target_op_pipe_graph_properties(pipegen_yaml_path)


@dataclass
class PipePerfReportRow:
    csv_row: Dict[str, Any] = field(default_factory=dict)

    @property
    def op_name(self):
        return self.csv_row["op_name"]

    @property
    def operand(self):
        return self.csv_row["operand"]

    @property
    def source_dest(self):
        return self.csv_row["source/dest"]

    @property
    def required_kernel_bw(self):
        try:
            return float(self.csv_row["required_kernel_bw"])
        except:
            return 0

    @property
    def actual_pipe_bw(self):
        try:
            return float(self.csv_row["pipe_bw"])
        except:
            return 0

    def is_pipe_connected_to_target_op(self) -> bool:
        return "target_op" in self.op_name or "target_op" in self.source_dest

    def is_pipe_feeder_op_output(self) -> bool:
        return "feeder_op" in self.op_name and "target_op" in self.source_dest

    def is_pipe_target_op_input(self) -> bool:
        return "target_op" in self.op_name and "input-0" in self.operand


def get_perf_test_results_worker(test_dir: str, perf_results_dir: str, total_num_tests: int) -> Optional[TestResults]:
    """Returns test data for a single perf test."""
    if not test_dir.startswith("test_"):
        return None

    with processed_perf_test_count.get_lock():
        processed_perf_test_count.value += 1
        local_processed_perf_results_count = processed_perf_test_count.value

    test_id = int(test_dir.split("_")[1])
    pipe_perf_report_csv = os.path.join(
        perf_results_dir, test_dir, "perf_results", "analyzer_results", "test_op", "pipe_perf_report.csv"
    )
    print_progress_bar(current_iter=int(local_processed_perf_results_count), total_iter=total_num_tests)

    if not os.path.exists(pipe_perf_report_csv):
        print(f"Test {test_id} does not have perf analyzer results")
        return None

    pipe_perf_result = None
    with open(pipe_perf_report_csv, "r") as csv_file:
        reader = csv.DictReader(csv_file)

        source_actual_bw = 0
        source_required_bw = 0
        dest_actual_bw = 0
        dest_required_bw = 0

        for csv_row in reader:
            pipe_perf_data = PipePerfReportRow(csv_row)

            if not pipe_perf_data.is_pipe_connected_to_target_op():
                continue

            if pipe_perf_data.is_pipe_target_op_input():
                dest_required_bw = pipe_perf_data.required_kernel_bw
                dest_actual_bw = pipe_perf_data.actual_pipe_bw

            if pipe_perf_data.is_pipe_feeder_op_output():
                source_required_bw = pipe_perf_data.required_kernel_bw
                source_actual_bw = pipe_perf_data.actual_pipe_bw

        pipe_perf_result = PipePerf(
            source_actual_bw=source_actual_bw,
            source_required_bw=source_required_bw,
            dest_actual_bw=dest_actual_bw,
            dest_required_bw=dest_required_bw,
        )

    if pipe_perf_result is None:
        print(f"Test {test_id} does not have perf results")
        return None

    target_op_loc = extract_op_coords(
        "target_op",
        os.path.join(perf_results_dir, test_dir, "temporal_epoch_0/overlay/pipegen.yaml")
    )

    feeder_op_loc = extract_op_coords(
        "feeder_op",
        os.path.join(perf_results_dir, test_dir, "temporal_epoch_0/overlay/pipegen.yaml")
    )

    return TestResults(
        test_id=test_id,
        perf_result=pipe_perf_result,
        pipe_graph_op_data=get_target_op_pipe_graph_data(perf_results_dir, test_dir, test_id),
        unpacker_info=get_stream_info(perf_results_dir, test_dir, test_id, target_op_loc, 4),
        packer_info=get_stream_info(perf_results_dir, test_dir, test_id, feeder_op_loc, 24),
        ncrisc_info=get_ncrisc_info(perf_results_dir, test_dir, test_id, target_op_loc, [4, 40])
    )

def extract_op_coords(op_name: str, pipegen_yaml_path: str) -> list:
    """Extracts the coordinates of the op from the pipegen yaml. Returns them as [R, C]."""
    with open(pipegen_yaml_path, "r") as f:
        pipegen_yaml = yaml.load_all(f, yaml.FullLoader)

        coords = []
        for pipegen_yaml_segment in pipegen_yaml:
            for pg_entry_name, pg_entry_spec in pipegen_yaml_segment.items():
                if not pg_entry_name.startswith("buffer_"):
                    continue

                if op_name not in pg_entry_spec["md_op_name"]:
                    continue

                if pg_entry_spec["buffer_type"] != "unpacker":
                    continue

                coords.append(pg_entry_spec["core_coordinates"])

        return coords


def find_all_ran_tests(perf_results_dir: str) -> dict:
    """Goes through folders under perf_results_dir and finds all the test results. Each test has its folder named
    test_1, test_2,..., test_n. Inside each test folder, there is perf_results folder. Under perf_results folder, there
    is folder device. That folder is given to get_perf_results_from_test to get the performance results.
    """

    total_num_tests = len(os.listdir(perf_results_dir))
    print(f"Total number of tests: {total_num_tests}")

    # walk through all the directories under perf_results_dir, but not the files
    processed_perf_tests_sync_var = mp.Value("i", 0)
    with mp.Pool(initializer=init_processed_perf_test_count, initargs=(processed_perf_tests_sync_var,)) as pool:
        all_test_results = pool.starmap(
            get_perf_test_results_worker,
            zip(os.listdir(perf_results_dir), repeat(perf_results_dir), repeat(total_num_tests)),
        )

    return {res.test_id: res for res in all_test_results if res}


@dataclass
class SingleTestBW:
    sweep_vars_combination: Dict[str, Union[int, bool]]
    pipe_perf_result: PipePerf

    tile_size: int

    # unpacker related fields
    unpacker_num_phases: int
    unpacker_num_iters_in_epoch: int
    unpacker_num_unroll_iter: int
    unpacker_buffer_size_bytes: int
    buf_space_available_ack_thr: int
    kernel_clear_granularity: int
    epoch_tiles: int

    # packer related fields
    packer_num_phases: int
    packer_num_iters_in_epoch: int
    packer_num_unroll_iter: int
    packer_buffer_size_bytes: int
    packer_scatter_gather_num_tiles: int
    packer_num_msgs_in_block: int
    scatter_pack: bool

    # dram related fields
    dram_buf_read_chunk_size_tiles: int
    dram_scatter_chunk_size_tiles: int
    dram_scatter_offsets_full_size: int
    total_readers: int
    merged_dram_unpacker_stream: bool

    @staticmethod
    def get_csv_header_line() -> str:
        header_list = [
            "source_actual_bw",
            "dest_actual_bw",
            "source_required_bw",
            "dest_required_bw",
            "source_dest_bw_diff",
            "unpacker_buffer_size_bytes",
            "kernel_clear_granularity",
            "unpacker_num_iters_in_epoch",
            "unpacker_num_phases",
            "unpacker_num_unroll_iter",
            "buf_space_available_ack_thr",
            "epoch_tiles",
            "tile_size",
            "packer_buffer_size_bytes",
            "packer_scatter_gather_num_tiles",
            "packer_num_iters_in_epoch",
            "packer_num_phases",
            "packer_num_unroll_iter",
            "dram_buf_read_chunk_size_tiles",
            "dram_scatter_chunk_size_tiles",
            "dram_scatter_offsets_full_size",
            "total_readers",
            "scatter_pack",
            "merged_dram_unpacker_stream",
        ]
        return ",".join(header_list)

    def get_csv_string_line(self) -> str:
        entries_to_write = [
            self.pipe_perf_result.source_actual_bw,
            self.pipe_perf_result.dest_actual_bw,
            self.pipe_perf_result.source_required_bw,
            self.pipe_perf_result.dest_required_bw,
            self.pipe_perf_result.source_actual_bw - self.pipe_perf_result.dest_actual_bw,
            self.unpacker_buffer_size_bytes,
            self.kernel_clear_granularity,
            self.unpacker_num_iters_in_epoch,
            self.unpacker_num_phases,
            self.unpacker_num_unroll_iter,
            self.buf_space_available_ack_thr,
            self.epoch_tiles,
            self.tile_size,
            self.packer_buffer_size_bytes,
            self.packer_scatter_gather_num_tiles,
            self.packer_num_iters_in_epoch,
            self.packer_num_phases,
            self.packer_num_unroll_iter,
            self.dram_buf_read_chunk_size_tiles,
            self.dram_scatter_chunk_size_tiles,
            self.dram_scatter_offsets_full_size,
            self.total_readers,
            self.scatter_pack,
            self.merged_dram_unpacker_stream,
        ]

        return ",".join([str(x) for x in entries_to_write])


def match_sweep_var_combination_to_perf_results(
    all_test_results: Dict[int, TestResults],
    test_sweep_vars_combination: Dict[int, Dict[str, Union[int, bool]]],
):
    """all_test_results is a dictionary of test_id to perf_results, while test_sweep_vars_combination is a dictionary of
    test_id to sweep_vars_combination. Function combines the two dictionaries."""

    # mapping from test_id to SingleTestBW
    result = {}
    for test_id, test_result in all_test_results.items():
        pipe_perf_result = test_result.perf_result
        if test_id not in test_sweep_vars_combination:
            print(f"Test {test_id} not found in test_sweep_vars_combination")
            continue

        # TODO get more info from stream phases
        result[test_id] = SingleTestBW(
            unpacker_num_phases=test_result.unpacker_info.num_phases,
            unpacker_num_iters_in_epoch=test_result.unpacker_info.num_iters_in_epoch,
            unpacker_num_unroll_iter=test_result.unpacker_info.num_unroll_iter,
            unpacker_buffer_size_bytes=test_result.unpacker_info.get_buffer_size_bytes,
            buf_space_available_ack_thr=test_result.unpacker_info.get_buf_space_available_ack_thr,
            kernel_clear_granularity=test_result.pipe_graph_op_data.kernel_clear_granularity,
            packer_num_phases=test_result.packer_info.num_phases,
            packer_num_iters_in_epoch=test_result.packer_info.num_iters_in_epoch,
            packer_num_unroll_iter=test_result.packer_info.num_unroll_iter,
            packer_buffer_size_bytes=test_result.packer_info.get_buffer_size_bytes,
            packer_scatter_gather_num_tiles=test_result.pipe_graph_op_data.producer_scatter_gather_num_tiles,
            packer_num_msgs_in_block=(
                test_result.packer_info.num_msgs_in_block
                if test_result.packer_info.num_msgs_in_block
                else test_result.pipe_graph_op_data.producer_scatter_gather_num_tiles
            ),
            scatter_pack=test_result.packer_info.scatter_pack,
            tile_size=test_result.unpacker_info.get_tile_size,
            pipe_perf_result=pipe_perf_result,
            sweep_vars_combination=test_sweep_vars_combination[test_id],
            epoch_tiles=test_result.pipe_graph_op_data.epoch_tiles,
            dram_buf_read_chunk_size_tiles=test_result.ncrisc_info.dram_buf_read_chunk_size_tiles,
            dram_scatter_chunk_size_tiles=test_result.ncrisc_info.dram_scatter_chunk_size_tiles,
            dram_scatter_offsets_full_size=test_result.ncrisc_info.dram_scatter_offsets_full_size,
            total_readers=test_result.ncrisc_info.total_readers,
            merged_dram_unpacker_stream=test_result.ncrisc_info.is_merged_dram_unpacker_stream
        )
    return result


def generate_csv_report(per_test_bw, report_file):
    """Generates a CSV report from per_test_bw which is test id to SingleTestBW mapping."""

    # add following columns:
    # num phases, num msgs per phase, num iters in epoch, buf_size tiles, tile size

    # sort per worst_core_bw
    per_test_bw = dict(sorted(per_test_bw.items(), key=lambda item: item[1].pipe_perf_result.dest_actual_bw))

    with open(report_file, "w") as f:
        f.write(f"test_id,{SingleTestBW.get_csv_header_line()}")
        sweep_var_names = next(iter(per_test_bw.values())).sweep_vars_combination.keys()
        for sweep_var_name in sweep_var_names:
            f.write(f",{sweep_var_name}")
        f.write("\n")

        for test_id, single_test_bw in per_test_bw.items():
            f.write(f"{test_id},{single_test_bw.get_csv_string_line()}")
            for value in single_test_bw.sweep_vars_combination.values():
                f.write(f",{value}")
            f.write("\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Match sweep variables combinations to performance results",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument(
        "--pipe-type",
        help="Type of pipe to generate tests for",
    )

    parser.add_argument("--test-configs-yaml", help="YAML file containing test configurations", required=True)

    parser.add_argument(
        "--perf-test-results-dir",
        help="Directory containing performance test results",
        required=True,
    )
    parser.add_argument(
        "--output-csv",
        help="Output CSV file",
        default="perf_results.csv",
    )
    args = parser.parse_args()

    search_config = SolutionSearchConfig.create_from_search_type(
        search_type=SearchType.parallel_sweep,
        test_module_name=get_perf_test_module_name(DataMovementPerfTestType[args.pipe_type]),
        random_seed=0,
        max_num_configs=1,
        arch="wormhole_b0",
        verbose=False,
        log_to_file=False,
        clamp_to_max_num_configs=False,
        timeout=0,
        num_retries_on_timeout=0,
        enable_strategy=False,
        harvested_rows=2,
        run_compiler=False,
    )
    module = TestModuleWrapper.create_from_search_config(search_config=search_config)
    all_sweep_vars = []
    for svg in module.sweep_vars_list:
        all_sweep_vars.extend(svg.get_var_names_list())

    all_test_results = find_all_ran_tests(args.perf_test_results_dir)

    test_configs = match_test_id_to_sweep_vars(args.test_configs_yaml, all_sweep_vars)

    per_test_bw = match_sweep_var_combination_to_perf_results(all_test_results, test_configs)

    generate_csv_report(per_test_bw, args.output_csv)

    print("Done")
