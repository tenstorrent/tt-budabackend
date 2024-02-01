# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from __future__ import annotations

from collections import deque
from enum import Enum
from logging import Logger

import os
import re

from pipegen_tests_utils import get_process_shared_logger


class StreamGraphComparisonStrategy(Enum):
    edges = 0
    topology = 1


def get_blog_logger_name() -> str:
    return f"blob_logger_pid_{os.getpid()}"


# Keys that will potentially ignored during comparison.
# If the value is None, key is always ignored.
# Also, if the value in original dict mathes the default value key will be ignored.
# Otherwise, key will not be ignored.
DEFAULT_KEYS_AND_VALUES = {
    "buf_addr": None,
    "buf_base_addr": None,
    # pipegen1 does not always set buf_id, so it's not reliable to compare it.
    "buf_id": None,
    "dest": None,
    # For PCIe streams NCRISC configs this field depends on receiving stream buffer L1 address, so it can differ
    # between pipegen1 and pipegen2. We will check this field only for DRAM NCRISC configs by temporarily removing
    # it from this list.
    "dram_buf_noc_addr": None,
    "dram_padding": False,
    "dram_ram": False,
    "dram_scatter_offsets": [],
    "dram_scatter_offsets_full_size": 0,
    # This field is used only in NCRISC configs of PCIe stream pairs, and since streams can have different IDs in
    # pipegen1 and pipegen2 this field can also be different. We compare PCIe NCRISC configs separately.
    "dram_streaming_dest": None,
    "dram_writes_with_cmd_buf": False,
    # Must skip fork_stream_ids because in pipegen1 it depends on the position in fork stream list. Concretely,
    # if we have fork streams A,B,C, A will have fork_stream_ids [A,B,C], while B and C will have fork_stream_ids [B,C].
    "fork_stream_ids": None,
    "group_priority": 0,
    # We have custom comparator for this field.
    "incoming_data_noc": -1,
    "input_index": None,
    "intermediate": False,
    "msg_info_buf_addr": 0,
    "next_phase_dest_change": True,
    "next_phase_src_change": True,
    # Must skip it because in pipegen1 it depends on the position in fork stream list
    "num_fork_streams": None,
    # It is compared as part of num_msgs comparison.
    "num_iters_in_epoch": None,
    "num_scatter_inner_loop": 1,
    # It is compared as part of num_msgs comparison.
    "num_unroll_iter": None,
    "output_index": None,
    "overlay_blob_extra_size": 0,
    "padding_scatter_order_size": None,
    "phase_id": None,
    # pipegen1 does not always set pipe_id, so it's not reliable to compare it.
    "pipe_id": None,
    "pipe_scatter_output_loop_count": 1,
    "pipe_type": None,
    "producer_epoch_id": 0,
    "ptrs_not_zero": False,
    "reg_update_vc": 3,
    "remote_receiver": False,
    "resend": False,
    "scatter_idx": 0,
    "scatter_order_size": 1,
    # Don't compare source fields as it's possible to have different stream ids assigned as part of the stream graph.
    "source": None,
}


# Custom functions which will be called for specific fields.
# Need to wrap functions in a lambda call in order to avoid non-declared function problems.
# TODO: Rethink if these comparators should be moved to a spearate file.
CUSTOM_BLOB_FIELD_COMPARATORS = {
    "dram_scatter_offsets": lambda *args, **kwargs: compare_dram_scatter_offsets(*args, **kwargs),
    "phase_auto_config": lambda *args, **kwargs: compare_phase_auto_config_field(*args, **kwargs),
    "num_msgs": lambda *args, **kwargs: compare_num_msgs(*args, **kwargs),
    "incoming_data_noc": lambda *args, **kwargs: compare_incoming_data_noc(*args, **kwargs),
    "next_phase_dest_change": lambda *args, **kwargs: compare_next_phase_dest_change(
        *args, **kwargs
    ),
    "dram_buf_read_chunk_size_tiles": lambda *args, **kwargs: compare_dram_buf_read_chunk_size_tiles(
        *args, **kwargs
    ),
    "ptrs_not_zero": lambda *args, **kwargs: compare_ptrs_not_zero(*args, **kwargs),
    "next_phase_src_change": lambda *args, **kwargs: compare_next_phase_src_change(*args, **kwargs),
}

DRAM_IO_IS_SCATTER_LOOP = 0x8000000000000000


def decode_scatter_magic_number(scatter_offset_magic_number: int) -> tuple:
    """Decodes scatter offset magic number to get number of loops and pattern length."""
    num_loops = (scatter_offset_magic_number & 0x7FFFFFFF00000000) >> 32
    pattern_length = scatter_offset_magic_number & 0xFFFFFFFF
    return num_loops, pattern_length


def decompress_scatter_pattern(scatter_pattern: list, increment: int, num_loop: int) -> list:
    """Repeats scatter pattern number of loop times always incrementing elements by a given value."""
    decompressed_offsets = []
    for loop_idx in range(1, num_loop + 1):
        for scatter_offset in scatter_pattern:
            decompressed_offsets.append(scatter_offset + loop_idx * increment)
    return decompressed_offsets


def decompress_scatter_offsets(compressed_scatter_offsets: list) -> list:
    """Decompresses given scatter offsets."""
    decompressed_offsets = []
    scatter_index = 0
    while scatter_index < len(compressed_scatter_offsets):
        scatter_offset = compressed_scatter_offsets[scatter_index]

        if scatter_offset & DRAM_IO_IS_SCATTER_LOOP:
            num_loops, pattern_length = decode_scatter_magic_number(scatter_offset)
            increment = compressed_scatter_offsets[scatter_index + 1]

            # Last `pattern_length` offsets represent the scatter pattern.
            scatter_pattern = decompressed_offsets[-pattern_length:]
            decompressed_offsets.extend(
                decompress_scatter_pattern(scatter_pattern, increment, num_loops)
            )

            # Scatter index is incremented by 2 in order to skip the increment value.
            scatter_index += 2
        else:
            decompressed_offsets.append(scatter_offset)
            scatter_index += 1

    return decompressed_offsets


def compare_dram_scatter_offsets(
    original_scatter_offsets: list,
    generated_scatter_offsets: list,
    msg_prefix: str,
    logger: Logger,
    **kwargs,
) -> bool:
    """Compares compressed original and generated dram scatter offsets. Uncompressed offsets has to match and the
    length of generated compressed offsets has to be smaller than the length of the original compressed offsets
    for comparison to be successfull."""
    original_length = len(original_scatter_offsets)
    generated_length = len(generated_scatter_offsets)

    if original_length < generated_length:
        logger.error(
            f"Got worse dram scatter offset than original pipegen:"
            f"({original_length}) offsets vs ({generated_length}) offsets"
        )
        return False
    elif original_length > generated_length:
        logger.info(
            f"Got better dram scatter offset compression than original pipegen: "
            f"({original_length}) offsets vs ({generated_length}) offsets"
        )

    original_decompressed = decompress_scatter_offsets(original_scatter_offsets)
    generated_decompressed = decompress_scatter_offsets(generated_scatter_offsets)
    if len(original_decompressed) != len(generated_decompressed):
        logger.error(
            f"{msg_prefix}: Length of decompressed scatter offsets is different: "
            f"({len(original_decompressed)}) vs ({len(generated_decompressed)})"
        )
        return False
    else:
        for original_offset, generated_offset in zip(original_decompressed, generated_decompressed):
            if original_offset != generated_offset:
                logger.error(
                    f"{msg_prefix}: Decompressed scatter offsets are different: "
                    f"({original_decompressed}) vs ({generated_decompressed})"
                )
                return False

    return True


