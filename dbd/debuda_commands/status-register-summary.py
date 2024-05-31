# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  srs [ <verbosity> ] [-d <D>...]

Arguments:
  verbosity    Verbosity level (0, 1 or 2) [default: 0]

Options:
  -d <D>        Device ID. Optional and repeatable. Default: current device

Description:
  Prints BRISC and NCRISC status registers

Examples:
  srs 1
"""

from tabulate import tabulate
import tt_util as util
from docopt import docopt

command_metadata = {
    "short": "srs",
    "long": "status-register",
    "type": "low-level",
    "description": __doc__,
}


def print_status_register_summary(verbosity, device):
    print(
        f"{util.CLR_INFO}Reading status registers on device {device._id}...{util.CLR_END}"
    )

    print("NCRISC status summary:")
    status_descs_rows = device.status_register_summary(
        device.NCRISC_STATUS_REG_ADDR, verbosity
    )
    if status_descs_rows:
        print(
            tabulate(status_descs_rows, headers=["X-Y", "Status", "Status Description"], disable_numparse=True)
        )
    else:
        print("- nothing unusual")

    print("BRISC status summary:")
    status_descs_rows = device.status_register_summary(
        device.BRISC_STATUS_REG_ADDR, verbosity
    )
    if status_descs_rows:
        print(
            tabulate(status_descs_rows, headers=["X-Y", "Status", "Status Description"], disable_numparse=True)
        )
    else:
        print("- nothing unusual")


def run(cmd_text, context, ui_state=None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    if args["<verbosity>"] == None:
        verbosity = 0
    else:
        verbosity = int(args["<verbosity>"], 0)

    devices = args["-d"]
    if devices:
        for device_id_str in devices:
            did = int(device_id_str, 0)
            device = context.devices[did]
            print_status_register_summary(verbosity, device)
    else:
        for device_id, device in context.devices.items():
            print_status_register_summary(verbosity, device)

    return None
