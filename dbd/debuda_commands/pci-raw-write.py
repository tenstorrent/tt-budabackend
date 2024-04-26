# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  pciw <addr> <data>

Arguments:
  addr        Address in PCI BAR to read from.
  data        Data to write to PCI BAR.

Description:
  Writes data to PCI BAR at address 'addr'. The mapping between the addresses and the on-chip data is stored within the Tensix TLBs.

Examples:
  pciw 0x0 0x0
"""

from docopt import docopt
from debuda import UIState
import tt_device

command_metadata = {"short": "pciw", "type": "dev", "description": __doc__}


def run(cmd_text, context, ui_state: UIState = None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    addr = int(args["<addr>"], 0)
    data = int(args["<data>"], 0)
    pci_write_result = tt_device.SERVER_IFC.pci_raw_write(
        ui_state.current_device_id, addr, data
    )
    print(f"PCI WR [0x{addr:x}] <- 0x{data:x}")
    return None
