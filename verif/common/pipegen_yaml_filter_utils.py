#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Utility methods and classes for pipegen yaml filters.
"""
from __future__ import annotations

from collections import deque
from copy import deepcopy
from enum import IntEnum
from typing import Callable, Optional, Tuple


class BufferLocation(IntEnum):
    DRAM = 1
    L1 = 2
    OTHER = 3


class BufferType(IntEnum):
    dram_prolog = 0
    dram_epilog = 1
    gradient_op = 2
    intermediate = 3
    packer = 4
    unpacker = 5
    dram_io = 6
    relay = 7
    ethernet_relay = 8
    prolog_relay = 9
    unknown = 10


class PrefetchType(IntEnum):
    PreTM = 0
    PostTM = 1


class GraphNode:
    def __init__(self):
        self.id: int = -1
        self.yaml_lines: list[str] = []


class BufferNode(GraphNode):
    OPERAND_INPUT_START_INDEX = 0
    OPERAND_OUTPUT_START_INDEX = 16
    OPERAND_INTERMEDIATES_START_INDEX = 24
    OPERAND_RELAY_START_INDEX = 32
    MAX_NUM_OPERANDS = 64

    def __init__(self):
        super().__init__()

        self.inputs: list[PipeNode] = []
        self.outputs: list[PipeNode] = []

        self.md_op_name: str = ""
        self.num_epoch_tiles: int = 0
        self.operand_id: int = 0
        self.dram_io_flag: bool = False
        self.dram_buf_flag: bool = False
        self.dram_buf_streaming: bool = False
        self.replicate: int = -1
        self.scatter_gather_num_tiles: int = -1
        self.core_x: int = -1
        self.core_y: int = -1
        self.buffer_type = BufferType.unknown
        self.chip_id: int = -1
        self.is_padding: int = 0
        self.use_ethernet_fw_stream: bool = False
        self.ethernet_chan: int = -1
        self.is_post_tm_relay_buf: bool = False

        self.is_scatter: bool = False
        # Parent buffer of the scatter group this buffer belongs to, if it is scatter buffer.
        self.scatter_group_parent: BufferNode = None
        # Set of unique pipes fed by all buffers in the scatter group this buffer belongs to,
        # filled only for scatter group parent.
        self.scatter_group_outputs: set[PipeNode] = set()

        self.untilized_output: bool = False
        self.buffer_space_shared: int = -1

        self.prefetch_type: PrefetchType = None

        self.tile_clear_granularity: int = 1

        # Epilogue flag.
        self.write_dram_buf_flag: bool = False

        self.embedding_index: bool = False
        self.embedding_table: bool = False

        self.hw_tilize: bool = False

    def __str__(self) -> str:
        return (
            f"Buffer\n{self.id}\n{self.md_op_name}\n"
            f"core [{self.core_x}, {self.core_y}]\n{self.buffer_type.name}"
        )

    def add_input_pipe(self, pipe: PipeNode):
        self.inputs.append(pipe)

    def add_output_pipe(self, pipe: PipeNode):
        self.outputs.append(pipe)
        if self.is_scatter:
            self.scatter_group_parent.scatter_group_outputs.add(pipe)

    def get_scatter_outputs(self) -> list[PipeNode]:
        if self.is_scatter:
            return list(self.scatter_group_parent.scatter_group_outputs)
        else:
            return self.outputs

    def is_root(self) -> bool:
        return len(self.inputs) == 0

    def is_forked(self) -> bool:
        return len(self.get_scatter_outputs()) > 1

    def get_location(self) -> BufferLocation:
        # TODO: Add more flags
        if self.dram_io_flag:
            return BufferLocation.DRAM
        else:
            return BufferLocation.L1

    def is_buf_dram_prefetch(self) -> bool:
        return (
            self.dram_buf_flag
            and not self.dram_buf_streaming
            and not self.is_buf_kernel_intermediate()
        )

    def is_buf_kernel_intermediate(self) -> bool:
        return len(self.inputs) == 0 and len(self.outputs) == 0 and not self.is_scatter

    def is_input_operand(self) -> bool:
        return (
            self.operand_id >= BufferNode.OPERAND_INPUT_START_INDEX
            and self.operand_id < BufferNode.OPERAND_OUTPUT_START_INDEX
        ) or self.is_intermediate_operand()

    def is_intermediate_operand(self) -> bool:
        return (
            self.operand_id >= BufferNode.OPERAND_INTERMEDIATES_START_INDEX
            and self.operand_id < BufferNode.OPERAND_RELAY_START_INDEX
        )

    def is_output_operand(self) -> bool:
        return (
            self.operand_id >= BufferNode.OPERAND_OUTPUT_START_INDEX
            and self.operand_id < BufferNode.OPERAND_RELAY_START_INDEX
        )


class PipeNode(GraphNode):
    def __init__(self):
        super().__init__()
        self.input_ids: list[int] = []
        self.output_ids: list[list[int]] = []
        self.output_padding_ids: list[int] = []
        self.inputs: list[BufferNode] = []
        self.outputs: list[list[BufferNode]] = []
        self.output_padding_list: list[BufferNode] = []
        self.consumer_repeat: int = 1
        self.periodic_repeat: int = 1
        self.mmio_pipe: bool = False
        self.output_padding_ids: list[int] = []
        self.direct_mcast: bool = False

    def __str__(self) -> str:
        return f"Pipe\n{self.id}"

    @staticmethod
    def __get_unique_buffers(buffers: list[BufferNode]) -> list[BufferNode]:
        unique_buffers = []
        visited_ids = set()
        for buffer in buffers:
            if buffer.id in visited_ids:
                continue
            unique_buffers.append(buffer)
            visited_ids.add(buffer.id)
        return unique_buffers

    def get_unique_inputs(self) -> list[BufferNode]:
        return PipeNode.__get_unique_buffers(self.inputs)

    def get_unique_outputs(self, scatter_idx: int) -> list[BufferNode]:
        return PipeNode.__get_unique_buffers(self.outputs[scatter_idx])

    def get_all_unique_outputs(self) -> list[BufferNode]:
        return PipeNode.__get_unique_buffers(sum(self.outputs, []))

    def get_scatter_outputs(self, scatter_idx: int = 0) -> list[BufferNode]:
        assert scatter_idx >= 0 and scatter_idx < len(self.outputs)
        return self.outputs[scatter_idx]

    def get_unique_scatter_outputs(self, scatter_idx: int = 0) -> list[BufferNode]:
        return PipeNode.__get_unique_buffers(self.get_scatter_outputs(scatter_idx))

    def get_unique_output_padding_buffers(self) -> list[BufferNode]:
        return PipeNode.__get_unique_buffers(self.output_padding_list)

    def is_multicast(self) -> bool:
        return len(self.outputs[0]) > 1

    def is_consumer_repeated(self) -> bool:
        return self.consumer_repeat > 1


class PipeGraph:
    BUFFER_PREFIX = "buffer_"
    PIPE_PREFIX = "pipe_"
    DELIMITER_PREFIX = "--"

    def __init__(self):
        self.buffers: list[BufferNode] = []
        self.buffers_map = {}
        self.pipes: list[PipeNode] = []
        self.pipes_map = {}

    def add_buffer(self, buffer: BufferNode, throw_if_exist: bool):
        if buffer.id in self.buffers_map:
            if throw_if_exist:
                raise Exception(
                    f"Buffer with same id ({buffer.id}) already exists in graph!"
                )
            return
        self.buffers.append(buffer)
        self.buffers_map[buffer.id] = buffer

    def add_pipe(self, pipe: PipeNode, throw_if_exist: bool):
        if pipe.id in self.pipes_map:
            if throw_if_exist:
                raise Exception(
                    f"Pipe with same id ({pipe.id}) already exists in graph!"
                )
            return
        self.pipes.append(pipe)
        self.pipes_map[pipe.id] = pipe

    @staticmethod
    def parse_attribute(yaml_line: str) -> Tuple[str, str]:
        values = yaml_line.split(":")
        assert len(values) == 2
        return values[0].strip(), values[1].strip()

    @staticmethod
    def parse_int_list(attr_value: str) -> list[int]:
        assert (
            len(attr_value) >= 2
            and attr_value.startswith("[")
            and attr_value.endswith("]")
        )
        return eval(attr_value)

    @staticmethod
    def parse_nested_int_list(attr_value: str) -> list[list[int]]:
        assert (
            len(attr_value) >= 4
            and attr_value.startswith("[[")
            and attr_value.endswith("]]")
        )
        return eval(attr_value)

    def parse_buffer_list(self, buffer_ids: list[int]) -> list[BufferNode]:
        buffer_list = []
        for buffer_id in buffer_ids:
            buffer_list.append(self.buffers_map[buffer_id])
        return buffer_list

    def parse_nested_buffer_list(
        self, buffer_ids: list[list[int]]
    ) -> list[list[BufferNode]]:
        buffer_list = []
        for scatter_ids in buffer_ids:
            scatter_list = []
            for buffer_id in scatter_ids:
                scatter_list.append(self.buffers_map[buffer_id])
            buffer_list.append(scatter_list)
        return buffer_list

    def add_buffer_from_yaml(self, yaml_lines: list[str]):
        buffer = BufferNode()
        for yaml_line in yaml_lines[1:]:
            attr_name, attr_value = PipeGraph.parse_attribute(yaml_line)
            if attr_name == "uniqid":
                buffer.id = int(attr_value)
            elif attr_name == "md_op_name":
                buffer.md_op_name = (
                    "relay_core" if attr_value.isnumeric() else attr_value
                )
            elif attr_name == "id":
                buffer.operand_id = int(attr_value)
            elif attr_name == "epoch_tiles":
                buffer.num_epoch_tiles = int(attr_value)
            elif attr_name == "dram_io_flag":
                buffer.dram_io_flag = attr_value == "1"
            elif attr_name == "dram_buf_flag":
                buffer.dram_buf_flag = attr_value == "1"
            elif attr_name == "dram_buf_streaming":
                buffer.dram_buf_streaming = attr_value == "1"
            elif attr_name == "is_scatter":
                buffer.is_scatter = attr_value == "1"
            elif attr_name == "scatter_gather_num_tiles":
                buffer.scatter_gather_num_tiles = int(attr_value)
            elif attr_name == "replicate":
                buffer.replicate = int(attr_value)
            elif attr_name == "core_coordinates":
                core_coordinates = PipeGraph.parse_int_list(attr_value)
                assert len(core_coordinates) == 2
                buffer.core_y = core_coordinates[0]
                buffer.core_x = core_coordinates[1]
            elif attr_name == "buffer_space_shared":
                buffer.buffer_space_shared = int(attr_value)
            elif attr_name == "buffer_type":
                buffer.buffer_type = BufferType[attr_value]
            elif attr_name == "untilized_output":
                buffer.untilized_output = bool(int(attr_value))
            elif attr_name == "prefetch_type":
                buffer.prefetch_type = (
                    PrefetchType.PreTM if int(attr_value) == 0 else PrefetchType.PostTM
                )
            elif attr_name == "tile_clear_granularity":
                buffer.tile_clear_granularity = int(attr_value)
            elif attr_name == "write_dram_buf_flag":
                buffer.write_dram_buf_flag = bool(int(attr_value))
            elif attr_name == "chip_id":
                chip_ids = PipeGraph.parse_int_list(attr_value)
                assert len(chip_ids) == 1
                buffer.chip_id = chip_ids[0]
            elif attr_name == "embedding_index":
                buffer.embedding_index = bool(int(attr_value))
            elif attr_name == "embedding_table":
                buffer.embedding_table = bool(int(attr_value))
            elif attr_name == "hw_tilize":
                buffer.hw_tilize = bool(int(attr_value))
            elif attr_name == "is_padding":
                buffer.is_padding = bool(int(attr_value))
            elif attr_name == "ethernet_chan":
                buffer.ethernet_chan = int(attr_value)
            elif attr_name == "is_post_tm_relay_buf":
                buffer.is_post_tm_relay_buf = bool(int(attr_value))
        buffer.yaml_lines = yaml_lines
        self.add_buffer(buffer, True)

    def add_pipe_from_yaml(self, yaml_lines: list[str]):
        pipe = PipeNode()
        for yaml_line in yaml_lines[1:]:
            attr_name, attr_value = PipeGraph.parse_attribute(yaml_line)
            if attr_name == "id":
                pipe.id = int(attr_value)
            elif attr_name == "input_list":
                pipe.input_ids = PipeGraph.parse_int_list(attr_value)
            elif attr_name == "output_padding_list":
                pipe.output_padding_ids = PipeGraph.parse_int_list(attr_value)
            elif attr_name == "output_list":
                if not attr_value.startswith("[["):
                    attr_value = f"[{attr_value}]"
                pipe.output_ids = PipeGraph.parse_nested_int_list(attr_value)
            elif attr_name == "pipe_consumer_repeat":
                pipe.consumer_repeat = max(1, int(attr_value))
            elif attr_name == "pipe_periodic_repeat":
                pipe.periodic_repeat = max(1, int(attr_value))
            elif attr_name == "mmio_pipe":
                pipe.mmio_pipe = bool(int(attr_value))
            elif attr_name == "output_padding_list":
                pipe.output_padding_ids = PipeGraph.parse_int_list(attr_value)
            elif attr_name == "direct_mcast":
                pipe.direct_mcast = bool(int(attr_value))
        pipe.yaml_lines = yaml_lines
        self.add_pipe(pipe, True)

    def add_node_from_yaml(self, yaml_lines: list[str]):
        if yaml_lines[0].startswith(PipeGraph.BUFFER_PREFIX):
            self.add_buffer_from_yaml(yaml_lines)
        elif yaml_lines[0].startswith(PipeGraph.PIPE_PREFIX):
            self.add_pipe_from_yaml(yaml_lines)
        else:
            raise Exception("Found graph node other than buffer and pipe!")

    def parse_from_yaml(self, yaml_path: str):
        with open(yaml_path, "r") as yaml_reader:
            yaml_lines = []
            line = yaml_reader.readline()
            while line:
                if not (
                    line.startswith(PipeGraph.BUFFER_PREFIX)
                    or line.startswith(PipeGraph.PIPE_PREFIX)
                    or len(yaml_lines) > 0
                ):
                    # Skipping lines outside of buffer and pipe definitions.
                    line = yaml_reader.readline()
                    continue
                elif line.startswith(PipeGraph.DELIMITER_PREFIX):
                    assert len(yaml_lines) > 0
                    self.add_node_from_yaml(yaml_lines)
                    yaml_lines = []
                else:
                    yaml_lines.append(line)
                line = yaml_reader.readline()
            if yaml_lines:
                self.add_node_from_yaml(yaml_lines)

    def expand_scatter_buffers(self):
        scatter_buffers = []
        for buffer in self.buffers:
            if buffer.is_scatter:
                scatter_buffers.append(buffer)

        for buffer in scatter_buffers:
            buffer.scatter_group_parent = buffer
            for i in range(1, buffer.replicate):
                new_buffer = deepcopy(buffer)
                new_buffer.id = buffer.id + i * buffer.scatter_gather_num_tiles
                new_buffer.scatter_group_parent = buffer
                self.add_buffer(new_buffer, True)

    def find_pipes_connections(self):
        for pipe in self.pipes:
            pipe.inputs = self.parse_buffer_list(pipe.input_ids)
            pipe.output_padding_list = self.parse_buffer_list(
                [x for x in pipe.output_padding_ids if x > 0]
            )
            for buffer in pipe.get_unique_inputs():
                buffer.add_output_pipe(pipe)
            for buffer in pipe.get_unique_output_padding_buffers():
                buffer.add_output_pipe(pipe)
            pipe.outputs = self.parse_nested_buffer_list(pipe.output_ids)
            for buffer in pipe.get_all_unique_outputs():
                buffer.add_input_pipe(pipe)

    @staticmethod
    def write_node_yaml_lines(yaml_writer, yaml_lines: list[str]):
        if len(yaml_lines) == 0:
            return
        yaml_writer.writelines(yaml_lines)
        if not yaml_lines[-1].endswith("\n"):
            yaml_writer.write("\n")
        yaml_writer.write("---\n")

    def write_to_blob_yaml(self, yaml_path: str):
        with open(yaml_path, "w") as yaml_writer:
            written_buffers = set()
            for buffer in self.buffers:
                buffer_to_write = (
                    buffer.scatter_group_parent if buffer.is_scatter else buffer
                )
                if buffer_to_write not in written_buffers:
                    PipeGraph.write_node_yaml_lines(
                        yaml_writer, buffer_to_write.yaml_lines
                    )
                    written_buffers.add(buffer_to_write)

            for pipe in self.pipes:
                PipeGraph.write_node_yaml_lines(yaml_writer, pipe.yaml_lines)


def add_pipe_connections(pipe: PipeNode, pipe_graph: PipeGraph, visited_node_ids: set):
    if pipe.id in visited_node_ids:
        return

    pipe_graph.add_pipe(pipe, True)
    visited_node_ids.add(pipe.id)

    for buffer in pipe.get_unique_inputs():
        add_buffer_connections(buffer, pipe_graph, visited_node_ids)
    for buffer in pipe.get_all_unique_outputs():
        add_buffer_connections(buffer, pipe_graph, visited_node_ids)
    for buffer in pipe.get_unique_output_padding_buffers():
        add_buffer_connections(buffer, pipe_graph, visited_node_ids)


def add_buffer_connections(
    buffer: BufferNode, pipe_graph: PipeGraph, visited_node_ids: set
):
    if buffer.id in visited_node_ids:
        return

    buf_queue = deque()
    buf_queue.append(buffer)
    while buf_queue:
        curr_buffer = buf_queue.popleft()
        if curr_buffer.id in visited_node_ids:
            continue
        pipe_graph.add_buffer(curr_buffer, True)
        visited_node_ids.add(curr_buffer.id)

        add_unvisited_pipes(curr_buffer.inputs, pipe_graph, visited_node_ids, buf_queue)
        add_unvisited_pipes(
            curr_buffer.get_scatter_outputs(), pipe_graph, visited_node_ids, buf_queue
        )
        add_unvisited_pipes(
            curr_buffer.outputs, pipe_graph, visited_node_ids, buf_queue
        )


def add_unvisited_pipes(
    pipe_list: list[PipeNode],
    pipe_graph: PipeGraph,
    visited_node_ids: set,
    buf_queue: deque,
):
    for pipe in pipe_list:
        if pipe.id in visited_node_ids:
            continue
        pipe_graph.add_pipe(pipe, True)
        visited_node_ids.add(pipe.id)
        for pipe_buf in pipe.get_unique_inputs():
            buf_queue.append(pipe_buf)
        for pipe_buf in pipe.get_unique_output_padding_buffers():
            buf_queue.append(pipe_buf)
        for pipe_buf in pipe.get_all_unique_outputs():
            buf_queue.append(pipe_buf)


def filter_graph_by_buffers(
    pipe_graph: PipeGraph, buffer_condition: Callable[[BufferNode], bool]
):
    filtered_pipe_graph = PipeGraph()

    visited_node_ids = set()
    for buffer in pipe_graph.buffers:
        if buffer_condition(buffer):
            add_buffer_connections(buffer, filtered_pipe_graph, visited_node_ids)

    return filtered_pipe_graph


def filter_graph_by_pipes(
    pipe_graph: PipeGraph, pipe_condition: Callable[[PipeNode], bool]
):
    filtered_pipe_graph = PipeGraph()

    visited_node_ids = set()
    for pipe in pipe_graph.pipes:
        if pipe_condition(pipe):
            add_pipe_connections(pipe, filtered_pipe_graph, visited_node_ids)

    return filtered_pipe_graph


def find_max_graph_depth(src_buf: BufferNode, depth: int = 0):
    """
    Returns maximal graph depth (in number of connected pipes) starting from
    the given source buffer.
    """
    max_depth = depth
    for pipe in src_buf.outputs:
        for buffer in pipe.get_all_unique_outputs():
            # We shouldn't have such a large graph that recursion overflows the stack.
            max_depth = max(max_depth, find_max_graph_depth(buffer, depth + 1))

    return max_depth


def does_pipe_have_input_padding(pipe: PipeNode) -> bool:
    return any(in_buffer.is_padding for in_buffer in pipe.inputs)


def does_pipe_have_output_padding(pipe: PipeNode) -> bool:
    return sum(pipe.output_padding_ids) > 0


def does_pipe_have_padding(pipe: PipeNode) -> bool:
    return does_pipe_have_input_padding(pipe) or does_pipe_have_output_padding(pipe)


def gathering_from_diff_cores(pipe: PipeNode) -> bool:
    core_x = pipe.inputs[0].core_x
    core_y = pipe.inputs[0].core_y
    for buffer in pipe.inputs[1:]:
        if buffer.core_y != core_y or buffer.core_x != core_x:
            return True
    return False


def gathering_from_diff_buffers(pipe: PipeNode) -> bool:
    # We could figure this out by decomposing buffers IDs and comparing the base parts, but choosing
    # to do it this way instead to avoid being dependent on ID structure coming from net2pipe.
    unique_parent_buf_ids = set()
    for buf in pipe.inputs:
        unique_parent_buf_ids.add(
            buf.scatter_group_parent.id if buf.is_scatter else buf.id
        )
    return len(unique_parent_buf_ids) > 1


def has_repeated_inputs(pipe: PipeNode) -> bool:
    return len(pipe.get_unique_inputs()) < len(pipe.inputs)


def has_repeated_outputs(pipe: PipeNode) -> bool:
    for idx, scatter_outputs in enumerate(pipe.outputs):
        if len(pipe.get_unique_outputs(idx)) < len(scatter_outputs):
            return True
    return False


def has_input_of_type(pipe: PipeNode, buffer_type: BufferType) -> bool:
    for buf in pipe.inputs:
        if buf.buffer_type == buffer_type:
            return True
    return False


def has_output_of_type(pipe: PipeNode, buffer_type: BufferType) -> bool:
    for buf in pipe.get_all_unique_outputs():
        if buf.buffer_type == buffer_type:
            return True
    return False


def is_mixed_inputs_pipe(pipe: PipeNode) -> bool:
    return has_input_of_type(pipe, BufferType.dram_io) and has_input_of_type(
        pipe, BufferType.packer
    )


def is_mixed_outputs_pipe(pipe: PipeNode) -> bool:
    return has_output_of_type(pipe, BufferType.dram_io) and has_output_of_type(
        pipe, BufferType.unpacker
    )


def is_1_to_1_pipe(pipe: PipeNode) -> bool:
    return (
        len(pipe.inputs) == 1
        and len(pipe.inputs[0].inputs) == 0
        and len(pipe.inputs[0].outputs) == 1
        and len(pipe.outputs) == 1
        and len(pipe.outputs[0]) == 1
        and len(pipe.outputs[0][0].outputs) == 0
        and pipe.inputs[0].buffer_type == BufferType.dram_io
        and pipe.outputs[0][0].buffer_type == BufferType.unpacker
    )


def is_n_to_1_pipe(pipe: PipeNode) -> bool:
    if len(pipe.inputs) < 2:
        return False

    pipe_scatter_inputs = set()
    for buf in pipe.inputs:
        if buf.scatter_group_parent:
            pipe_scatter_inputs.add(buf.scatter_group_parent.id)
        else:
            pipe_scatter_inputs.add(buf.id)
    if len(pipe_scatter_inputs) < 2:
        return False

    return (
        len(pipe.inputs) == 2
        and len(pipe.inputs[0].inputs) == 0
        and len(pipe.inputs[0].outputs) == 1
        and len(pipe.inputs[1].inputs) == 0
        and len(pipe.inputs[1].outputs) == 1
        and len(pipe.outputs) == 1
        and len(pipe.outputs[0]) == 1
        and len(pipe.outputs[0][0].outputs) == 0
        and pipe.inputs[0].buffer_type == BufferType.packer
        and pipe.outputs[0][0].buffer_type == BufferType.unpacker
    )


def is_gathering_to_dram(pipe: PipeNode) -> bool:
    return (
        len(pipe.inputs) > 1
        and len(pipe.outputs) == 1
        and len(pipe.outputs[0]) == 1
        and pipe.outputs[0][0].buffer_type == BufferType.dram_io
    )


def is_1_to_n_pipe(pipe: PipeNode) -> bool:
    return (
        len(pipe.inputs) == 1
        and len(pipe.inputs[0].inputs) == 0
        and len(pipe.inputs[0].outputs) == 1
        and len(pipe.outputs) == 1
        and len(pipe.outputs[0]) == 2
        and len(pipe.outputs[0][0].outputs) == 0
        and len(pipe.outputs[0][1].outputs) == 0
        and pipe.inputs[0].buffer_type == BufferType.packer
        and pipe.outputs[0][0].buffer_type == BufferType.unpacker
    )


def is_multicast_to_dram_pipe(pipe: PipeNode) -> bool:
    for buffers in pipe.outputs:
        if len(buffers) < 2:
            continue
        for buffer in buffers:
            if buffer.buffer_type == BufferType.dram_io:
                return True
    return False


def is_packer_specific_fork(buffer: BufferNode) -> bool:
    if not (
        buffer.buffer_type == BufferType.dram_io
        and not buffer.is_scatter
        and len(buffer.outputs) == 2
    ):
        return False
    dram_io = False
    unpacker = False
    for pipe in buffer.outputs:
        dram_io = dram_io or has_output_of_type(pipe, BufferType.dram_io)
        unpacker = unpacker or has_output_of_type(pipe, BufferType.unpacker)
    return unpacker and not dram_io


def is_dram_specific_fork(buffer: BufferNode) -> bool:
    if not (
        buffer.buffer_type == BufferType.dram_io
        and len(buffer.get_scatter_outputs()) == 2
    ):
        return False
    for pipe in buffer.get_scatter_outputs():
        if len(pipe.inputs) != 1:
            return False
    dram_io = False
    unpacker = False
    for pipe in buffer.outputs:
        dram_io = dram_io or has_output_of_type(pipe, BufferType.dram_io)
        unpacker = unpacker or has_output_of_type(pipe, BufferType.unpacker)
    return unpacker and not dram_io


def is_n_to_n_pipe(pipe: PipeNode) -> bool:
    return (
        len(pipe.inputs) == 2
        and len(pipe.inputs[0].inputs) == 0
        and len(pipe.inputs[0].outputs) == 1
        and len(pipe.inputs[1].inputs) == 0
        and len(pipe.inputs[1].outputs) == 1
        and len(pipe.outputs) == 1
        and len(pipe.outputs[0]) == 2
        and len(pipe.outputs[0][0].outputs) == 0
        and len(pipe.outputs[0][1].outputs) == 0
        and pipe.inputs[0].buffer_type == BufferType.dram_io
        and pipe.outputs[0][0].buffer_type == BufferType.unpacker
    )


def is_1_to_1_to_n_pipe(pipe: PipeNode) -> bool:
    return (
        len(pipe.inputs) == 1
        and len(pipe.inputs[0].inputs) == 0
        and len(pipe.inputs[0].outputs) == 1
        and len(pipe.outputs) == 2
        and len(pipe.outputs[0]) == 1
        and len(pipe.outputs[0][0].outputs) == 0
        and pipe.consumer_repeat == 1
    )


def is_1_to_n_to_n_pipe(pipe: PipeNode) -> bool:
    return (
        len(pipe.inputs) == 1
        and len(pipe.inputs[0].inputs) == 0
        and len(pipe.inputs[0].outputs) == 1
        and len(pipe.outputs) > 1
        and len(pipe.outputs[0]) == 2
        and len(pipe.outputs[0][0].outputs) == 0
        and len(pipe.outputs[0][1].outputs) == 0
        and pipe.consumer_repeat == 2
    )


def is_n_to_1_to_n_pipe(pipe: PipeNode) -> bool:
    return (
        len(pipe.inputs) > 1
        and len(pipe.inputs[0].inputs) == 0
        and len(pipe.inputs[0].outputs) == 1
        and len(pipe.inputs[1].inputs) == 0
        and len(pipe.inputs[1].outputs) == 1
        and len(pipe.outputs) > 1
        and len(pipe.outputs[0]) == 1
        and len(pipe.outputs[0][0].outputs) == 0
        and pipe.consumer_repeat == 1
    )


def is_n_to_n_to_n_pipe(pipe: PipeNode) -> bool:
    return (
        len(pipe.inputs) == 2
        and len(pipe.inputs[0].inputs) == 0
        and len(pipe.inputs[0].outputs) == 1
        and len(pipe.inputs[1].inputs) == 0
        and len(pipe.inputs[1].outputs) == 1
        and len(pipe.outputs) == 2
        and len(pipe.outputs[0]) == 2
        and len(pipe.outputs[0][0].outputs) == 0
        and len(pipe.outputs[0][1].outputs) == 0
    )


def is_pipe_scattering_to_dram(pipe: PipeNode) -> bool:
    return len(pipe.outputs) > 1 and has_output_of_type(pipe, BufferType.dram_io)


def is_pipe_scattering_from_dram(pipe: PipeNode) -> bool:
    return len(pipe.outputs) > 1 and has_input_of_type(pipe, BufferType.dram_io)


def are_all_buffers_on_same_chip(pipe: PipeNode) -> bool:
    inputs_chip_id = pipe.inputs[0].chip_id

    for buf in pipe.inputs:
        if buf.chip_id != inputs_chip_id:
            return False

    for out_bufs in pipe.outputs:
        for buf in out_bufs:
            if buf.chip_id != inputs_chip_id:
                return False

    return True


def is_unicast_rg_pipe(pipe: PipeNode) -> bool:
    # Check if it has single output and it is unpacker
    if (
        len(pipe.outputs) > 1
        or len(pipe.outputs[0]) > 1
        or pipe.outputs[0][0].buffer_type != BufferType.unpacker
    ):
        return False

    # Check if it has no repeats for now, later we might enable this.
    if pipe.consumer_repeat > 1 or pipe.periodic_repeat > 1:
        return False

    # Check if all inputs are packer and not forked
    for buf in pipe.inputs:
        if buf.buffer_type != BufferType.packer or buf.is_forked():
            return False

        # TODO: Support in the future
        if buf.buffer_space_shared != -1:
            return False

    # Check if all buffers are on the same chip
    if not are_all_buffers_on_same_chip(pipe):
        return False

    # Check if it has single input buffer (which can be scattered)
    if gathering_from_diff_buffers(pipe):
        return False

    return True


def is_unicast_to_dram_pipe(pipe: PipeNode) -> bool:
    # Check if it has single output and single input
    if len(pipe.outputs) > 1 or len(pipe.outputs[0]) > 1 or len(pipe.inputs) > 1:
        return False

    # Check if output buffer is dram_io
    if pipe.outputs[0][0].buffer_type != BufferType.dram_io:
        return False

    # Check if input buffer is packer
    if pipe.inputs[0].buffer_type != BufferType.packer:
        return False

    if pipe.inputs[0].buffer_space_shared != -1:
        return False

    # Check if input buffer is forked
    if pipe.inputs[0].is_forked():
        return False

    if gathering_from_diff_buffers(pipe):
        return False

    out_buf = pipe.outputs[0][0]
    if len(out_buf.outputs) > 0:
        return False

    return True


def is_packer_fork_buffer(buffer: BufferNode) -> bool:
    if buffer.buffer_type != BufferType.packer:
        return False
    if not buffer.is_forked():
        return False
    if not buffer.is_root():
        return False
    if buffer.buffer_space_shared != -1:
        return False

    # Go through fork pipes and check if any of them is multicast.
    for pipe in buffer.get_scatter_outputs():
        # TODO: remove when gather is refactored
        if gathering_from_diff_buffers(pipe):
            return False

        if pipe.outputs[0][0].buffer_type != BufferType.unpacker:
            return False

    return True


def is_dram_parallel_fork_buffer(buffer: BufferNode) -> bool:
    if buffer.buffer_type != BufferType.dram_io:
        return False
    if not buffer.is_forked():
        return False
    if not buffer.is_root():
        return False

    # Go through fork pipes and check if any of them is multicast.
    for pipe in buffer.get_scatter_outputs():
        if pipe.is_multicast():
            return False

    return True


def is_dram_unicast_pipe(pipe: PipeNode) -> bool:
    if gathering_from_diff_buffers(pipe):
        return False
    if len(pipe.outputs) != 1:
        return False
    if len(pipe.outputs[0]) != 1:
        return False
    if is_embedding_pipe(pipe):
        # Embedding pipes are tested in separate filter and we want to avoid situation where we
        # filter only embedding index pipe without the embedding table pipe.
        return False

    in_buffer = pipe.inputs[0]
    out_buffer = pipe.outputs[0][0]
    if (
        in_buffer.buffer_type != BufferType.dram_io
        or out_buffer.buffer_type != BufferType.unpacker
    ):
        return False
    if in_buffer.is_forked():
        return False
    if not out_buffer.is_input_operand():
        return False

    return True


def is_gather_rg_pipe(pipe: PipeNode) -> bool:
    if does_pipe_have_padding(pipe):
        return False

    # Check if all inputs are packer
    for buf in pipe.inputs:
        if buf.buffer_type != BufferType.packer:
            return False

        # TODO: Support in the future
        if buf.buffer_space_shared != -1:
            return False

    # Check if it has multiple input buffers
    if not gathering_from_diff_buffers(pipe):
        return False

    return True


def is_dram_gather_pipe(pipe: PipeNode) -> bool:
    if not gathering_from_diff_buffers(pipe):
        return False
    if len(pipe.outputs) != 1:
        return False
    if len(pipe.outputs[0]) != 1:
        return False

    out_buffer = pipe.outputs[0][0]
    for in_buffer in pipe.inputs:
        if in_buffer.buffer_type != BufferType.dram_io:
            return False
        if in_buffer.is_forked():
            return False

    if out_buffer.buffer_type != BufferType.unpacker:
        return False
    if not out_buffer.is_input_operand():
        return False

    return True


def is_dram_prefetch_post_tm_pipe(pipe: PipeNode) -> bool:
    if len(pipe.outputs) != 1:
        return False
    if len(pipe.outputs[0]) != 1:
        return False

    out_buffer = pipe.outputs[0][0]
    for in_buffer in pipe.inputs:
        if in_buffer.buffer_type != BufferType.dram_prolog:
            return False
        if in_buffer.prefetch_type != PrefetchType.PostTM:
            return False
        if in_buffer.is_forked():
            return False

    if out_buffer.buffer_type != BufferType.unpacker:
        return False
    if not out_buffer.is_input_operand():
        return False

    return True


def is_serial_fork_pipe(pipe: PipeNode) -> bool:
    if does_pipe_have_padding(pipe):
        return False

    pipe_fan_out = len(pipe.outputs)
    if pipe_fan_out == 1:
        return False

    for buf in pipe.inputs:
        if buf.buffer_space_shared != -1:
            return False

    # Check if all buffers are on the same chip
    if not are_all_buffers_on_same_chip(pipe):
        return False

    if pipe.is_multicast():
        return False

    return True


def check_common_multicast_conditions(pipe: PipeNode) -> bool:
    # Check if it has multiple outputs and it is not pipe scattering
    if len(pipe.outputs) != 1 or not pipe.is_multicast():
        return False

    # Check if all outputs are unpacker
    for out_buf in pipe.outputs[0]:
        if out_buf.buffer_type != BufferType.unpacker:
            return False

    # Check if all inputs are not forked nor have shared buffer space
    for in_buf in pipe.inputs:
        if in_buf.is_forked() or in_buf.buffer_space_shared != -1:
            return False

    # Check if all buffers are on the same chip
    if not are_all_buffers_on_same_chip(pipe):
        return False

    return True


def is_multicast_rg_pipe(pipe: PipeNode) -> bool:
    if does_pipe_have_padding(pipe):
        return False

    if not check_common_multicast_conditions(pipe):
        return False

    # Check if input is packer
    if pipe.inputs[0].buffer_type != BufferType.packer:
        return False

    return True


def is_dram_multicast_rg_pipe(pipe: PipeNode) -> bool:
    if not check_common_multicast_conditions(pipe):
        return False

    # Check if input is dram
    for in_buffer in pipe.inputs:
        if (
            in_buffer.buffer_type != BufferType.dram_io
            and in_buffer.buffer_type != BufferType.dram_prolog
        ):
            return False

    return True


def is_gather_multicast_rg_pipe(pipe: PipeNode) -> bool:
    if does_pipe_have_padding(pipe):
        return False

    if not check_common_multicast_conditions(pipe):
        return False

    # Check if it has multiple input buffers
    if not gathering_from_diff_buffers(pipe):
        return False

    # Check if all inputs are packer
    for in_buf in pipe.inputs:
        if in_buf.buffer_type != BufferType.packer:
            return False

    return True


def is_dram_gather_multicast_rg_pipe(pipe: PipeNode) -> bool:
    if not check_common_multicast_conditions(pipe):
        return False

    # Check if all inputs are dram
    for in_buf in pipe.inputs:
        if in_buf.buffer_type != BufferType.dram_io:
            return False

    return True


def is_consumer_repeated_pipe(pipe: PipeNode) -> bool:
    # Try gather
    if not gathering_from_diff_buffers(pipe):
        return False
    if pipe.consumer_repeat == len(pipe.outputs):
        return False
    # Discard multicast
    if len(pipe.outputs[0]) != 1:
        return False
    return pipe.consumer_repeat > 1


def is_buffer_input_to_serial_fork(buffer: BufferNode) -> bool:
    found_serial_pipe = False
    for pipe in buffer.get_scatter_outputs():
        if is_serial_fork_pipe(pipe):
            found_serial_pipe = True
        # TODO: Skip gather pipes for now.
        if gathering_from_diff_buffers(pipe):
            return False
    return found_serial_pipe


def does_pipe_have_consumer_duplicates(pipe: PipeNode) -> bool:
    if does_pipe_have_padding(pipe):
        return False

    # Force pipe scatter to be true
    if len(pipe.outputs) == 1:
        return False

    # Discard explicit consumer repeat
    if pipe.consumer_repeat != 1:
        return False

    output_dests = set()
    for output_buf_list in pipe.outputs:
        dest_tuple = tuple(output_buf_list)
        if dest_tuple in output_dests:
            return True
        output_dests.add(dest_tuple)

    return False


def is_relay_pipe(pipe: PipeNode) -> bool:
    for input in pipe.inputs:
        if input.buffer_space_shared != -1:
            return False
        if (
            input.buffer_type != BufferType.dram_io
            and input.buffer_type != BufferType.packer
            and input.buffer_type != BufferType.dram_prolog
        ):
            return False

    if (
        pipe.outputs[0][0].buffer_type != BufferType.relay
        and pipe.outputs[0][0].buffer_type != BufferType.ethernet_relay
    ):
        return False

    return True


def is_ethernet_relay_pipe(pipe: PipeNode) -> bool:
    return pipe.outputs[0][0].buffer_type == BufferType.ethernet_relay


def is_ethernet_fw_relay_pipe(pipe: PipeNode) -> bool:
    return is_ethernet_relay_pipe(pipe) and pipe.outputs[0][0].use_ethernet_fw_stream


def is_dram_prefetch_pre_tm_rg_pipe(pipe: PipeNode) -> bool:
    if len(pipe.outputs) != 1:
        return False

    for in_buffer in pipe.inputs:
        if in_buffer.buffer_type != BufferType.dram_prolog:
            return False
        if in_buffer.prefetch_type != PrefetchType.PreTM:
            return False

    for out_buffer in pipe.outputs[0]:
        if not out_buffer.is_input_operand():
            return False

    return True


def is_embedding_pipe(pipe: PipeNode) -> bool:
    input_buffer = pipe.inputs[0]
    if input_buffer.embedding_table or input_buffer.embedding_index:
        return True

    return False


def get_shared_packer_buffer(
    pipe_graph: PipeGraph, intermed_id: BufferNode
) -> Optional[BufferNode]:
    for buffer in pipe_graph.buffers:
        if buffer.buffer_space_shared == intermed_id:
            return buffer

    # Intermediates used in fused ops don't share space with other buffers.
    return None


def is_mmio_unicast_pipe(pipe: PipeNode) -> bool:
    if not pipe.mmio_pipe:
        return False
    if len(pipe.outputs) > 1 or len(pipe.outputs[0]) > 1:
        return False
    if len(pipe.inputs) > 1 or pipe.inputs[0].is_scatter:
        return False

    return True


def is_mmio_gather_pipe(pipe: PipeNode) -> bool:
    if not pipe.mmio_pipe:
        return False
    if len(pipe.outputs) > 1 or len(pipe.outputs[0]) > 1:
        return False
    if len(pipe.inputs) == 1 and not pipe.inputs[0].is_scatter:
        return False

    return True


def is_mmio_multicast_pipe(pipe: PipeNode) -> bool:
    if not pipe.mmio_pipe:
        return False
    if len(pipe.outputs) > 1 or len(pipe.outputs[0]) == 1:
        return False
    if len(pipe.inputs) > 1 or pipe.inputs[0].is_scatter:
        return False

    return True


def is_mmio_gather_multicast_pipe(pipe: PipeNode) -> bool:
    if not pipe.mmio_pipe:
        return False
    if len(pipe.outputs) > 1 or len(pipe.outputs[0]) == 1:
        return False
    if len(pipe.inputs) == 1 and not pipe.inputs[0].is_scatter:
        return False

    return True


def is_direct_packer_multicast_pipe(pipe: PipeNode) -> bool:
    if not pipe.direct_mcast:
        return False
    for in_buffer in pipe.inputs:
        if in_buffer.buffer_space_shared != -1:
            return False

    return True


def is_output_padding_pipe(pipe: PipeNode) -> bool:
    return does_pipe_have_output_padding(pipe)


def is_input_padding_pipe(pipe: PipeNode) -> bool:
    return does_pipe_have_input_padding(pipe) and not does_pipe_have_output_padding(
        pipe
    )


def is_e2e_queue(buffer: BufferNode) -> bool:
    return buffer.dram_io_flag and len(buffer.inputs) > 0 and len(buffer.outputs) > 0


def is_scatter_prefetch_post_tm_buffer(buffer: BufferNode) -> bool:
    return (
        buffer.is_scatter
        and buffer.is_buf_dram_prefetch()
        and buffer.prefetch_type == PrefetchType.PostTM
    )


def is_scatter_prefetch_post_tm_pipe(pipe: PipeNode) -> bool:
    return all(is_scatter_prefetch_post_tm_buffer(buffer) for buffer in pipe.inputs)


def is_single_output_pipe(pipe: PipeNode) -> bool:
    return len(pipe.outputs) == 1 and len(pipe.outputs[0]) == 1


def can_do_post_tm_optimization(src_pipe: PipeNode) -> bool:
    """
    Detect
    DRAM scatter buffer -> pipe (with single output) -> post tm relay buffer -> pipe (with single input)
    case.
    """
    if not (
        is_single_output_pipe(src_pipe) and is_scatter_prefetch_post_tm_pipe(src_pipe)
    ):
        return False

    middle_buffer = src_pipe.outputs[0][0]

    if len(middle_buffer.outputs) != 1 or not middle_buffer.is_post_tm_relay_buf:
        return False

    dest_pipe = middle_buffer.outputs[0]

    if len(dest_pipe.inputs) != 1:
        return False

    return True
