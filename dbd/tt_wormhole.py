# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import tt_util as util, os
import tt_device
from tt_coordinate import CoordinateTranslationError, OnChipCoordinate

phase_state_map = {
    0: "PHASE_START",
    1: "PHASE_AUTO_CONFIG",
    2: "PHASE_AUTO_CONFIG_SENT",
    3: "PHASE_ADVANCE_WAIT",
    4: "PHASE_PREV_DATA_FLUSH_WAIT",
    5: "PHASE_FWD_DATA",
}

dest_state_map = {
    0: "DEST_IDLE",
    1: "DEST_REMOTE",
    2: "DEST_LOCAL_RDY_WAIT",
    3: "DEST_LOCAL_HS",
    4: "DEST_LOCAL",
    5: "DEST_ENDPOINT",
    6: "DEST_NO_FWD",
}

dest_ready_state_map = {
    0: "DEST_READY_IDLE",
    1: "DEST_READY_SEND_FIRST",
    2: "DEST_READY_WAIT_DATA",
    3: "DEST_READY_SEND_SECOND",
    4: "DEST_READY_FWD_DATA",
}

src_ready_state_map = {
    0: "SRC_READY_IDLE",
    1: "SRC_READY_WAIT_CFG",
    2: "SRC_READY_DEST_READY_TABLE_RD",
    3: "SRC_READY_SEND_UPDATE",
    4: "SRC_READY_WAIT_ALL_DESTS",
    5: "SRC_READY_FWD_DATA",
}

src_state_map = {0: "SRC_IDLE", 1: "SRC_REMOTE", 2: "SRC_LOCAL", 3: "SRC_ENDPOINT"}


class WormholeL1AddressMap(tt_device.L1AddressMap):
    def __init__(self):
        super().__init__()

        ## Taken from l1_address_map.h. Ideally make this auto-generated
        self._l1_address_map = dict()
        self._l1_address_map["trisc0"] = tt_device.BinarySlot(offset_bytes = 0 + 20 * 1024 + 32 * 1024, size_bytes = 20 * 1024)
        self._l1_address_map["trisc1"] = tt_device.BinarySlot(offset_bytes = self._l1_address_map["trisc0"].offset_bytes + self._l1_address_map["trisc0"].size_bytes, size_bytes = 16 * 1024)
        self._l1_address_map["trisc2"] = tt_device.BinarySlot(offset_bytes = self._l1_address_map["trisc1"].offset_bytes + self._l1_address_map["trisc1"].size_bytes, size_bytes = 20 * 1024)
        # Brisc, ncrisc, to be added

class WormholeDRAMEpochCommandAddressMap(tt_device.L1AddressMap):
    def __init__(self):
        super().__init__()
        
        ## Taken from dram_address_map.h. Ideally make this auto-generated
        self._l1_address_map = dict()
        self._l1_address_map["trisc0"] = tt_device.BinarySlot(offset_bytes = -1, size_bytes = 20 * 1024)
        self._l1_address_map["trisc1"] = tt_device.BinarySlot(offset_bytes = -1, size_bytes = 16 * 1024)
        self._l1_address_map["trisc2"] = tt_device.BinarySlot(offset_bytes = -1, size_bytes = 20 * 1024)
        # Brisc, ncrisc, to be added

class WormholeEthL1AddressMap(tt_device.L1AddressMap):
    def __init__(self):
        super().__init__()
        
        ## Taken from l1_address_map.h. Ideally make this auto-generated
        self._l1_address_map = dict()
        # erisc, erisc-app to be added

