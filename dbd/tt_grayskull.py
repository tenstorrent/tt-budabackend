# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import tt_util as util, os
import tt_device


class GrayskullL1AddressMap(tt_device.L1AddressMap):
    def __init__(self):
        super().__init__()

        ## Taken from l1_address_map.h. Ideally make this auto-generated
        self._l1_address_map = dict()
        self._l1_address_map["trisc0"] = tt_device.BinarySlot(offset_bytes = 0 + 20 * 1024 + 32 * 1024, size_bytes = 20 * 1024)
        self._l1_address_map["trisc1"] = tt_device.BinarySlot(offset_bytes = self._l1_address_map["trisc0"].offset_bytes + self._l1_address_map["trisc0"].size_bytes, size_bytes = 16 * 1024)
        self._l1_address_map["trisc2"] = tt_device.BinarySlot(offset_bytes = self._l1_address_map["trisc1"].offset_bytes + self._l1_address_map["trisc1"].size_bytes, size_bytes = 20 * 1024)
        # Brisc, ncrisc, to be added
        
class GrayskullDRAMEpochCommandAddressMap(tt_device.L1AddressMap):
    def __init__(self):
        super().__init__()
        
        ## Taken from dram_address_map.h. Ideally make this auto-generated
        self._l1_address_map = dict()
        self._l1_address_map["trisc0"] = tt_device.BinarySlot(offset_bytes = -1, size_bytes = 20 * 1024)
        self._l1_address_map["trisc1"] = tt_device.BinarySlot(offset_bytes = -1, size_bytes = 16 * 1024)
        self._l1_address_map["trisc2"] = tt_device.BinarySlot(offset_bytes = -1, size_bytes = 20 * 1024)
        # Brisc, ncrisc, to be added

