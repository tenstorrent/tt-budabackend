# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  gpr

Description:
  Prints all RISC-V registers for TRISC0, TRISC1, TRISC2 and Brisc on current core.
  If the core cannot be paused(halted), it prints nothing. If core is not active, an exception is thrown.

Examples:
  gpr
"""

import tabulate
from tt_debug_risc import RiscDebug, RiscLoc

command_metadata = {
    "short" : "gpr",
    "type" : "low-level",
    "description" : __doc__
}

RISC_REGS = {
    0:'zero', 1:'ra', 2:'sp', 3:'gp', 4:'tp', 5:'t0', 6:'t1', 7:'t2', 8:'s0 / fp', 9:'s1', 10:'a0', 11:'a1', 12:'a2',
    13:'a3', 14:'a4', 15:'a5', 16:'a6', 17:'a7', 18:'s2', 19:'s3', 20:'s4', 21:'s5', 22:'s6', 23:'s7', 24:'s8', 25:'s9',
    26:'s10', 27:'s11', 28:'t3', 29:'t4', 30:'t5', 31:'t6', 32:'PC'}

def run(cmd_text, context, ui_state = None):
    result = {}
    device_id = ui_state['current_device_id']
    loc = ui_state['current_loc']
    for i in range(0,4):
        risc = RiscDebug(RiscLoc(loc, 0, i))
        risc.enable_debug()
        risc.pause()
        if risc.is_paused():
            result[i]={}
            for j in range(0,33):
                reg_val = risc.read_gpr(j)
                result[i][j] = reg_val
            risc.contnue()
        else:
            print(f"Core {i} cannot be paused.")

    table=[]
    for i in range(0,33):
        row = [f"{i} - {RISC_REGS[i]}"]
        for j in range(0,4):
            row.append(f"0x{result[j][i]:08x}" if j in result else '-')
        table.append(row)
    if len(table) > 0:
        print (tabulate.tabulate(table, headers=["Register", "Brisc", "Trisc0", "Trisc1", "Trisc2" ] ))

    return None