def compare_phase_auto_config_field(
    original: bool, generated: bool, msg_prefix: str, logger: Logger, **kwargs
) -> bool:
    """Compare phase_auto_config field so that it ignores if, on the last phase, original is set to False and the
    generated value is set to True, in order to avoid unnecessary errors when comparing relay streams as this field
    is set to False for the last phase by blobgen."""
    is_last_phase = kwargs.get("is_last_phase", False)
    if is_last_phase:
        return True
    if original != generated:
        logger.error(
            f"{msg_prefix}: Found different values ({original} vs {generated}) for key phase_auto_config"
        )
        return False
    return True


def compare_incoming_data_noc(
    original: int, generated: int, msg_prefix: str, logger: Logger, **kwargs
) -> bool:
    """Compare incoming data noc only if stream reads from dram. In other cases, pipegen1 does not always set this
    field, so it's not reliable to compare it. On the other hand, comparing outgoing_data_noc should be sufficient to
    check if the connection between two streams is going over the correct noc."""
    is_dram_input = kwargs.get("dram_input", False)
    if not is_dram_input:
        return True

    if original != generated:
        logger.error(
            f"{msg_prefix}: Found different values ({original} vs {generated}) for key incoming_data_noc"
        )
        return False
    return True


def compare_num_msgs(
    original_num_msgs: int, generated_num_msgs: int, msg_prefix: str, logger: Logger, **kwargs
) -> bool:
    """Compare num_msgs field. Skip comparison if field is part of a prefetch NCRISC config as it is not relevant in
    that case. Also, skip it if it is part of dram write config because pipegen2 may generate greater number of messages
    because stream is unrolled more times. But when comparing dram write config we don't know unroll factors so we can't
    compare num_msgs exactly.
    In regular case, compare `num_iters_in_epoch * num_unroll_iter * num_msgs`.
    """
    is_dram_prefetch = kwargs.get("is_dram_prefetch", False)
    if is_dram_prefetch:
        return True

    is_dram_write = kwargs.get("is_dram_write", False)
    if is_dram_write:
        if generated_num_msgs < original_num_msgs or generated_num_msgs % original_num_msgs != 0:
            logger.error(
                f"{msg_prefix}: Found different values ({original_num_msgs} vs {generated_num_msgs}) for key "
                "num_msgs in the case of dram write"
            )
            return False
        return True

    original_num_iters_in_epoch = kwargs.get("original_num_iters_in_epoch", 1)
    generated_num_iters_in_epoch = kwargs.get("generated_num_iters_in_epoch", 1)
    original_num_unroll_iter = kwargs.get("original_num_unroll_iter", 1)
    generated_num_unroll_iter = kwargs.get("generated_num_unroll_iter", 1)

    original_product = original_num_iters_in_epoch * original_num_unroll_iter * original_num_msgs
    generated_product = (
        generated_num_iters_in_epoch * generated_num_unroll_iter * generated_num_msgs
    )

    if original_product != generated_product:
        logger.error(
            f"{msg_prefix}: Found different values {original_product} vs {generated_product} for product of "
            "num_iters_in_epoch * num_unroll_iter * num_msgs"
        )
        return False
    return True


def compare_next_phase_dest_change(
    original: bool, generated: bool, msg_prefix: str, logger: Logger, **kwargs
) -> bool:
    """Compares next_phase_dest_change unless it is the last phase, in which case it is ignored."""
    is_last_phase = kwargs.get("is_last_phase", False)
    if is_last_phase:
        return True
    if original != generated:
        logger.error(
            f"{msg_prefix}: Found different values ({original} vs {generated}) for key next_phase_dest_change"
        )
        return False
    return True


def compare_dram_buf_read_chunk_size_tiles(
    original: int, generated: int, msg_prefix: str, logger: Logger, **kwargs
) -> bool:
    """Pipegen2 can legitimately create larger dram_buf_read_chunk_size_tiles."""
    if original > generated:
        logger.error(
            f"{msg_prefix}: Found different values ({original} vs {generated}) for key "
            "dram_buf_read_chunk_size_tiles"
        )
        return False
    return True


def compare_ptrs_not_zero(
    original: int, generated: int, msg_prefix: str, logger: Logger, **kwargs
) -> bool:
    """
    Pipegen2 can unroll more phases than pipegen1 leading to different ptrs_not_zero calculation,
    because it depends on the number of tiles per iteration. Ignoring comparison of this field in
    that case.
    """
    original_num_iters_in_epoch = kwargs.get("original_num_iters_in_epoch", 1)
    generated_num_iters_in_epoch = kwargs.get("generated_num_iters_in_epoch", 1)
    original_num_unroll_iter = kwargs.get("original_num_unroll_iter", 1)
    generated_num_unroll_iter = kwargs.get("generated_num_unroll_iter", 1)
    if (original_num_iters_in_epoch != generated_num_iters_in_epoch
        or original_num_unroll_iter != generated_num_unroll_iter):
        return True
    if original != generated:
        logger.error(
            f"{msg_prefix}: Found different values ({original} vs {generated}) for key "
            "ptrs_not_zero"
        )
        return False
    return True


def compare_next_phase_src_change(
    original: int, generated: int, msg_prefix: str, logger: Logger, **kwargs
) -> bool:
    """
    In case when pipegen2 calculates different ptrs_not_zero than pipegen1 there is no point
    comparing next_phase_src_change since its calculation depends on ptrs_not_zero.
    """
    original_ptrs_not_zero  = kwargs.get("original_ptrs_not_zero", False)
    generated_ptrs_not_zero  = kwargs.get("generated_ptrs_not_zero", False)
    if original_ptrs_not_zero != generated_ptrs_not_zero:
        return True
    if original != generated:
        logger.error(
            f"{msg_prefix}: Found different values ({original} vs {generated}) for key "
            "next_phase_src_change"
        )
        return False
    return True


def compare_dicts(
    original_dict: dict, generated_dict: dict, msg_prefix: str, logger: Logger, **kwargs
) -> bool:
    """Returns whether two dicts contain same key-value pairs. If a key is present in both dicts,
    the values must be the same. Otherwise, if key is present in one of the dicts and does not have
    default value, error is issued."""
    original_has_extra_keys = has_extra_keys(
        original_dict, "original", generated_dict, "generated", msg_prefix, logger
    )
    generated_has_extra_keys = has_extra_keys(
        generated_dict, "generated", original_dict, "original", msg_prefix, logger
    )
    have_different_keys = have_different_common_keys(
        original_dict, generated_dict, msg_prefix, logger, **kwargs
    )
    return not (original_has_extra_keys or generated_has_extra_keys or have_different_keys)


