# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  hq

Description:
  Prints the summary of queues on the host

Examples:
  hq
"""

import tt_util as util
import tt_device
from tt_netlist import Queue
from tabulate import tabulate

command_metadata = {
    "long": "host-queue",
    "short": "hq",
    "type": "high-level",
    "description": __doc__,
}


def run(cmd_txt, context, ui_state):
    table = []

    for graph_name in context.netlist.graph_names():
        print(f"{util.CLR_INFO}HOST queues for graph {graph_name}{util.CLR_END}")
        graph = context.netlist.graph(graph_name)

        for buffer_id, buffer in graph.temporal_epoch.buffers.items():
            buffer_data = buffer.root
            if buffer_data["dram_io_flag_is_remote"] != 0:
                dram_addr = (
                    buffer_data["dram_addr"] & 0x3FFFFFFF
                )  # Clear upper 2 bits, that carries host channel already.
                dram_chan = buffer_data["dram_chan"]
                chip_id = buffer_data["chip_id"][0]
                rdptr = tt_device.SERVER_IFC.host_dma_read(
                    chip_id, dram_addr, dram_chan
                )
                wrptr = tt_device.SERVER_IFC.host_dma_read(
                    chip_id, dram_addr + 4, dram_chan
                )
                slot_size_bytes = buffer_data["size_tiles"] * buffer_data["tile_size"]
                queue_size_bytes = slot_size_bytes * buffer_data["q_slots"]
                occupancy = Queue.occupancy(buffer_data["q_slots"], wrptr, rdptr)

                # IMPROVE: Duplicated from print_dram_queue_summary. Merge into one function.
                input_buffer_op_name_list = []
                for other_buffer_id in graph.get_connected_buffers(
                    [buffer.id()], "src"
                ):
                    input_buffer_op_name_list.append(
                        graph.temporal_epoch.buffers.find_id(other_buffer_id).root[
                            "md_op_name"
                        ]
                    )
                output_buffer_op_name_list = []
                for other_buffer_id in graph.get_connected_buffers(
                    [buffer.id()], "dest"
                ):
                    output_buffer_op_name_list.append(
                        graph.temporal_epoch.buffers.find_id(other_buffer_id).root[
                            "md_op_name"
                        ]
                    )

                input_ops = f"{' '.join (input_buffer_op_name_list)}"
                output_ops = f"{' '.join (output_buffer_op_name_list)}"

                table.append(
                    [
                        buffer.id(),
                        buffer_data["md_op_name"],
                        input_ops,
                        output_ops,
                        buffer_data["dram_buf_flag"],
                        buffer_data["dram_io_flag"],
                        f"0x{dram_addr:x}",
                        f"{dram_chan}",
                        f"{rdptr}",
                        f"{wrptr}",
                        occupancy,
                        buffer_data["q_slots"],
                        queue_size_bytes,
                    ]
                )

    if len(table) > 0:
        print(
            tabulate(
                table,
                headers=[
                    "Buffer name",
                    "Op",
                    "Input Ops",
                    "Output Ops",
                    "dram_buf_flag",
                    "dram_io_flag",
                    "Address",
                    "Channel",
                    "RD ptr",
                    "WR ptr",
                    "Occupancy",
                    "Q slots",
                    "Q Size [bytes]",
                ],
                disable_numparse=True,
            )
        )
    else:
        print("No host queues found")
