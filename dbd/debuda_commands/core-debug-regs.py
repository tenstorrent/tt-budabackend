# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  cdr [ <core-loc> ]

Arguments:
  core-loc    Either X-Y or R,C location of the core

Description:
  Prints the state of the debug registers for core 'x-y'. If coordinates are not supplied, it iterates through all cores.

Examples:
  cdr 18-18
"""

from docopt import docopt
from debuda import UIState
import tt_util as util
from tt_coordinate import OnChipCoordinate

command_metadata = {"short": "cdr", "type": "low-level", "description": __doc__}


def run(cmd_text, context, ui_state: UIState = None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    current_device_id = ui_state.current_device_id
    current_device = context.devices[current_device_id]

    if args["<core-loc>"]:
        core_locations = [
            OnChipCoordinate.create(args["<core-loc>"], device=current_device)
        ]
    else:
        core_locations = current_device.get_block_locations(
            block_type="functional_workers"
        )

    for core_loc in core_locations:
        print(f"=== Debug registers for core {core_loc.to_str()} ===")
        THREADS = ["T0", "T1", "T2", "FW"]

        # Get the register values
        debug_tables = current_device.get_debug_regs(core_loc)

        render_tables = [dict()] * len(THREADS)
        for thread_idx in range(len(THREADS)):
            for reg_id, reg_vals in enumerate(debug_tables[thread_idx]):
                render_tables[thread_idx][f"DBG[{2 * reg_id}]"] = (
                    "0x%04x" % reg_vals["lo_val"]
                )
                render_tables[thread_idx][f"DBG[{2 * reg_id + 1}]"] = (
                    "0x%04x" % reg_vals["hi_val"]
                )

        # Finally, print the table:
        util.print_columnar_dicts(render_tables, [*THREADS])

    navigation_suggestions = []
    return navigation_suggestions
