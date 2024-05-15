# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  go [ -d <device> ] [ -l <loc> ]

Description:
  Sets the current device/location.

Examples:
  go -d 0 -l 0,0
"""
import tt_util as util
import tt_commands

command_metadata = {
    "short": "go",
    "type": "high-level",
    "description": __doc__
}

def run(cmd_text, context, ui_state=None):
    dopt = tt_commands.tt_docopt(command_metadata["description"], argv=cmd_text.split()[1:],
                                common_option_names=[ "--device", "--loc" ]
                                )

    for device in dopt.for_each("--device", context, ui_state):
        ui_state.current_device_id = device.id()
        for loc in dopt.for_each("--loc", context, ui_state, device=device):
            ui_state.current_location = loc
            break
        break
