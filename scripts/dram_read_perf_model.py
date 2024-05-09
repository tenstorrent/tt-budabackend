# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0
import csv
from itertools import repeat
from math import ceil, gcd
from numpy import lcm
import os
import multiprocessing as mp
from typing import Any, Dict, List, Tuple
from ruamel.yaml import YAML
from dataclasses import dataclass
import yaml
from enum import Enum


class PredictionResult(Enum):
    Good = 1
    Bad = 2
    Fail = 3


@dataclass
class Features:
    epoch_tiles: int
    tile_size: int
    kernel_clear_granularity: int
    unpacker_buffer_size_bytes: int
    dram_buf_read_chunk_size_tiles: int
    dram_scatter_chunk_size_tiles: int


@dataclass
class PerfPredictionDataEntry:
    test_id: int
    predited_bucket: int
    real_bucket: int
    real_bw: int
    features: Features


class PipeGraphNode:
    def __init__(self, pg_dict: dict) -> None:
        self.pg_dict = pg_dict

    def __getattr__(self, __name: str) -> Any:
        assert __name in self.pg_dict, "Failed to find  pipegen yaml entry"

        return self.pg_dict[__name]

    def __str__(self) -> str:
        return str(self.pg_dict)

    def __repr__(self) -> str:
        return str(self)


class PipeGraph:
    def __init__(self, pipe_graph: list) -> None:
        self.buffers = {}
        self.pipes = {}

        self.scatter_buf_to_parent_buf = {}

        # Parse buffers and pipes.
        for pipegen_yaml_entry in pipe_graph:
            for data_key, data in pipegen_yaml_entry.items():
                if data_key.startswith("buffer_"):
                    self.buffers[data["uniqid"]] = PipeGraphNode(data)

                if data_key.startswith("pipe_"):
                    self.pipes[data["id"]] = PipeGraphNode(data)

        # Expand scatter buffers.
        for buffer_id, buffer in self.buffers.items():
            for i in range(buffer.replicate):
                self.scatter_buf_to_parent_buf[
                    buffer_id + i * buffer.scatter_gather_num_tiles
                ] = buffer_id

    def get_buffer(self, buffer_id: int) -> PipeGraphNode:
        return self.buffers[self.scatter_buf_to_parent_buf[buffer_id]]

    def get_pipe(self, pipe_id: int) -> PipeGraphNode:
        return self.pipes[pipe_id]

    def find_buffers(self, func) -> List[PipeGraphNode]:
        result = []

        for buffer in self.buffers.values():
            if func(buffer):
                result.append(buffer)

        return result

    def find_pipes(self, func) -> List[PipeGraphNode]:
        result = []

        for pipe in self.pipes.values():
            if func(pipe):
                result.append(pipe)

        return result


def load_netlist(tt_build_path: str, test_id: int) -> dict:
    netlist_path = os.path.join(tt_build_path, f"test_{test_id}", f"netlist_{test_id}.yaml")

    yaml_reader = YAML(typ="rt")
    yaml_reader.width = 200

    return yaml_reader.load(open(netlist_path))


def load_pipe_graph(tt_build_path: str, test_id: int) -> PipeGraph:
    pipegen_yaml_path = os.path.join(
        tt_build_path,
        "tt_build_dir",
        f"test_{test_id}",
        "temporal_epoch_0",
        "overlay",
        "pipegen.yaml",
    )

    return PipeGraph(yaml.load_all(open(pipegen_yaml_path, "r"), yaml.FullLoader))


def get_input_queue_pipe_bw(pipe_perf_results_path: str) -> float:
    with open(pipe_perf_results_path, "r") as csv_file:
        reader = csv.DictReader(csv_file)
        for csv_row in reader:
            if csv_row["op_name"] != "target_op0":
                continue

            if csv_row["pipe_type"] != "input-queue-pipe":
                continue

            return float(csv_row["pipe_bw"])

    return 0


