# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  gdb start [--port <port>]
  gdb stop

Description:
  Starts or stops gdb server.

Examples:
  gdb start --port 6767
  gdb stop
"""
from docopt import docopt
from debuda import UIState
import tt_util as util

command_metadata = {
    "short": "gdb",
    "type": "high-level",
    "description": __doc__
}

def run(cmd_text, context, ui_state: UIState = None):
    dopt = docopt(command_metadata["description"], argv=cmd_text.split()[1:])

    if dopt["start"]:
        try:
            port = int(dopt["<port>"])
            ui_state.start_gdb(port)
        except:
            util.ERROR("Invalid port number")
    elif dopt["stop"]:
        ui_state.stop_gdb()
    else:
        util.ERROR("Invalid command")
