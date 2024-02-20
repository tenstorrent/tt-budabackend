# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  riscv ( halt | step | cont | status )                                 [ -r <risc> ] [ --all ]
  riscv rd [ -a <address> ]                                             [ -r <risc> ] [ --all ]
  riscv wr [ -a <address> ] [ -d <data> ]                               [ -r <risc> ] [ --all ]
  riscv bkpt (set | del)  [ -pt <point> ] [ -a <address> ]              [ -r <risc> ] [ --all ]
  riscv wchpt (setr | setw | setrw | del)  [ -pt <point> ] [ -a <address> ] [ -d <data> ] [ -r <risc> ] [ --all ]

Options:
  -r <risc>       RiscV ID (0: brisc, 1-3 triscs, all). [ default: 0 ]
  -a <address>    Address to set breakpoint at. Required for bkpt
  -d <data>       Data argument for commands that need it (bkpt)
  -pt <point>     Index of the breakpoint or watchpoint register. 8 points are supported (0-7)
  --all           Apply to all locations (i.e. not just the currently selected location)

Description:
    Commands for RISC-V debugging:
      halt: Stop the core and enter debug mode
      step: Execute one instruction and reenter debug mode
      cont: Exit debug mode and continue execution
      status: Print the status of the core
      rd: Read a word from memory
      wr: Write a word to memory
      bkpt: Set or delete a breakpoint. Address is required for setting
      wchpt: Set or delete a watchpoint. Address is required for setting

Examples:
    riscv halt                         # Halt brisc
    riscv status                       # Print status
    riscv step                         # Step
    riscv cont                         # Continue
    riscv wr -a 0x0 -d 0x2010006f      # Write a word to address 0
    riscv rd -a 0x0                    # Read a word from address 0
    riscv bkpt set -pt 0 -a 0x1244     # Set breakpoint
    riscv bkpt del -pt 0               # Delete breakpoint
    riscv wchpt setr -pt 0 -a 0xc      # Set a read watchpoint
"""

import tt_util as util
from tt_debug_risc import RiscDebug, RiscLoc, get_risc_name
from docopt import docopt
from debuda import UIState

command_metadata = {"short": "r5", "type": "low-level", "description": __doc__}


def run(cmd_text, context, ui_state: UIState = None):
    args = docopt(command_metadata["description"], argv=cmd_text.split()[1:])

    device_id = ui_state.current_device_id
    device = context.devices[device_id]

    noc_id = 0
    risc_ids = set()
    if args["-r"] is None:
        risc_ids = [ 0 ]
    else:
        if args["-r"] == "all":
            risc_ids = [0, 1, 2, 3]
        else:
            risc_ids = [ int(args["-r"], 0) ]

    if args["--all"]:
        locs = device.get_block_locations(block_type="functional_workers")
    else:
        locs = [ ui_state.current_location ]

    for loc in locs:
        for risc_id in risc_ids:
            where = f"{get_risc_name(risc_id)} { ui_state.current_location.full_str()}"

            risc = RiscDebug(RiscLoc(loc, noc_id, risc_id))
            risc.enable_debug()

            if args["halt"]:
                util.INFO(f"Halting {where}")
                risc.halt()

            elif args["step"]:
                util.INFO(f"Stepping {where}")
                risc.step()

            elif args["cont"]:
                util.INFO(f"Continuing {where}")
                risc.cont()

            elif args["rd"]:
                if args["-a"] is None:
                    util.ERROR("Address is required for memory read")
                else:
                    addr = int(args.get("-a", 0), 0)
                    util.INFO(f"Reading from memory {where}")
                    data = risc.read_memory(addr)
                    util.INFO("  0x{:08x}".format(data))

            elif args["wr"]:
                if args["-a"] is None or args["-d"] is None:
                    util.ERROR("Address and data are required for memory write")
                else:
                    addr = int(args.get("-a", 0), 0)
                    data = int(args.get("-d", 0), 0)
                    util.INFO(f"Writing to memory {where}")
                    risc.write_memory(addr, data)

            elif args["bkpt"]:
                if "set" in args and args["set"]:
                    util.INFO(f"Setting breakpoint at address {args['-a']} for {where}")
                    bkpt_index = int(args.get("-pt", "0"), 0)
                    risc.set_watchpoint_on_pc_address(bkpt_index, int(args["-a"], 0))
                elif "del" in args and args["del"]:
                    # Handle 'bkpt del' case
                    bkpt_index = int(args.get("-pt", "0"), 0)
                    risc.disable_watchpoint(bkpt_index)
                else:
                    util.ERROR("Invalid breakpoint command")

            elif args["wchpt"]:
                if "setr" in args and args["setr"]:
                    util.INFO(f"Setting read watchpoint at address {args['-a']} for {where}")
                    bkpt_index = int(args.get("-pt", "0"), 0)
                    risc.set_watchpoint_on_memory_write(bkpt_index, int(args["-a"], 0))
                elif "setw" in args and args["setw"]:
                    util.INFO(f"Setting write watchpoint at address {args['-a']} for {where}")
                    bkpt_index = int(args.get("-pt", "0"), 0)
                    risc.set_watchpoint_on_memory_read(bkpt_index, int(args["-a"], 0))
                elif "setrw" in args and args["setrw"]:
                    util.INFO(
                        f"Setting read/write watchpoint at address {args['-a']} for {where}"
                    )
                    bkpt_index = int(args.get("-pt", "0"), 0)
                    risc.set_watchpoint_on_memory_access(bkpt_index, int(args["-a"], 0))
                elif "del" in args and args["del"]:
                    bkpt_index = int(args.get("-pt", "0"), 0)
                    risc.disable_watchpoint(bkpt_index)
                else:
                    util.ERROR("Invalid watchpoint command")

            elif args["status"]:
                halted = risc.is_halted()
                if halted:
                    PC = risc.read_gpr(33)
                    util.DEBUG(f"  HALTED PC=0x{PC:08x} - {where}")
                else:
                    util.DEBUG(f"  RUNNING - {where}")

    return None
