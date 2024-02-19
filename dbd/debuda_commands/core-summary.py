# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""UNDER DEVELOPMENT
"""

command_metadata = {
    "short": "c",
    "type": "dev",
    "description": "Shows summary for core 'x-y'.",
}

from debuda import UIState
import tt_util as util


def run(args, context, ui_state: UIState = None):
    noc0_loc = int(args[1]), int(args[2])
    current_device_id = ui_state.current_device_id
    current_device = context.devices[current_device_id]

    output_table = dict()

    # 1. Get epochs for core
    core_epochs = util.set()
    epoch = get_epoch_id(noc0_loc)
    core_epochs.add(epoch)

    output_table[f"epoch{'s' if len(core_epochs) > 1 else ''}"] = " ".join(
        list({str(e) for e in core_epochs})
    )

    # Finally, print the table:
    util.print_columnar_dicts([output_table], [f"Core {noc0_loc[0]}-{noc0_loc[1]}"])

    navigation_suggestions = []
    return navigation_suggestions
