# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from dataclasses import dataclass
from tt_coordinate import OnChipCoordinate
import tt_util as util
import os
from contextlib import contextmanager

# Register address
REG_STATUS = 0
REG_COMMAND = 1
REG_COMMAND_ARG_0 = 2
REG_COMMAND_ARG_1 = 3
REG_COMMAND_RETURN_VALUE = 4
REG_HW_WATCHPOINT_SETTINGS = 5
REG_HW_WATCHPOINT_0 = 10
REG_HW_WATCHPOINT_1 = 11
REG_HW_WATCHPOINT_2 = 12
REG_HW_WATCHPOINT_3 = 13
REG_HW_WATCHPOINT_4 = 14
REG_HW_WATCHPOINT_5 = 15
REG_HW_WATCHPOINT_6 = 16
REG_HW_WATCHPOINT_7 = 17

# Status register bits
STATUS_HALTED = 0x1
STATUS_PC_WATCHPOINT_HIT = 0x2
STATUS_MEMORY_WATCHPOINT_HIT = 0x4

# Command register bits
COMMAND_HALT = 0x00000001
COMMAND_STEP = 0x00000002  # resume for one cycle
COMMAND_CONTINUE = 0x00000004
COMMAND_READ_REGISTER = 0x00000008
COMMAND_WRITE_REGISTER = 0x00000010
COMMAND_READ_MEMORY = 0x00000020
COMMAND_WRITE_MEMORY = 0x00000040
COMMAND_FLUSH_REGISTERS = 0x00000080
COMMAND_FLUSH = 0x00000100
COMMAND_DEBUG_MODE = 0x80000000

# Watchpoint - REG_HW_WATCHPOINT_SETTINGS
HW_WATCHPOINT_BREAKPOINT = 0x00000000
HW_WATCHPOINT_READ = 0x00000001
HW_WATCHPOINT_WRITE = 0x00000002
HW_WATCHPOINT_ACCESS = 0x00000003
HW_WATCHPOINT_ENABLED = 0x00000008
HW_WATCHPOINT_MASK = 0x0000000F

# Registers for access debug registers
RISC_DBG_CNTL0   = 0xFFB12080
RISC_DBG_CNTL1   = 0xFFB12084
RISC_DBG_STATUS0 = 0xFFB12088
RISC_DBG_STATUS1 = 0xFFB1208C

RISCV_REGS = {
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
    32: "pc",
}

def get_register_index(reg_index_or_name):
    if reg_index_or_name in RISCV_REGS:
        return reg_index_or_name
    else: # try to find register by name
        for i, name in RISCV_REGS.items():
            if reg_index_or_name.lower() in name:
                return i
    raise ValueError(f"Unknown register {reg_index_or_name}")

def get_reg_name_for_address(addr):
    if addr == RISC_DBG_CNTL0:
        return "CNTL0"
    elif addr == RISC_DBG_CNTL1:
        return "CNTL1"
    elif addr == RISC_DBG_STATUS0:
        return "STATUS0"
    elif addr == RISC_DBG_STATUS1:
        return "STATUS1"
    else:
        return f"Unknown register {addr}"

RISC_NAMES = [ "BRISC", "TRISC0", "TRISC1", "TRISC2", "NCRISC" ]

def get_risc_name(id):
    if 0 <= id < len(RISC_NAMES):
        return RISC_NAMES[id]
    else:
        return f"Unknown RISC id {id}"

def get_risc_id(name):
    for i, n in enumerate(RISC_NAMES):
        if name.lower() in n.lower():
            return i
    raise ValueError(f"Unknown RISC name {name}")

def get_risc_reset_shift(id):
    """
    Return the reset register bit position for a given RISC.
    """
    if id == 0:
        return 11
    elif id == 1:
        return 12
    elif id == 2:
        return 13
    elif id == 3:
        return 14
    elif id == 4:
        return 18
    else:
        return f"Unknown RISC id {id}"

@dataclass
class RiscLoc:
    loc: OnChipCoordinate
    noc_id: int = 0
    risc_id: int = 0


