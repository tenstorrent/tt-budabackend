# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  run-elf <elf-file> [ -v ] [ -t ] [ -d <device> ] [ -l <loc> ]

Description:
  Loads an elf file into a brisc and runs it.

Examples:
  run-elf brisc.elf
"""

import tt_util as util
import tt_commands
from tt_debug_risc import RiscLoader
from tt_firmware import ELF
import tt_device
from tt_object import DataArray

command_metadata = {
    "short": "re",
    "type": "high-level",
    "description": __doc__
}

def print_PC_and_source (PC, elf):
    # Find the location in source code given the PC
    pc_map = elf.names["brisc"]["file-line"]
    if PC in pc_map:
        source_loc = pc_map[PC]
        if source_loc:
            util.INFO (f"PC: 0x{PC:x} is at {str(source_loc[0])}:{source_loc[1]}")
        else:
            util.INFO (f"PC: 0x{PC:x} is not in the source code.")
    else:
        print (f"PC 0x{PC:x} not found in the ELF file.")

# TODO:
# - Test disable watchpoint
# - Test memory access watchpoints
# - Run on all riscs

def run(cmd_text, context, ui_state=None):
    dopt = tt_commands.tt_docopt(command_metadata["description"], argv=cmd_text.split()[1:],
                                common_option_names=[ "--device", "--loc", "--risc", "--verbose", "--test" ]
                                )

    RISC_ID = 0 # Only brisc for now

    for device in dopt.for_each("--device", context, ui_state):
        util.VERBOSE (f"Putting all RISCs on device {device.id()} under reset.")
        device.all_riscs_assert_soft_reset()
        for loc in dopt.for_each("--loc", context, ui_state, device=device):
            rloader = RiscLoader(loc, RISC_ID, context, tt_device.DEBUDA_SERVER_SOCKET_IFC, dopt.args['-v'])
            rdbg = rloader.risc_debug

            in_reset = rdbg.is_in_reset()
            if in_reset:
                util.VERBOSE (f"RISC at location {loc} is in reset. Setting L1[0] = 0x6f (infinite loop) and taking it out of reset.")
                util.VERBOSE (f"  We need it out of reset to load the elf file.")
                tt_device.SERVER_IFC.pci_write_xy (device.id(), *loc.to("nocVirt"), 0, 0, 0x6f)
                rdbg.set_reset_signal(0)
            assert not rdbg.is_in_reset(), f"RISC at location {loc} is still in reset."

            util.VERBOSE (f"Invalidating instruction cache so that the jump instruction is actually loaded.")
            if not rdbg.is_halted():
                rdbg.halt()

            util.VERBOSE (f"Loading ELF file into memories.")
            init_section_address = rloader.load_elf(dopt.args['<elf-file>']) # Load the elf file
            if init_section_address == None:
                raise ValueError("No .init section found in the ELF file")

            jump_to_start_of_init_section_instruction = rloader.get_jump_to_address_instruction(init_section_address)
            tt_device.SERVER_IFC.pci_write_xy(loc._device.id(), *loc.to("nocVirt"), 0, 0, jump_to_start_of_init_section_instruction)

            rdbg.invalidate_instruction_cache()
            util.VERBOSE (f"Continuing BRISC at location {loc}.")
            rdbg.cont()

    if dopt.args['-t']:
        test_run_elf(context, ui_state, dopt, RISC_ID)

def test_run_elf(context, ui_state, dopt, RISC_ID):
    gpr_command = tt_commands.find_command(context.commands, "gpr")

    # Testing
    elf = ELF ({ "fw" : dopt.args['<elf-file>'] })
    MAILBOX_ADDR, MAILBOX_SIZE, _ = elf.parse_addr_size_type("fw.g_MAILBOX")
    TESTBYTEACCESS_ADDR, TESTBYTEACCESS_SIZE, _ = elf.parse_addr_size_type("fw.g_TESTBYTEACCESS")

    all_good = True
    for device in dopt.for_each("--device", context, ui_state):
        for loc in dopt.for_each("--loc", context, ui_state, device=device):
            rloader = RiscLoader(loc, RISC_ID, context, tt_device.DEBUDA_SERVER_SOCKET_IFC, dopt.args['-v'])
            rdbg = rloader.risc_debug

            # Step 0
            def halt_cont_test():
                util.INFO (f"Step 0: Halt and continue a couple of times.")
                rdbg.halt()
                assert rdbg.is_halted(), f"RISC at location {loc} is not halted."
                rdbg.cont()
                assert not rdbg.is_halted(), f"RISC at location {loc} is halted."
                rdbg.halt()
                assert rdbg.is_halted(), f"RISC at location {loc} is not halted."
                rdbg.cont()
                assert not rdbg.is_halted(), f"RISC at location {loc} is halted."
            halt_cont_test()

            # Step 1
            util.INFO (f"Step 1: Check that the RISC at location {loc} set the mailbox value to 0xFFB1208C.")
            mbox_val = rloader.read_block(MAILBOX_ADDR, MAILBOX_SIZE); da = DataArray("g_MAILBOX"); mbox_val = da.from_bytes(mbox_val)[0]
            print (f"get_mailbox_val on device {device.id()} at location {loc}: 0x{mbox_val:x}")
            if mbox_val != 0xFFB1208C:
                util.ERROR (f"RISC at location {loc} did not set the mailbox value to 0xFFB1208C.")
                gpr_command["module"].run("gpr pc,sp", context, ui_state)
                continue

            # Step 2
            util.INFO (f"Step 2: Write 0x1234 to the mailbox to resume operation.")
            try:
                da.data = [0x1234]; bts = da.bytes(); rloader.write_block(MAILBOX_ADDR, bts)
            except Exception as e:
                if e.args[0].startswith("Failed to continue"):
                    # We are expecting this to assert as here, the core will halt istself by calling halt()
                    pass
                else:
                    raise e

            # Step 3
            util.INFO (f"Step 3: Check that the RISC at location {loc} set the mailbox value to 0xFFB12080.")
            mbox_val = rloader.read_block(MAILBOX_ADDR, MAILBOX_SIZE); da = DataArray("g_MAILBOX"); mbox_val = da.from_bytes(mbox_val)[0]
            print (f"get_mailbox_val on device {device.id()} at location {loc}: 0x{mbox_val:x}")
            if mbox_val != 0xFFB12080:
                util.ERROR (f"RISC at location {loc} did not set the mailbox value to 0xFFB12080.")
                all_good = False
                continue

            # Step 4
            util.INFO (f"Step 4: Check that the RISC at location {loc} is halted.")
            rloader = RiscLoader(loc, RISC_ID, context, tt_device.DEBUDA_SERVER_SOCKET_IFC, dopt.args['-v'])
            status = rdbg.read_status()
            # print_PC_and_source(rdbg.read_gpr(32), elf)
            if not status.is_halted:
                util.ERROR (f"Step 4: RISC at location {loc} is not halted.")
                continue
            if not status.is_ebreak_hit:
                util.ERROR (f"Step 4: RISC at location {loc} is not halted with ebreak.")
                continue

            # Step 5a: Make sure that the core did not reach step 5
            util.INFO (f"Step 5a: Check that the RISC at location {loc} did not reach step 5.")
            mbox_val = rloader.read_block(MAILBOX_ADDR, MAILBOX_SIZE); da = DataArray("g_MAILBOX"); mbox_val = da.from_bytes(mbox_val)[0]
            print (f"get_mailbox_val on device {device.id()} at location {loc}: 0x{mbox_val:x}")
            if mbox_val == 0xFFB12088:
                util.ERROR (f"RISC at location {loc} reached step 5, but it should not have.")
                all_good = False
                continue

            # Step 5b: Continue and check that the core reached 0xFFB12088. But first set the breakpoint at
            # function "decrement_mailbox"
            decrement_mailbox_die = elf.names["fw"]["subprogram"]["decrement_mailbox"]
            decrement_mailbox_linkage_name = decrement_mailbox_die.attributes["DW_AT_linkage_name"].value.decode("utf-8")
            decrement_mailbox_address = elf.names["fw"]["symbols"][decrement_mailbox_linkage_name]

            util.INFO(f"Step 6. Setting breakpoint at decrement_mailbox at 0x{decrement_mailbox_address:x}")
            watchpoint_id = 1 # Out of 8
            rdbg.set_watchpoint_on_pc_address(watchpoint_id, decrement_mailbox_address)
            rdbg.set_watchpoint_on_memory_write(0, TESTBYTEACCESS_ADDR) # Set memory watchpoint on TESTBYTEACCESS
            rdbg.set_watchpoint_on_memory_write(3, TESTBYTEACCESS_ADDR+3)
            rdbg.set_watchpoint_on_memory_write(4, TESTBYTEACCESS_ADDR+4)
            rdbg.set_watchpoint_on_memory_write(5, TESTBYTEACCESS_ADDR+5)

            mbox_val = 1
            timeout_retries = 20
            while mbox_val >= 0 and mbox_val < 0xff000000 and timeout_retries > 0:
                if rdbg.is_halted():
                    if rdbg.is_pc_watchpoint_hit():
                        util.INFO (f"Breakpoint hit.")

                try:
                    rdbg.cont()
                except Exception as e:
                    if e.args[0].startswith("Failed to continue"):
                        # We are expecting this to assert as here, the core will hit a breakpoint
                        pass
                    else:
                        raise e
                mbox_val = rloader.read_block(MAILBOX_ADDR, MAILBOX_SIZE); da = DataArray("g_MAILBOX"); mbox_val = da.from_bytes(mbox_val)[0]
                util.INFO (f"Step 5b: Continue RISC at location {loc}. Mailbox value: {mbox_val}")
                timeout_retries -= 1

            if timeout_retries == 0 and mbox_val != 0:
                util.ERROR (f"RISC at location {loc} did not get past step 6.")
                all_good = False
                continue

            if rdbg.is_pc_watchpoint_hit():
                util.ERROR (f"RISC at location {loc} hit the breakpoint but it should not have.")
                all_good = False
                continue

            # STEP 7
            util.INFO(f"Step 7. Testing byte access memory watchpoints")
            mbox_val = rloader.read_block(MAILBOX_ADDR, MAILBOX_SIZE); da = DataArray("g_MAILBOX"); mbox_val = da.from_bytes(mbox_val)[0]
            if mbox_val != 0xff000003:
                util.ERROR (f"RISC at location {loc} did not set the mailbox value to 0xff000003.")
                all_good = False
                continue
            status = rdbg.read_status()
            if not status.is_halted:
                util.ERROR (f"Step 7: RISC at location {loc} is not halted.")
                continue
            if not status.is_memory_watchpoint_hit or not status.is_watchpoint3_hit:
                util.ERROR (f"Step 7: RISC at location {loc} is not halted with memory watchpoint 3.")
                continue
            rdbg.cont(verify=False)

            mbox_val = rloader.read_block(MAILBOX_ADDR, MAILBOX_SIZE); da = DataArray("g_MAILBOX"); mbox_val = da.from_bytes(mbox_val)[0]
            if mbox_val != 0xff000005:
                util.ERROR (f"RISC at location {loc} did not set the mailbox value to 0xff000005.")
                all_good = False
                continue
            status = rdbg.read_status()
            if not status.is_halted:
                util.ERROR (f"Step 7: RISC at location {loc} is not halted.")
                continue
            if not status.is_memory_watchpoint_hit or not status.is_watchpoint5_hit:
                util.ERROR (f"Step 7: RISC at location {loc} is not halted with memory watchpoint 5.")
                continue
            rdbg.cont(verify=False)

            mbox_val = rloader.read_block(MAILBOX_ADDR, MAILBOX_SIZE); da = DataArray("g_MAILBOX"); mbox_val = da.from_bytes(mbox_val)[0]
            if mbox_val != 0xff000000:
                util.ERROR (f"RISC at location {loc} did not set the mailbox value to 0xff000000.")
                all_good = False
                continue
            status = rdbg.read_status()
            if not status.is_halted:
                util.ERROR (f"Step 7: RISC at location {loc} is not halted.")
                continue
            if not status.is_memory_watchpoint_hit or not status.is_watchpoint0_hit:
                util.ERROR (f"Step 7: RISC at location {loc} is not halted with memory watchpoint 0.")
                continue
            rdbg.cont(verify=False)

            mbox_val = rloader.read_block(MAILBOX_ADDR, MAILBOX_SIZE); da = DataArray("g_MAILBOX"); mbox_val = da.from_bytes(mbox_val)[0]
            if mbox_val != 0xff000004:
                util.ERROR (f"RISC at location {loc} did not set the mailbox value to 0xff000004.")
                all_good = False
                continue
            status = rdbg.read_status()
            if not status.is_halted:
                util.ERROR (f"Step 7: RISC at location {loc} is not halted.")
                continue
            if not status.is_memory_watchpoint_hit or not status.is_watchpoint4_hit:
                util.ERROR (f"Step 7: RISC at location {loc} is not halted with memory watchpoint 4.")
                continue
            rdbg.cont(verify=False)

            # STEP END:
            mbox_val = rloader.read_block(MAILBOX_ADDR, MAILBOX_SIZE); da = DataArray("g_MAILBOX"); mbox_val = da.from_bytes(mbox_val)[0]
            print (f"get_mailbox_val on device {device.id()} at location {loc}: 0x{mbox_val:x}")
            if mbox_val != 0xFFB12088:
                util.ERROR (f"RISC at location {loc} did not reach step STEP END.")
                all_good = False
                continue

    if all_good:
        util.INFO ("All good")
    else:
        util.ERROR ("One or more BRISCs did not run the elf file correctly.")
