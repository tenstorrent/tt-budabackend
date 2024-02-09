# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  info <access-path> [ -f <format> ] [ -c <core-loc>... ] [-d <device-id>...]

Arguments:
  access-path     A C-style access path to a variable. Example: brisc.EPOCH_INFO_PTR.epoch_id. The first part
                  of the path points to the ELF file.

Options:
  -d <device-id>  Device ID. Optional and repeatable. Default: current device
  -c <core-loc>   Either X-Y or R,C location of the core. Optional and repeatable. Default: current core
  -f <format>     If format is specified, the access-path is interpreted with a given format.

Description:
  Reads one or more values from the chip based on the <access-path>. The <access-path> is a C-style access path
  to a variable. Example: brisc.EPOCH_INFO_PTR.epoch_id. The first part of the path points to the ELF file.

Examples:
    info brisc.EPOCH_INFO_PTR                                  # Print the epoch struct of the current core
    info brisc.EPOCH_INFO_PTR.epoch_id -c 18-18 -c 19-19 -d 0  # Print all epoch_ids of cores 18-18 and 19-19 on device 0
"""

command_metadata = {"short": "i", "type": "high-level", "description": __doc__}

from docopt import docopt
import tt_util as util
from tt_object import DataArray
from tt_coordinate import OnChipCoordinate
import tt_device
import re


def print_access_path(device, core_loc, elf, path):
    """
    Given a C access access path 'path', print all members and their current value.
    The format_descriptor is a way to customize the output. See util.FORMAT_DESCRIPTOR_DEFAULTS in tt_util.py.
    Returns: number of members found and printed.
    """
    elf_name, path = path.split(".", 1)
    members = elf.get_member_paths(elf_name, path)

    # Iterate over members, read data from device and print
    def process_member(member):
        if member["children"]:
            for child in member["children"]:
                process_member(child)
        else:
            addr, size = member["addr"], member["size"]
            last_dot_pos = member["name"].rfind(".")
            member_short_name = member["name"][last_dot_pos + 1 :]

            name_color, value_color, format, end = (
                util.CLR_BLUE,
                util.CLR_END,
                util.DEC_AND_HEX_FORMAT,
                "\n",
            )

            # Read data from device
            da = DataArray(f"L1-0x{addr:08x}-{size}", 4)
            num_words = (size + 3) // 4
            for i in range(num_words):
                data = tt_device.SERVER_IFC.pci_read_xy(
                    device.id(), *core_loc.to("nocVirt"), 0, addr + 4 * i
                )
                da.data.append(data)

            if size > 4:
                txt = util.dump_memory(addr, da.data, 4, 32, True)
                txt = "\n".join([f"  {line}" for line in txt.split("\n")])  # Add indent
                txt = f"{name_color}{member_short_name}{util.CLR_END} =\n{value_color}{txt}{util.CLR_END}"
                # Indent by 2
            else:
                d = da.data[0]
                d = d & ((1 << (size * 8)) - 1)
                txt = f"{name_color}{member_short_name}{util.CLR_END} = {value_color}{eval(format)}{util.CLR_END}"
            print(txt, end=end)

    process_member(members)

    return len(members)


def run(cmd_text, context, ui_state=None):
    args = docopt(command_metadata["description"], argv=cmd_text.split()[1:])

    current_device_id = ui_state["current_device_id"]
    current_device = context.devices[current_device_id]
    current_loc = ui_state["current_loc"]

    core_locs = args["-c"] if args["-c"] else [current_loc.to_str()]
    core_array = []
    for core_loc in core_locs:
        coord = OnChipCoordinate.create(core_loc, current_device)
        core_array.append(coord)

    device_ids = args["-d"] if args["-d"] else [f"{current_device_id}"]
    device_array = []
    for device_id in device_ids:
        did = int(device_id, 0)
        device_array.append(context.devices[did])

    access_path = args["<access-path>"]
    for device in device_array:
        for core_loc in core_array:
            util.PRINT(f"Reading from device {device.id()}, core {core_loc.to_str()}")
            num_printed = print_access_path(device, core_loc, context.elf, access_path)
            if num_printed == 0:
                util.WARN(f"Could not find access-path '{access_path}'")
