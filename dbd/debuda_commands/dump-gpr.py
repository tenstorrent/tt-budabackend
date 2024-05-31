# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  gpr   [ <reg-list> ] [ <elf-file> ]                 [ -v ] [ -d <device> ] [ -l <loc> ] [ -r <risc> ]

Options:
    <reg-list>                          List of registers to dump, comma-separated
    <elf-file>                          Name of the elf file to use to resolve the source code location

Description:
  Prints all RISC-V registers for BRISC, TRISC0, TRISC1, and TRISC2 on the current core.
  If the core cannot be halted, it prints nothing. If core is not active, an exception
  is thrown.

Examples:
  gpr
  gpr ra,sp,pc
"""
command_metadata = {"short": "gpr", "type": "low-level", "description": __doc__}

from debuda import UIState
from tt_debug_risc import RiscDebug, RiscLoc, RISCV_REGS, get_risc_name, get_register_index
import tt_util as util
import tt_commands
import tabulate
from tt_firmware import ELF

def reg_included(reg_index, regs_to_include):
    if regs_to_include:
        return reg_index in regs_to_include
    return True

def get_register_data(device, server_ifc, loc, args):
    regs_to_include = args["<reg-list>"].split(",") if args["<reg-list>"] else []
    regs_to_include = [ get_register_index(reg) for reg in regs_to_include ]
    riscs_to_include = args["-r"].split(",") if args["-r"] else range(0,4)
    riscs_to_include = range(0,4) if "all" in args["-r"] else [ int(risc) for risc in riscs_to_include ]
    elf_file = args["<elf-file>"] if args["<elf-file>"] else None
    elf = ELF({ "elf" : elf_file }) if elf_file else None
    pc_map = elf.names["elf"]["file-line"] if elf else None

    reg_value = {}
    noc_id = 0     # Always use noc 0

    halted_state={}
    reset_state={}

    # Read the registers
    for risc_id in riscs_to_include:
        risc = RiscDebug(RiscLoc(loc, noc_id, risc_id), ifc=server_ifc, verbose=args["-v"])
        reset_state[risc_id] = risc.is_in_reset()
        if reset_state[risc_id]:
            continue # We cannot read registers from a core in reset

        risc.enable_debug()

        already_halted = risc.is_halted()
        halted_state[risc_id] = already_halted

        if not already_halted:
            risc.halt()         # We must halt the core to read the registers

        if risc.is_halted():
            reg_value[risc_id] = {}
            for reg_id in range(0, 33):
                if regs_to_include and reg_id not in regs_to_include:
                    continue
                reg_val = risc.read_gpr(reg_id)
                reg_value[risc_id][reg_id] = reg_val
            if not already_halted:
                risc.cont() # Resume the core if it was not found halted
        else:
            util.ERROR(f"Core {risc_id} cannot be halted.")

    # Construct the table to print
    table = []
    for reg_id in range(0, 33):
        if regs_to_include and reg_id not in regs_to_include:
            continue

        row = [f"{reg_id} - {RISCV_REGS[reg_id]}"]
        for risc_id in riscs_to_include:
            if risc_id not in reg_value:
                continue
            src_location = ""
            if pc_map and reg_id == 32:
                PC = reg_value[risc_id][reg_id]
                if PC in pc_map:
                    source_loc = pc_map[PC]
                    if source_loc:
                        src_location= f"- {source_loc[0].decode('utf-8')}:{source_loc[1]}"
            row.append(f"0x{reg_value[risc_id][reg_id]:08x}{src_location}" if reg_id in reg_value[risc_id] else "-")
        table.append(row)

    # Print soft reset status
    row = ["Soft reset"]
    for j in riscs_to_include:
        row.append(reset_state[j])
    table.append(row)

    # Print halted status
    row = ["Halted"]
    for j in riscs_to_include:
        row.append(halted_state[j] if not reset_state[j] else "-")
    table.append(row)

    # Print the table
    if len(table) > 0:
        headers = ["Register"]
        for risc_id in range (0, 4):
            headers.append(get_risc_name(risc_id))
        return tabulate.tabulate(
                table, headers=headers, disable_numparse=True
            )
    return None

def run(cmd_text, context, ui_state: UIState = None):
    dopt = tt_commands.tt_docopt(command_metadata["description"], argv=cmd_text.split()[1:],
                               common_option_names=[ "--verbose", "--device", "--loc", "--risc" ])

    for device in dopt.for_each("--device", context, ui_state):
        for loc in dopt.for_each("--loc", context, ui_state, device=device):
            table = get_register_data(device, context.server_ifc, loc, dopt.args)
            if table:
                util.INFO (f"RISC-V registers for location {loc} on device {device.id()}")
                print(table)
            else:
                print(f"No data available for location {loc} on device {device.id()}")
