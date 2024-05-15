# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from docopt import docopt
from tt_coordinate import OnChipCoordinate

class tt_docopt:
    """
    This is a wraper to docopt that allows for:
    - The common options to be used in multiple commands
    - Iterating over the values of the common options (for_each)
    """
    def device_id_for_each(device_id, context, ui_state):
        if not device_id:
            device_id = ui_state.current_device_id
            device = context.devices[device_id]
            yield device
        elif device_id == "all":
            for device in context.devices:
                yield device
        else:
            yield context.devices[int(device_id,0)]

    def loc_for_each(loc_str, context, ui_state, device):
        if not loc_str:
            yield ui_state.current_location
        elif loc_str == "all":
            for loc in device.get_block_locations(block_type="functional_workers"):
                yield loc
        elif "/" in loc_str:
            for loc in loc_str.split("/"):
                yield OnChipCoordinate.create(loc, device)
        else:
            yield OnChipCoordinate.create(loc_str, device)

    def risc_id_for_each(risc_id, context, ui_state):
        if not risc_id or risc_id == "all":
            for risc_id in range(4):
                yield risc_id
        else:
            yield int (risc_id, 0)

    # We define command options that apply to more than one command here
    OPTIONS = {
        "--verbose": {
            "short": "-v",
            "arg": "",
            "description": "Print verbose output. [default: False]",
        },
        "--test": {
            "short": "-t",
            "arg": "",
            "description": "Test mode. [default: False]",
        },
        "--device": {
            "short": "-d",
            "arg": "<device-id>",
            "description": "Device ID. Defaults to the current device.",
            "for_each": device_id_for_each,
        },
        "--loc": {
            "short": "-l",
            "arg": "<loc>",
            "description": "Grid location. Defaults to the current location.",
            "for_each": loc_for_each,
        },
        "--risc": {
            "short": "-r",
            "arg": "<risc-id>",
            "description": "RiscV ID (0: brisc, 1-3 triscs, all). [default: all]",
            "for_each": risc_id_for_each,
        }
    }

    def create_docopt_options_string(option_names):
        """
        Given an argument name or an iterable over names, return an "Options" string for that argument.
        """
        if not option_names:
            return ""
        if type(option_names) == str:
            option_names = [option_names]

        options_string = ""
        for opt_name in option_names:
            if opt_name not in tt_docopt.OPTIONS:
                raise ValueError(f"Invalid argument name: {opt_name}")
            opt_info = tt_docopt.OPTIONS[opt_name]
            if opt_info["arg"]:
                arg = f" {opt_info['arg']}"
            else:
                arg = ""
            combined_option = f"{opt_info['short']}{arg}"
            options_string += f"  {combined_option: <20}   {opt_info['description']}\n"
        return options_string


    def __init__(self, doc, argv=None, common_option_names=[]):
        additional_options = tt_docopt.create_docopt_options_string(common_option_names)
        self.doc = doc + f"\nOptions:\n{additional_options}"
        self.argv = argv
        self.option_names = common_option_names
        self.args = docopt(self.doc, self.argv)

    def for_each(self, option_name, context, ui_state, **kwargs):
        option_short_name = tt_docopt.OPTIONS[option_name]["short"]
        opt_value = self.args[option_short_name]
        func = tt_docopt.OPTIONS[option_name]["for_each"]
        yield from func(opt_value, context, ui_state, **kwargs)

def find_command(commands, name):
    for c in commands:
        if c["long"] == name or c["short"] == name:
            return c
    return None