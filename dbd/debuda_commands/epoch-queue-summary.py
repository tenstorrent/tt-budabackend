# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  eq

Description:
  Prints epoch queue summary

Examples:
  eq
"""

from debuda import UIState
import tt_util as util
import tt_device
from tt_netlist import Queue
from tabulate import tabulate

command_metadata = {"type": "low-level", "short": "eq", "description": __doc__}


# Prints epoch queues
def run(args, context, ui_state: UIState = None):
    verbose = len(args) > 1
    runtime_data = context.netlist.runtime_data_yaml.root
    arch_name = runtime_data.get("arch_name").upper()
    graph_name = ui_state.current_graph_name
    device_id = context.netlist.graph_name_to_device_id(graph_name)
    epoch_device = context.devices[device_id]
    distribtued_eq = bool(runtime_data.get("distribute_epoch_tables", 1))
    EPOCH_Q_NUM_SLOTS = runtime_data.get("epoch_queue_num_slots", 64)
    DRAM_EPOCH_METADATA_LIMIT = runtime_data.get(
        "dram_epoch_metadata_limit", 8 * 1024 * 1024
    )
    grid_size_row = runtime_data.get("grid_size_row", 12)
    grid_size_col = runtime_data.get("grid_size_col", 10)

    print(
        f"{util.CLR_INFO}Epoch queues for graph {graph_name}, device id {device_id} with EPOCH_Q_NUM_SLOTS: {EPOCH_Q_NUM_SLOTS} {util.CLR_END}"
    )

    # From tt_epoch_dram_manager::tt_epoch_dram_manager and following the constants
    EPOCH_Q_SLOT_SIZE = 64
    EPOCH_Q_SLOTS_OFFSET = 64
    EPOCH_QUEUE_SIZE_BYTES = (
        grid_size_row
        * grid_size_col
        * (EPOCH_Q_NUM_SLOTS * EPOCH_Q_SLOT_SIZE + EPOCH_Q_SLOTS_OFFSET)
    )
    EPOCH_QUEUE_START_ADDR = DRAM_EPOCH_METADATA_LIMIT - EPOCH_QUEUE_SIZE_BYTES

    dram_chan = 0  # CHECK: This queue is always in channel 0
    dram_loc = epoch_device.get_block_locations(block_type="dram")[dram_chan]

    table = []
    loc_str = "DRAM"

    if arch_name != "GRAYSKULL":
        # Get the enum mapping for EpochQueueCmd from ERISC ELF file
        EpochQueueCmd_val_to_str = context.elf.get_enum_mapping(
            "erisc_app.epoch_queue::EpochQueueCmd"
        )
        # Remove "EpochCmd" from the value strings
        EpochQueueCmd_val_to_str = {
            k: v.replace("EpochCmd", "") for k, v in EpochQueueCmd_val_to_str.items()
        }
    else:
        EpochQueueCmd_val_to_str = None

    occupied_epoch_queues_count = 0
    for blocktype in ["functional_workers", "eth"]:
        coretype = "Worker" if blocktype == "functional_workers" else "Ethernet"
        for loc in epoch_device.get_block_locations(block_type=blocktype):
            (x, y) = loc.to("noc0")
            if distribtued_eq:
                dram_loc = epoch_device.get_t6_queue_location(
                    arch_name, {"x": x, "y": y}
                )

            offset = (grid_size_col * y + x) * (
                EPOCH_Q_NUM_SLOTS * EPOCH_Q_SLOT_SIZE + EPOCH_Q_SLOTS_OFFSET
            )
            addr = EPOCH_QUEUE_START_ADDR + offset
            dx, dy = dram_loc["x"], dram_loc["y"]  # mapped dram location for this core

            # dloc = OnChipCoordinate(dx, dy, "noc0", epoch_device)
            rdptr = (
                tt_device.SERVER_IFC.pci_read_xy(device_id, dx, dy, 0, addr) & 0xFFFF
            )
            wrptr = (
                tt_device.SERVER_IFC.pci_read_xy(device_id, dx, dy, 0, addr + 4)
                & 0xFFFF
            )
            occupancy = int(Queue.occupancy(EPOCH_Q_NUM_SLOTS, wrptr, rdptr))

            addr_at_rdptr = ""
            cmd_at_rdptr = ""
            if occupancy != 0:
                occupied_epoch_queues_count += 1
                addr_at_rdptr = (
                    addr
                    + EPOCH_Q_SLOTS_OFFSET
                    + (EPOCH_Q_SLOT_SIZE * (rdptr % EPOCH_Q_NUM_SLOTS))
                )
                cmd_rd_word = (
                    tt_device.SERVER_IFC.pci_read_xy(
                        device_id, dx, dy, 0, addr_at_rdptr + 4
                    )
                    & 0xFFFFFFFF
                )
                cmd_at_rdptr = (cmd_rd_word >> 28) & 0xFFFF

            if occupancy != 0 or verbose:
                if EpochQueueCmd_val_to_str:
                    cmd_enum_str = (
                        EpochQueueCmd_val_to_str[cmd_at_rdptr]
                        if cmd_at_rdptr in EpochQueueCmd_val_to_str
                        else f"{cmd_at_rdptr}"
                    )
                else:
                    cmd_enum_str = f"{cmd_at_rdptr}"

                table.append(
                    [
                        f"{x}-{y}",
                        coretype,
                        loc_str,
                        f"{dx}-{dy}",
                        f"0x{addr:x}",
                        f"{rdptr}",
                        f"{wrptr}",
                        (
                            occupancy
                            if occupancy == 0
                            else f"{util.CLR_RED}{occupancy}{util.CLR_END}"
                        ),
                        "-" if occupancy == 0 else f"0x{addr_at_rdptr:x}",
                        "-" if occupancy == 0 else cmd_enum_str,
                    ]
                )

    if len(table) > 0:
        print(
            tabulate(
                table,
                headers=[
                    "T6 (noc0)",
                    "Core Type",
                    "Location",
                    "DRAM (noc0)",
                    "Address",
                    "RD ptr",
                    "WR ptr",
                    "Occupancy",
                    "Addr@RDptr",
                    "CmdType@RDptr",
                ],
                disable_numparse=True,
            )
        )
        if occupied_epoch_queues_count:
            util.WARN(
                f"There are {occupied_epoch_queues_count} occupied epoch queues. The occupancy of an epoch queue will be greater than 0 until the epoch is complete."
            )
    else:
        print("No epoch queues have occupancy > 0")