class RiscDebug:
    def __init__(self, location: RiscLoc, ifc, verbose=False):
        assert 0 <= location.risc_id <= 4, f"Invalid risc id({location.risc_id})"
        self.location = location
        self.CONTROL0_WRITE = 0x80010000 + (self.location.risc_id << 17)
        self.CONTROL0_READ  = 0x80000000 + (self.location.risc_id << 17)
        self.verbose = verbose
        self.ifc = ifc

    def __write(self, addr, data):
        self.assert_not_in_reset()
        if self.verbose:
            util.DEBUG(f"{get_reg_name_for_address(addr)} <- WR   0x{data:08x}")
        self.ifc.pci_write_xy(
            self.location.loc._device._id,
            *self.location.loc.to("nocVirt"),
            self.location.noc_id,
            addr,
            data,
        )

    def __read(self, addr):
        self.assert_not_in_reset()
        data = self.ifc.pci_read_xy(
            self.location.loc._device._id,
            *self.location.loc.to("nocVirt"),
            self.location.noc_id,
            addr,
        )
        if self.verbose:
            util.DEBUG(f"{get_reg_name_for_address(addr)} -> RD == 0x{data:08x}")
        return data

    def __trigger_write(self, reg_addr):
        if self.verbose:
            util.INFO(f"      __trigger_write({reg_addr})")
        self.__write(RISC_DBG_CNTL0, self.CONTROL0_WRITE + reg_addr)
        self.__write(RISC_DBG_CNTL0, 0)

    def __trigger_read(self, reg_addr):
        if self.verbose:
            util.INFO(f"      __trigger_read({reg_addr})")
        self.__write(RISC_DBG_CNTL0, self.CONTROL0_READ  + reg_addr)
        self.__write(RISC_DBG_CNTL0, 0)

    def __riscv_write(self, reg_addr, value):
        if self.verbose:
            util.INFO(f"    __riscv_write({reg_addr}, 0x{value:08x})")
        # set wrdata
        self.__write(RISC_DBG_CNTL1, value)
        self.__trigger_write(reg_addr)

    def __is_read_valid(self):
        if self.verbose:
            util.INFO("  __is_read_valid()")
        status0 = self.__read(RISC_DBG_STATUS0)
        DEBUG_READ_VALID_BIT = 1 << 30
        return (status0 & DEBUG_READ_VALID_BIT) == DEBUG_READ_VALID_BIT

    def __riscv_read(self, reg_addr):
        if self.verbose:
            util.INFO(f"  __riscv_read({reg_addr})")
        self.__trigger_read(reg_addr)

        if (not self.__is_read_valid()):
            util.WARN (f"Reading from RiscV debug registers failed (debug read valid bit is set to 0). Run `srs 0` to check if core is active.")
        return self.__read(RISC_DBG_STATUS1)

    def enable_debug(self):
        if self.verbose:
            util.INFO("  enable_debug()")
        self.__riscv_write(REG_COMMAND, COMMAND_DEBUG_MODE)

    def halt(self):
        if self.is_halted():
            util.WARN (f"Halt: {get_risc_name(self.location.risc_id)} core at {self.location.loc} is already halted")
            return
        if self.verbose:
            util.INFO("  halt()")
        self.__riscv_write(REG_COMMAND, COMMAND_DEBUG_MODE + COMMAND_HALT)
        assert self.is_halted(), f"Failed to halt {get_risc_name(self.location.risc_id)} core at {self.location.loc}"

    @contextmanager
    def ensure_halted(self):
        """
        Ensures that an operation is performed while the RISC-V core is halted, and then resumes it.
        """
        was_halted = self.is_halted()
        if not was_halted:
            self.halt()
        try:
            yield
        finally:
            if not was_halted:
                self.cont()

    def step(self):
        if self.verbose:
            util.INFO("  step()")
        self.__riscv_write(REG_COMMAND, COMMAND_DEBUG_MODE + COMMAND_STEP)

    def cont(self):
        if not self.is_halted():
            util.WARN (f"Continue: {get_risc_name(self.location.risc_id)} core at {self.location.loc} is alredy running")
            return
        if self.verbose:
            util.INFO("  cont()")
        self.__riscv_write(REG_COMMAND, COMMAND_DEBUG_MODE + COMMAND_CONTINUE)
        assert not self.is_halted(), f"Failed to continue {get_risc_name(self.location.risc_id)} core at {self.location.loc}"

    def is_halted(self):
        if self.verbose:
            util.INFO("  is_halted()")
        return (self.__riscv_read(REG_STATUS) & STATUS_HALTED) == STATUS_HALTED

    def is_pc_watchpoint_hit(self):
        if self.verbose:
            util.INFO("  is_pc_watchpoint_hit()")
        hit = (
            self.__riscv_read(REG_STATUS) & STATUS_PC_WATCHPOINT_HIT
        ) == STATUS_PC_WATCHPOINT_HIT
        return hit

    def is_in_reset(self):
        reset_reg = self.ifc.pci_read_xy(self.location.loc._device.id(), *self.location.loc.to("nocVirt"), 0, 0xffb121b0)
        shift = get_risc_reset_shift(self.location.risc_id)
        return (reset_reg >> shift) & 1

    def set_reset_signal(self, value):
        """
        Assert (1) or deassert (0) the reset signal of the RISC-V core.
        """
        assert value in [0, 1]
        shift = get_risc_reset_shift(self.location.risc_id)
        reset_reg = self.ifc.pci_read_xy(self.location.loc._device.id(), *self.location.loc.to("nocVirt"), 0, 0xffb121b0)
        reset_reg = (reset_reg & ~(1 << shift)) | (value << shift)
        self.ifc.pci_write_xy(self.location.loc._device.id(), *self.location.loc.to("nocVirt"), 0, 0xffb121b0, reset_reg)
        new_reset_reg = self.ifc.pci_read_xy(self.location.loc._device.id(), *self.location.loc.to("nocVirt"), 0, 0xffb121b0)
        if new_reset_reg != reset_reg:
            util.ERROR(f"Error writing reset signal. Expected 0x{reset_reg:08x}, got 0x{new_reset_reg:08x}")

    def assert_not_in_reset(self, message=""):
        """
        Make sure that the RISC-V core is not in reset.
        """
        if self.is_in_reset():
            exception_message = f"{get_risc_name(self.location.risc_id)} is in reset"
            if message:
                exception_message += f": {message}"
            raise ValueError(exception_message)

    def assert_halted(self, message=""):
        """
        Make sure that the RISC-V core is halted.
        """
        if not self.is_halted():
            exception_message = f"{get_risc_name(self.location.risc_id)} is not halted"
            if message:
                exception_message += f": {message}"
            raise ValueError(exception_message)

    def is_memory_watchpoint_hit(self):
        return (
            self.__riscv_read(REG_STATUS) & STATUS_MEMORY_WATCHPOINT_HIT
        ) == STATUS_MEMORY_WATCHPOINT_HIT

    def read_gpr(self, reg_index):
        if self.verbose:
            util.INFO(f"  read_gpr({reg_index})")
        self.__riscv_write(REG_COMMAND_ARG_0, reg_index)
        self.__riscv_write(REG_COMMAND, 0x80000000 + COMMAND_READ_REGISTER)
        return self.__riscv_read(REG_COMMAND_RETURN_VALUE)

    def write_gpr(self, reg_index, value):
        if self.verbose:
            util.INFO(f"  write_gpr({reg_index}, 0x{value:08x})")
        self.__riscv_write(REG_COMMAND_ARG_1, value)
        self.__riscv_write(REG_COMMAND_ARG_0, reg_index)
        self.__riscv_write(REG_COMMAND, 0x80000000 + COMMAND_WRITE_REGISTER)

    def read_memory(self, addr):
        self.assert_halted()
        if self.verbose:
            util.INFO(f"  read_memory(0x{addr:08x})")
        self.__riscv_write(REG_COMMAND_ARG_0, addr)
        self.__riscv_write(REG_COMMAND, 0x80000000 + COMMAND_READ_MEMORY)
        data = self.__riscv_read(REG_COMMAND_RETURN_VALUE)
        if self.verbose:
            util.INFO(f"                             read -> 0x{data:08x}")
        return data

    def write_memory(self, addr, value):
        self.assert_halted()
        if self.verbose:
            util.INFO(f"  write_memory(0x{addr:08x}, 0x{value:08x})")
        self.__riscv_write(REG_COMMAND_ARG_1, value)
        self.__riscv_write(REG_COMMAND_ARG_0, addr)
        self.__riscv_write(REG_COMMAND, 0x80000000 + COMMAND_WRITE_MEMORY)

    def __update_watchpoint_setting(self, id, value):
        assert 0 <= value <= 15
        with self.ensure_halted():
            old_wp_settings = self.__riscv_read(REG_HW_WATCHPOINT_SETTINGS)
            mask = HW_WATCHPOINT_MASK << (id * 4)
            new_wp_settings = (old_wp_settings & (~mask)) + (value << (id * 4))
            self.__riscv_write(REG_HW_WATCHPOINT_SETTINGS, new_wp_settings)

    def __set_watchpoint(self, id, address, setting):
        with self.ensure_halted():
            self.__riscv_write(REG_HW_WATCHPOINT_0 + id, address)
            self.__update_watchpoint_setting(id, setting)

    def set_watchpoint_on_pc_address(self, id, address):
        self.__set_watchpoint(id, address, HW_WATCHPOINT_ENABLED + HW_WATCHPOINT_BREAKPOINT)

    def set_watchpoint_on_memory_read(self, id, address):
        self.__set_watchpoint(id, address, HW_WATCHPOINT_ENABLED + HW_WATCHPOINT_READ)

    def set_watchpoint_on_memory_write(self, id, address):
        self.__set_watchpoint(id, address, HW_WATCHPOINT_ENABLED + HW_WATCHPOINT_WRITE)

    def set_watchpoint_on_memory_access(self, id, address):
        self.__set_watchpoint(id, address, HW_WATCHPOINT_ENABLED + HW_WATCHPOINT_ACCESS)

    def disable_watchpoint(self, id):
        self.__update_watchpoint_setting(id, 0)

    def invalidate_instruction_cache(self):
        """
        Invalidates the instruction cache of the RISC-V core.
        """
        self.assert_halted()
        TENSIX_CFG_BASE = 0xFFEF0000
        RISCV_IC_INVALIDATE_InvalidateAll_ADDR32 = 157
        self.write_memory (TENSIX_CFG_BASE + RISCV_IC_INVALIDATE_InvalidateAll_ADDR32 * 4, 1 << self.location.risc_id)
        self.write_memory (TENSIX_CFG_BASE + RISCV_IC_INVALIDATE_InvalidateAll_ADDR32 * 4, 0x0)

