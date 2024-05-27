# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  riscv ( halt | step | cont | status )                                             [ -v ] [ -d <device> ] [ -r <risc> ] [ -l <loc> ]
  riscv rd [ <address> ]                                                            [ -v ] [ -d <device> ] [ -r <risc> ] [ -l <loc> ]
  riscv rreg [ <index> ]                                                            [ -v ] [ -d <device> ] [ -r <risc> ] [ -l <loc> ]
  riscv wr [ <address> ] [ <data> ]                                                 [ -v ] [ -d <device> ] [ -r <risc> ] [ -l <loc> ]
  riscv wreg [ <index> ] [ <data> ]                                                 [ -v ] [ -d <device> ] [ -r <risc> ] [ -l <loc> ]
  riscv bkpt (set | del)  [ -pt <point> ] [ <address> ]                             [ -v ] [ -d <device> ] [ -r <risc> ] [ -l <loc> ]
  riscv wchpt (setr | setw | setrw | del)  [ -pt <point> ] [ <address> ] [ <data> ] [ -v ] [ -d <device> ] [ -r <risc> ] [ -l <loc> ]
  riscv reset [ 1 | 0 ]                                                             [ -v ] [ -d <device> ] [ -r <risc> ] [ -l <loc> ]

Options:
  -pt <point>     Index of the breakpoint or watchpoint register. 8 points are supported (0-7)

Description:
    Commands for RISC-V debugging:
      halt: Stop the core and enter debug mode
      step: Execute one instruction and reenter debug mode
      cont: Exit debug mode and continue execution
      status: Print the status of the core
      rd: Read a word from memory
      wr: Write a word to memory
      rreg: Read a word from register
      wreg: Write a word to register
      bkpt: Set or delete a breakpoint. Address is required for setting
      wchpt: Set or delete a watchpoint. Address is required for setting
      reset: Sets (1) or clears (0) the reset signal for the core. If no argument is provided: set and clear.

Examples:
    riscv halt                      # Halt brisc
    riscv status                    # Print status
    riscv step                      # Step
    riscv cont                      # Continue
    riscv wr 0x0 0x2010006f         # Write a word to address 0
    riscv rd 0x0                    # Read a word from address 0
    riscv wreg 1 0xabcd             # Write a word to register 1
    riscv rreg 1                    # read a word to register 1
    riscv bkpt set 0 0x1244         # Set breakpoint
    riscv bkpt del 0                # Delete breakpoint
    riscv wchpt setr 0 0xc          # Set a read watchpoint
    riscv wchpt setr 0 0xc          # Set a read watchpoint
    riscv                           # Set and clear reset (the core will run)
"""
command_metadata = {"short": "rv", "type": "low-level", "description": __doc__}

import tt_util as util
import tt_commands
from tt_debug_risc import RiscDebug, RiscLoc, get_risc_name
from debuda import UIState
from tt_coordinate import OnChipCoordinate
import tt_device


def run_riscv_command(device, loc, risc_id, args):
    """
    Given a command trough args, run the corresponding RISC-V command
    """
    verbose = args["-v"]
    where = f"{get_risc_name(risc_id)} { loc.to_str('netlist')}"

    noc_id = 0
    risc = RiscDebug(RiscLoc(loc, noc_id, risc_id), tt_device.DEBUDA_SERVER_SOCKET_IFC, verbose=verbose)

    if args["halt"]:
        risc.enable_debug()
        util.INFO(f"Halting {where}")
        risc.halt()

    elif args["step"]:
        util.INFO(f"Stepping {where}")
        risc.step()

    elif args["cont"]:
        util.INFO(f"Continuing {where}")
        risc.cont()

    elif args["rd"]:
        if args['<address>'] is None:
            util.ERROR("Address is required for memory read")
        else:
            addr = int(args.get('<address>', 0), 0)
            util.INFO(f"Reading from memory {where}")
            data = risc.read_memory(addr)
            util.INFO("  0x{:08x}".format(data))

    elif args["rreg"]:
        if args['<index>'] is None:
            util.ERROR("Index is required for register read")
        else:
            reg_index = int(args.get('<index>', 0), 0)
            util.INFO(f"Reading from register {reg_index} on {where}")
            data = risc.read_gpr(reg_index)
            util.INFO("  0x{:08x}".format(data))

    elif args["wr"]:
        if args['<address>'] is None or args["<data>"] is None:
            util.ERROR("Address and data are required for memory write")
        else:
            addr = int(args.get('<address>', 0), 0)
            data = int(args.get("<data>", 0), 0)
            util.INFO(f"Writing to memory {where}")
            risc.write_memory(addr, data)

    elif args["wreg"]:
        if args['<index>'] is None or args["<data>"] is None:
            util.ERROR("Index and data are required for register write")
        else:
            reg_index = int(args.get('<index>', 0), 0)
            data = int(args.get("<data>", 0), 0)
            util.INFO(f"Writing to register {reg_index} on {where}")
            risc.write_gpr(reg_index, data)

    elif args["bkpt"]:
        if "set" in args and args["set"]:
            util.INFO(f"Setting breakpoint at address {args['<address>']} for {where}")
            bkpt_index = int(args.get("-pt", "0"), 0)
            risc.set_watchpoint_on_pc_address(bkpt_index, int(args['<address>'], 0))
        elif "del" in args and args["del"]:
            # Handle 'bkpt del' case
            bkpt_index = int(args.get("-pt", "0"), 0)
            util.INFO(f"Deleting breakpoint {bkpt_index} for {where}")
            risc.disable_watchpoint(bkpt_index)
        else:
            util.ERROR("Invalid breakpoint command")

    elif args["wchpt"]:
        if "setr" in args and args["setr"]:
            util.INFO(f"Setting read watchpoint at address {args['<address>']} for {where}")
            bkpt_index = int(args.get("-pt", "0"), 0)
            risc.set_watchpoint_on_memory_write(bkpt_index, int(args['<address>'], 0))
        elif "setw" in args and args["setw"]:
            util.INFO(f"Setting write watchpoint at address {args['<address>']} for {where}")
            bkpt_index = int(args.get("-pt", "0"), 0)
            risc.set_watchpoint_on_memory_read(bkpt_index, int(args['<address>'], 0))
        elif "setrw" in args and args["setrw"]:
            util.INFO(
                f"Setting read/write watchpoint at address {args['<address>']} for {where}"
            )
            bkpt_index = int(args.get("-pt", "0"), 0)
            risc.set_watchpoint_on_memory_access(bkpt_index, int(args['<address>'], 0))
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

    elif args["reset"]:
        if args['1']:
            util.INFO(f"Setting reset for {where}")
            risc.set_reset_signal(1)
        elif args['0']:
            util.INFO(f"Clearing reset for {where}")
            risc.set_reset_signal(0)
        else:
            util.INFO(f"Setting and clearing reset for {where}")
            risc.set_reset_signal(1)
            risc.set_reset_signal(0)

def run(cmd_text, context, ui_state: UIState = None):
    dopt = tt_commands.tt_docopt(command_metadata["description"], argv=cmd_text.split()[1:],
                                common_option_names=[ "--verbose", "--device", "--loc", "--risc" ]
                                )
    for device in dopt.for_each("--device", context, ui_state):
        for loc in dopt.for_each("--loc", context, ui_state, device=device):
            for risc_id in dopt.for_each("--risc", context, ui_state):
                run_riscv_command(device, loc, risc_id, dopt.args)
    return None