#
# Device
#
class GrayskullDevice(tt_device.Device):
    SIG_SEL_CONST = 9
    # Some of this can be read from architecture yaml file
    DRAM_CHANNEL_TO_NOC0_LOC = [
        (1, 0),
        (1, 6),
        (4, 0),
        (4, 6),
        (7, 0),
        (7, 6),
        (10, 0),
        (10, 6),
    ]

    # Physical location mapping
    DIE_X_TO_NOC_0_X = [0, 12, 1, 11, 2, 10, 3, 9, 4, 8, 5, 7, 6]
    DIE_Y_TO_NOC_0_Y = [0, 11, 1, 10, 2, 9, 3, 8, 4, 7, 5, 6]
    DIE_X_TO_NOC_1_X = [12, 0, 11, 1, 10, 2, 9, 3, 8, 4, 7, 5, 6]
    DIE_Y_TO_NOC_1_Y = [11, 0, 10, 1, 9, 2, 8, 3, 7, 4, 6, 5]
    NOC_0_X_TO_DIE_X = util.reverse_mapping_list(DIE_X_TO_NOC_0_X)
    NOC_0_Y_TO_DIE_Y = util.reverse_mapping_list(DIE_Y_TO_NOC_0_Y)
    NOC_1_X_TO_DIE_X = util.reverse_mapping_list(DIE_X_TO_NOC_1_X)
    NOC_1_Y_TO_DIE_Y = util.reverse_mapping_list(DIE_Y_TO_NOC_1_Y)

    # Just an identity mapping
    NOC0_X_TO_NOCTR_X = {i: i for i in range(0, len(NOC_0_X_TO_DIE_X))}
    NOCTR_X_TO_NOC0_X = {v: k for k, v in NOC0_X_TO_NOCTR_X.items()}

    def get_harvested_noc0_y_rows(self):
        harvested_workers = self._block_locations["harvested_workers"]
        return list({y for x, y in harvested_workers})

    def noc0_to_tensix(self, noc0_loc):
        noc0_x, noc0_y = noc0_loc
        if noc0_y == 0 or noc0_y == 6:
            assert False, "NOC0 y=0 and y=6 do not have an RC coordinate"
        if noc0_x == 0:
            assert False, "NOC0 x=0 does not have an RC coordinate"
        row = noc0_y - 1
        col = noc0_x - 1
        if noc0_y > 6:
            row -= 1
        return row, col

    def tensix_to_noc0(self, netlist_loc):
        row, col = netlist_loc
        noc0_y = row + 1
        noc0_x = col + 1
        if noc0_y > 5:
            noc0_y += 1  # DRAM at noc0 Y coord of 6 is a hole in RC coordinates
        return noc0_x, noc0_y

    def _handle_harvesting_for_nocTr_noc0_map(self, num_harvested_rows):
        assert num_harvested_rows == 0, "Harvesting not supported for Grayskull"

    def __init__(self, id, arch, cluster_desc, device_desc_path, context):
        super().__init__(id, arch, cluster_desc, {"functional_workers": GrayskullL1AddressMap(), "dram": GrayskullDRAMEpochCommandAddressMap()}, device_desc_path, context)

    def row_count(self):
        return len(GrayskullDevice.DIE_Y_TO_NOC_0_Y)

    def no_tensix_row_count(self):
        return 2

    def no_tensix_col_count(self):
        return 1

    def get_num_msgs_received(self, noc0_loc, stream_id):
        return int(self.get_stream_reg_field(noc0_loc, stream_id, 224 + 5, 0, 16))

    def get_curr_phase_num_msgs_remaining(self, noc0_loc, stream_id):
        return int(self.get_stream_reg_field(noc0_loc, stream_id, 35, 0, 12))

    def get_remote_source(self, noc0_loc, stream_id):
        return self.get_stream_reg_field(noc0_loc, stream_id, 10, 5, 1)

    def get_remote_receiver(self, noc0_loc, stream_id):
        return self.get_stream_reg_field(noc0_loc, stream_id, 10, 8, 1)

    # Populates a dict with register names and current values on core x-y for stream with id 'stream_id'
    def read_stream_regs_direct(self, loc, stream_id):
        reg = {}
        reg["STREAM_ID"] = self.get_stream_reg_field(loc, stream_id, 224 + 9, 0, 6)
        reg["PHASE_AUTO_CFG_PTR (word addr)"] = self.get_stream_reg_field(
            loc, stream_id, 12, 0, 24
        )
        reg["CURR_PHASE"] = self.get_stream_reg_field(loc, stream_id, 11, 0, 20)
        reg["CURR_PHASE_NUM_MSGS_REMAINING"] = self.get_stream_reg_field(
            loc, stream_id, 35, 0, 12
        )
        reg["NUM_MSGS_RECEIVED"] = self.get_stream_reg_field(
            loc, stream_id, 224 + 5, 0, 16
        )
        reg["NEXT_MSG_ADDR"] = self.get_stream_reg_field(loc, stream_id, 224 + 6, 0, 16)
        reg["NEXT_MSG_SIZE"] = self.get_stream_reg_field(
            loc, stream_id, 224 + 6, 16, 16
        )
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
                    loc, stream_id, 15, 0, 16
                )

        if local_sources_connected == 1:
            local_src_mask_lo = self.get_stream_reg_field(loc, stream_id, 48, 0, 32)
            local_src_mask_hi = self.get_stream_reg_field(loc, stream_id, 49, 0, 32)
            local_src_mask = (local_src_mask_hi << 32) | local_src_mask_lo
            reg["LOCAL_SRC_MASK"] = local_src_mask
            reg["MSG_ARB_GROUP_SIZE"] = self.get_stream_reg_field(
                loc, stream_id, 13, 16, 3
            )
            reg["MSG_SRC_IN_ORDER_FWD"] = self.get_stream_reg_field(
                loc, stream_id, 13, 19, 1
            )
            reg["STREAM_MSG_SRC_IN_ORDER_FWD_NUM_MSREG_INDEX"] = (
                self.get_stream_reg_field(loc, stream_id, 14, 0, 24)
            )
        else:
            reg["BUF_START (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 6, 0, 16
            )
            reg["BUF_SIZE (words)"] = self.get_stream_reg_field(
                loc, stream_id, 7, 0, 16
            )
            reg["BUF_RD_PTR (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 23, 0, 16
            )
            reg["BUF_WR_PTR (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 24, 0, 16
            )
            reg["MSG_INFO_PTR (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 8, 0, 16
            )
            reg["MSG_INFO_WR_PTR (word addr)"] = self.get_stream_reg_field(
                loc, stream_id, 25, 0, 16
            )
            reg["STREAM_BUF_SPACE_AVAILABLE_REG_INDEX (word addr)"] = (
                self.get_stream_reg_field(loc, stream_id, 27, 0, 16)
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

        return reg