def compute_max_num_tiles_per_phase(start_value: int, root_tiles_per_input: int) -> int:
    common_divisor = start_value

    if root_tiles_per_input <= 2048:
        lcm_with_tiles_per_input = lcm(common_divisor, root_tiles_per_input)
        if lcm_with_tiles_per_input <= 2048:
            common_divisor = lcm_with_tiles_per_input

    return (2048 // common_divisor) * common_divisor


def compute_dram_scatter_chunk_size_tiles(pipe_graph: PipeGraph, pipe: PipeGraphNode) -> int:
    scatter_gather_num_tiles = 0
    for pipe_input in pipe.input_list:
        input_buffer = pipe_graph.get_buffer(pipe_input)
        scatter_gather_num_tiles = gcd(
            scatter_gather_num_tiles, input_buffer.scatter_gather_num_tiles
        )

    return scatter_gather_num_tiles


def compute_min_num_tiles_to_transfer(pipe_graph: PipeGraph, pipe: PipeGraphNode) -> int:
    min_chunk_size = 999999
    for pipe_input in pipe.input_list:
        min_chunk_size = min(
            min_chunk_size, pipe_graph.get_buffer(pipe_input).scatter_gather_num_tiles
        )

    return min_chunk_size


def is_transfer_chunk_size_within_limits(
    transfer_chunk_size_tiles: int,
    tile_size_bytes: int,
    max_transfer_size_tiles: int,
    max_transfer_size_bytes: int,
):
    return (
        transfer_chunk_size_tiles <= max_transfer_size_tiles
        and transfer_chunk_size_tiles * tile_size_bytes <= max_transfer_size_bytes
    )


def get_transfer_chunk_size_tiles(
    transfer_chunk_size_tiles: int,
    min_transfer_chunk_size_tiles: int,
    tile_size_bytes: int,
    max_transfer_size_tiles: int,
    max_transfer_size_bytes: int,
) -> int:
    if not is_transfer_chunk_size_within_limits(
        transfer_chunk_size_tiles, tile_size_bytes, max_transfer_size_tiles, max_transfer_size_bytes
    ):

        for div in range(2, transfer_chunk_size_tiles + 1):
            if transfer_chunk_size_tiles % div != 0:
                continue

            new_chunk_size = transfer_chunk_size_tiles // div
            if (
                new_chunk_size % min_transfer_chunk_size_tiles == 0
                and is_transfer_chunk_size_within_limits(
                    new_chunk_size,
                    tile_size_bytes,
                    max_transfer_size_tiles,
                    max_transfer_size_bytes,
                )
            ):
                transfer_chunk_size_tiles = new_chunk_size
                break

    if not is_transfer_chunk_size_within_limits(
        transfer_chunk_size_tiles, tile_size_bytes, max_transfer_size_tiles, max_transfer_size_bytes
    ):
        transfer_chunk_size_tiles = min_transfer_chunk_size_tiles

    return transfer_chunk_size_tiles


def compute_dram_buf_read_chunk_size_tiles(
    pipe_graph: PipeGraph,
    pipe: PipeGraphNode,
    kernel_clear_granularity: int,
    tiles_to_transfer: int,
    tile_size: int,
) -> int:
    min_chunk_size = compute_min_num_tiles_to_transfer(pipe_graph, pipe)

    phase_tiles = (2048 // kernel_clear_granularity) * kernel_clear_granularity
    chunk_size = gcd(phase_tiles, tiles_to_transfer)
    chunk_size = lcm(chunk_size, min_chunk_size)

    return get_transfer_chunk_size_tiles(
        chunk_size, min_chunk_size, tile_size, 64 * 1024 - 1, 52 * 1024
    )


def compute_merged_dram_unpacker_buffer_size_bytes(
    unpacker_buffer: PipeGraphNode, dram_buf_read_chunk_size_tiles: int
) -> int:
    tile_size = unpacker_buffer.tile_size
    dram_read_chunk_size = dram_buf_read_chunk_size_tiles * tile_size
    unpacker_clearing_size = unpacker_buffer.tile_clear_granularity * tile_size
    full_unpacker_buffer_size = unpacker_buffer.size_tiles * tile_size
    base_merged_buffer_size = lcm(dram_read_chunk_size, unpacker_clearing_size)

    return ceil(full_unpacker_buffer_size / base_merged_buffer_size) * base_merged_buffer_size


def scale_up_dram_unpacker_buffer(
    base_buffer_size_bytes: int, max_tiles_per_phase: int, tile_size: int
) -> int:
    max_num_tiles_per_phase_bytes = max_tiles_per_phase * tile_size
    scale_factor = 1

    min_buffer_size = 52 * 1024
    while (scale_factor + 1) * base_buffer_size_bytes < min_buffer_size:
        if (
            scale_factor * base_buffer_size_bytes >= min_buffer_size // 2
            and max_num_tiles_per_phase_bytes % (scale_factor * base_buffer_size_bytes) == 0
        ):
            break

        scale_factor += 1

    if scale_factor == 1 and (
        2 * base_buffer_size_bytes <= 100 * 1024
        and max_num_tiles_per_phase_bytes % (2 * base_buffer_size_bytes) == 0
    ):
        scale_factor = 2

    return scale_factor * base_buffer_size_bytes


def compute_dram_unpacker_buffer_size_bytes(
    dram_buffer: PipeGraphNode,
    unpacker_buffer: PipeGraphNode,
    dram_buf_read_chunk_size_tiles: int,
    max_tiles_per_phase: int,
) -> int:
    merged_buffer_size_bytes = compute_merged_dram_unpacker_buffer_size_bytes(
        unpacker_buffer, dram_buf_read_chunk_size_tiles
    )

    unpacker_buffer_size_bytes = unpacker_buffer.size_tiles * unpacker_buffer.tile_size
    can_merge_streams = merged_buffer_size_bytes <= max(100 * 1024, unpacker_buffer_size_bytes)

    tiles_to_transfer = unpacker_buffer.tiles_per_input
    if can_merge_streams:
        if tiles_to_transfer > 2048:
            max_tiles_per_phase = compute_max_num_tiles_per_phase(
                unpacker_buffer.size_tiles, dram_buffer.tiles_per_input
            )

        return scale_up_dram_unpacker_buffer(
            merged_buffer_size_bytes,
            max_tiles_per_phase,
            unpacker_buffer.tile_size,
        )
    else:
        return unpacker_buffer_size_bytes


def make_dram_read_bw_prediction(
    epoch_tiles: int,
    tile_size: int,
    kernel_clear_granularity: int,
    unpacker_buffer_size_bytes: int,
    dram_buf_read_chunk_size_tiles: int,
    dram_scatter_chunk_size_tiles: int,
) -> float:
    if dram_buf_read_chunk_size_tiles <= 6.50:
        if dram_buf_read_chunk_size_tiles <= 2.50:
            return 0
        else:  # if dram_buf_read_chunk_size_tiles > 2.50
            if tile_size <= 1600.00:
                if dram_scatter_chunk_size_tiles <= 1.50:
                    if epoch_tiles <= 127488.00:
                        return 0
                    else:  # if epoch_tiles > 127488.00
                        return 0
                else:  # if dram_scatter_chunk_size_tiles > 1.50
                    if dram_buf_read_chunk_size_tiles <= 4.50:
                        return 0
                    else:  # if dram_buf_read_chunk_size_tiles > 4.50
                        if epoch_tiles <= 896.00:
                            return 0
                        else:  # if epoch_tiles > 896.00
                            return 1
            else:  # if tile_size > 1600.00
                if dram_scatter_chunk_size_tiles <= 1.50:
                    return 1
                else:  # if dram_scatter_chunk_size_tiles > 1.50
                    if kernel_clear_granularity <= 3.50:
                        if epoch_tiles <= 5376.00:
                            return 1
                        else:  # if epoch_tiles > 5376.00
                            return 2
                    else:  # if kernel_clear_granularity > 3.50
                        if epoch_tiles <= 62208.00:
                            return 1
                        else:  # if epoch_tiles > 62208.00
                            return 2
    else:  # if dram_buf_read_chunk_size_tiles > 6.50
        if dram_buf_read_chunk_size_tiles <= 12.50:
            if tile_size <= 1600.00:
                if dram_buf_read_chunk_size_tiles <= 11.00:
                    if epoch_tiles <= 576.00:
                        return 0
                    else:  # if epoch_tiles > 576.00
                        if dram_scatter_chunk_size_tiles <= 9.50:
                            return 1
                        else:  # if dram_scatter_chunk_size_tiles > 9.50
                            return 1
                else:  # if dram_buf_read_chunk_size_tiles > 11.00
                    if epoch_tiles <= 5376.00:
                        return 1
                    else:  # if epoch_tiles > 5376.00
                        if epoch_tiles <= 25728.00:
                            return 2
                        else:  # if epoch_tiles > 25728.00
                            return 2
            else:  # if tile_size > 1600.00
                if dram_scatter_chunk_size_tiles <= 4.50:
                    if dram_buf_read_chunk_size_tiles <= 11.00:
                        return 2
                    else:  # if dram_buf_read_chunk_size_tiles > 11.00
                        return 3
                else:  # if dram_scatter_chunk_size_tiles > 4.50
                    if epoch_tiles <= 66048.00:
                        if epoch_tiles <= 544.00:
                            return 1
                        else:  # if epoch_tiles > 544.00
                            return 2
                    else:  # if epoch_tiles > 66048.00
                        if dram_scatter_chunk_size_tiles <= 11.00:
                            return 3
                        else:  # if dram_scatter_chunk_size_tiles > 11.00
                            return 3
        else:  # if dram_buf_read_chunk_size_tiles > 12.50
            if unpacker_buffer_size_bytes <= 64640.00:
                if unpacker_buffer_size_bytes <= 52080.00:
                    if dram_scatter_chunk_size_tiles <= 1.50:
                        if dram_buf_read_chunk_size_tiles <= 26.50:
                            return 1
                        else:  # if dram_buf_read_chunk_size_tiles > 26.50
                            return 2
                    else:  # if dram_scatter_chunk_size_tiles > 1.50
                        if epoch_tiles <= 19392.00:
                            return 2
                        else:  # if epoch_tiles > 19392.00
                            return 2
                else:  # if unpacker_buffer_size_bytes > 52080.00
                    if dram_scatter_chunk_size_tiles <= 19.50:
                        if kernel_clear_granularity <= 6.50:
                            return 3
                        else:  # if kernel_clear_granularity > 6.50
                            return 2
                    else:  # if dram_scatter_chunk_size_tiles > 19.50
                        if epoch_tiles <= 8064.00:
                            return 2
                        else:  # if epoch_tiles > 8064.00
                            return 2
            else:  # if unpacker_buffer_size_bytes > 64640.00
                if unpacker_buffer_size_bytes <= 76640.00:
                    if dram_scatter_chunk_size_tiles <= 1.50:
                        if tile_size <= 1600.00:
                            return 2
                        else:  # if tile_size > 1600.00
                            return 3
                    else:  # if dram_scatter_chunk_size_tiles > 1.50
                        if epoch_tiles <= 1536.00:
                            return 1
                        else:  # if epoch_tiles > 1536.00
                            return 4
                else:  # if unpacker_buffer_size_bytes > 76640.00
                    if dram_scatter_chunk_size_tiles <= 16.00:
                        if kernel_clear_granularity <= 4.50:
                            return 5
                        else:  # if kernel_clear_granularity > 4.50
                            return 2
                    else:  # if dram_scatter_chunk_size_tiles > 16.00
                        if epoch_tiles <= 10304.00:
                            return 3
                        else:  # if epoch_tiles > 10304.00
                            return 3


def get_dram_blob_fields(overlay_blob: dict) -> Tuple[int, int]:
    stream_ids_to_try = [4, 40]
    for stream_id in stream_ids_to_try:
        dram_stream_key = f"chip_0__y_18__x_18__stream_id_{stream_id}"
        if dram_stream_key in overlay_blob["dram_blob"]:
            ncrisc_config = overlay_blob["dram_blob"][dram_stream_key][0]
            return (
                ncrisc_config["dram_buf_read_chunk_size_tiles"],
                ncrisc_config["dram_scatter_chunk_size_tiles"],
            )

    return 0, 0


def get_unpacker_buffer_size(overlay_blob: dict) -> int:
    return overlay_blob["phase_0"]["chip_0__y_18__x_18__stream_id_4"]["buf_size"]


def compare_computed_and_actual_blob_fields(
    blob_yaml_path: str,
    unpacker_buffer_size_bytes: int,
    dram_scatter_chunk_size_tiles: int,
    dram_buf_read_chunk_size_tiles: int,
    test_id: int,
):
    with open(blob_yaml_path, "r") as f:
        overlay_blob = yaml.safe_load(f)

        chunk_size, scatter_chunk = get_dram_blob_fields(overlay_blob)
        if dram_buf_read_chunk_size_tiles != chunk_size:
            print(
                f"Expected to compute the exact read chunk size but got {dram_buf_read_chunk_size_tiles} isntead of {chunk_size}"
            )

        if dram_scatter_chunk_size_tiles != scatter_chunk:
            print(
                f"Expected to compute the exact scatter chunk size but got {dram_scatter_chunk_size_tiles} isntead of {scatter_chunk}"
            )

        buf_size = get_unpacker_buffer_size(overlay_blob)
        if unpacker_buffer_size_bytes != buf_size:
            print(
                f"Expected to compute the unpacker buffer size but got {unpacker_buffer_size_bytes} isntead of {buf_size} for test id {test_id}"
            )

        # print("Everything OK")


def estimate_dram_read_bw_without_fork(
    pipe_graph: PipeGraph,
    dest_buffer: PipeGraphNode,
    pipe: PipeGraphNode,
    blob_yaml_path: str,
    test_id: int,
) -> Tuple[float, Features]:
    dram_input_buffer = pipe_graph.get_buffer(pipe.input_list[0])
    max_tiles_per_phase = compute_max_num_tiles_per_phase(1, dram_input_buffer.tiles_per_input)

    dram_scatter_chunk_size_tiles = compute_dram_scatter_chunk_size_tiles(pipe_graph, pipe)
    dram_buf_read_chunk_size_tiles = compute_dram_buf_read_chunk_size_tiles(
        pipe_graph,
        pipe,
        dest_buffer.tile_clear_granularity,
        dest_buffer.tiles_per_input,
        dest_buffer.tile_size,
    )
    unpacker_buffer_size_bytes = compute_dram_unpacker_buffer_size_bytes(
        dram_input_buffer, dest_buffer, dram_buf_read_chunk_size_tiles, max_tiles_per_phase
    )

    compare_computed_and_actual_blob_fields(
        blob_yaml_path,
        unpacker_buffer_size_bytes,
        dram_scatter_chunk_size_tiles,
        dram_buf_read_chunk_size_tiles,
        test_id,
    )

    with open(blob_yaml_path, "r") as f:
        overlay_blob = yaml.safe_load(f)
        unpacker_buffer_size_bytes = get_unpacker_buffer_size(overlay_blob)

    # TODO: For now use the values read from blob yaml, later implement the logic to estimate these values
    bw_bucket = make_dram_read_bw_prediction(
        dest_buffer.epoch_tiles,
        dest_buffer.tile_size,
        dest_buffer.tile_clear_granularity,
        unpacker_buffer_size_bytes,
        dram_buf_read_chunk_size_tiles,
        dram_scatter_chunk_size_tiles,
    )

    return bw_bucket, Features(
        epoch_tiles=dest_buffer.epoch_tiles,
        tile_size=dest_buffer.tile_size,
        kernel_clear_granularity=dest_buffer.tile_clear_granularity,
        unpacker_buffer_size_bytes=unpacker_buffer_size_bytes,
        dram_buf_read_chunk_size_tiles=dram_buf_read_chunk_size_tiles,
        dram_scatter_chunk_size_tiles=dram_scatter_chunk_size_tiles,
    )


def find_pipe_feeding_dest_cores(
    pipe_graph: PipeGraph, dest_unp_buffer: List[PipeGraphNode]
) -> Dict[int, PipeGraphNode]:
    pipe_map = {}

    for dest_buf in dest_unp_buffer:

        def __is_pipe_feeding_dest(pipe: PipeGraphNode) -> bool:
            return dest_buf.uniqid in pipe.output_list

        pipes = pipe_graph.find_pipes(__is_pipe_feeding_dest)
        assert len(pipes) == 1, "Expecting to find exactly one pipe feeding dest buffer"

        pipe_map[dest_buf.uniqid] = pipes[0]

    return pipe_map


def estimate_fork_impact_on_bw(bw_no_fork: float, fork_factor: int) -> float:
    linear_cap = 20 / fork_factor
    theoretical_cap = 24 / fork_factor

    if bw_no_fork <= linear_cap or fork_factor == 1:
        return bw_no_fork

    dx = 24 - bw_no_fork
    dy = (bw_no_fork - linear_cap) * (theoretical_cap - linear_cap)

    return dy / dx + linear_cap


def predict_dram_read_bw(
    netlist_dict: dict, pipe_graph: PipeGraph, blob_yaml_path: str, test_id: int
) -> float:
    dest_unp_buffers = pipe_graph.find_buffers(
        lambda buf: buf.md_op_name == "target_op0" and buf.buffer_type == "unpacker"
    )

    pipe_per_dest_core = find_pipe_feeding_dest_cores(pipe_graph, dest_unp_buffers)

    bw_estimate_per_dest_core = {}
    for dest_buffer in dest_unp_buffers:
        feeder_pipe = pipe_per_dest_core[dest_buffer.uniqid]

        bw_estimate_no_fork, estimation_features = estimate_dram_read_bw_without_fork(
            pipe_graph, dest_buffer, feeder_pipe, blob_yaml_path, test_id
        )

        max_fork_factor = max(feeder_pipe.dram_pipe_total_readers)
        # move to abs bw range
        mid_bucket_bw = 2 * (2 * bw_estimate_no_fork + 1)
        corrected_bw_estimate = estimate_fork_impact_on_bw(mid_bucket_bw, max_fork_factor)

        bw_estimate_per_dest_core[dest_buffer.uniqid] = (
            corrected_bw_estimate // 4
        )  # back to bucket range

    return min(bw_estimate_per_dest_core.values())


def compare_dram_read_perf(perf_test_build_dir: str, test_id: int) -> PredictionResult:
    try:
        netlist_dict = load_netlist(perf_test_build_dir, test_id)
        pipe_grpah = load_pipe_graph(perf_test_build_dir, test_id)

        pipe_perf_results_path = os.path.join(
            perf_test_build_dir,
            "tt_build_dir",
            f"test_{test_id}",
            "perf_results",
            "analyzer_results",
            "test_op",
            "pipe_perf_report.csv",
        )
        real_bw = get_input_queue_pipe_bw(pipe_perf_results_path)

        blob_yaml_path = os.path.join(
            perf_test_build_dir,
            "tt_build_dir",
            f"test_{test_id}",
            "temporal_epoch_0",
            "overlay",
            "blob.yaml",
        )
        bw_bucket = predict_dram_read_bw(netlist_dict, pipe_grpah, blob_yaml_path, test_id)

        bw_range = [bw_bucket * 4, (bw_bucket + 1) * 4]

        if real_bw >= bw_range[0] and real_bw <= bw_range[1]:
            print("ESTIMATED CORRECT BW")
            return PredictionResult.Good, 0
        else:
            print("ESTIMATED WRONG BW")
            real_bw_bucket = real_bw // 4
            return PredictionResult.Bad, bw_bucket - real_bw_bucket

        # print(f"Test: {test_id} - predicted:} | real: {real_bw}")
    except:
        # print(f"Failed to compare perf results for test id: {test_id}")
        return PredictionResult.Fail, 0


def compare_predicted_and_real_perf(perf_test_dir: str, num_tests: int) -> None:
    with mp.Pool() as pool:
        bw_predictions = pool.starmap(
            compare_dram_read_perf, zip(repeat(perf_test_dir), range(num_tests))
        )

    good = 0
    bad = 0
    x = {}
    for prediction_result, bucket_missmatch in bw_predictions:
        if prediction_result == PredictionResult.Good:
            good += 1
        elif prediction_result == PredictionResult.Bad:
            bad += 1
            if bucket_missmatch not in x:
                x[bucket_missmatch] = 0
            x[bucket_missmatch] += 1

    print(f"Accurracy: {good} / {good + bad} ({100 * good / (good + bad)}) %")
    print(x)


def main():
    perf_test_dir = "test_perf_dram_large_data_set_all_combined"
    num_tests = 10224

    # perf_test_dir = "test_debug_perf_sweep"
    # num_tests = 1

    # perf_test_dir = "test_perf_dram_gather_fork_test_set_large"
    # num_tests = 650

    compare_predicted_and_real_perf(perf_test_dir, num_tests)


if __name__ == "__main__":
    main()
