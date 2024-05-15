# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  fd

Description:
  Dumps all stream configuration at the currently selected core.

Examples:
  fd
"""

from debuda import UIState


command_metadata = {"short": "fd", "type": "dev", "description": __doc__}


def run(cmd_text, context, ui_state: UIState = None):
    ui_state.current_device.full_dump_xy(ui_state.current_location)

    return None
