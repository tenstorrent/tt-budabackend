# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
This module is used to represent the firmware
"""

import time
import tt_parse_elf
import tt_util as util
import os, re
from fuzzywuzzy import process, fuzz

# This is a list of firmware variables that do not make it to the ELF file (e.g. they
# are hard-coded through #define). We need to 'inject' them into the symbol table with
# the correct type to have the lookup work.
BUDA_FW_VARS = {
    "EPOCH_INFO_PTR": {
        "offset": "EPOCH_INFO_ADDR",  # The address of the variable. If string, it is looked up in the ELF
        # If int, it is used as is
        "type": "epoch_t",  # The type of the variable.
    },
    "ETH_EPOCH_INFO_PTR": {"offset": "ETH_EPOCH_INFO_ADDR", "type": "epoch_t"},
}

class FAKE_DIE(object):
    """
    Some pointers are hard-coded in the source with #defines. These names do not make
    it to the DWARF info. We add them here. To keep the rest of code consistent, we create
    fake DIEs for these names, whith minimal required configuration.
    """

    def __init__(self, var_name, addr, resolved_type):
        self.name = var_name
        self.address = addr
        self.resolved_type = resolved_type
        self.size = self.resolved_type.size


class ELF:
    """
    This class wraps around a list of ELF files and provides a unified interface to them.
    """

    def __init__(self, filemap, extra_vars=None) -> None:
        """
        Given a filemap "prefix" -> "filename", load all the ELF files and store them in
        self.names. For example, if filemap is { "brisc" : "./debuda_test/brisc/brisc.elf" }
        """
        self.names = dict()
        self.filemap = filemap
        for prefix, filename in filemap.items():
            if prefix not in self.names:
                self.names[prefix] = dict()

            abspath = os.path.abspath(filename)
            util.INFO(f"Loading ELF file: '{abspath}'", end="")
            start_time = time.time()
            self.names[prefix] = tt_parse_elf.read_elf(abspath)
            util.INFO(
                f" ({os.path.getsize(abspath)} bytes loaded in {time.time() - start_time:.2f}s)"
            )
            self.name_word_pattern = re.compile(r"[_@.a-zA-Z]+")

            # Inject the variables that are not in the ELF
            if extra_vars:
                self._process_extra_vars(prefix, extra_vars)

    def _process_extra_vars(self, prefix, extra_vars):
        """
        Given a prefix, inject the variables that are not in the ELF
        """
        for var_name, var in extra_vars.items():
            if var_name in self.names[prefix]["variable"]:
                util.INFO(f"Variable '{var_name}' already in ELF. Skipping")
                continue
            offset_var = var["offset"]
            if offset_var not in self.names[prefix]["variable"]:
                raise util.TTException(
                    f"Variable '{offset_var}' not found in ELF. Cannot add '{var_name}'"
                )
            ov = self.names[prefix]["variable"][offset_var]
            addr = addr = ov.value if ov.value else ov.address
            resolved_type = self.names[prefix]["type"][var["type"]].resolved_type
            self.names[prefix]["variable"][var_name] = FAKE_DIE(
                var_name, addr=addr, resolved_type=resolved_type
            )

    def _get_prefix_and_suffix(self, path_str):
        dot_pos = path_str.find(".")
        if dot_pos == -1:
            return "", path_str  # just the suffix
        return path_str[:dot_pos], path_str[dot_pos + 1 :]

    def fuzzy_find_multiple(self, path, limit):
        """
        Given a path, return a list of all symbols that match the path. This is used for auto-completion.
        """
        at_prefix = False
        if path.startswith("@"):
            path = path[1:]
            at_prefix = True
        elf_name, suffix = self._get_prefix_and_suffix(path)

        if elf_name not in self.names:
            elf = None
            choices = self.names.keys()
        else:
            elf = self.names[elf_name]
            choices = elf["variable"].keys()

        # Uses Levenshtein distance to find the best match for a query in a list of keys
        matches = process.extract(suffix, choices, scorer=fuzz.QRatio, limit=limit)

        sorted_matches = sorted(matches, key=lambda x: x[1], reverse=True)

        if elf:
            filtered_matches = [
                f"{'@' if at_prefix else ''}{elf_name}.{match}"
                for match, score in sorted_matches
            ]
        else:
            filtered_matches = [match for match, score in sorted_matches]
        return filtered_matches

    def substitute_names_with_values(self, text):
        """
        Replace all names starting with @ with their addresses
        """

        def repl(match):
            addr, _ = self.parse_addr_size(match.group(0))
            return f"0x{addr:08x}"

        return re.sub(r"@[_@.a-zA-Z0-9\[\]]+", repl, text)

    def get_enum_mapping(self, enum_path):
        """
        Given a path, return the enum mapping for it. Example: get_enum_mapping("erisc_app.epoch_queue.EpochQueueCmd")
        will return a dict of {1: "EpochCmdValid", 2: "EpochCmdNotValid...", ...}
        """
        ret_val = {}
        elf_name, enum_path = self._get_prefix_and_suffix(enum_path)
        names = self.names[elf_name]
        for name, data in names["enumerator"].items():
            if name.startswith(enum_path):
                val = data.value

                ret_val[val] = name[name.rfind("::") + 2 :]
        return ret_val

    def get_member_paths(self, elf_name, access_path, mem_reader=None):
        """
        Given an access path, return all the member paths recursively. The return value is a tree
        with the format as seen in 'ret_val' below.
        """
        addr, size, type_die = self._get_var_addr_size_type(elf_name, access_path, mem_reader)
        ret_val = {
            "name": access_path,
            "addr": addr,
            "size": size,
            "type": type_die,  # ELF DIE (see tt_parse_elf.py)
            "children": [],
        }

        access_operator = "."
        if type_die.tag_is("pointer_type"):
            type_die = type_die.dereference_type
            access_operator = "->"

        for child in type_die.iter_children():
            if child.tag == "DW_TAG_member":
                if "DW_AT_name" in child.attributes:
                    child_name = child.attributes["DW_AT_name"].value.decode("utf-8")
                    ret_val["children"].append(
                        self.get_member_paths(elf_name, f"{access_path}{access_operator}{child_name}", mem_reader)
                    )
        return ret_val

    def _get_var_addr_size_type(self, elf_name, var_name, mem_reader=None):
        """
        Given an access path to a variable (e.g. "EPOCH_INFO_PTR.epoch_id"), return the
        address, size and type_die of the variable. If the variable is not found, return None.
        """

        def my_mem_reader(addr, size_bytes):
            pass

        if mem_reader is None:
            mem_reader = my_mem_reader

        _, ret_addr, ret_size_bytes, type_die = tt_parse_elf.mem_access(
            self.names[elf_name], var_name, mem_reader
        )
        return ret_addr, ret_size_bytes, type_die

    def _get_var_addr_size(self, elf_name, var_name, mem_reader=None):
        """
        Given an access path to a variable (e.g. "EPOCH_INFO_PTR.epoch_id"), return the
        address, size the variable. If the variable is not found, return None.
        """
        ret_addr, ret_size_bytes, _ = self._get_var_addr_size_type(elf_name, var_name, mem_reader)
        return ret_addr, ret_size_bytes

    def parse_addr_size_type(self, path_str, mem_reader=None):
        """
        When path is given with the elf prefix
        """
        if path_str.startswith("@"):
            path_str = path_str[1:]
        return self._get_var_addr_size_type(*self._get_prefix_and_suffix(path_str), mem_reader)

    def parse_addr_size(self, path_str, mem_reader=None):
        """
        When path is given with the elf prefix
        """
        addr, size, _ = self.parse_addr_size_type(path_str, mem_reader)
        return addr, size

    def read_path(self, path_str, mem_reader):
        """
        Given a path, read the data using mem_reader and return it.
        """
        if path_str.startswith("@"):
            path_str = path_str[1:]
        elf_name, var_name = self._get_prefix_and_suffix(path_str)
        data, ret_addr, ret_size_bytes, type_die = tt_parse_elf.mem_access(
            self.names[elf_name], var_name, mem_reader
        )
        return data

    @staticmethod
    def get_mem_reader(device_id, core_loc):
        """
        Returns a simple memory reader function that reads from a given device and a given core.
        """

        def mem_reader(addr, size_bytes):
            import tt_device

            data = tt_device.SERVER_IFC.pci_read_xy(
                device_id, *core_loc.to("nocVirt"), 0, addr
            )
            return [data]

        return mem_reader
