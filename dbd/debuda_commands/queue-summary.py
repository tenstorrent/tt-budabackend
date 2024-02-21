# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  q [<queue-name> [<start-addr> [<num-bytes>]]]

Arguments:
    queue-name    The name of the queue to show.
    start-addr    The start address of the queue to show.
    num-bytes     The number of bytes to show.

Description:
  Prints summary of all queues. If the arguments are supplied, it prints the given queue contents.

Examples:
  q
  q input0
  q input0 32 256
"""

command_metadata = {"short": "q", "type": "high-level", "description": __doc__}

from typing import Sequence
import tt_util as util
from tt_object import TTObjectIDDict, DataArray
import tt_device
from tt_graph import Queue
from docopt import docopt

# IMPROVE: More details available. See struct tt_queue_header


def get_queue_data(context, queue):
    q_data = queue.root
    q_data["outputs"] = queue.outputs_as_str()
    queue_locations = []
    if queue.is_host():  # This queues is on the host
        q_data["ch_addr"] = q_data["host"]
        target_device = int(q_data["target_device"])
        for queue_position in range(len(q_data["host"])):
            dram_place = q_data["host"][queue_position]
            if isinstance(dram_place, Sequence):
                host_chan = dram_place[0]
                host_addr = dram_place[1]
            elif type(dram_place) is int:
                # Host Queues back-compat support for no mem-channel, default to 0x0.
                host_chan = 0
                host_addr = dram_place[0]
            else:
                assert False, f"Unexpected Host queue addr format. Current format: {type(dram_place)}, expected Sequence or int"

            rdptr = (
                tt_device.SERVER_IFC.host_dma_read(target_device, host_addr, host_chan)
                & 0xFFFF
            )  # ptrs are 16-bit
            wrptr = (
                tt_device.SERVER_IFC.host_dma_read(
                    target_device, host_addr + 4, host_chan
                )
                & 0xFFFF
            )
            entries = q_data["entries"]
            occupancy = Queue.occupancy(entries, wrptr, rdptr)
            queue_locations.append((rdptr, wrptr, occupancy))
    else:
        device_id = q_data["target_device"]
        device = context.devices[device_id]
        entries = q_data["entries"]
        q_data["ch_addr"] = q_data["dram"]
        for queue_position in range(len(q_data["dram"])):
            dram_place = q_data["dram"][queue_position]
            dram_chan = dram_place[0]
            dram_addr = dram_place[1]
            dram_loc = device.DRAM_CHANNEL_TO_NOC0_LOC[dram_chan]
            rdptr = (
                device.pci_read_xy(dram_loc[0], dram_loc[1], 0, dram_addr) & 0xFFFF
            )  # ptrs are 16-bit
            wrptr = (
                device.pci_read_xy(dram_loc[0], dram_loc[1], 0, dram_addr + 4) & 0xFFFF
            )
            occupancy = Queue.occupancy(entries, wrptr, rdptr)
            queue_locations.append((rdptr, wrptr, occupancy))
    return q_data, queue_locations


# Returns word array
def read_queue_contents(context, queue, start_addr, num_bytes):
    util.INFO(f"Contents of queue {queue.id()}:")
    num_words = (num_bytes - 1) // 4 + 1
    ret_val = TTObjectIDDict()
    q_data = queue.root
    device_id = q_data["target_device"]
    device = context.devices[device_id]

    if "host" in q_data:  # This queues is on the host
        for queue_position in range(len(q_data["host"])):
            dram_place = q_data["host"][queue_position]

            if isinstance(dram_place, Sequence):
                host_chan = dram_place[0]
                host_addr = dram_place[1]
            elif type(dram_place) is int:
                # Host Queues back-compat support for no mem-channel, default to 0x0.
                host_chan = 0
                host_addr = dram_place[0]
            else:
                assert False, f"Unexpected Host queue addr format. Current format: {type(dram_place)}, expected Sequence or int"

            da = DataArray(f"host-0x{host_addr:08x}-ch{host_chan}-{num_words * 4}")
            for i in range(num_words):
                data = tt_device.SERVER_IFC.host_dma_read(
                    device_id, host_addr + start_addr + i * 4, host_chan
                )
                da.data.append(data)
            ret_val.add(da)
    else:
        for queue_position in range(len(q_data["dram"])):
            dram_place = q_data["dram"][queue_position]
            dram_chan = dram_place[0]
            addr = dram_place[1]
            dram_loc = device.DRAM_CHANNEL_TO_NOC0_LOC[dram_chan]
            da = DataArray(f"dram-ch{dram_chan}-0x{addr:08x}-{num_words * 4}")
            for i in range(num_words):
                data = device.pci_read_xy(
                    dram_loc[0], dram_loc[1], 0, addr + start_addr + i * 4
                )
                da.data.append(data)
            ret_val.add(da)
    return ret_val


def run(cmd_text, context, ui_state=None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])

    queue_id = args["<queue-name>"] if args["<queue-name>"] else None

    column_format = [
        {"key_name": "entries", "title": "Entries", "formatter": None},
        {"key_name": "wrptr", "title": "Wr", "formatter": None},
        {"key_name": "rdptr", "title": "Rd", "formatter": None},
        {
            "key_name": "occupancy",
            "title": "Occ",
            "formatter": lambda x: (
                (f"{x}") if x == "0" else (f"{util.CLR_RED}{x}{util.CLR_END}")
            ),
        },
        {"key_name": "type", "title": "Type", "formatter": None},
        {"key_name": "target_device", "title": "Device", "formatter": None},
        {"key_name": "loc", "title": "Loc", "formatter": None},
        {"key_name": None, "title": "Name", "formatter": None},
        {"key_name": "input", "title": "Input", "formatter": None},
        {"key_name": "outputs", "title": "Outputs", "formatter": None},
        {
            "key_name": "ch_addr",
            "title": "HOST/DRAM ch-addr",
            "formatter": lambda x: (
                ", ".join(Queue.to_str(e[0], e[1]) for e in x) if x != "-" else "-"
            ),
        },
    ]

    table = util.TabulateTable(column_format)

    # Whether to print all DRAM positions or aggregate them.
    # IMPROVE: Pass this through args
    show_each_queue_dram_location = False

    for q_id, queue in context.netlist.queues().items():
        q_name = queue.id()
        if queue_id and queue_id != q_name:
            continue
        q_data, queue_locations = get_queue_data(context, queue)

        def aggregate_queue_locations_to_str(queue_locations, i):
            mini = min(tup[i] for tup in queue_locations)
            maxi = max(tup[i] for tup in queue_locations)
            return f"{mini}" if mini == maxi else f"{mini}..{maxi}"

        num_queue_locations = len(queue_locations)
        show_index = num_queue_locations > 1
        if show_each_queue_dram_location:
            for i, qt in enumerate(queue_locations):
                q_data["rdptr"] = qt[0]
                q_data["wrptr"] = qt[1]
                q_data["occupancy"] = qt[2]
                table.add_row(q_name if not show_index else f"{q_name}[{i}]", q_data)
        else:  # Show only aggregate
            q_data["rdptr"] = aggregate_queue_locations_to_str(queue_locations, 0)
            q_data["wrptr"] = aggregate_queue_locations_to_str(queue_locations, 1)
            q_data["occupancy"] = aggregate_queue_locations_to_str(queue_locations, 2)
            table.add_row(
                q_name if not show_index else f"{q_name}[0..{num_queue_locations}]",
                q_data,
            )

    print(table)

    if queue_id:
        queue = context.netlist.queues().find_id(queue_id)
        if not queue:
            util.ERROR(f"Cannot find queue with id '{queue_id}'")
        else:
            queue_start_addr = (
                int(args["<start-addr>"], 0) if args["<start-addr>"] else 0
            )
            queue_num_bytes = (
                int(args["<num-bytes>"], 0) if args["<num-bytes>"] else 128
            )
            alignment_bytes = queue_start_addr % 4
            queue_start_addr -= alignment_bytes
            queue_num_bytes += alignment_bytes

            data = read_queue_contents(
                context, queue, queue_start_addr, queue_num_bytes
            )
            # for d in data:
            #     d.to_bytes_per_entry(1)
            for d in data:
                print(data[d])