def has_extra_keys(
    first_dict: dict,
    first_dict_name: str,
    second_dict: dict,
    second_dict_name: str,
    msg_prefix: str,
    logger: Logger,
) -> bool:
    """Checks if first dict has some extra keys not present in second dict, with non-default value."""
    result = False
    for k, v in first_dict.items():
        if (k in second_dict or key_is_ignored(k) or key_has_same_default_value(k, v)
            or key_has_custom_comparator_and_default_value(k)):
            continue
        logger.error(
            f"{msg_prefix}: Found key {k} in {first_dict_name} dict but not in {second_dict_name} one"
        )
        result = True
    return result


def have_different_common_keys(
    original_dict: dict, generated_dict: dict, msg_prefix: str, logger: Logger, **kwargs
) -> bool:
    """Checks if two dicts have some common keys with different values."""
    result = False
    for k, v in original_dict.items():
        if ((k not in generated_dict and not key_has_custom_comparator_and_default_value(k))
            or key_is_ignored(k)):
            continue
        elif k in CUSTOM_BLOB_FIELD_COMPARATORS:
            comparator_func = CUSTOM_BLOB_FIELD_COMPARATORS[k]
            gen_val = generated_dict[k] if k in generated_dict else DEFAULT_KEYS_AND_VALUES[k]
            if not comparator_func(v, gen_val, msg_prefix, logger, **kwargs):
                # Logging for this case is done inside custom comparator functions.
                result = True
        elif v != generated_dict[k]:
            logger.error(
                f"{msg_prefix}: Found different values ({v} vs {generated_dict[k]}) for key {k}"
            )
            result = True
    for k, v in generated_dict.items():
        if k in original_dict or not key_has_custom_comparator_and_default_value(k):
            continue
        if not comparator_func(DEFAULT_KEYS_AND_VALUES[k], v, msg_prefix, logger, **kwargs):
            # Logging for this case is done inside custom comparator functions.
            result = True
    return result


def key_is_ignored(key: str) -> bool:
    """Check if key is marked to be ignored during comparison, by having default value set to None."""
    return key in DEFAULT_KEYS_AND_VALUES and DEFAULT_KEYS_AND_VALUES[key] is None


def key_has_same_default_value(key: str, val) -> bool:
    """Check if key has default value and it matches the given value."""
    return key in DEFAULT_KEYS_AND_VALUES and DEFAULT_KEYS_AND_VALUES[key] == val


def key_has_custom_comparator_and_default_value(key: str) -> bool:
    """Checks if key has custom comparator and default value."""
    return key in DEFAULT_KEYS_AND_VALUES and key in CUSTOM_BLOB_FIELD_COMPARATORS


def compare_phase_dicts(
    original_phases: list[tuple[int, dict]],
    generated_phases: list[tuple[int, dict]],
    msg_prefix: str,
    logger: Logger,
) -> bool:
    """Compares original and generated phases."""
    for phase_idx, (x, y) in enumerate(zip(original_phases, generated_phases)):
        is_last_phase = phase_idx == len(original_phases) - 1
        original_phase_num = x[0]
        generated_phase_num = y[0]
        original_dict = x[1]
        generated_dict = y[1]
        if not compare_dicts(
            original_dict=original_dict,
            generated_dict=generated_dict,
            msg_prefix=f"{msg_prefix} [phase_{original_phase_num} vs phase_{generated_phase_num}]",
            logger=logger,
            is_last_phase=is_last_phase,
            dram_input=original_dict.get("dram_input", False),
            original_ptrs_not_zero=original_dict.get("ptrs_not_zero", False),
            generated_ptrs_not_zero=generated_dict.get("ptrs_not_zero", False),
            original_num_unroll_iter=original_dict.get("num_unroll_iter", 1),
            original_num_iters_in_epoch=original_dict.get("num_iters_in_epoch", 1),
            generated_num_unroll_iter=generated_dict.get("num_unroll_iter", 1),
            generated_num_iters_in_epoch=generated_dict.get("num_iters_in_epoch", 1),
        ):
            return False
    return True


class StreamDesc:
    def __init__(self, stream_key, stream_cfg=None):
        m = re.findall("\d+", stream_key)
        self.chip_id = int(m[0])
        self.y = int(m[1])
        self.x = int(m[2])
        self.id = int(m[3])
        self.buf_id = 0
        self.pipe_id = 0
        self.dram_buf_noc_addr = 0
        self.dram_streaming = False
        self.scatter_idx = 0
        if not stream_cfg:
            return
        if "buf_id" in stream_cfg:
            self.buf_id = stream_cfg["buf_id"]
        if "pipe_id" in stream_cfg:
            self.pipe_id = stream_cfg["pipe_id"]
        if "dram_buf_noc_addr" in stream_cfg:
            self.dram_buf_noc_addr = stream_cfg["dram_buf_noc_addr"]
        if "dram_streaming" in stream_cfg:
            self.dram_streaming = bool(int(stream_cfg["dram_streaming"]))
        if "scatter_idx" in stream_cfg:
            self.scatter_idx = stream_cfg["scatter_idx"]

    def get_loc(self) -> tuple:
        return (self.chip_id, self.y, self.x)

    def get_comparator_tuple(self) -> tuple:
        return (self.chip_id, self.y, self.x, self.id)

    def get_sink_id(self) -> str:
        assert (
            self.buf_id != 0 or self.pipe_id != 0 or self.dram_buf_noc_addr != 0
        ), f"Found sink {self} without buf_id or pipe_id or dram_buf_noc_addr set"
        if self.dram_buf_noc_addr != 0 and not self.dram_streaming:
            # In case of DRAM streaming we can't use dram_buf_noc_addr to match streams since it
            # depends on the local address of the receiving stream. We also can't use
            # dram_buf_noc_addr alone because multiple streams can write into same NOC address (for
            # example in gather to DRAM), so we append either buf_id or location.
            if self.buf_id != 0:
                return f"{self.buf_id}_{self.dram_buf_noc_addr}"
            else:
                return f"{self.chip_id}_{self.y}_{self.x}_{self.dram_buf_noc_addr}"
        elif self.buf_id != 0:
            # buf_id is not as reliable as dram_buf_noc_addr because when there is a fork from
            # packer to DRAM there will be multiple fork streams with same buf_id.
            return str(self.buf_id)
        elif self.dram_streaming:
            # There can be multiple streams with the same pipe_id in case of pipe scatter, so we
            # have to check for scatter index.
            return f"{self.pipe_id}_{self.scatter_idx}"
        else:
            # pipe_id is least reliable because in many cases we can generate multiple streams for
            # the same pipe.
            return str(self.pipe_id)

    def __eq__(self, other_stream: StreamDesc) -> bool:
        return (
            self.chip_id == other_stream.chip_id
            and self.y == other_stream.y
            and self.x == other_stream.x
            and self.id == other_stream.id
        )

    def __str__(self) -> str:
        return f"chip_{self.chip_id}__y_{self.y}__x_{self.x}__stream_id_{self.id}"

    def __repr__(self) -> str:
        return str(self)

    def __hash__(self):
        return hash(self.__str__())


