# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  gpr

Description:
  Prints all RISC-V registers for BRISC, TRISC0, TRISC1, and TRISC2 on the current core.
  If the core cannot be halted, it prints nothing. If core is not active, an exception
  is thrown.

Examples:
  gpr
"""

import tabulate
from debuda import UIState
from tt_debug_risc import RiscDebug, RiscLoc, get_risc_name
import tt_device

command_metadata = {"short": "gpr", "type": "low-level", "description": __doc__}

RISC_REGS = {
    0: "zero",
    1: "ra",
    2: "sp",
    3: "gp",
    4: "tp",
    5: "t0",
    6: "t1",
    7: "t2",
    8: "s0 / fp",
    9: "s1",
    10: "a0",
    11: "a1",
    12: "a2",
    13: "a3",
    14: "a4",
    15: "a5",
    16: "a6",
    17: "a7",
    18: "s2",
    19: "s3",
    20: "s4",
    21: "s5",
    22: "s6",
    23: "s7",
    24: "s8",
    25: "s9",
    26: "s10",
    27: "s11",
    28: "t3",
    29: "t4",
    30: "t5",
    31: "t6",
    32: "PC",
}


def run(cmd_text, context, ui_state: UIState = None):
    result = {}
    device_id = ui_state.current_device_id
    noc_id = 0
    device = context.devices[device_id]
    loc = ui_state.current_location

    halted_state={}

    for i in range(0, 4):
        risc = RiscDebug(RiscLoc(loc, 0, i))
        was_halted = risc.is_halted()
        halted_state[i] = was_halted

        risc.enable_debug()
        risc.halt()
        if risc.is_halted():
            result[i] = {}
            for j in range(0, 33):
                reg_val = risc.read_gpr(j)
                result[i][j] = reg_val
            if not was_halted:
                risc.cont()
        else:
            print(f"Core {i} cannot be paused.")

    table = []
    for i in range(0, 33):
        row = [f"{i} - {RISC_REGS[i]}"]
        for j in range(0, 4):
            row.append(f"0x{result[j][i]:08x}" if j in result else "-")
        table.append(row)

    # Determine soft reset status
    reset_reg = tt_device.SERVER_IFC.pci_read_xy(device_id, *loc.to("nocVirt"), noc_id, 0xffb121b0)
    table.append ([
        "Soft reset",
        (reset_reg >> 11) & 1,
        (reset_reg >> 12) & 1,
        (reset_reg >> 13) & 1,
        (reset_reg >> 14) & 1,
    ])

    # Determine the halted status
    table.append ([
        "Halted",
        halted_state[0],
        halted_state[1],
        halted_state[2],
        halted_state[3],
    ])

    # Print the table
    if len(table) > 0:
        headers = ["Register"]
        for risc_id in range (0, 4):
            headers.append(get_risc_name(risc_id))
        print(
            tabulate.tabulate(
                table, headers=headers
            )
        )

    return None
