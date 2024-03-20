# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  ha [ <verbose> ]

Arguments:
  verbose    Verbosity level (0 or 1) [default: 0]

Description:
  Traverses all devices looking for the point of failure in the netlist execution. The traversal is
  done in topological order with the intention of finding the first problematic core/stream.

Examples:
  ha
"""

import sys
from tabulate import tabulate
from tt_object import TTObjectIDDict
import tt_util as util
import tt_device
from tt_graph import Queue
from docopt import docopt


def LOG(s, **kwargs):
    util.PRINT(util.get_indent() + s, **kwargs)


def WARN(s, **kwargs):
    util.WARN(util.get_indent() + s, **kwargs)


command_metadata = {"short": "ha", "type": "high-level", "description": __doc__}


def queue_has_data(context, device_id, q: Queue):
    """
    Checking if queue has data
    """
    device = context.devices[device_id]
    if not q.is_host() and not q.is_dram():
        WARN(f"Unknown memory location: {q.get_loc()}")
        return True

    for mem_addr in q.get_mem_addr():
        if q.is_dram():
            dram_chan, dram_addr = mem_addr[0], mem_addr[1]
            x, y = device.DRAM_CHANNEL_TO_NOC0_LOC[dram_chan]
            read_ptr = device.pci_read_xy(x, y, 0, dram_addr)
            write_ptr = device.pci_read_xy(x, y, 0, dram_addr + 4)
        if q.is_host():
            host_chan, host_addr = mem_addr[0], mem_addr[1]
            read_ptr = tt_device.SERVER_IFC.host_dma_read(
                device_id, host_addr, host_chan
            )
            write_ptr = tt_device.SERVER_IFC.host_dma_read(
                device_id, host_addr + 4, host_chan
            )

        # Get read and write pointers
        if Queue.occupancy(q.entries(), write_ptr, read_ptr) == 0:
            return False

    return True


# check if destination buffer is connected to source op
def is_destination_buffer(graph, src_op_or_q, dest_buffer):
    if graph.is_dest_buffer(dest_buffer):
        src_buffers = graph.get_fanin(dest_buffer)
        src_buffers.keep(lambda b: b.root["md_op_name"] == src_op_or_q.id())
        if src_buffers:
            return True
    return False


def is_stream_done(device, stream):
    moves_raw_data = (
        stream.root["moves_raw_data"] if "moves_raw_data" in stream.root else False
    )

    # LOG(f"Pretty print stream {stream}: {stream.pretty_print()}")

    stream_loc = stream.loc()

    if moves_raw_data:
        LOG(f"Stream '{stream}' moves raw data")
        REMOTE_DEST_BUF_SIZE = device.get_stream_reg_field(
            stream_loc, stream.stream_id(), 3, 0, 16
        )
        REMOTE_DEST_BUF_START = device.get_stream_reg_field(
            stream_loc, stream.stream_id(), 4, 0, 16
        )
        return REMOTE_DEST_BUF_SIZE == REMOTE_DEST_BUF_START
    else:
        return device.get_num_msgs_received(
            stream_loc, stream.stream_id()
        ) == device.get_curr_phase_num_msgs_remaining(stream_loc, stream.stream_id())


def get_stream_data(device, stream):
    ret_type = dict()
    stream_loc = stream.loc()

    ret_type["phase"] = device.get_stream_phase(stream_loc, stream.stream_id())
    ret_type["msg_received"] = device.get_num_msgs_received(
        stream_loc, stream.stream_id()
    )
    ret_type["msg_remaining"] = device.get_curr_phase_num_msgs_remaining(
        stream_loc, stream.stream_id()
    )
    if device.get_remote_receiver(stream_loc, stream.stream_id()):
        ret_type["source"] = True
    if device.get_remote_source(stream_loc, stream.stream_id()):
        ret_type["dest"] = True
    return ret_type


# Returns if the Op wants more data on a given input
def get_streams_waiting_for_data(graph, device, src_op_or_q, dest_op_or_q):
    op_buffers = graph.get_buffers(dest_op_or_q)
    # As not all streams have buf_id, and not all have pipe_id, we try to find either one
    relevant_buffers = TTObjectIDDict()
    relevant_pipes = TTObjectIDDict()
    util.VERBOSE(
        f"Running wants_more_data_from_input for {dest_op_or_q} on input {src_op_or_q}"
    )
    util.VERBOSE(f"  Found these buffers for {dest_op_or_q}: {op_buffers}")
    for dest_buffer_id, dest_buffer in op_buffers.items():
        if is_destination_buffer(graph, src_op_or_q, dest_buffer):
            relevant_buffers.add(dest_buffer)
            pipes = graph.get_pipes(dest_buffer)
            relevant_pipes.update(pipes)
    util.VERBOSE(
        f"  Found these source buffers for {src_op_or_q}->{dest_op_or_q} conection: {relevant_buffers}"
    )
    relevant_streams = TTObjectIDDict(
        {
            stream_id: stream
            for stream_id, stream in graph.temporal_epoch.streams.items()
            if relevant_buffers.find_id(stream.get_buffer_id())
            or relevant_pipes.find_id(stream.get_pipe_id())
        }
    )

    if not relevant_streams:
        buflist = [str(buffer.id()) for buffer in relevant_buffers]
        WARN(f"No relevant streams for buffers {' '.join (buflist)}")
    else:
        util.VERBOSE(
            f"  Found these relevant_streams for {src_op_or_q}->{dest_op_or_q} conection: {' '.join ([ stream.__str__() for stream in relevant_streams ])}"
        )

    streams_waiting_for_data = []
    for stream_id, stream in relevant_streams.items():
        if not is_stream_done(device, stream):
            streams_waiting_for_data.append(stream)
    return streams_waiting_for_data


def get_graph_input_queues(graph):
    input_queues = graph.queues().copy()
    # Keep only queues that are not fed by an op
    input_queues.keep(lambda q: q.root["input"] not in graph.ops)
    return input_queues


def get_input_queues_without_data(context, device_id, graph):
    """
    Finding input queues that do not have data
    """
    return {}  # WIP

    device = context.devices[device_id]
    input_queues = get_graph_input_queues(graph)
    queue_ops = {}
    for q_id, q in input_queues.items():
        has_data = queue_has_data(context, device_id, q)
        if not has_data:
            for op_id, op in graph.get_fanout(q).items():
                streams = get_streams_waiting_for_data(graph, device, q, op)
                if streams:
                    if q not in queue_ops:
                        queue_ops[q] = {}
                    queue_ops[q][op] = []
                    for stream in streams:
                        data = get_stream_data(device, stream)
                        queue_ops[q][op].append(
                            [stream.on_chip_id(), "No data in queue", data]
                        )

    return queue_ops


# Returns true if the buffer is ready to be consumed by the op
def get_buffer_ready_state(context, graph, device_id, stream, buffer):
    device = context.devices[device_id]
    stream_loc = stream.loc()

    # Get num_messages_received for the stream
    num_messages_received = device.get_num_msgs_received(stream_loc, stream.stream_id())

    state = {
        "num_messages_received": num_messages_received,
        "tile_clear_granularity": None,
    }

    if buffer.is_output_of_pipe():
        # Get tile_clear_granularity from buffer
        state["tile_clear_granularity"] = buffer.root["tile_clear_granularity"]
        buffer_ready = (
            state["num_messages_received"] > 0
            and (state["num_messages_received"] % state["tile_clear_granularity"]) == 0
        )
    else:
        buffer_ready = state["num_messages_received"] > 0

    return buffer_ready, state


# Checks if all inputs are ready, both there is no output produced. This normally
# indicates that the operation itself is hung.
def check_input_output_misbalance(context, graph, device_id, op):
    """
    Looking for input/output ready misbalance
    """
    device = context.devices[device_id]
    input_buffers = graph.get_buffers(op, "out")
    output_buffers = graph.get_buffers(op, "in")
    ok = True

    # Inputs
    not_ready_input_buffers = dict()
    for _, buffer in input_buffers.items():
        for _, stream in get_buffer_streams_in_phase(device, graph, buffer).items():
            buffer_ready, state = get_buffer_ready_state(
                context, graph, device_id, stream, buffer
            )
            if not buffer_ready:
                not_ready_input_buffers[buffer.id()] = state

    # Outputs
    not_ready_output_buffers = dict()
    for _, buffer in output_buffers.items():
        for _, stream in get_buffer_streams_in_phase(device, graph, buffer).items():
            buffer_ready, state = get_buffer_ready_state(
                context, graph, device_id, stream, buffer
            )
            if not buffer_ready:
                not_ready_output_buffers[buffer.id()] = state

    def print_non_ready_buffers(not_ready_buffers, msg):
        for buffer_id, state in not_ready_buffers.items():
            buffer = graph.temporal_epoch.buffers[buffer_id]
            print(f"      Buffer {buffer} is not ready: {state}")

    # Check if there is a mix of ready/non-ready
    if len(not_ready_input_buffers) == 0 and len(not_ready_output_buffers) > 0:
        WARN(f"All input buffers of op {op} are ready, but the output buffers are not.")
        print_non_ready_buffers(
            not_ready_output_buffers,
            "  Output buffers not ready and fed by operations:",
        )
        ok = False

    # Check if there is both ready and non-ready
    if len(not_ready_input_buffers) > 0 and len(not_ready_output_buffers) > 0:
        WARN(f"Neither input nor output buffers of op {op} are ready")
        print_non_ready_buffers(
            not_ready_input_buffers, "  Input buffers not ready and fed by operations:"
        )
        print_non_ready_buffers(
            not_ready_output_buffers,
            "  Output buffers not ready and fed by operations:",
        )
        ok = False
    return ok


def get_buffer_streams_in_phase(device, graph, buffer):
    streams_in_phase = TTObjectIDDict()
    # Get all streams for the buffer
    for s_id, stream in graph.temporal_epoch.streams.items():
        # if stream.get_buffer_id() is None:
        #     print (f"Stream {stream} has no buffer id")
        if stream.get_buffer_id() == buffer.id():
            phase = device.get_stream_phase(stream.loc(), stream.stream_id())
            if phase == stream.phase_id():
                streams_in_phase.add(stream)
    return streams_in_phase


# Goes through all inputs of an op and checks if there is a misbalance between
# the number of inputs that are ready and the number of inputs that are not ready.
def check_inputs_ready_misbalance(context, graph, device_id, op):
    """
    Looking for inputs ready misbalance
    """
    device = context.devices[device_id]
    input_buffers = graph.get_buffers(op, "out")
    ok = True

    buffer_ready = dict()
    buffer_state = dict()
    for buffer_id, buffer in input_buffers.items():
        if buffer.is_input_of_pipe() and buffer.is_output_of_pipe():
            # WARN (f"Ignoring buffer {buffer} as it is both input and output of a pipe")
            continue

        # Get all streams for the buffer
        for s_id, stream in get_buffer_streams_in_phase(device, graph, buffer).items():
            buffer_ready[buffer_id], buffer_state[buffer_id] = get_buffer_ready_state(
                context, graph, device_id, stream, buffer
            )

    # If there is a mix of ready/non-ready report a warning
    if True in buffer_ready.values() and False in buffer_ready.values():
        ready_ops = set()
        non_ready_ops = set()
        WARN(f"Mix of ready/non-ready buffers for op {op}")
        for buffer_id, buffer_ready in buffer_ready.items():
            buffer = graph.temporal_epoch.buffers[buffer_id]
            state = buffer_state[buffer_id]
            src_buffers = graph.get_buffers(buffer.output_of_pipes, "in")
            src_ops = {
                src_buffers[src_buffer_id].root["md_op_name"]
                for src_buffer_id in src_buffers
            }
            if buffer_ready:
                ready_ops.update(src_ops)
            else:
                non_ready_ops.update(src_ops)
        WARN(f"  Ready buffers fed by operations:     {', '.join(list(ready_ops))}")
        WARN(f"  Non-ready buffers fed by operations: {', '.join(list(non_ready_ops))}")
        ok = False
    return ok


# go through all operation in topological
# order and search for the first stream
# that is not done and not active
def find_ops_with_hang(context, graph, device_id):
    """
    Looking for hangs in a graph
    """
    ops_with_hang = TTObjectIDDict()
    for op_id, op in graph.ops.items():
        i_ok = check_inputs_ready_misbalance(context, graph, device_id, op)
        io_ok = check_input_output_misbalance(context, graph, device_id, op)
        if not i_ok or not io_ok:
            ops_with_hang.add(op)

    return ops_with_hang


def print_hung_ops(ops_with_hang_list):
    column_format = [
        {"key_name": "device", "title": "Device ID", "formatter": None},
        {"key_name": "epoch", "title": "Epoch ID", "formatter": None},
        {"key_name": "graph", "title": "Graph Name", "formatter": None},
        {"key_name": "src", "title": "Source", "formatter": None},
        {"key_name": "dst", "title": "Destination", "formatter": None},
        {
            "key_name": "hang",
            "title": "Hang Reason",
            "formatter": lambda x: f"{util.CLR_RED}{x}{util.CLR_END}",
        },
        {"key_name": "stream", "title": "Stream", "formatter": None},
        {"key_name": "info", "title": "Additional Stream Info", "formatter": None},
    ]
    table = util.TabulateTable(column_format)
    table.rows = []
    only_show_first_row = True
    multiple_streams_per_link_collapsed = False
    for element in ops_with_hang_list:
        for src, dst_streams in element["operations"].items():
            for dst, details in dst_streams.items():
                row = {
                    "device": element["device_id"],
                    "epoch": element["epoch_id"],
                    "graph": element["graph_name"],
                    "src": src,
                    "dst": dst,
                }
                first_row_for_link = True
                for stream_details in details:
                    stream_coords, hang_message, additional_info = stream_details
                    row.update(
                        {
                            "hang": hang_message,
                            "stream": stream_coords,
                            "info": additional_info,
                        }
                    )
                    if only_show_first_row:
                        if len(details) > 1:
                            more_streams_str = f"... {len(details)-1} more"
                            if row["stream"] is None:
                                row["stream"] = (more_streams_str,)
                            elif type(row["stream"]) is tuple:
                                row["stream"] += (more_streams_str,)
                            else:
                                WARN(
                                    f"Unhandled type for row['stream']: {type(row['stream'])}"
                                )

                            table.add_row(None, row)
                            multiple_streams_per_link_collapsed = False
                            break
                    if first_row_for_link:
                        table.add_row(None, row)
                    row = {"device": "", "epoch": "", "graph": "", "src": "", "dst": ""}
                    first_row_for_link = False

    if table.rows:
        print(table)
        if multiple_streams_per_link_collapsed:
            print(
                f"Note: Some src->dest links contain multiple hung streams, only the first one is shown."
            )


def read_device_epochs(device):
    """
    Read all cores on a device and return a dictionary of epoch_id to core locations
    :return: { epoch_id : [ core_loc ] }
    """
    cte = device.read_core_to_epoch_mapping("functional_workers")
    epoch_to_core_locs = dict()
    for loc, epoch_id in cte.items():
        if epoch_id not in epoch_to_core_locs:
            epoch_to_core_locs[epoch_id] = set()
        epoch_to_core_locs[epoch_id].add(loc)

    return epoch_to_core_locs


def read_current_epochs(context):
    """
    Probing all devices for all active cores and their current epochs
    :return: { device_id : { epoch_id : [ core_loc ] } }
    """
    device_id_to_epoch_dict = dict()

    for device_id, _ in context.netlist.device_graph_names().items():
        device = context.devices[device_id]
        device_id_to_epoch_dict[device_id] = read_device_epochs(device)
    return device_id_to_epoch_dict


def run(cmd_text, context, ui_state=None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    verbose = 0 if args["<verbose>"] is None else int(args["<verbose>"])

    if verbose:
        # Turn on tracing for function calls in this module. This prints docstrings and function call argument values.
        # FIX: This is sticky across all calls of the same command
        util.decorate_all_module_functions_for_tracing(sys.modules[__name__])

    LOG(f"Running hang analysis (verbose={verbose})")

    navigation_suggestions = []
    queue_ops_missing_data = []
    ops_with_hang_list = []

    all_good = True

    # A. Run analysis on all devices
    device_id_to_epoch_dict = read_current_epochs(context)
    for device_id in device_id_to_epoch_dict:
        epoch_id_list = list(device_id_to_epoch_dict[device_id].keys())
        epoch_id_list.sort()
        if len(epoch_id_list) > 1:
            # FINISH: This scenario has not been tested
            WARN(f"More than one epoch running on device {device_id}):")
        LOG(f"Epochs running on device {device_id}: {util.space_join(epoch_id_list)}")

        for epoch_id in epoch_id_list:
            graph_name = context.netlist.get_graph_name(epoch_id, device_id)
            if graph_name is None:
                LOG(
                    f"- Skipping epoch {epoch_id}, as there is no information on epoch {epoch_id} in the netlist"
                )
                continue
            LOG(f"Analyzing graph {graph_name} ( epoch {epoch_id} )")
            with util.LOG_INDENT():
                graph = context.netlist.graph(graph_name)

                # -- 1. Check input queues
                queue_ops_errors = get_input_queues_without_data(
                    context, device_id, graph
                )
                if len(queue_ops_errors) > 0:
                    queue_ops_missing_data.append(
                        {
                            "device_id": device_id,
                            "epoch_id": epoch_id,
                            "graph_name": graph_name,
                            "operations": queue_ops_errors,
                        }
                    )
                    util.ERROR(
                        f"Found {len(queue_ops_errors)} input queues without data: {list (queue_ops_errors.keys())}"
                    )
                    all_good = False

                # -- 2. Check hung ops
                ops_with_hang = find_ops_with_hang(context, graph, device_id)
                if ops_with_hang:
                    ops_with_hang_list.append(
                        {
                            "device_id": device_id,
                            "epoch_id": epoch_id,
                            "graph_name": graph_name,
                            "operations": ops_with_hang,
                        }
                    )
                    all_good = False

    if all_good:
        util.INFO("No issues detected")
    return navigation_suggestions
