# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  ddb <trisc-id> <num-words> [<format>] [<core-loc>] [<device-id>]

Arguments:
  trisc-id      Trisc ID (0|1|2)
  num-words     Number of words to dump
  format        Print format (i8, i16, i32, hex8, hex16, hex32). Default: hex32
  core-loc      Either X-Y or R,C location of the core
  device-id     Optional device-id

Description:
  Prints a debug buffer.

Examples:
  ddb 1 16
  ddb 0 32 hex32 18-18
"""

from docopt import docopt
from cgi import print_form
from debuda import UIState
from tt_object import DataArray
import tt_device
import tt_util as util
from tt_coordinate import OnChipCoordinate

command_metadata = {"short": "ddb", "type": "low-level", "description": __doc__}


def run(cmd_text, context, ui_state: UIState = None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    trisc_id = int(args["<trisc-id>"])
    num_words = int(args["<num-words>"])
    print_format = args["<format>"] if args["<format>"] else "hex32"
    loc = ui_state.current_location
    device_id = ui_state.current_device_id
    device = context.devices[device_id]
    TRISC_DEBUG_BASE = [71680, 88064, 108544]

    if args["<core-loc>"]:
        loc = OnChipCoordinate.create(args["<core-loc>"], device=device)
    if args["<device-id>"]:
        device_id = int(args["<device-id>"])
        if device_id not in context.devices:
            util.ERROR(
                f"Invalid device ID ({device_id}). Valid devices IDs: {list(context.devices)}"
            )
            return []

    addr = TRISC_DEBUG_BASE[trisc_id]
    da = DataArray(f"L1-0x{addr:08x}-{num_words * 4}", 4)
    for i in range(num_words):
        data = tt_device.SERVER_IFC.pci_read_xy(
            device_id, *loc.to("nocVirt"), 0, addr + 4 * i
        )
        da.data.append(data)

    is_hex = util.PRINT_FORMATS[print_format]["is_hex"]
    bytes_per_entry = util.PRINT_FORMATS[print_format]["bytes"]

    if bytes_per_entry != 4:
        da.to_bytes_per_entry(bytes_per_entry)
    formated = f"{da._id}\n" + util.dump_memory(
        addr, da.data, bytes_per_entry, 16, is_hex
    )
    print(formated)
