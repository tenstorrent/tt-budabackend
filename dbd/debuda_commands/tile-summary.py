# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  t <tile-id> [--raw]

Options:
  --raw   If specified, prints raw bytes.

Description:
  Prints tile for the current stream in the currently active phase. If --raw is specified, it prints raw bytes.

Examples:
  s 0,2 4; t 1         # Must be on a stream with a buffer
  s 0,2 4; t 1 --raw   # Prints raw bytes
"""

from tabulate import tabulate
from debuda import UIState
import tt_util as util
from tt_coordinate import OnChipCoordinate
from docopt import docopt

from tt_debuda_server import debuda_server_not_supported

command_metadata = {"short": "t", "type": "high-level", "description": __doc__}


# converts data format to string
def get_data_format_from_string(str):
    data_format = {}
    data_format["Float32"] = 0
    data_format["Float16"] = 1
    data_format["Bfp8"] = 2
    data_format["Bfp4"] = 3
    data_format["Bfp2"] = 11
    data_format["Float16_b"] = 5
    data_format["Bfp8_b"] = 6
    data_format["Bfp4_b"] = 7
    data_format["Bfp2_b"] = 15
    data_format["Lf8"] = 10
    data_format["UInt16"] = 12
    data_format["Int8"] = 14
    data_format["Tf32"] = 4
    if str in data_format:
        return data_format[str]
    return None


# gets information about stream buffer in l1 cache from blob
def get_l1_buffer_info_from_blob(device_id, graph, loc, stream_id, phase):
    buffer_addr = 0
    msg_size = 0
    buffer_size = 0

    stream_loc = (device_id, *loc.to("nocTr"), stream_id, phase)
    stream = graph.get_streams(stream_loc).first()

    if stream.root.get("buf_addr"):
        buffer_addr = stream.root.get("buf_addr")
        buffer_size = stream.root.get("buf_size")
        msg_size = stream.root.get("msg_size")
    return buffer_addr, buffer_size, msg_size


# Prints a tile (message) from a given buffer
def dump_message_xy(context, ui_state: UIState, tile_id, raw):
    is_tile = raw == 0
    device_id = ui_state.current_device_id
    graph_name = ui_state.current_graph_name
    graph = context.netlist.graph(graph_name)
    current_device = context.devices[device_id]
    loc, stream_id = ui_state.current_location, ui_state.current_stream_id
    current_phase = current_device.get_stream_phase(loc, stream_id)
    try:
        buffer_addr, buffer_size, msg_size = get_l1_buffer_info_from_blob(
            device_id, graph, loc, stream_id, current_phase
        )
    except:
        util.ERROR(f"No information")
        return
    print(
        f"{loc.to_str()} buffer_addr: 0x{(buffer_addr):08x} buffer_size: 0x{buffer_size:0x} msg_size:{msg_size}"
    )
    if buffer_addr > 0 and buffer_size > 0 and msg_size > 0:
        if tile_id > 0 and tile_id <= buffer_size / msg_size:
            msg_addr = buffer_addr + (tile_id - 1) * msg_size
            if is_tile:
                # 1. Get the op name through coordinates.
                stream_loc = (device_id, *loc.to("nocTr"), stream_id, current_phase)
                stream = graph.get_streams(stream_loc).first()
                if stream is None:
                    util.ERROR(f"Cannot find stream {stream_loc}")
                    return
                bid = stream.get_buffer_id()
                if bid is None:
                    util.ERROR(f"Cannot find buffer for stream {stream_loc}")
                    return
                buffer = graph.temporal_epoch.buffers[bid]
                op_name = graph.location_to_op_name(loc)

                # 2. Get the operation so we can get the data format
                op = graph.root[op_name]
                assert (
                    buffer.root["md_op_name"] == op_name
                )  # Make sure the op name is the same as the one in the buffer itself

                if buffer.is_input_of_pipe():
                    data_format_str = op["in_df"][
                        0
                    ]  # FIX: we are picking the first from the list
                else:
                    data_format_str = op["out_df"]

                data_format = get_data_format_from_string(data_format_str)

                # 3. Dump the tile
                try:
                    current_device.dump_tile(loc, msg_addr, msg_size, data_format)
                except debuda_server_not_supported as e:
                    util.ERROR("Error while trying to dump tile.")
                    raise e
            else:
                current_device.dump_memory(loc, msg_addr, msg_size)
        else:
            util.ERROR(f"Message id should be in range (1, {buffer_size//msg_size})")
    else:
        util.ERROR("Not enough data in blob.yaml")


def run(cmd_text, context, ui_state: UIState = None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    try:
        tile_id = int(args["<tile-id>"])
    except ValueError:
        util.ERROR("Tile ID must be an integer")
        return None

    raw = 1 if args["--raw"] else 0

    dump_message_xy(context, ui_state, tile_id, raw)

    return None
