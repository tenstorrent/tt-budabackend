#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Filters pipegen yaml for specific type of pipes and buffers.
"""
from __future__ import annotations

from verif.common.pipegen_yaml_filter_utils import *


class FilterType(IntEnum):
    # Experimental filters
    Nothing = 0  # Filters nothing
    DirectPipes_L1_L1 = 1  # packer->pipe->unpacker
    DirectPipes_DRAM_L1 = 2  # dram->pipe->unpacker
    DirectPipes_L1_DRAM = 3  # packer->pipe->dram
    DRAM_Prefetch_PreTM = 4  # dram prefetch pre TM buffers and their pipes
    DRAM_Prefetch_PostTM = 5  # dram prefetch post TM buffers and their pipes
    GraphDepth = 6  # Filters graphs of certain depth
    DRAM_Fork = 7  # Dram IO forked buffers
    DRAM_Prefetch_Fork = 8  # Dram Prefetch forked buffers
    Packer_Fork = 9  # Forked packer buffers
    MulticoreGather = 10  # Filters pipes gathering from different cores
    E2E_queues = 11  # Filters DRAM buffers that are both input and output to DRAM pipes
    PostTMPipesOptimization = 12  # Filters pipes eligible for optimization
    Draft = 999  # Filter for trying out various things

    # Filters used for testing of the implementation of specific RG pipes
    UnicastPipe = 1000
    DramParallelForkPipe = 1001
    ParallelForkPipe = 1002
    DramUnicastPipe = 1003
    DramGatherPipe = 1004
    DramPrefetchPostTMPipe = 1005
    GatherToDramPipe = 1006
    GatherPipe = 1007
    SerialForkPipe = 1008
    MulticastPipe = 1009
    DramMulticastPipe = 1010
    GatherMulticastPipe = 1011
    DramGatherMulticastPipe = 1012
    UnicastToDramPipe = 1013
    RelayPipe = 1014
    DramPrefetchPreTMPipe = 1015
    EmbeddingPipe = 1016
    IntermediateBuffers = 1017
    MMIOPipe = 1018
    EthernetRelayPipe = 1019
    EthernetFWRelayPipe = 1020
    SerialForkWithDuplicates = 1021
    DramTilizerPipe = 1022
    DirectPackerMulticastPipe = 1023
    InputPaddingPipe = 1024
    OutputPaddingPipe = 1025


def filter_pipegen_yaml(
    original_yaml_path: str, filtered_yaml_path: str, filter_type: FilterType
) -> bool:
    pipe_graph = PipeGraph()
    pipe_graph.parse_from_yaml(original_yaml_path)
    pipe_graph.expand_scatter_buffers()
    pipe_graph.find_pipes_connections()

    if filter_type == FilterType.DirectPipes_L1_L1:
        filtered_pipe_graph = filter_direct_pipes_l1_l1(pipe_graph)
    elif filter_type == FilterType.DirectPipes_DRAM_L1:
        filtered_pipe_graph = filter_direct_pipes_dram_l1(pipe_graph)
    elif filter_type == FilterType.DirectPipes_L1_DRAM:
        filtered_pipe_graph = filter_direct_pipes_l1_dram(pipe_graph)
    elif filter_type == FilterType.DRAM_Prefetch_PreTM:
        filtered_pipe_graph = filter_dram_prefetch(pipe_graph, PrefetchType.PreTM)
    elif filter_type == FilterType.DRAM_Prefetch_PostTM:
        filtered_pipe_graph = filter_dram_prefetch(pipe_graph, PrefetchType.PostTM)
    elif filter_type == FilterType.GraphDepth:
        filtered_pipe_graph = filter_graph_depth(pipe_graph)
    elif filter_type == FilterType.DRAM_Fork:
        filtered_pipe_graph = filter_dram_fork(pipe_graph)
    elif filter_type == FilterType.DRAM_Prefetch_Fork:
        filtered_pipe_graph = filter_dram_prefetch_fork(pipe_graph)
    elif filter_type == FilterType.Packer_Fork:
        filtered_pipe_graph = filter_packer_fork(pipe_graph)
    elif filter_type == FilterType.MulticoreGather:
        filtered_pipe_graph = filter_multicore_gather_pipes(pipe_graph)
    elif filter_type == FilterType.E2E_queues:
        filtered_pipe_graph = filter_e2e_queues(pipe_graph)
    elif filter_type == FilterType.PostTMPipesOptimization:
        filtered_pipe_graph = filter_post_tm_optimization_ready_pipes(pipe_graph)
    elif filter_type == FilterType.Draft:
        filtered_pipe_graph = filter_draft(pipe_graph)
    elif filter_type == FilterType.UnicastPipe:
        filtered_pipe_graph = filter_unicast_pipes(pipe_graph)
    elif filter_type == FilterType.DramParallelForkPipe:
        filtered_pipe_graph = filter_dram_parallel_fork_pipes(pipe_graph)
    elif filter_type == FilterType.ParallelForkPipe:
        filtered_pipe_graph = filter_parallel_fork_pipes(pipe_graph)
    elif filter_type == FilterType.DramUnicastPipe:
        filtered_pipe_graph = filter_dram_unicast_pipes(pipe_graph)
    elif filter_type == FilterType.DramGatherPipe:
        filtered_pipe_graph = filter_dram_gather_pipes(pipe_graph)
    elif filter_type == FilterType.DramPrefetchPostTMPipe:
        filtered_pipe_graph = filter_dram_prefetch_post_tm_pipes(pipe_graph)
    elif filter_type == FilterType.GatherToDramPipe:
        filtered_pipe_graph = filter_gather_to_dram_pipes(pipe_graph)
    elif filter_type == FilterType.GatherPipe:
        filtered_pipe_graph = filter_gather_pipes(pipe_graph)
    elif filter_type == FilterType.SerialForkPipe:
        filtered_pipe_graph = filter_serial_fork_pipes(pipe_graph)
    elif filter_type == FilterType.MulticastPipe:
        filtered_pipe_graph = filter_multicast_pipes(pipe_graph)
    elif filter_type == FilterType.DramMulticastPipe:
        filtered_pipe_graph = filter_dram_multicast_pipes(pipe_graph)
    elif filter_type == FilterType.GatherMulticastPipe:
        filtered_pipe_graph = filter_gather_multicast_pipes(pipe_graph)
    elif filter_type == FilterType.DramGatherMulticastPipe:
        filtered_pipe_graph = filter_dram_gather_multicast_pipes(pipe_graph)
    elif filter_type == FilterType.UnicastToDramPipe:
        filtered_pipe_graph = filter_unicast_to_dram_pipes(pipe_graph)
    elif filter_type == FilterType.DramPrefetchPreTMPipe:
        filtered_pipe_graph = filter_dram_prefetch_pre_tm_pipes(pipe_graph)
    elif filter_type == FilterType.RelayPipe:
        filtered_pipe_graph = filter_relay_pipes(pipe_graph)
    elif filter_type == FilterType.EmbeddingPipe:
        filtered_pipe_graph = filter_embedding_pipes(pipe_graph)
    elif filter_type == FilterType.IntermediateBuffers:
        filtered_pipe_graph = filter_intermediate_buffers(pipe_graph)
    elif filter_type == FilterType.MMIOPipe:
        filtered_pipe_graph = filter_mmio_pipes(pipe_graph)
    elif filter_type == FilterType.EthernetRelayPipe:
        filtered_pipe_graph = filter_ethernet_relay_pipes(pipe_graph)
    elif filter_type == FilterType.EthernetFWRelayPipe:
        filtered_pipe_graph = filter_ethernet_fw_relay_pipes(pipe_graph)
    elif filter_type == FilterType.SerialForkWithDuplicates:
        filtered_pipe_graph = filter_serial_fork_pipes_with_duplicates(pipe_graph)
    elif filter_type == FilterType.DramTilizerPipe:
        filtered_pipe_graph = filter_dram_tilizer_pipes(pipe_graph)
    elif filter_type == FilterType.DirectPackerMulticastPipe:
        filtered_pipe_graph = filter_direct_packer_multicast_pipes(pipe_graph)
    elif filter_type == FilterType.InputPaddingPipe:
        filtered_pipe_graph = filter_input_padding_pipes(pipe_graph)
    elif filter_type == FilterType.OutputPaddingPipe:
        filtered_pipe_graph = filter_output_padding_pipes(pipe_graph)
    else:
        raise NotImplementedError

    if len(filtered_pipe_graph.buffers) > 0 or len(filtered_pipe_graph.pipes) > 0:
        filtered_pipe_graph.write_to_blob_yaml(filtered_yaml_path)
        return True
    else:
        return False


def filter_direct_pipes_l1_l1(pipe_graph: PipeGraph) -> PipeGraph:
    filtered_pipe_graph = PipeGraph()
    for pipe in pipe_graph.pipes:
        if (
            len(pipe.inputs) == 1
            and len(pipe.outputs) == 1
            and len(pipe.outputs[0]) == 1
            and pipe.inputs[0].get_location() == BufferLocation.L1
            and pipe.outputs[0][0].get_location() == BufferLocation.L1
        ):
            filtered_pipe_graph.add_pipe(pipe, True)
            filtered_pipe_graph.add_buffer(pipe.inputs[0], False)
            filtered_pipe_graph.add_buffer(pipe.outputs[0][0], False)

    return filtered_pipe_graph


def filter_direct_pipes_l1_dram(pipe_graph: PipeGraph) -> PipeGraph:
    filtered_pipe_graph = PipeGraph()
    for pipe in pipe_graph.pipes:
        buffer = pipe.inputs[0]
        if buffer.buffer_space_shared > 0:
            continue
        if buffer.is_forked():
            continue
        if (
            len(pipe.inputs) == 1
            and len(pipe.outputs) == 1
            and len(pipe.outputs[0]) == 1
            and pipe.inputs[0].get_location() == BufferLocation.L1
            and pipe.outputs[0][0].get_location() == BufferLocation.DRAM
        ):
            filtered_pipe_graph.add_pipe(pipe, True)
            filtered_pipe_graph.add_buffer(pipe.inputs[0], False)
            filtered_pipe_graph.add_buffer(pipe.outputs[0][0], False)

    return filtered_pipe_graph


def filter_direct_pipes_dram_l1(pipe_graph: PipeGraph) -> PipeGraph:
    filtered_pipe_graph = PipeGraph()
    for pipe in pipe_graph.pipes:
        input_buf = pipe.inputs[0]

        # Make sure there is a single input and single output buffer. Also make sure output buffer is operand input to
        # the kernel.

        output_buf = pipe.outputs[0][0]
        if (
            not gathering_from_diff_buffers(pipe)
            and len(pipe.outputs) == 1
            and len(pipe.outputs[0]) == 1
            and input_buf.get_location() == BufferLocation.DRAM
            and output_buf.get_location() == BufferLocation.L1
            and not input_buf.is_forked()
            and output_buf.is_input_operand()
        ):
            filtered_pipe_graph.add_pipe(pipe, True)
            filtered_pipe_graph.add_buffer(input_buf, False)
            filtered_pipe_graph.add_buffer(output_buf, False)

    return filtered_pipe_graph


def filter_dram_prefetch(
    pipe_graph: PipeGraph, prefetch_type: PrefetchType
) -> PipeGraph:
    return filter_graph_by_buffers(
        pipe_graph,
        lambda buffer: buffer.is_buf_dram_prefetch()
        and prefetch_type == buffer.prefetch_type,
    )


def filter_graph_depth(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_buffers(
        pipe_graph,
        lambda buffer: len(buffer.inputs) == 0 and find_max_graph_depth(buffer) >= 3,
    )


# Filters almost any L1 to L1 connection. Saving it as a checkpoint since tests passed for cases filtered by it.
def filter_L1_2_L1(pipe_graph: PipeGraph) -> PipeGraph:
    filtered_pipe_graph = PipeGraph()

    visited_node_ids = set()
    for pipe in pipe_graph.pipes:
        if pipe.consumer_repeat > 1:
            continue
        buffer = pipe.inputs[0]
        if buffer.buffer_space_shared > 0:
            continue
        if buffer.is_buf_kernel_intermediate():
            continue
        if buffer.get_location() != BufferLocation.L1 or buffer.dram_buf_flag:
            continue
        if pipe.outputs[0][0].get_location() != BufferLocation.L1:
            continue
        add_pipe_connections(pipe, filtered_pipe_graph, visited_node_ids)

    for buf in filtered_pipe_graph.buffers:
        if buf.get_location() == BufferLocation.DRAM or buf.dram_buf_flag:
            return PipeGraph()

    return filtered_pipe_graph


def find_single_dest(buffer: BufferNode):
    if len(buffer.outputs) == 0:
        return buffer

    while True:
        pipe = buffer.outputs[0]
        new_buffer = pipe.outputs[0][0]

        if len(new_buffer.outputs) == 0:
            return new_buffer

        buffer = new_buffer


def filter_dram_output(pipe_graph: PipeGraph) -> PipeGraph:
    filtered_pipe_graph = PipeGraph()

    visited_node_ids = set()
    for buffer in pipe_graph.buffers:
        dest_buf = find_single_dest(buffer)
        if dest_buf.num_epoch_tiles < 2048:
            continue
        if buffer.is_root() and buffer.get_location() == BufferLocation.DRAM:
            continue
        if dest_buf.get_location() != BufferLocation.DRAM:
            continue
        if buffer.untilized_output:
            continue
        add_buffer_connections(buffer, filtered_pipe_graph, visited_node_ids)

    return filtered_pipe_graph


def filter_dram_fork(pipe_graph: PipeGraph) -> PipeGraph:
    def buffer_condition(buffer):
        return (
            buffer.is_root()
            and buffer.get_location() == BufferLocation.DRAM
            and buffer.dram_buf_flag == False
            and buffer.is_forked()
        )

    return filter_graph_by_buffers(pipe_graph, buffer_condition)


def filter_dram_prefetch_fork(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_buffers(
        pipe_graph, lambda buffer: buffer.is_buf_dram_prefetch() and buffer.is_forked()
    )


def filter_packer_fork(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_buffers(
        pipe_graph,
        lambda buffer: buffer.get_location() == BufferLocation.L1
        and buffer.is_forked(),
    )


def filter_multicore_gather_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, gathering_from_diff_cores)


def filter_e2e_queues(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_buffers(pipe_graph, is_e2e_queue)


def filter_unicast_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_unicast_rg_pipe)


def filter_gather_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_gather_rg_pipe)


def filter_parallel_fork_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_buffers(pipe_graph, is_packer_fork_buffer)


def filter_dram_parallel_fork_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_buffers(pipe_graph, is_dram_parallel_fork_buffer)


def filter_dram_unicast_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_dram_unicast_pipe)


def filter_dram_gather_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_dram_gather_pipe)


def filter_dram_prefetch_post_tm_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_dram_prefetch_post_tm_pipe)


def filter_gather_to_dram_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(
        pipe_graph,
        lambda pipe: is_gathering_to_dram(pipe) and gathering_from_diff_buffers(pipe),
    )


def filter_serial_fork_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_serial_fork_pipe)


def filter_multicast_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_multicast_rg_pipe)


def filter_dram_multicast_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_dram_multicast_rg_pipe)


def filter_gather_multicast_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_gather_multicast_rg_pipe)


def filter_dram_gather_multicast_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_dram_gather_multicast_rg_pipe)


def filter_unicast_to_dram_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_unicast_to_dram_pipe)


def filter_relay_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_relay_pipe)


def filter_dram_prefetch_pre_tm_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_dram_prefetch_pre_tm_rg_pipe)


def filter_embedding_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_embedding_pipe)


def filter_intermediate_buffers(pipe_graph: PipeGraph) -> PipeGraph:
    """
    This is a unified filter finding intermediate buffers used in all three contexts we use them in:
    - matmul intermed buffers
    - gradient matmul
    - fused op intermed buffers

    For intermediate buffers used in regular matmul, we end up with pipegen.yaml that has buffers with
    `buffer_type: intermediate` attribute. In this case, in pipegen.yaml there has to be a packer buffer which has
    buffer_space_shared property set to intermed buf's id, which we find and add it, along with its connections, to
    the filtered_pipe_graph.

    For intermediate buffers used in gradient matmul, we end up with pipegen.yaml that has buffers with
    `buffer_type: gradient_op` attribute. In this case, these buffers aren't connected to any other buffer in any way,
    they become lone nodes in filtered_pipe_graph.

    For intermediate buffers used in fused ops, we end up with pipegen.yaml that has buffers with
    `buffer_type: intermediate` attribute. In this case, these buffers aren't connected to any other buffer in any way,
    they become lone nodes in filtered_pipe_graph.
    """
    filtered_pipe_graph = PipeGraph()

    intermed_bufs = filter_graph_by_buffers(
        pipe_graph,
        lambda buffer: buffer.buffer_type == BufferType.intermediate
        or buffer.buffer_type == BufferType.gradient_op,
    ).buffers

    visited_node_ids = set()

    for buf in intermed_bufs:
        add_buffer_connections(buf, filtered_pipe_graph, visited_node_ids)
        shared_packer_buffer = get_shared_packer_buffer(pipe_graph, buf.id)
        if shared_packer_buffer is not None:
            # This will be executed only in case of matmul intermed buffers.
            add_buffer_connections(
                shared_packer_buffer, filtered_pipe_graph, visited_node_ids
            )

    return filtered_pipe_graph


def filter_mmio_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, lambda pipe: pipe.mmio_pipe == True)


def filter_ethernet_relay_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_ethernet_relay_pipe)


def filter_ethernet_fw_relay_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_ethernet_fw_relay_pipe)


def filter_serial_fork_pipes_with_duplicates(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(
        pipe_graph, lambda pipe: does_pipe_have_consumer_duplicates(pipe)
    )


def filter_dram_tilizer_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_buffers(pipe_graph, lambda buffer: buffer.hw_tilize == True)


def filter_direct_packer_multicast_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_direct_packer_multicast_pipe)


def filter_input_padding_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_input_padding_pipe)


def filter_output_padding_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, is_output_padding_pipe)


def filter_post_tm_optimization_ready_pipes(pipe_graph: PipeGraph) -> PipeGraph:
    return filter_graph_by_pipes(pipe_graph, can_do_post_tm_optimization)


def filter_draft(pipe_graph: PipeGraph) -> PipeGraph:
    filtered_pipe_graph = PipeGraph()

    # Use this filter to try out things you don't want to commit.

    return filtered_pipe_graph
