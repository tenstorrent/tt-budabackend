# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  op-map

Description:
  Shows a table of all operations in the current epoch, along with their location
  in the netlist and the NOC.

Examples:
  om
"""

from tabulate import tabulate
from tt_coordinate import OnChipCoordinate

command_metadata = {"short": "om", "type": "high-level", "description": __doc__}


def run(cmd_text, context, ui_state=None):
    rows = []
    for graph_id, graph in context.netlist.graphs.items():
        graph_name = graph.id()
        epoch_id = context.netlist.graph_name_to_epoch_id(graph_name)
        device_id = context.netlist.graph_name_to_device_id(graph_name)
        device = context.devices[device_id]
        for op_name in graph.op_names():
            op = graph.root[op_name]
            loc = OnChipCoordinate(*op["grid_loc"], "netlist", device)
            row = [
                f"{graph_name}/{op_name}",
                op["type"],
                epoch_id,
                f"{graph.root['target_device']}",
                f"{loc.to_str('netlist')}",
                f"{loc.to_str('nocTr')}",
                f"{op['grid_size']}",
            ]
            rows.append(row)

    print(
        tabulate(
            rows,
            headers=[
                "Graph/Op",
                "Op type",
                "Epoch",
                "Device",
                "Netlist Loc",
                "NOC Tr",
                "Grid Size",
            ],
            disable_numparse=True,
        )
    )

    return None
