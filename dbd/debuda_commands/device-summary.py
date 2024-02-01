# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  device [<device-id> [<axis-coordinate> [<cell-contents>]]]

Arguments:
  device-id            ID of the device [default: 0]
  axis-coordinate      Coordinate system for the axis [default: netlist]
                       Supported: netlist, noc0, noc1, nocTr, nocVirt, die, tensix
  cell-contents        A comma separated list of the cell contents [default: op]
                       Supported:
                         op - show operation running on the core with epoch ID in parenthesis
                         block - show the type of the block at that coordinate
                         netlist, noc0, noc1, nocTr, nocVirt, die, tensix - show coordinate

Description:
  Shows a device summary. When no argument is supplied, it iterates through all devices used by the
  currently loaded netlist.

Examples:
  device                 # Shows op mapping for all devices
  device 0 nocTr         # Shows op mapping in nocTr coordinates for device 0
  device 0 netlist nocTr # Shows netlist to nocTr mapping for device 0
"""  # Note: Limit the above comment to 100 characters in width

command_metadata = {
    "short": "d",
    "long": "device",
    "type": "high-level",
    "description": __doc__,
}

import tt_util as util
from tt_coordinate import VALID_COORDINATE_TYPES
from docopt import docopt


def run(cmd_text, context, ui_state=None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])

    if args["<device-id>"]:
        device_id = int(args["<device-id>"])
        if device_id not in context.devices:
            util.ERROR(
                f"Invalid device ID ({device_id}). Valid devices IDs: {list(context.devices)}"
            )
            return []
        devices_list = [device_id]
    else:
        devices_list = list(context.devices.keys())

    axis_coordinate = args["<axis-coordinate>"] or "netlist"
    if axis_coordinate not in VALID_COORDINATE_TYPES:
        util.ERROR(
            f"Invalid axis coordinate type: {axis_coordinate}. Valid types: {VALID_COORDINATE_TYPES}"
        )
        return []

    cell_contents = args["<cell-contents>"] or "op"

    for device_id in devices_list:
        device = context.devices[device_id]
        util.INFO(f"==== Device {device.id()}")

        func_workers = device.get_block_locations(block_type="functional_workers")

        # What to render in each cell
        cell_contents_array = [s.strip() for s in cell_contents.split(",")]

        if "op" in cell_contents_array:
            loc_to_epoch = dict()  # loc -> epoch_id
            op_color_map = dict()  # op_name -> color

            for loc in func_workers:
                loc_to_epoch[loc] = device.get_epoch_id(loc)

            epoch_ids = list(set(loc_to_epoch.values()))
            epoch_ids.sort()
            if len(epoch_ids) > 1:
                util.WARN(
                    f"Device {device_id} has functional workers in multiple epochs: {epoch_ids}"
                )
            else:
                graph_name = context.netlist.get_graph_name(epoch_ids[0], device_id)
                if not graph_name:
                    util.WARN(
                        f"Device {device_id} has no graph in epoch {epoch_ids[0]}"
                    )
                    continue

        def cell_render_function(loc):
            # One string for each of cell_contents_array elements
            cell_contents_str = []

            for ct in cell_contents_array:
                if ct == "op":
                    if loc in func_workers:
                        epoch_id = device.get_epoch_id(loc)
                        graph_name = context.netlist.get_graph_name(epoch_id, device_id)
                        if graph_name:
                            graph = context.netlist.graph(graph_name)
                            op_name = graph.location_to_op_name(loc)
                            if op_name:
                                if (
                                    op_name not in op_color_map
                                ):  # Assign a new color for each op
                                    op_color_map[op_name] = util.clr_by_index(
                                        len(op_color_map)
                                    )
                                cell_contents_str.append(
                                    f"{op_color_map[op_name]}{op_name} ({loc_to_epoch[loc]}){util.CLR_END}"
                                )
                elif ct == "block":
                    block_type = device.get_block_type(loc)
                    cell_contents_str.append(block_type)
                elif ct in VALID_COORDINATE_TYPES:
                    try:
                        coord_str = loc.to_str(ct)
                    except Exception as e:
                        coord_str = "N/A"
                    cell_contents_str.append(coord_str)
                else:
                    util.ERROR(f"Invalid cell contents requested: {ct}")
            return ", ".join(cell_contents_str)

        print(
            device.render(
                legend=[],
                axis_coordinate=axis_coordinate,
                cell_renderer=cell_render_function,
            )
        )

    navigation_suggestions = []
    return navigation_suggestions
