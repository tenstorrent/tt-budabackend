# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Script which updates regular broadcast netlists to kernel broadcast netlists.

IMPORTANT:
In order to be able to run this script, update net2pipe.cpp
In function Net2Pipe::emit_pipes(YAML::Emitter &out) add the following piece of code
        if (pipe.has_consumer())
        {
            out << SET_KEY_VAL("consumer_name", pipe.consumer_name());
            out << SET_KEY_VAL("consumer_idx", pipe.consumer_input_index());
        }
This code is only for debug purposes (to make it easier to find pipes which feed the consumer op on
a given input index in pipegen.yaml) and thus won't be pushed to master.
"""

import argparse
import math
import multiprocessing as mp
import os
import random
import shutil
from itertools import repeat
from typing import Iterator, Optional

import yaml
from test_modules.common.compilation_pipeline_runner import (
    CompilationPipelineRunner,
    PipelineCompilationResult,
)

from verif.common.runner_net2pipe import Net2PipeRunner


def lcm(a, b):
    return abs(a * b) // math.gcd(a, b)


def extract_netlist_paths(test_dir: str) -> Iterator[str]:
    for netlist_dir in os.listdir(test_dir):
        # Skip everything which is not a directory.
        if not netlist_dir.startswith("test_") or not os.path.isdir(
            os.path.join(test_dir, netlist_dir)
        ):
            continue

        netlist_id = netlist_dir.split("_")[1]
        netlist_path = os.path.join(test_dir, netlist_dir, f"netlist_{netlist_id}.yaml")
        yield netlist_path


def get_pipe_graph(pipegen_yaml_path: str, consumer_op_name: str, input_idx: int) -> tuple:
    buffer_scatter_gather_num_tiles = {}
    pipes_feeding_consumer_op = []

    with open(pipegen_yaml_path, "r") as f:
        pipegen_yamls = yaml.load_all(f, yaml.FullLoader)

        for pipegen_yaml_entry in pipegen_yamls:
            for pipegen_yaml_entry_type, data in pipegen_yaml_entry.items():
                if pipegen_yaml_entry_type.startswith("pipe_"):
                    if "consumer_name" not in data or "consumer_idx" not in data:
                        raise RuntimeError(
                            f"consumer_name and consumer_idx keys have not been found in produced "
                            f"pipegen yaml. Try rebuilding net2pipe with the instructions above.?"
                        )
                    if (
                        data["consumer_name"] == consumer_op_name
                        and data["consumer_idx"] == input_idx
                    ):
                        pipes_feeding_consumer_op.append(data)
                elif pipegen_yaml_entry_type.startswith("buffer_"):
                    # Expand scatter buffers.
                    buffer_id = int(data["uniqid"])
                    replicate = int(data.get("replicate", 0))
                    scatter_gather_num_tiles = int(data.get("scatter_gather_num_tiles", 1))
                    for i in range(replicate):
                        buffer_scatter_gather_num_tiles[
                            buffer_id + i * scatter_gather_num_tiles
                        ] = scatter_gather_num_tiles

    return pipes_feeding_consumer_op, buffer_scatter_gather_num_tiles


def find_smallest_period(pipe_offsets: list) -> list:
    if len(pipe_offsets) == 0:
        return []

    period_candidate = []

    # Largest period can be half of the size of the original pipe offsets list.
    for i in range(0, len(pipe_offsets) // 2):
        period_candidate.append(pipe_offsets[i])

        # Size of the pipe offsets must be divisble by the period in order for it to be periodic.
        if len(pipe_offsets) % len(period_candidate) != 0:
            continue

        # If the period is repeated the required number of times and the original pipe offsets are reconstructed, 
        # then we have found the smallest period.
        period_repeat = len(pipe_offsets) // len(period_candidate)
        if pipe_offsets == period_candidate * period_repeat:
            return period_candidate

    return pipe_offsets


def compute_kernel_broadcast_factor(
    pipegen_yaml_path: str, consumer_op_name: str, input_idx: int
) -> tuple:
    kernel_broadcast_factor = 1
    kernel_broadcast_factor_upper_bound = 99999999
    pipes_feeding_consumer_op, buffer_scatter_gather_num_tiles = get_pipe_graph(
        pipegen_yaml_path, consumer_op_name, input_idx
    )

    assert pipes_feeding_consumer_op, "No pipes feeding the consumer op found."

    def __get_scatter_offsets_size_tiles(scatter_buffers) -> int:
        return sum(
            [buffer_scatter_gather_num_tiles[scatter_buffer] for scatter_buffer in scatter_buffers]
        )

    for pipe in pipes_feeding_consumer_op:
        num_unrolls = max(pipe["pipe_periodic_repeat"] - 1, 1)
        unrolled_input_list = sum(repeat(pipe["input_list"], num_unrolls), [])
        smallest_period = find_smallest_period(unrolled_input_list)
        kernel_broadcast_factor = lcm(
            kernel_broadcast_factor, __get_scatter_offsets_size_tiles(smallest_period)
        )
        kernel_broadcast_factor_upper_bound = min(
            kernel_broadcast_factor_upper_bound,
            __get_scatter_offsets_size_tiles(unrolled_input_list),
        )

    return kernel_broadcast_factor, kernel_broadcast_factor_upper_bound


def move_netlist_dir(netlist_yaml_path: str, destination_dir: Optional[str] = None) -> None:
    netlist_dir_path = os.path.dirname(netlist_yaml_path)
    if not destination_dir:
        shutil.rmtree(netlist_dir_path)
    else:
        shutil.move(netlist_dir_path, destination_dir, copy_function=shutil.copytree)


def update_netlist_with_kernel_broadcast_factor(
    netlist_path: str, kernel_broadcast_factor: int, input_idx: int
) -> None:
    with open(netlist_path, "r") as netlist_file:
        netlist_text = netlist_file.read()

    # Full replacement (i.e. if consumer op previously had no attributes).
    netlist_text = netlist_text.replace(
        f"# $TEMPLATE_kernel_broadcast_attribute_entire_spec",
        f"attributes: {{kernel_broadcast: {{input_{input_idx}: {kernel_broadcast_factor}}} }}",
    )

    # Partial replacement, for matmul/reduce ops.
    netlist_text = netlist_text.replace(
        f"# $TEMPLATE_kernel_bcast_attribute_partial",
        f"kernel_broadcast: {{input_{input_idx}: {kernel_broadcast_factor}}}",
    )

    with open(netlist_path, "w") as netlist_file:
        netlist_file.write(netlist_text)


def is_netlist_passing_backend_compilation(netlist_path: str, arch: str, out_dir: str) -> bool:
    netlist_dir = os.path.dirname(netlist_path)
    result: PipelineCompilationResult = CompilationPipelineRunner.run_net2pipe_pipegen_single_test(
        test_dir=os.path.dirname(netlist_dir),
        netlist_dir=os.path.basename(netlist_dir),
        out_dir=out_dir,
        arch=arch,
    )
    if not result.passed:
        print(f"Netlist: {netlist_path} failed to compile on backend. Reason:")
        print(result.message)
        error_log_file_path = os.path.join(netlist_dir, "error.log")
        with open(error_log_file_path, "w") as error_log_file:
            error_log_file.write(result.message)

    return result.passed


def get_kernel_broadcast_index(netlist_path: str) -> int:
    with open(netlist_path, "r") as netlist_file:
        netlist_dict = yaml.safe_load(netlist_file.read())
    return (
        netlist_dict.get("test-config", {})
        .get("test-generation-debug", {})
        .get("kernel_broadcast_index", 0)
    )


def run_fix_kernel_broadcast_factor_worker(
    netlist_path: str,
    out_dir: str,
    arch: str,
    consumer_op_name: str,
    invalid_netlists_dir: str,
    useless_kernel_broadcast_netlists_dir: str,
    invalid_kernel_broadcast_netlists_dir: str,
) -> None:
    netlist_id = os.path.basename(netlist_path).rstrip(".yaml")
    netlist_out_path = os.path.join(out_dir, netlist_id)
    # Optimization: skip net2pipe run if possible.
    if not os.path.exists(netlist_out_path):
        os.makedirs(netlist_out_path)
        Net2PipeRunner.run_net2pipe(netlist_path, netlist_out_path, arch)

    pipegen_yaml_path = os.path.join(
        netlist_out_path, "temporal_epoch_0", "overlay", "pipegen.yaml"
    )
    if not os.path.exists(pipegen_yaml_path):
        print(f"Running net2pipe failed on netlist: {netlist_path}, removing the netlist.")
        move_netlist_dir(netlist_path, destination_dir=invalid_netlists_dir)
        return

    input_idx = get_kernel_broadcast_index(netlist_path)
    kernel_broadcast_factor, kernel_broadcast_factor_upper_bound = compute_kernel_broadcast_factor(
        pipegen_yaml_path, consumer_op_name, input_idx
    )

    # TODO: Revise this
    if kernel_broadcast_factor >= kernel_broadcast_factor_upper_bound:
        move_netlist_dir(netlist_path, destination_dir=useless_kernel_broadcast_netlists_dir)
        return

    possible_kb_factors = range(
        kernel_broadcast_factor, kernel_broadcast_factor_upper_bound, kernel_broadcast_factor
    )
    kernel_broadcast_factor = random.choice(possible_kb_factors)
    update_netlist_with_kernel_broadcast_factor(netlist_path, kernel_broadcast_factor, input_idx)

    if not is_netlist_passing_backend_compilation(netlist_path, arch, out_dir):
        move_netlist_dir(netlist_path, destination_dir=invalid_kernel_broadcast_netlists_dir)
        return

    print(f"Finished processing netlist: {netlist_path}")


def make_dir(*args) -> str:
    dir_path = os.path.join(*args)
    os.makedirs(dir_path, exist_ok=True)
    return dir_path


def fix_kernel_broadcast_factors(test_dir: str, consumer_op_name: str, arch: str) -> None:
    out_dir = make_dir(test_dir, "debug_output", "net2pipe_output")
    invalid_netlists_dir = make_dir(test_dir, "debug_output", "invalid_netlists")
    useless_kernel_broadcast_netlists_dir = make_dir(
        test_dir, "debug_output", "useless_kernel_broadcast_netlists"
    )
    invalid_kernel_broadcast_netlists_dir = make_dir(
        test_dir, "debug_output", "invalid_kernel_broadcast_netlists"
    )

    netlist_path_iterator = extract_netlist_paths(test_dir)
    with mp.Pool() as pool:
        pool.starmap(
            run_fix_kernel_broadcast_factor_worker,
            zip(
                netlist_path_iterator,
                repeat(out_dir),
                repeat(arch),
                repeat(consumer_op_name),
                repeat(invalid_netlists_dir),
                repeat(useless_kernel_broadcast_netlists_dir),
                repeat(invalid_kernel_broadcast_netlists_dir),
            ),
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--test-dir", type=str, required=True, help="Dir containing test netlists.")
    parser.add_argument("--consumer", type=str, required=True, help="Consumer op name.")
    parser.add_argument(
        "--arch", type=str, required=True, help="Device architecture [wormhole_b0|grayskull]"
    )
    args = parser.parse_args()

    fix_kernel_broadcast_factors(args.test_dir, args.consumer, args.arch)


if __name__ == "__main__":
    main()
