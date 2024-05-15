# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  wxy <core-loc> <addr> <data>

Description:
  Writes data word to address 'addr' at noc0 location x-y of the current chip.

Arguments:
  core-loc    Either X-Y or R,C location of a core, or dram channel (e.g. ch3)
  addr        Address to read from
  data        Data to write

Examples:
  wxy 18-18 0x0 0x1234
"""

command_metadata = {"short": "wxy", "type": "low-level", "description": __doc__}

from debuda import UIState
import tt_device
from tt_coordinate import OnChipCoordinate
from docopt import docopt


# A helper to print the result of a single PCI read
def print_a_pci_write(x, y, addr, val, comment=""):
    print(f"{x}-{y} 0x{addr:08x} ({addr}) <= 0x{val:08x} ({val:d})")


def run(cmd_text, context, ui_state: UIState = None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])

    core_loc_str = args["<core-loc>"]
    addr = int(args["<addr>"], 0)
    data = int(args["<data>"], 0)

    current_device_id = ui_state.current_device_id
    current_device = context.devices[current_device_id]
    core_loc = OnChipCoordinate.create(core_loc_str, device=current_device)

    tt_device.SERVER_IFC.pci_write_xy(
        ui_state.current_device_id, *core_loc.to("nocVirt"), 0, addr, data=data
    )
    print_a_pci_write(*core_loc.to("nocTr"), addr, data)

    return None