class StreamEdge:
    def __init__(self, source: StreamDesc, dest: StreamDesc, logger: Logger):
        self.source = source
        self.dest = dest
        self.phases = []
        self.logger = logger
        self.min_phase_id = float("inf")

    def add_phase(self, phase: int, phase_cfg: dict):
        self.min_phase_id = min(self.min_phase_id, phase)
        self.phases.append((phase, phase_cfg))

    def get_phases(self) -> list[int]:
        return self.phases

    def compare_phases(self, other_edge: StreamEdge) -> bool:
        if len(self.phases) != len(other_edge.get_phases()):
            self.logger.error(
                f"Found different number of phases: {self} ({len(self.phases)}) vs"
                + f" {other_edge} ({len(other_edge.get_phases())})"
            )
            return False
        if self.source.get_loc() != other_edge.source.get_loc():
            self.logger.error(f"Found different location of source of edge: {self} vs {other_edge}")
            return False
        if self.dest.get_loc() != other_edge.dest.get_loc():
            self.logger.error(f"Found different location of dest of edge: {self} vs {other_edge}")
            return False

        # Now that we know edges have the same source & dest, and same number of phases, compare each phase.
        if not compare_phase_dicts(
            self.phases, other_edge.phases, f"{self} vs {other_edge}", self.logger
        ):
            self.logger.error(f"Found difference in phases for edges {self} vs {other_edge}")
            return False

        return True

    def __eq__(self, other_edge: StreamEdge) -> bool:
        return self.source == other_edge.source and self.dest == other_edge.dest

    def __hash__(self) -> int:
        return hash(str(self.source) + str(self.dest))

    def __str__(self) -> str:
        return f"{self.source} -> {self.dest}"

    def __repr__(self) -> str:
        return str(self)

    def get_comparator_tuple(self) -> tuple:
        # We must sort based on locations first, and then differentiate based on stream and phase IDs.
        return (
            self.source.get_loc(),
            self.dest.get_loc(),
            self.dest.id,
            self.min_phase_id,
            self.source.id,
        )


class StreamGraph:
    def __init__(
        self, starting_phase, logger: Logger, comparison_strategy: StreamGraphComparisonStrategy
    ):
        self.starting_phase = starting_phase
        self.edges = {}
        self.sink_configs = {}
        self.graph_depth = 0
        self.logger = logger
        self.comparison_strategy = comparison_strategy

    def add_edge(self, source: StreamDesc, dest: StreamDesc, phase_num: int, phase_cfg: dict):
        edge = StreamEdge(source, dest, self.logger)
        dict_key = str(edge)
        if not dict_key in self.edges:
            self.edges[dict_key] = edge
        self.edges[dict_key].add_phase(phase_num, phase_cfg)

    def add_sink_config(self, sink_node: StreamDesc, phase_num: int, phase_cfg: dict):
        if sink_node not in self.sink_configs:
            self.sink_configs[sink_node] = []
        self.sink_configs[sink_node].append((phase_num, phase_cfg))

    def __str__(self):
        res = "\n"
        for _, edge in self.edges.items():
            res += f"{edge}\n"
        return res

    def get_sorted_edges(self) -> list[StreamEdge]:
        edges = list(self.edges.values())
        edges.sort(key=lambda edge: edge.get_comparator_tuple())
        return edges

    def compare_sinks(self, other_sg: StreamGraph) -> bool:
        if len(self.sink_configs) != len(other_sg.sink_configs):
            self.logger.error(
                "Found different number of sinks "
                f"({len(self.sink_configs)} vs {len(other_sg.sink_configs)})\n"
                f"stream_graph: {str(self)}other_stream_graph: {str(other_sg)}"
            )
            return False

        my_sinks = sorted(self.sink_configs.keys(), key=lambda x: x.get_comparator_tuple())
        other_sinks = sorted(other_sg.sink_configs.keys(), key=lambda x: x.get_comparator_tuple())

        sinks_equal = True
        for sink_key, other_sink_key in zip(my_sinks, other_sinks):
            my_configs = self.sink_configs[sink_key]
            other_configs = other_sg.sink_configs[other_sink_key]
            if len(my_configs) != len(other_configs):
                self.logger.error(
                    "Found different number of phases in original and generated "
                    f"config for {sink_key} vs {other_sink_key}"
                )
                return False
            if not compare_phase_dicts(
                my_configs, other_configs, f"{sink_key} vs {other_sink_key}", self.logger
            ):
                self.logger.error(
                    "Found difference in phases for sinks " f"{sink_key} vs {other_sink_key}"
                )
                sinks_equal = False

        return sinks_equal

    def __eq__(self, other_sg: StreamGraph) -> bool:
        edges_list = self.get_sorted_edges()
        other_edges_list = other_sg.get_sorted_edges()
        if len(edges_list) != len(other_edges_list):
            # TODO: We are relying here on fact that in compare_phase_section() function order of
            # comparison is original == generated. We should instead extract __eq__ and subsequent
            # functions from StreamGraph into BlobComparator class and create function which
            # compares generated with original stream graph instead of the __eq__ function here.
            if StreamGraph.is_optimized_gather_difference(self, other_sg):
                self.logger.warning("Ignored full graph comparison for optimized gather situation")
                # In this situation we can only match graphs' sinks.
                return self.compare_sinks(other_sg)
            else:
                self.logger.error(
                    "Length of data flow graph edges is different "
                    f"({len(edges_list)} vs {len(other_edges_list)})\n"
                    f"stream_graph: {str(self)}other_stream_graph: {str(other_sg)}"
                )
                return False
        if self.comparison_strategy == StreamGraphComparisonStrategy.topology:
            return self.compare_graphs_by_topology(other_sg)
        else:
            return self.compare_graphs_by_edges(edges_list, other_edges_list, other_sg)

    @staticmethod
    def is_optimized_gather_difference(
        orig_graph: StreamGraph, generated_graph: StreamGraph
    ) -> bool:
        """Returns true if this is situation where graphs are legitimately different due to
        situations where one pipegen uses optimized gather and the other doesn't. It can happen in
        two cases:
            1) Pipegen1 sometimes unnecessary uses unoptimized gather.
            2) There are not enough gather/multicast streams on the core to use optimized gather,
               so both pipegens create unoptimized gather but for different pipes.
        """
        # This is working good enough for now, in case we find situations where it hides some other
        # illegitimate graph structures we should update this logic to be more sophisticated.
        return ((orig_graph.graph_depth == 2 and generated_graph.graph_depth == 3)
                or (orig_graph.graph_depth == 3 and generated_graph.graph_depth == 2))

    def compare_graphs_by_edges(
        self,
        edges_list: list[StreamEdge],
        other_edges_list: list[StreamEdge],
        other_sg: StreamGraph,
    ) -> bool:
        # Purposefully extracted into separate statements because we want to run both comparisons
        # in order to report all errors in the log.
        edges_equal = all(e1.compare_phases(e2) for e1, e2 in zip(edges_list, other_edges_list))
        sinks_equal = self.compare_sinks(other_sg)
        return edges_equal and sinks_equal

    def make_source_streams_graph(self) -> dict[StreamDesc, list[StreamEdge]]:
        source_streams = {}

        # Create a graph dest_stream - (edge with min phase id) -> source_stream.
        for edge in self.edges.values():
            if edge.dest not in source_streams:
                source_streams[edge.dest] = []

            source_streams[edge.dest].append(edge)

        # Sort all dest -> source edges by phase id in ascending order.
        for dest_stream in source_streams:
            source_streams[dest_stream].sort(key=lambda e: e.min_phase_id)

        return source_streams

    def find_root_stream(self, source_streams: dict[StreamDesc, list[StreamEdge]]) -> StreamDesc:
        root_stream = None

        # Find root stream, i.e. the stream which has no ingoing edges.
        for sink_stream in self.sink_configs:
            if sink_stream in source_streams:
                assert not root_stream, "Didn't expect to find multiple root streams"
                root_stream = sink_stream

        return root_stream

    def compare_graphs_by_topology(self, other_sg: StreamGraph) -> bool:
        source_streams_graph = self.make_source_streams_graph()
        root_stream = self.find_root_stream(source_streams_graph)

        other_source_streams_graph = other_sg.make_source_streams_graph()
        other_root_stream = other_sg.find_root_stream(other_source_streams_graph)

        # Traverse the graph in BFS style to get the edges ordering - always processing source edges by the increasing
        # initial phase id.
        # First, compare stream graph topologies.
        stream_queue = deque()
        stream_queue.append(root_stream)

        other_stream_queue = deque()
        other_stream_queue.append(other_root_stream)

        sorted_edges = []
        other_sorted_edges = []

        while stream_queue and other_stream_queue:
            dest_stream = stream_queue.popleft()
            other_dest_stream = other_stream_queue.popleft()

            source_edges = source_streams_graph.get(dest_stream, [])
            other_source_edges = other_source_streams_graph.get(other_dest_stream, [])

            if len(source_edges) != len(other_source_edges):
                self.logger.error(
                    f"Found different number of source edges for stream: "
                    f"{dest_stream} (num source edges: {len(source_edges)}) vs "
                    f"{other_dest_stream} (num source edges: {len(other_source_edges)})"
                )
                return False

            for source_edge, other_source_edge in zip(source_edges, other_source_edges):
                # Second, compare number of phase configs on each edge.
                if len(source_edge.get_phases()) != len(other_source_edge.get_phases()):
                    self.logger.error(
                        f"Found different number of phases: "
                        f"{source_edge} ({len(source_edge.get_phases())}) vs "
                        f"{other_source_edge} ({len(other_source_edge.get_phases())})"
                    )
                    return False

                sorted_edges.append(source_edge)
                other_sorted_edges.append(other_source_edge)
                stream_queue.append(source_edge.source)
                other_stream_queue.append(other_source_edge.source)

        # Third, compare phase configs.
        return self.compare_graphs_by_edges(sorted_edges, other_sorted_edges, other_sg)


