# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from dataclasses import dataclass
from tt_device import DEBUDA_SERVER_SOCKET_IFC
from tt_coordinate import OnChipCoordinate
import tt_util as util

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
STATUS_PAUSED = 0x1
STATUS_PC_WATCHPOINT_HIT = 0x2
STATUS_MEMORY_WATCHPOINT_HIT = 0x4

# Command register bits
COMMAND_PAUSE = 0x00000001
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
RISC_DBG_CNTL0 = 0xFFB12080
RISC_DBG_CNTL1 = 0xFFB12084
RISC_DBG_STATUS0 = 0xFFB12088
RISC_DBG_STATUS1 = 0xFFB1208C


@dataclass
class RiscLoc:
    loc: OnChipCoordinate
    noc_id: int = 0
    risc_id: int = 0


class RiscDebug:
    def __init__(self, location: RiscLoc):
        assert 0 <= location.risc_id <= 4, f"Invalid risc id({location.risc_id})"
        self.location = location
        self.control0_write = 0x80010000 + (self.location.risc_id << 17)
        self.control0_read = 0x80000000 + (self.location.risc_id << 17)

    def __write(self, addr, data):
        DEBUDA_SERVER_SOCKET_IFC.pci_write_xy(
            self.location.loc._device._id,
            *self.location.loc.to("nocVirt"),
            self.location.noc_id,
            addr,
            data,
        )

    def __read(self, addr):
        data = DEBUDA_SERVER_SOCKET_IFC.pci_read_xy(
            self.location.loc._device._id,
            *self.location.loc.to("nocVirt"),
            self.location.noc_id,
            addr,
        )
        return data

    def __trigger_write(self, reg_addr):
        self.__write(RISC_DBG_CNTL0, 0)
        self.__write(RISC_DBG_CNTL0, self.control0_write + reg_addr)

    def __trigger_read(self, reg_addr):
        self.__write(RISC_DBG_CNTL0, 0)
        self.__write(RISC_DBG_CNTL0, self.control0_read + reg_addr)

    def __riscv_write(self, reg_addr, value):
        # set wrdata
        self.__write(RISC_DBG_CNTL1, value)
        self.__trigger_write(reg_addr)

    def __is_read_valid(self):
        status0 = self.__read(RISC_DBG_STATUS0)
        DEBUG_READ_VALID_BIT = 1 << 30
        return (status0 & DEBUG_READ_VALID_BIT) == DEBUG_READ_VALID_BIT

    def __riscv_read(self, reg_addr):
        self.__trigger_read(reg_addr)
        # check 3 times if read is valid
        if (
            not self.__is_read_valid()
            and not self.__is_read_valid()
            and not self.__is_read_valid()
        ):
            util.INFO(
                "Reading failed. Debug read valid bit is set to 0. Run `srs 0` to check if core is active."
            )
        return self.__read(RISC_DBG_STATUS1)

    def enable_debug(self):
        self.__riscv_write(REG_COMMAND, COMMAND_DEBUG_MODE)

    def pause(self):
        self.__riscv_write(REG_COMMAND, COMMAND_DEBUG_MODE + COMMAND_PAUSE)

    def step(self):
        self.__riscv_write(REG_COMMAND, COMMAND_DEBUG_MODE + COMMAND_STEP)

    def contnue(self):
        self.__riscv_write(REG_COMMAND, COMMAND_DEBUG_MODE + COMMAND_CONTINUE)

    def is_paused(self):
        return (self.__riscv_read(REG_STATUS) & STATUS_PAUSED) == STATUS_PAUSED

    def is_pc_watchpoint_hit(self):
        return (
            self.__riscv_read(REG_STATUS) & STATUS_PC_WATCHPOINT_HIT
        ) == STATUS_PC_WATCHPOINT_HIT

    def is_memory_watchpoint_hit(self):
        return (
            self.__riscv_read(REG_STATUS) & STATUS_MEMORY_WATCHPOINT_HIT
        ) == STATUS_MEMORY_WATCHPOINT_HIT

    def read_gpr(self, reg_index):
        self.__riscv_write(REG_COMMAND_ARG_0, reg_index)
        self.__riscv_write(REG_COMMAND, COMMAND_READ_REGISTER)
        return self.__riscv_read(REG_COMMAND_RETURN_VALUE)

    def write_gpr(self, reg_index, value):
        self.__riscv_write(REG_COMMAND_ARG_1, value)
        self.__riscv_write(REG_COMMAND_ARG_0, reg_index)
        self.__riscv_write(REG_COMMAND, COMMAND_WRITE_REGISTER)

    def read_memory(self, addr):
        self.__riscv_write(REG_COMMAND_ARG_0, addr)
        self.__riscv_write(REG_COMMAND, COMMAND_READ_MEMORY)
        return self.__riscv_read(REG_COMMAND_RETURN_VALUE)

    def write_memory(self, addr, value):
        self.__riscv_write(REG_COMMAND_ARG_1, value)
        self.__riscv_write(REG_COMMAND_ARG_0, addr)
        self.__riscv_write(REG_COMMAND, COMMAND_WRITE_MEMORY)

    def __update_watchpoint_setting(self, watchpoint_id, value):
        assert 0 <= value <= 15
        old_wp_settings = self.__riscv_read(REG_HW_WATCHPOINT_SETTINGS)
        mask = HW_WATCHPOINT_MASK << (id * 4)
        new_wp_settings = old_wp_settings & ~mask + (value << (id * 4))
        self.__risc_write(REG_HW_WATCHPOINT_SETTINGS, new_wp_settings)

    def __set_watchpoint(self, id, address, setting):
        self.__riscv_write(REG_HW_WATCHPOINT_0 + id, address)
        self.__update_watchpoint_setting(id, setting)

    def set_watchpoint_on_pc_address(self, id, address):
        self.__set_watchpoint(
            id, address, HW_WATCHPOINT_ENABLED + HW_WATCHPOINT_BREAKPOINT
        )

    def set_watchpoint_on_memory_read(self, id, address):
        self.__set_watchpoint(id, address, HW_WATCHPOINT_ENABLED + HW_WATCHPOINT_READ)

    def set_watchpoint_on_memory_write(self, id, address):
        self.__set_watchpoint(id, address, HW_WATCHPOINT_ENABLED + HW_WATCHPOINT_WRITE)

    def set_watchpoint_on_memory_access(self, id, address):
        self.__set_watchpoint(id, address, HW_WATCHPOINT_ENABLED + HW_WATCHPOINT_ACCESS)

    def disable_watchpoint(self, id):
        self.__update_watchpoint_setting(id, 0)
