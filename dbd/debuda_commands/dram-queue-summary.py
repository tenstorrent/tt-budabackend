# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  dq

Description:
  Prints DRAM queue summary

Examples:
  dq
"""

import tt_util as util
import tt_device
from tt_netlist import Queue
from tabulate import tabulate

command_metadata = {
    "long": "dram-queue",
    "short": "dq",
    "type": "high-level",
    "description": __doc__,
}


def run(cmd_text, context, ui_state=None):
    table = []
    for graph_name in context.netlist.graph_names():
        print(f"{util.CLR_INFO}DRAM queues for graph {graph_name}{util.CLR_END}")
        graph = context.netlist.graph(graph_name)
        device_id = context.netlist.graph_name_to_device_id(graph_name)
        device = context.devices[device_id]

        for buffer_id, buffer in graph.temporal_epoch.buffers.items():
            buffer_data = buffer.root
            if (
                buffer_data["dram_buf_flag"] != 0
                or (
                    buffer_data["dram_io_flag"] != 0
                    and buffer_data["dram_io_flag_is_remote"] == 0
                )
            ) and not buffer.replicated:
                dram_chan = buffer_data["dram_chan"]
                dram_addr = buffer_data["dram_addr"]
                dram_loc = device.DRAM_CHANNEL_TO_NOC0_LOC[dram_chan]
                rdptr = tt_device.SERVER_IFC.pci_read_xy(
                    device_id, dram_loc[0], dram_loc[1], 0, dram_addr
                )
                wrptr = tt_device.SERVER_IFC.pci_read_xy(
                    device_id, dram_loc[0], dram_loc[1], 0, dram_addr + 4
                )
                slot_size_bytes = buffer_data["size_tiles"] * buffer_data["tile_size"]
                queue_size_bytes = slot_size_bytes * buffer_data["q_slots"]
                occupancy = Queue.occupancy(buffer_data["q_slots"], wrptr, rdptr)

                input_buffer_op_name_list = []
                for other_buffer_id in graph.get_connected_buffers(
                    [buffer.id()], "src"
                ):
                    input_buffer_op_name_list.append(
                        str(
                            graph.get_buffers(other_buffer_id)
                            .first()
                            .root["md_op_name"]
                        )
                    )
                output_buffer_op_name_list = []
                for other_buffer_id in graph.get_connected_buffers(
                    [buffer.id()], "dest"
                ):
                    output_buffer_op_name_list.append(
                        str(
                            graph.get_buffers(other_buffer_id)
                            .first()
                            .root["md_op_name"]
                        )
                    )

                input_ops = (
                    f"{' '.join (input_buffer_op_name_list)}"
                    if input_buffer_op_name_list
                    else ""
                )
                output_ops = (
                    f"{' '.join (output_buffer_op_name_list)}"
                    if output_buffer_op_name_list
                    else ""
                )
                table.append(
                    [
                        buffer.id(),
                        buffer_data["md_op_name"],
                        input_ops,
                        output_ops,
                        buffer_data["dram_buf_flag"],
                        buffer_data["dram_io_flag"],
                        dram_chan,
                        f"0x{dram_addr:x}",
                        f"{rdptr}",
                        f"{wrptr}",
                        (
                            occupancy
                            if occupancy == 0
                            else f"{util.CLR_RED}{occupancy}{util.CLR_END}"
                        ),
                        buffer_data["q_slots"],
                        queue_size_bytes,
                    ]
                )

    print(
        tabulate(
            table,
            headers=[
                "Buffer ID",
                "Op/Queue",
                "Input Ops",
                "Output Ops",
                "dram_buf_flag",
                "dram_io_flag",
                "Channel",
                "Address",
                "RD ptr",
                "WR ptr",
                "Occupancy",
                "Slots",
                "Queue Size [bytes]",
            ],
            disable_numparse=True,
        )
    )