class BlobComparator:
    PHASE_PREFIX = "phase_"
    DRAM_SECTION_KEY = "dram_blob"
    DRAM_PERF_INFO_SECTION_KEY = "dram_perf_dump_blob"
    DRAM_PERF_INFO_SECTION_NOC_ADDR_KEY = "dram_perf_buf_noc_addr"
    DRAM_PERF_INFO_SECTION_MAX_REQ_KEY = "dram_perf_buf_max_req"

    def __init__(
        self,
        orig_blob: dict,
        new_blob: dict,
        comparison_log_path: str,
    ) -> None:
        self.orig_blob = orig_blob
        self.new_blob = new_blob
        self.logger = get_process_shared_logger(get_blog_logger_name(), comparison_log_path)

    def compare(self, sg_comparison_strategy: StreamGraphComparisonStrategy) -> bool:
        """Returns whether blobs are considered same or not."""
        if self.orig_blob == self.new_blob:
            return True

        orig_phase_map = self.parse_phase_map(self.orig_blob)
        self.propagate_scatter_idxs(orig_phase_map)
        self.orig_streams_to_node_id = self.find_streams_to_node_id_mapping(orig_phase_map)
        self.orig_graphs = self.create_data_flow_graphs(orig_phase_map, sg_comparison_strategy)

        generated_phase_map = self.parse_phase_map(self.new_blob)
        self.propagate_scatter_idxs(generated_phase_map)
        self.generated_streams_to_node_id = self.find_streams_to_node_id_mapping(
            generated_phase_map
        )
        self.generated_graphs = self.create_data_flow_graphs(
            generated_phase_map, sg_comparison_strategy
        )

        # Purposefully extracted into separate statements because we want to run all comparisons in
        # order to report all errors in the log.
        dram_section_equal = self.compare_dram_section()
        phase_section_equal = self.compare_phase_section()
        dram_perf_info_section_equal = self.compare_dram_perf_info_section()
        return dram_section_equal and phase_section_equal and dram_perf_info_section_equal

    def compare_phase_section(self):
        orig_sinks_per_id = {}
        for sink in self.orig_graphs.keys():
            orig_sinks_per_id[sink.get_sink_id()] = sink

        # Try matching every generated sink with original sink by sink id.
        result = True
        generated_sink_ids = set()
        for generated_sink in self.generated_graphs.keys():
            generated_sink_id = generated_sink.get_sink_id()
            generated_sink_ids.add(generated_sink_id)
            if generated_sink_id not in orig_sinks_per_id:
                if self.has_different_dram_chunk_size_orig_sink(generated_sink):
                    self.logger.warning(
                        "Created different dram read graphs due to different chunk"
                        f" size for generated sink {generated_sink}"
                    )
                    continue
                else:
                    self.logger.error(
                        f"Did not find matching sink for {generated_sink} in original blob"
                    )
                    return False
            orig_sink = orig_sinks_per_id[generated_sink_id]
            orig_graph = self.orig_graphs[orig_sink]
            generated_graph = self.generated_graphs[generated_sink]
            result = result and (orig_graph == generated_graph)

        # Check if there are missing sinks in generated blob.
        if len(self.orig_graphs) > len(self.generated_graphs):
            for sink in self.orig_graphs.keys():
                if sink.get_sink_id() in generated_sink_ids:
                    continue
                self.logger.error(f"Did not find matching sink for {sink} in generated blob")
                return False

        return result

    def has_different_dram_chunk_size_orig_sink(self, generated_sink: StreamDesc):
        generated_graph = self.generated_graphs[generated_sink]
        if generated_graph.graph_depth > 1:
            return False
        for sink in self.orig_graphs.keys():
            orig_graph: StreamGraph = self.orig_graphs[sink]
            if (
                generated_graph.graph_depth == 0
                and orig_graph.graph_depth == 1
                and BlobComparator.same_dram_read_graphs_with_different_chunk_size(
                    generated_graph, orig_graph
                )
            ) or (
                generated_graph.graph_depth == 1
                and orig_graph.graph_depth == 0
                and BlobComparator.same_dram_read_graphs_with_different_chunk_size(
                    orig_graph, generated_graph
                )
            ):
                return True
        return False

    @staticmethod
    def same_dram_read_graphs_with_different_chunk_size(
        direct_graph: StreamGraph, relay_graph: StreamGraph
    ):
        direct_graph_sink_cfg = list(direct_graph.sink_configs.values())[0][0][1]
        if (
            "dram_io" not in direct_graph_sink_cfg
            or not direct_graph_sink_cfg["dram_io"]
            or "dram_buf_noc_addr" not in direct_graph_sink_cfg
        ):
            return False
        dram_buf_noc_addr = direct_graph_sink_cfg["dram_buf_noc_addr"]
        relay_cfg = list(relay_graph.edges.values())[0].phases[0][1]
        if (
            "dram_io" not in relay_cfg
            or not relay_cfg["dram_io"]
            or "dram_buf_noc_addr" not in relay_cfg
            or relay_cfg["dram_buf_noc_addr"] != dram_buf_noc_addr
        ):
            return False
        return True

    def compare_dram_section(self):
        orig_ncriscs = self.parse_ncriscs(self.orig_blob[BlobComparator.DRAM_SECTION_KEY])
        generated_ncriscs = self.parse_ncriscs(self.new_blob[BlobComparator.DRAM_SECTION_KEY])

        if len(orig_ncriscs) != len(generated_ncriscs):
            self.logger.error(
                f"Found different number of stream ncrisc configs between two pipegens"
            )
            return False

        # We first have to compare and remove PCIe NCRISC configs, because they can have different dram_buf_noc_addr
        # between pipegen1 and pipegen2.
        ncrisc_configs_equal = self.compare_pcie_ncriscs(orig_ncriscs, generated_ncriscs)

        # Temporarily removing this field from defaults so it can be checked for DRAM NCRISC configs.
        DEFAULT_KEYS_AND_VALUES.pop("dram_buf_noc_addr")

        # Mapping from dram_buf_noc_addr to ncrisc config. In case of readers, we might have multiple reader streams, so
        # we have to additionally map according to reader_index. For writers, mapping with dram_buf_noc_addr will be
        # sufficient.
        # As dram addresses are not unique across chips, we have to add additional layer of indirection with chip_id.
        dram_buf_reader_configs = {}
        dram_buf_writer_configs = {}
        for stream_desc, orig_ncrisc_list in orig_ncriscs.items():
            chip_id = stream_desc.chip_id
            for idx in range(len(orig_ncrisc_list)):
                ncrisc_cfg = orig_ncrisc_list[idx]
                is_dram_input = ncrisc_cfg.get("dram_input", False)
                is_dram_output = ncrisc_cfg.get("dram_output", False)
                is_dram_padding = ncrisc_cfg.get("dram_padding", False)
                dram_buf_noc_addr = ncrisc_cfg["dram_buf_noc_addr"]

                if is_dram_input and not is_dram_output and not is_dram_padding:
                    if chip_id not in dram_buf_reader_configs:
                        dram_buf_reader_configs[chip_id] = {}
                    if dram_buf_noc_addr not in dram_buf_reader_configs[chip_id]:
                        dram_buf_reader_configs[chip_id][dram_buf_noc_addr] = {}
                    dram_buf_reader_configs[chip_id][dram_buf_noc_addr][
                        ncrisc_cfg.get("reader_index", 0)
                    ] = ncrisc_cfg
                elif is_dram_output:
                    if chip_id not in dram_buf_writer_configs:
                        dram_buf_writer_configs[chip_id] = {}
                    dram_buf_writer_configs[chip_id][dram_buf_noc_addr] = ncrisc_cfg

        # Compare generated ncrisc configs with originals.
        for stream_desc, generated_ncrisc_list in generated_ncriscs.items():
            for idx in range(len(generated_ncrisc_list)):
                generated_ncrisc_cfg = generated_ncrisc_list[idx]
                is_dram_input = generated_ncrisc_cfg.get("dram_input", False)
                is_dram_output = generated_ncrisc_cfg.get("dram_output", False)
                is_dram_padding = generated_ncrisc_cfg.get("dram_padding", False)

                if is_dram_input and not is_dram_output and not is_dram_padding:
                    ncrisc_configs_equal = (
                        ncrisc_configs_equal
                        and self.compare_generated_ncrisc_with_original_reader_config(
                            stream_desc, generated_ncrisc_cfg, dram_buf_reader_configs
                        )
                    )
                elif is_dram_output:
                    ncrisc_configs_equal = (
                        ncrisc_configs_equal
                        and self.compare_generated_ncrisc_with_original_writer_config(
                            stream_desc, generated_ncrisc_cfg, dram_buf_writer_configs
                        )
                    )

        # Returning this field back to list so it doesn't get checked during streams phase comparison.
        DEFAULT_KEYS_AND_VALUES["dram_buf_noc_addr"] = None

        return ncrisc_configs_equal

    def compare_dram_perf_info_section(self):
        """Comparison is run only if both blobs have dram_perf_dump_blob section, otherwise it is disregarded."""
        orig_perf_info = self.orig_blob.get(BlobComparator.DRAM_PERF_INFO_SECTION_KEY, None)
        generated_perf_info = self.new_blob.get(BlobComparator.DRAM_PERF_INFO_SECTION_KEY, None)

        if orig_perf_info == None:
            self.logger.warning(
                f"{BlobComparator.DRAM_PERF_INFO_SECTION_KEY} section is not found in original blob!"
                f" Not comparing these sections."
            )
            return True

        if generated_perf_info == None:
            self.logger.warning(
                f"{BlobComparator.DRAM_PERF_INFO_SECTION_KEY} section is not found in generated blob!"
                f" Not comparing these sections."
            )
            return True

        if len(generated_perf_info) > len(orig_perf_info):
            self.logger.warning(
                f"Generated {BlobComparator.DRAM_PERF_INFO_SECTION_KEY} contains more elements than "
                f"the original: ({set(generated_perf_info.keys()) - set(orig_perf_info.keys())})"
            )
            return False

        match = True

        # Don't break the loop at any point. Log all errors that occur.
        for worker_loc in orig_perf_info.keys():
            orig_subdict = orig_perf_info[worker_loc]
            generated_subdict = generated_perf_info.get(worker_loc, None)

            if generated_subdict == None:
                self.logger.error(
                    f"{worker_loc} is not found in generated blob's "
                    f"{BlobComparator.DRAM_PERF_INFO_SECTION_KEY} section!"
                )
                match = False
                continue

            orig_noc_addr = orig_subdict[BlobComparator.DRAM_PERF_INFO_SECTION_NOC_ADDR_KEY]
            generated_noc_addr = generated_subdict[
                BlobComparator.DRAM_PERF_INFO_SECTION_NOC_ADDR_KEY
            ]

            if orig_noc_addr != generated_noc_addr:
                self.logger.error(
                    f"{worker_loc} {BlobComparator.DRAM_PERF_INFO_SECTION_NOC_ADDR_KEY} is different "
                    f"(original: {[hex(addr) for addr in orig_noc_addr]} vs "
                    f"generated: {[hex(addr) for addr in generated_noc_addr]})"
                )
                match = False

            orig_max_req = orig_subdict[BlobComparator.DRAM_PERF_INFO_SECTION_MAX_REQ_KEY]
            generated_max_req = generated_subdict[BlobComparator.DRAM_PERF_INFO_SECTION_MAX_REQ_KEY]

            if orig_max_req != generated_max_req:
                self.logger.error(
                    f"{worker_loc} {BlobComparator.DRAM_PERF_INFO_SECTION_MAX_REQ_KEY} is different "
                    f"(original: {orig_max_req} vs generated: {generated_max_req})"
                )
                match = False

        return match

    def parse_ncriscs(self, blob_ncrisc_section: dict):
        ncrisc_map = {}
        if not blob_ncrisc_section:
            return ncrisc_map
        for stream_key, ncrisc_configs in blob_ncrisc_section.items():
            stream_desc = StreamDesc(stream_key)
            assert stream_desc not in ncrisc_map

            # Pipegen v1 will create empty ncrisc config for all the relay streams (they will have empty
            # dram_buf_noc_addr field), so we need to remove them as:
            # 1) they are not used anywhere
            # 2) would mess up the comparison, as we would have the different number of ncrisc configs
            filtered_ncrisc_configs = {}
            for config_id in ncrisc_configs:
                if ncrisc_configs[config_id].get("dram_buf_noc_addr", None):
                    filtered_ncrisc_configs[config_id] = ncrisc_configs[config_id]
            if len(filtered_ncrisc_configs):
                ncrisc_map[stream_desc] = filtered_ncrisc_configs
        return ncrisc_map

    def compare_pcie_ncriscs(self, orig_ncriscs: dict, generated_ncriscs: dict) -> bool:
        orig_pcie_ncriscs = self.find_pcie_ncriscs(orig_ncriscs, self.orig_streams_to_node_id)
        generated_pcie_ncriscs = self.find_pcie_ncriscs(
            generated_ncriscs, self.generated_streams_to_node_id
        )
        if len(orig_pcie_ncriscs) != len(generated_pcie_ncriscs):
            self.logger.error(f"Found different number of PCIe ncrisc configs between two pipegens")
            return False

        ncrisc_configs_equal = True

        for node_id, (orig_ncrisc_sd, orig_ncrisc_config) in orig_pcie_ncriscs.items():
            if node_id not in generated_pcie_ncriscs:
                ncrisc_configs_equal = False
                self.logger.error(
                    f"Couldn't find generated PCIe NCRISC config for streams of node {node_id}"
                )
                continue

            generated_ncrisc_sd = generated_pcie_ncriscs[node_id][0]
            generated_ncrisc_config = generated_pcie_ncriscs[node_id][1]

            ncrisc_configs_equal = ncrisc_configs_equal and compare_dicts(
                original_dict=orig_ncrisc_config,
                generated_dict=generated_ncrisc_config,
                msg_prefix=f"orig: {str(orig_ncrisc_sd)} vs generated: {str(generated_ncrisc_sd)}",
                logger=self.logger,
                is_dram_write="dram_streaming_dest" in orig_ncrisc_config,
            )

            # Remove PCIe NCRISC configs so they don't get compared with DRAM configs.
            orig_ncriscs.pop(orig_ncrisc_sd)
            generated_ncriscs.pop(generated_ncrisc_sd)

        return ncrisc_configs_equal

    def find_pcie_ncriscs(self, ncrisc_configs: dict, streams_to_node_id: dict):
        """
        Returns map of stream descriptors paired with corresponding NCRISC config, mapped by node id for which
        that stream and NCRISC config was created.
        """
        pcie_ncriscs = {}
        for stream_desc, ncrisc_list in ncrisc_configs.items():
            if len(ncrisc_list) != 1 or "dram_streaming_dest" not in ncrisc_list[0]:
                continue

            assert (
                stream_desc in streams_to_node_id
            ), "Expected to find node id for PCIe sending stream"
            pcie_ncriscs[streams_to_node_id[stream_desc]] = (stream_desc, ncrisc_list[0])

            receiving_stream_desc = StreamDesc(ncrisc_list[0]["dram_streaming_dest"])
            assert (
                receiving_stream_desc in ncrisc_configs
            ), "Expected to find receiving PCIe NCRISC config"
            receiving_ncrisc_configs = ncrisc_configs[receiving_stream_desc]
            assert (
                len(receiving_ncrisc_configs) == 1
            ), "Expected to find only one receiving PCIe NCRISC config"
            receiving_ncrisc_config = receiving_ncrisc_configs[0]
            assert (
                receiving_stream_desc in streams_to_node_id
            ), "Expected to find node id for PCIe receiving stream"
            pcie_ncriscs[streams_to_node_id[receiving_stream_desc]] = (
                receiving_stream_desc,
                receiving_ncrisc_config,
            )

        return pcie_ncriscs

    def compare_generated_ncrisc_with_original_reader_config(
        self, stream_desc: StreamDesc, generated_ncrisc_cfg: dict, dram_buf_reader_configs: dict
    ):
        chip_id = stream_desc.chip_id
        dram_buf_noc_addr = generated_ncrisc_cfg["dram_buf_noc_addr"]
        dram_buf_reader_configs_on_chip = dram_buf_reader_configs.get(chip_id, {})

        if dram_buf_noc_addr not in dram_buf_reader_configs_on_chip:
            self.logger.error(
                f"{stream_desc}: Generated config for {hex(dram_buf_noc_addr)} address, but address "
                f"is not found in original blob"
            )
            return False

        orig_ncrisc_cfgs = dram_buf_reader_configs_on_chip[dram_buf_noc_addr]
        gen_reader_index = generated_ncrisc_cfg.get("reader_index", 0)
        if gen_reader_index not in orig_ncrisc_cfgs:
            self.logger.error(
                f"{stream_desc}: Found unexpected reader index {gen_reader_index} in generated dram "
                f"config for {hex(dram_buf_noc_addr)} address"
            )
            return False

        original_ncrisc_config = orig_ncrisc_cfgs[gen_reader_index]
        return compare_dicts(
            original_dict=original_ncrisc_config,
            generated_dict=generated_ncrisc_cfg,
            msg_prefix=str(stream_desc) + f"[reader_index={gen_reader_index}]",
            logger=self.logger,
            is_dram_prefetch=not original_ncrisc_config.get("dram_io", False),
        )

    def compare_generated_ncrisc_with_original_writer_config(
        self, stream_desc: StreamDesc, generated_ncrisc_cfg: dict, dram_buf_writer_configs: dict
    ):
        chip_id = stream_desc.chip_id
        dram_buf_noc_addr = generated_ncrisc_cfg["dram_buf_noc_addr"]
        dram_buf_writer_configs_on_chip = dram_buf_writer_configs.get(chip_id, {})

        if dram_buf_noc_addr not in dram_buf_writer_configs_on_chip:
            self.logger.error(
                f"{stream_desc}: Generated config for {hex(dram_buf_noc_addr)} address, but address "
                f"is not found in original blob"
            )
            return False

        return compare_dicts(
            original_dict=dram_buf_writer_configs_on_chip[dram_buf_noc_addr],
            generated_dict=generated_ncrisc_cfg,
            msg_prefix=str(stream_desc),
            logger=self.logger,
            is_dram_write=True,
        )

    def parse_phase_map(self, blob_d: dict) -> dict:
        phase_map = {}
        for phase_key, phase_content in blob_d.items():
            if not phase_key.startswith(BlobComparator.PHASE_PREFIX):
                continue
            stream_configs = self.parse_first_iter_configs(phase_content)
            if not stream_configs:
                continue
            phase_map[int(phase_key.split("_")[-1])] = stream_configs

        return phase_map

    def parse_first_iter_configs(self, phase_content: dict) -> dict:
        stream_configs = {}
        for stream_key, stream_cfg in phase_content.items():
            if "unroll_iter" in stream_cfg and stream_cfg["unroll_iter"] > 0:
                continue
            stream_configs[stream_key] = stream_cfg

        return stream_configs

    def propagate_scatter_idxs(self, phase_map: dict):
        """Propagates scatter index from source to destination streams so we can use it to match the sinks."""
        for _, phase_content in phase_map.items():
            for _, stream_cfg in phase_content.items():
                if "scatter_idx" not in stream_cfg:
                    continue
                scatter_idx = stream_cfg["scatter_idx"]
                self.propagate_scatter_idx(scatter_idx, stream_cfg, phase_content)

    def propagate_scatter_idx(self, scatter_idx: int, stream_cfg: dict, phase_content: dict):
        """Propagates scatter index to destination streams of stream with a given config."""
        dests = self.get_stream_dests(stream_cfg)
        for dest in dests:
            if dest not in phase_content:
                continue
            dest_cfg = phase_content[dest]
            if "scatter_idx" in dest_cfg:
                continue
            dest_cfg["scatter_idx"] = scatter_idx
            self.propagate_scatter_idx(scatter_idx, dest_cfg, phase_content)

    def find_streams_to_node_id_mapping(self, phase_map: dict):
        """Returns map of stream descriptors, mapped by node id for which that stream config was created."""
        streams_to_node_id = {}
        for _, phase_content in phase_map.items():
            for stream_key, stream_cfg in phase_content.items():
                sd = StreamDesc(stream_key, stream_cfg)
                streams_to_node_id[sd] = sd.get_sink_id()

        return streams_to_node_id

    def create_data_flow_graphs(
        self, phase_map: dict, sg_comparison_strategy: StreamGraphComparisonStrategy
    ):
        self.fill_source_streams(phase_map)
        sink_streams = self.find_sink_streams(phase_map)

        max_phase = max(phase_map.keys())
        graphs = {}
        for sink, phases in sink_streams.items():
            graphs[sink] = self.create_graph(
                phase_map, sink, phases[0], max_phase, sg_comparison_strategy
            )

        return graphs

    def find_sink_streams(self, phase_map) -> dict:
        sink_streams = {}
        for phase_num, phase_content in phase_map.items():
            for stream_key, stream_cfg in phase_content.items():
                if self.is_sink_stream(stream_cfg):
                    sd = StreamDesc(stream_key, stream_cfg)
                    if sd not in sink_streams:
                        sink_streams[sd] = []
                    sink_streams[sd].append(phase_num)
        return sink_streams

    def fill_source_streams(self, phase_map: dict):
        """Fills source keys in phase map."""
        for _, phase_content in phase_map.items():
            for stream_key, stream_cfg in phase_content.items():
                if "source" not in stream_cfg:
                    stream_cfg["source"] = None
                dests = self.get_stream_dests(stream_cfg)
                for dest in dests:
                    if dest not in phase_content:
                        continue
                    phase_content[dest]["source"] = stream_key

    def create_graph(
        self,
        phase_map: dict,
        sink: StreamDesc,
        starting_phase: int,
        max_phase: int,
        sg_comparison_strategy: StreamGraphComparisonStrategy,
    ) -> StreamGraph:
        sink_cfg = phase_map[starting_phase][str(sink)]

        # In case this data flow goes through scattered pipe, save the pipe scatter idx
        # so we can filter streams that are involved only in this flow.
        scatter_idx = -1
        source = sink_cfg["source"]
        dest = str(sink)

        # Data path represents set of all streams that are involved in data transfers toward the sink.
        data_path = set()
        data_path.add(dest)

        # Resulting stream graph.
        stream_graph = StreamGraph(starting_phase, self.logger, sg_comparison_strategy)
        stream_graph.add_sink_config(sink, starting_phase, sink_cfg)

        # Create initial path of streams from source to the sink.
        while source:
            stream_graph.graph_depth = stream_graph.graph_depth + 1
            source_cfg = phase_map[starting_phase][str(source)]
            stream_graph.add_edge(StreamDesc(source), StreamDesc(dest), starting_phase, source_cfg)
            dest = source
            data_path.add(source)
            if "scatter_idx" in source_cfg:
                scatter_idx = source_cfg["scatter_idx"]
            source = source_cfg["source"]

        # Go over all other phases and add more edges to the graph. We add any edge that goes from a stream to a stream
        # that is already in the path.
        for phase_num in range(starting_phase + 1, max_phase + 1):
            if phase_num not in phase_map:
                continue
            phase_content = phase_map[phase_num]

            added_streams = set()
            while True:
                found_new_edge = False
                for stream_key, stream_cfg in phase_content.items():
                    if stream_key in added_streams:
                        continue
                    if (
                        scatter_idx != -1
                        and "scatter_idx" in stream_cfg
                        and stream_cfg["scatter_idx"] != scatter_idx
                    ):
                        continue
                    dests = self.get_stream_dests(stream_cfg)
                    if len(dests) == 0:
                        if sink == StreamDesc(stream_key):
                            stream_graph.add_sink_config(
                                StreamDesc(stream_key), phase_num, stream_cfg
                            )
                            added_streams.add(stream_key)
                        continue
                    for dest in dests:
                        if dest not in data_path:
                            continue
                        stream_graph.add_edge(
                            StreamDesc(stream_key), StreamDesc(dest), phase_num, stream_cfg
                        )
                        data_path.add(stream_key)
                        added_streams.add(stream_key)
                        found_new_edge = True
                if not found_new_edge:
                    break

        return stream_graph

    def get_stream_dests(self, stream_cfg) -> list[str]:
        if "dest" not in stream_cfg:
            return []

        cfg_dests = stream_cfg["dest"]
        if len(cfg_dests) < 2:
            return cfg_dests

        dests = []
        top_left_dest = StreamDesc(cfg_dests[0])
        bottom_right_dest = StreamDesc(cfg_dests[1])
        for y in range(top_left_dest.y, bottom_right_dest.y + 1):
            for x in range(top_left_dest.x, bottom_right_dest.x + 1):
                dest = StreamDesc(cfg_dests[0])
                dest.y = y
                dest.x = x
                dests.append(str(dest))

        return dests

    def is_sink_stream(self, stream_cfg) -> bool:
        return len(self.get_stream_dests(stream_cfg)) == 0