from elftools.elf.elffile import ELFFile
from tt_object import DataArray

class RiscLoader:
    """
    This class is used to load elf file to a RISC-V core.
    """
    def __init__(self, location: OnChipCoordinate, risc_id: int, ifc, verbose=False):
        assert 0 <= risc_id <= 4, f"Invalid risc id({location.risc_id})"
        self.verbose = verbose
        self.risc_debug = RiscDebug(RiscLoc(location, 0, risc_id), ifc, self.verbose)

    SECTIONS_TO_LOAD = [".init", ".text", ".ldm_data", ".stack"]

    def get_jump_to_address_instruction (self, address):
        """
        Return the JAL instruction to jump to a given address. This instruction
        is then loaded into the location 0, as brisc always starts executing from address 0.
        """
        # Constants for JAL instruction
        JAL_OPCODE = 0x6f
        JAL_MAX_OFFSET = 0x0007ffff

        # Ensure the firmware base is within the maximum offset
        assert address < JAL_MAX_OFFSET, "Firmware base exceeds JAL maximum offset."

        # See RISC-V spec for offset encoding
        jal_offset_bit_20 = 0
        jal_offset_bits_10_to_1 = (address & 0x7fe) << 20
        jal_offset_bit_11 = (address & 0x800) << 9
        jal_offset_bits_19_to_12 = (address & 0xff000)

        # Combine the offset parts into the full offset
        jal_offset = (
            jal_offset_bit_20 |
            jal_offset_bits_10_to_1 |
            jal_offset_bit_11 |
            jal_offset_bits_19_to_12
        )
        return jal_offset | JAL_OPCODE

    def write_block_through_debug(self, address, data):
        """
        Writes a block of data to a given address through the debug interface.
        """
        rd = self.risc_debug
        rd.enable_debug()
        with rd.ensure_halted():
            for i in range(0, len(data), 4):
                word = data[i:i + 4]
                word = word.ljust(4, b'\x00')
                word = int.from_bytes(word, byteorder='little')
                rd.write_memory(address + i, word)
                # ww = rd.read_memory(address + i)
                # if ww != word:
                #     util.ERROR(f"Error writing word at address 0x{address + i:08x}. Expected 0x{word:08x}, got 0x{ww:08x}")

    def read_block_through_debug(self, address, byte_count):
        """
        Reads a block of data from a given address through the debug interface.
        """
        rd = self.risc_debug
        rd.enable_debug()
        with rd.ensure_halted():
            data = bytearray()
            for i in range(0, byte_count, 4):
                word = rd.read_memory(address + i)
                data.extend(word.to_bytes(4, byteorder='little'))


        return data

    def write_block(self, address, data: bytes):
        """
        Writes a block of bytes to a given address. Knows about the sections not accessible through NOC (0xFFB00000), and uses
        the debug interface to write them.
        """
        if address & 0xFFB00000 == 0xFFB00000:
            # Use debug interface
            self.write_block_through_debug(address, data)
        else:
            noc_id = 0
            self.risc_debug.ifc.pci_write_buf(self.risc_debug.location.loc._device.id(),*self.risc_debug.location.loc.to("nocVirt"),noc_id,address,data)

    def read_block(self, address, byte_count):
        """
        Reads a block of bytes from a given address. Knows about the sections not accessible through NOC (0xFFF00000), and uses
        the debug interface to read them.
        """
        if address & 0xFFB00000 == 0xFFB00000:
            # Use debug interface
            return self.read_block_through_debug(address, byte_count)
        else:
            noc_id = 0
            return self.risc_debug.ifc.pci_read_buf(self.risc_debug.location.loc._device.id(),*self.risc_debug.location.loc.to("nocVirt"),noc_id,address,byte_count)

    def load_elf(self, elf_path):
        """
        Given an ELF file, this function loads the sections specified in SECTIONS_TO_LOAD to the
        memory of the RISC-V core. It also loads (into location 0) the jump instruction to the 
        address of the .init section.
        """
        if not os.path.exists(elf_path):
            raise FileNotFoundError(f"File {elf_path} not found")

        # Remember the .init section address to jump to after loading
        init_section_address = None

        with open(elf_path, 'rb') as elf_file:
            elf_file = ELFFile(elf_file)

            for section in elf_file.iter_sections():
                if section.data() and hasattr(section.header, 'sh_addr'):
                    name = section.name
                    if name in self.SECTIONS_TO_LOAD:
                        address = section.header.sh_addr
                        if address % 4 != 0:
                            raise ValueError(f"Section address 0x{address:08x} is not 32-bit aligned")

                        data = section.data()

                        if name == ".init":
                            init_section_address = address

                        util.VERBOSE (f"Writing section {name} to address 0x{address:08x}. Size: {len(data)} bytes")
                        self.write_block(address, data)

            # Check that what we have written is correct
            for section in elf_file.iter_sections():
                if section.data() and hasattr(section.header, 'sh_addr'):
                    name = section.name
                    if name in self.SECTIONS_TO_LOAD:
                        address = section.header.sh_addr
                        data = section.data()
                        read_data = self.read_block(address, len(data))
                        if read_data != data:
                            util.ERROR(f"Error writing section {name} to address 0x{address:08x}.")
                            continue
                        else:
                            util.VERBOSE(f"Section {name} loaded successfully to address 0x{address:08x}. Size: {len(data)} bytes")

        return init_section_address