#
# Device
#
class WormholeDevice(tt_device.Device):
    SIG_SEL_CONST = 5
    # IMPROVE: some of this can be read from architecture yaml file
    DRAM_CHANNEL_TO_NOC0_LOC = [(0, 11), (0, 5), (5, 11), (5, 2), (5, 8), (5, 5)]

    # Physical location mapping. Physical coordinates are the geografical coordinates on a chip's die.
    DIE_X_TO_NOC_0_X = [0, 9, 1, 8, 2, 7, 3, 6, 4, 5]
    DIE_Y_TO_NOC_0_Y = [0, 11, 1, 10, 2, 9, 3, 8, 4, 7, 5, 6]
    DIE_X_TO_NOC_1_X = [9, 0, 8, 1, 7, 2, 6, 3, 5, 4]
    DIE_Y_TO_NOC_1_Y = [11, 0, 10, 1, 9, 2, 8, 3, 7, 4, 6, 5]
    NOC_0_X_TO_DIE_X = util.reverse_mapping_list(DIE_X_TO_NOC_0_X)
    NOC_0_Y_TO_DIE_Y = util.reverse_mapping_list(DIE_Y_TO_NOC_0_Y)
    NOC_1_X_TO_DIE_X = util.reverse_mapping_list(DIE_X_TO_NOC_1_X)
    NOC_1_Y_TO_DIE_Y = util.reverse_mapping_list(DIE_Y_TO_NOC_1_Y)

    NOC0_X_TO_NOCTR_X = {
        0: 16,
        1: 18,
        2: 19,
        3: 20,
        4: 21,
        5: 17,
        6: 22,
        7: 23,
        8: 24,
        9: 25,
    }
    NOCTR_X_TO_NOC0_X = {v: k for k, v in NOC0_X_TO_NOCTR_X.items()}

    # The following is used to convert harvesting mask to NOC0 Y location. If harvesting mask bit 0 is set, then
    # the NOC0 Y location is 11. If harvesting mask bit 1 is set, then the NOC0 Y location is 1, etc...
    HARVESTING_NOC_LOCATIONS = [11, 1, 10, 2, 9, 3, 8, 4, 7, 5]

    def get_harvested_noc0_y_rows(self):
        harvested_noc0_y_rows = []
        if self._harvesting:
            bitmask = self._harvesting["harvest_mask"]
            for h_index in range(0, self.row_count()):
                if (1 << h_index) & bitmask:  # Harvested
                    harvested_noc0_y_rows.append(self.HARVESTING_NOC_LOCATIONS[h_index])
        return harvested_noc0_y_rows

    # Coordinate conversion functions (see tt_coordinate.py for description of coordinate systems)
    def noc0_to_tensix(self, loc):
        if isinstance(loc, OnChipCoordinate):
            noc0_x, noc0_y = loc._noc0_coord
        else:
            noc0_x, noc0_y = loc
        if noc0_x == 0 or noc0_x == 5:
            raise CoordinateTranslationError(
                "NOC0 x=0 and x=5 do not have an RC coordinate"
            )
        if noc0_y == 0 or noc0_y == 6:
            raise CoordinateTranslationError(
                "NOC0 y=0 and y=6 do not have an RC coordinate"
            )
        row = noc0_y - 1
        col = noc0_x - 1
        if noc0_x > 5:
            col -= 1
        if noc0_y > 6:
            row -= 1
        return row, col

    def tensix_to_noc0(self, netlist_loc):
        row, col = netlist_loc
        noc0_y = row + 1
        noc0_x = col + 1
        if noc0_x >= 5:
            noc0_x += 1
        if noc0_y >= 6:
            noc0_y += 1
        return noc0_x, noc0_y

    def _handle_harvesting_for_nocTr_noc0_map(self, num_harvested_rows):
        # 1. Handle Ethernet rows
        self.nocTr_y_to_noc0_y[16] = 0
        self.nocTr_y_to_noc0_y[17] = 6

        # 2. Handle non-harvested rows
        harvested_noc0_y_rows = self.get_harvested_noc0_y_rows()

        nocTr_y = 18
        for noc0_y in range(0, self.row_count()):
            if noc0_y in harvested_noc0_y_rows or noc0_y == 0 or noc0_y == 6:
                pass  # Skip harvested rows and Ethernet rows
            else:
                self.nocTr_y_to_noc0_y[nocTr_y] = noc0_y
                nocTr_y += 1

        # 3. Handle harvested rows
        for netlist_row in range(0, num_harvested_rows):
            self.nocTr_y_to_noc0_y[
                16 + self.row_count() - num_harvested_rows + netlist_row
            ] = harvested_noc0_y_rows[netlist_row]

    def __init__(self, id, arch, cluster_desc, device_desc_path, context):
        super().__init__(id, arch, cluster_desc, {"functional_workers": WormholeL1AddressMap(), "eth": WormholeEthL1AddressMap(), "dram": WormholeDRAMEpochCommandAddressMap()}, device_desc_path, context)

    def row_count(self):
        return len(WormholeDevice.DIE_Y_TO_NOC_0_Y)

    def no_tensix_row_count(self):
        return 2

    def no_tensix_col_count(self):
        return 2

    def get_num_msgs_received(self, loc, stream_id):
        return int(self.get_stream_reg_field(loc, stream_id, 224 + 5, 0, 24))

    def get_curr_phase_num_msgs_remaining(self, loc, stream_id):
        return int(self.get_stream_reg_field(loc, stream_id, 36, 12, 12))

    def get_remote_source(self, loc, stream_id):
        return self.get_stream_reg_field(loc, stream_id, 10, 5, 1)

    def get_remote_receiver(self, loc, stream_id):
        return self.get_stream_reg_field(loc, stream_id, 10, 8, 1)

    # Populates a dict with register names and current values on core x-y for stream with id 'stream_id'
    def read_stream_regs_direct(self, loc, stream_id):
        reg = {}
        reg["STREAM_ID"] = self.get_stream_reg_field(loc, stream_id, 224 + 5, 24, 6)
        reg["PHASE_AUTO_CFG_PTR (word addr)"] = self.get_stream_reg_field(
            loc, stream_id, 12, 0, 24
        )
        reg["CURR_PHASE"] = self.get_stream_reg_field(loc, stream_id, 11, 0, 20)

        reg["CURR_PHASE_NUM_MSGS_REMAINING"] = self.get_stream_reg_field(
            loc, stream_id, 36, 12, 12
        )
        reg["NUM_MSGS_RECEIVED"] = self.get_stream_reg_field(
            loc, stream_id, 224 + 5, 0, 24
        )
        reg["NEXT_MSG_ADDR"] = self.get_stream_reg_field(loc, stream_id, 224 + 6, 0, 32)
        reg["NEXT_MSG_SIZE"] = self.get_stream_reg_field(loc, stream_id, 224 + 7, 0, 32)
        reg["OUTGOING_DATA_NOC"] = self.get_stream_reg_field(loc, stream_id, 10, 1, 1)
        local_sources_connected = self.get_stream_reg_field(loc, stream_id, 10, 3, 1)
        reg["LOCAL_SOURCES_CONNECTED"] = local_sources_connected
        reg["SOURCE_ENDPOINT"] = self.get_stream_reg_field(loc, stream_id, 10, 4, 1)
        remote_source = self.get_stream_reg_field(loc, stream_id, 10, 5, 1)
        reg["REMOTE_SOURCE"] = remote_source
        reg["RECEIVER_ENDPOINT"] = self.get_stream_reg_field(loc, stream_id, 10, 6, 1)
        reg["LOCAL_RECEIVER"] = self.get_stream_reg_field(loc, stream_id, 10, 7, 1)
        remote_receiver = self.get_stream_reg_field(loc, stream_id, 10, 8, 1)
        reg["REMOTE_RECEIVER"] = remote_receiver
        reg["NEXT_PHASE_SRC_CHANGE"] = self.get_stream_reg_field(
            loc, stream_id, 10, 12, 1
        )
        reg["NEXT_PHASE_DST_CHANGE"] = self.get_stream_reg_field(
            loc, stream_id, 10, 13, 1
        )

        if remote_source == 1:
            reg["INCOMING_DATA_NOC"] = self.get_stream_reg_field(
                loc, stream_id, 10, 0, 1
            )
            reg["REMOTE_SRC_X"] = self.get_stream_reg_field(loc, stream_id, 0, 0, 6)
            reg["REMOTE_SRC_Y"] = self.get_stream_reg_field(loc, stream_id, 0, 6, 6)
            reg["REMOTE_SRC_STREAM_ID"] = self.get_stream_reg_field(
                loc, stream_id, 0, 12, 6
            )
            reg["REMOTE_SRC_UPDATE_NOC"] = self.get_stream_reg_field(
                loc, stream_id, 10, 2, 1
            )
            reg["REMOTE_SRC_PHASE"] = self.get_stream_reg_field(
                loc, stream_id, 1, 0, 20
            )
            reg["REMOTE_SRC_DEST_INDEX"] = self.get_stream_reg_field(
                loc, stream_id, 0, 18, 6
            )
            reg["REMOTE_SRC_IS_MCAST"] = self.get_stream_reg_field(
                loc, stream_id, 10, 16, 1
            )

        if remote_receiver == 1:
            reg["OUTGOING_DATA_NOC"] = self.get_stream_reg_field(
                loc, stream_id, 10, 1, 1
            )
            reg["REMOTE_DEST_STREAM_ID"] = self.get_stream_reg_field(
                loc, stream_id, 2, 12, 6
            )
            reg["REMOTE_DEST_X"] = self.get_stream_reg_field(loc, stream_id, 2, 0, 6)
            reg["REMOTE_DEST_Y"] = self.get_stream_reg_field(loc, stream_id, 2, 6, 6)
            reg["REMOTE_DEST_BUF_START"] = self.get_stream_reg_field(
                loc, stream_id, 3, 0, 16
            )
            reg["REMOTE_DEST_BUF_SIZE"] = self.get_stream_reg_field(
                loc, stream_id, 4, 0, 16
            )
            reg["REMOTE_DEST_BUF_WR_PTR"] = self.get_stream_reg_field(
                loc, stream_id, 5, 0, 16
            )
            reg["REMOTE_DEST_MSG_INFO_WR_PTR"] = self.get_stream_reg_field(
                loc, stream_id, 9, 0, 16
            )
            reg["DEST_DATA_BUF_NO_FLOW_CTRL"] = self.get_stream_reg_field(
                loc, stream_id, 10, 15, 1
            )
            mcast_en = self.get_stream_reg_field(loc, stream_id, 13, 12, 1)
            reg["MCAST_EN"] = mcast_en
            if mcast_en == 1:
                reg["MCAST_END_X"] = self.get_stream_reg_field(loc, stream_id, 13, 0, 6)
                reg["MCAST_END_Y"] = self.get_stream_reg_field(loc, stream_id, 13, 6, 6)
                reg["MCAST_LINKED"] = self.get_stream_reg_field(
                    loc, stream_id, 13, 13, 1
                )
                reg["MCAST_VC"] = self.get_stream_reg_field(loc, stream_id, 13, 14, 1)
                reg["MCAST_DEST_NUM"] = self.get_stream_reg_field(
                    loc, stream_id, 14, 0, 16
                )
                for i in range(0, 31):
                    reg["DEST_BUF_SPACE_AVAILABLE[{i:d}]"] = self.get_stream_reg_field(
                        loc, stream_id, 64 + i, 0, 32
                    )
            else:
                reg["DEST_BUF_SPACE_AVAILABLE[0]"] = self.get_stream_reg_field(
                    loc, stream_id, 64, 0, 32
                )

        if local_sources_connected == 1:
            local_src_mask_lo = self.get_stream_reg_field(loc, stream_id, 48, 0, 32)
            local_src_mask_hi = self.get_stream_reg_field(loc, stream_id, 49, 0, 32)
            local_src_mask = (local_src_mask_hi << 32) | local_src_mask_lo
            reg["LOCAL_SRC_MASK"] = local_src_mask
            reg["MSG_ARB_GROUP_SIZE"] = self.get_stream_reg_field(
                loc, stream_id, 15, 0, 3
            )
            reg["MSG_SRC_IN_ORDER_FWD"] = self.get_stream_reg_field(
                loc, stream_id, 15, 3, 1
            )
            reg["STREAM_MSG_SRC_IN_ORDER_FWD_NUM_MSREG_INDEX"] = (
                self.get_stream_reg_field(loc, stream_id, 16, 0, 24)
            )
        else:
            reg["BUF_START (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 6, 0, 16
            )
            reg["BUF_SIZE (words)"] = self.get_stream_reg_field(
                loc, stream_id, 7, 0, 16
            )
            reg["BUF_RD_PTR (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 24, 0, 16
            )
            reg["BUF_WR_PTR (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 25, 0, 16
            )
            reg["MSG_INFO_PTR (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 8, 0, 16
            )
            reg["MSG_INFO_WR_PTR (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 26, 0, 16
            )
            reg["STREAM_BUF_SPACE_AVAILABLE_REG_INDEX (word addr)"] = (
                self.get_stream_reg_field(loc, stream_id, 28, 0, 16)
            )
            reg["DATA_BUF_NO_FLOW_CTRL"] = self.get_stream_reg_field(
                loc, stream_id, 10, 14, 1
            )
            reg["UNICAST_VC_REG"] = self.get_stream_reg_field(loc, stream_id, 10, 18, 3)
            reg["REG_UPDATE_VC_REG"] = self.get_stream_reg_field(
                loc, stream_id, 10, 21, 3
            )

        reg["SCRATCH_REG0"] = self.get_stream_reg_field(loc, stream_id, 248, 0, 32)
        reg["SCRATCH_REG1"] = self.get_stream_reg_field(loc, stream_id, 249, 0, 32)
        reg["SCRATCH_REG2"] = self.get_stream_reg_field(loc, stream_id, 250, 0, 32)
        reg["SCRATCH_REG3"] = self.get_stream_reg_field(loc, stream_id, 251, 0, 32)
        reg["SCRATCH_REG4"] = self.get_stream_reg_field(loc, stream_id, 252, 0, 32)
        reg["SCRATCH_REG5"] = self.get_stream_reg_field(loc, stream_id, 253, 0, 32)
        for i in range(0, 10):
            reg[f"DEBUG_STATUS[{i:d}]"] = self.get_stream_reg_field(
                loc, stream_id, 224 + i, 0, 32
            )
            if i == 8:
                phase_state = self.get_stream_reg_field(loc, stream_id, 224 + i, 0, 4)
                src_ready_state = self.get_stream_reg_field(
                    loc, stream_id, 224 + i, 4, 3
                )
                dest_ready_state = self.get_stream_reg_field(
                    loc, stream_id, 224 + i, 7, 3
                )
                src_side_phase_complete = self.get_stream_reg_field(
                    loc, stream_id, 224 + i, 10, 1
                )
                dest_side_phase_complete = self.get_stream_reg_field(
                    loc, stream_id, 224 + i, 11, 1
                )
                src_state = self.get_stream_reg_field(loc, stream_id, 224 + i, 16, 4)
                dest_state = self.get_stream_reg_field(loc, stream_id, 224 + i, 20, 3)
                # IMPROVE: add back the interpretation in get_as_str
                reg["PHASE_STATE"] = phase_state
                reg["SRC_READY_STATE"] = src_ready_state
                reg["DEST_READY_STATE"] = dest_ready_state
                reg["SRC_SIDE_PHASE_COMPLETE"] = src_side_phase_complete
                reg["DEST_SIDE_PHASE_COMPLETE"] = dest_side_phase_complete
                reg["SRC_STATE"] = src_state
                reg["DEST_STATE"] = dest_state

        return reg

    # This is from device/bin/silicon/<device>/ttx_status.py
    def get_endpoint_type(self, x, y):
        if x == 0:
            if y == 3:
                return "PCIE"
            elif y == 10:
                return "ARC"
            elif y in [2, 9, 4, 8]:
                return "Padding"
            else:
                return "GDDR"
        elif x == 5:
            return "GDDR"
        elif y in [0, 6]:
            return "Ethernet"
        else:
            return "Tensix"

    def read_print_noc_reg(
        self, loc, noc_id, reg_name, reg_index, reg_type=tt_device.Device.RegType.Status
    ):
        if reg_type == tt_device.Device.RegType.Cmd:
            status_offset = 0
        elif reg_type == tt_device.Device.RegType.Config:
            status_offset = 0x100
        else:
            status_offset = 0x200
        (x, y) = loc.to("nocVirt")
        endpoint_type = self.get_endpoint_type(x, y)
        if endpoint_type in ["Ethernet", "Tensix"]:
            reg_addr = 0xFFB20000 + (noc_id * 0x10000) + status_offset + (reg_index * 4)
            val = self.pci_read_xy(x, y, 0, reg_addr)
        elif endpoint_type in ["GDDR", "PCIE", "ARC"]:
            reg_addr = 0xFFFB20000 + status_offset + (reg_index * 4)
            xr = x if noc_id == 0 else 9 - x
            yr = y if noc_id == 0 else 11 - y
            val = self.pci_read_xy(xr, yr, noc_id, reg_addr)
        elif endpoint_type in ["Padding"]:
            reg_addr = 0xFFB20000 + status_offset + (reg_index * 4)
            xr = x if noc_id == 0 else 9 - x
            yr = y if noc_id == 0 else 11 - y
            val = self.pci_read_xy(xr, yr, noc_id, reg_addr)
        else:
            util.ERROR(f"Unknown endpoint type {endpoint_type}")
        print(
            f"{endpoint_type} x={x:02d},y={y:02d} => NOC{noc_id:d} {reg_name:s} (0x{reg_addr:09x}) = 0x{val:08x} ({val:d})"
        )
        return val
