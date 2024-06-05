from __future__ import annotations

import argparse
import os
import random

from pathlib import Path
from ruamel.yaml import YAML
from ruamel.yaml.scalarint import HexInt
import zipfile

from test_modules.common.data_formats import DataFormat
from test_modules.common.device_architecture import BlackholeArchitecture


class Netlist:
    def __init__(self, path: str, parent_zip: str = None) -> None:
        self.netlist_path = Path(path)
        self.parent_folder = self.netlist_path.parent
        self.parent_zip = parent_zip

    @property
    def path(self) -> str:
        return str(self.netlist_path)

    @property
    def zip_archive(self) -> str:
        return f"{str(self.parent_folder.stem)}/{str(self.netlist_path.name)}"

    def store_in_zip(self) -> None:
        if self.parent_zip:
            with zipfile.ZipFile(self.parent_zip, "a") as zipf:
                zipf.write(self.path, self.zip_archive)


class NetlistFinder:
    YAML_EXTENSION = ".yaml"

    def __init__(self) -> None:
        self.netlists = []

    def find_all_netlists(self, netlists_path: str) -> list[Netlist]:
        netlists = []
        netlists.extend(self.find_netlists(netlists_path))
        netlists.extend(self.unzip_and_collect_netlists(self.find_netlist_zips(netlists_path)))
        self.remove_old_zips(netlists_path)
        return netlists

    def get_netlist_name(self, netlist_path: str) -> str:
        return Path(netlist_path).stem

    def find_netlists(self, root_path: str) -> list[Netlist]:
        invalid_netlists_names = [
            "default_netlist",
            "netlist_queues",
            "blob",
            "pipegen",
            "queue_to_consumer",
            "test_configs",
        ]

        netlists = []

        if os.path.isdir(root_path):
            for entry_path in os.listdir(root_path):
                # Skip dbd/dbd/dbd... symbolic link infinite loop.
                if entry_path == "dbd":
                    continue

                full_entry_path = os.path.join(root_path, entry_path)
                if os.path.isdir(full_entry_path):
                    netlists.extend(self.find_netlists(full_entry_path))
                elif full_entry_path.endswith(NetlistFinder.YAML_EXTENSION):
                    netlist_name = self.get_netlist_name(entry_path)
                    if netlist_name not in invalid_netlists_names:
                        netlists.append(Netlist(full_entry_path))
        elif root_path.endswith(NetlistFinder.YAML_EXTENSION):
            netlist_name = self.get_netlist_name(root_path)
            if netlist_name not in invalid_netlists_names:
                netlists.append(Netlist(root_path))

        return netlists

    def find_netlist_zips(self, dir_path: str) -> list[str]:
        netlist_zips = []

        if os.path.isdir(dir_path):
            for entry_path in os.listdir(dir_path):
                full_entry_path = os.path.join(dir_path, entry_path)
                if os.path.isdir(full_entry_path):
                    netlist_zips.extend(self.find_netlist_zips(full_entry_path))
                elif full_entry_path.endswith(".zip"):
                    netlist_zips.append(full_entry_path)

        return netlist_zips

    def unzip_and_collect_netlists(self, netlist_zips: list[str]) -> list[Netlist]:
        unzipped_netlists = []

        for netlist_zip in netlist_zips:
            netlist_zip = Path(netlist_zip)
            out_dir = Path(netlist_zip.parent, netlist_zip.stem)
            os.makedirs(out_dir, exist_ok=True)
            os.system(f"unzip -qo {netlist_zip} -d {out_dir}")

            unzipped_netlists.extend(
                [Netlist(netlist.path, netlist_zip) for netlist in self.find_netlists(out_dir)]
            )

        return unzipped_netlists

    def remove_old_zips(self, dir: str) -> None:
        os.system(f"rm -rf {dir}/*.zip")


class QueueNode:
    """Abstraction of a queue in netlist."""

    def __init__(self, queue_name: str, queue_dict: dict) -> None:
        self.queue_name = queue_name
        self.type = queue_dict["type"]
        self.input = queue_dict["input"]
        self.entries = queue_dict["entries"]
        self.grid_size = queue_dict["grid_size"]
        self.t_dim = queue_dict["t"]
        self.mblock = queue_dict["mblock"]
        self.ublock = queue_dict["ublock"]
        self.df = DataFormat[queue_dict["df"]]
        self.ublock_order = queue_dict.get("ublock_order", "r")
        self.target_device = queue_dict["target_device"]
        self.loc = queue_dict["loc"]
        setattr(self, self.loc, queue_dict[self.loc])

    @property
    def name(self) -> str:
        return self.queue_name

    @property
    def grid_y(self) -> int:
        return self.grid_size[0]

    @property
    def grid_x(self) -> int:
        return self.grid_size[1]

    @property
    def mb_m(self) -> int:
        return self.mblock[0]

    @property
    def mb_n(self) -> int:
        return self.mblock[1]

    @property
    def ub_r(self) -> int:
        return self.ublock[0]

    @property
    def ub_c(self) -> int:
        return self.ublock[1]

    @property
    def t(self) -> int:
        return self.t_dim

    @property
    def data_format(self) -> DataFormat:
        return self.df

    @property
    def location(self) -> int:
        return self.loc

    def is_in_dram(self) -> bool:
        return self.loc == "dram"

    @property
    def num_buffers(self) -> int:
        return self.grid_x * self.grid_y

    @property
    def num_tiles_in_buffer(self) -> int:
        return self.mb_m * self.mb_n * self.ub_r * self.ub_c * self.t * self.entries

    @property
    def buffers_locations(self) -> list[int]:
        return getattr(self, self.loc)

    @buffers_locations.setter
    def buffers_locations(self, value) -> None:
        setattr(self, self.loc, value)

    @property
    def dram_channel(self) -> list[int]:
        assert self.is_in_dram()
        return self.buffers_locations[0][0]


class FixNetlistForBH:
    def __init__(self, netlists: list[Netlist], keep_old_arch: bool) -> None:
        self.netlists = netlists
        self.keep_old_arch = keep_old_arch
        self.arch = BlackholeArchitecture()
        # Yaml settings.
        self.yaml = YAML(typ="rt")
        self.yaml.width = 200
        self.yaml.allow_duplicate_keys = True

    def reset_starting_addresses(self) -> None:
        # Starting addresses for queues located on DRAM and HOST.
        self.curr_dram_start_address = [
            self.arch.dram_buffer_start_addr
        ] * self.arch.num_dram_channels
        self.curr_host_addr = 0

    def read_netlist(self, netlist_path: str) -> dict:
        assert os.path.exists(netlist_path), f"Netlist file: '{netlist_path}' not found."

        with open(netlist_path, "r") as file:
            netlist_dict = self.yaml.load(file)

        return netlist_dict

    def pick_dram_channel(self, old_dram_channel, total_queue_size) -> int:
        """Finds DRAM channel to use for queue."""
        dram_channel = old_dram_channel
        # Pick any DRAM channel we haven't filled all the way if we don't fit in this one.
        while True:
            queue_end_address = self.curr_dram_start_address[dram_channel] + total_queue_size
            if queue_end_address < self.arch.dram_buffer_end_addr:
                break
            else:
                dram_channel = random.choice(range(self.arch.num_dram_channels))

        return dram_channel

    def get_new_channels_and_addresses(
        self, dram_channel, queue_start_address, queue_end_address, single_dram_buffer_size
    ) -> list[int]:
        """Returns list of [channel, addr] pairs for all buffers in queue."""
        # Make DRAM channel fixed and pick successsive addresses in steps of single buffer size aligned to NOC width.
        return [
            [dram_channel, HexInt(addr)]
            for addr in range(queue_start_address, queue_end_address, single_dram_buffer_size)
        ]

    def calculate_new_buffers_locations(self, queue: QueueNode) -> None:
        """Based on old locations and new arch restrictions, calculates new locations for queues."""
        # Single DRAM buffer size is guaranteed to be aligned to arch, since queue headers are aligned as well as
        # tile sizes. Therefore, total queue size will also be arch aligned.
        single_dram_buffer_size = (
            queue.num_tiles_in_buffer * self.arch.get_tile_size(queue.data_format)
            + self.arch.dram_queue_header_size
        )
        total_queue_size = queue.num_buffers * single_dram_buffer_size

        if queue.is_in_dram():
            dram_channel = self.pick_dram_channel(queue.dram_channel, total_queue_size)
            queue_start_address = self.curr_dram_start_address[dram_channel]
            queue_end_address = queue_start_address + total_queue_size
            # Set new DRAM locations.
            queue.buffers_locations = self.get_new_channels_and_addresses(
                dram_channel, queue_start_address, queue_end_address, single_dram_buffer_size
            )
            # Next queue will start where this one ends.
            self.curr_dram_start_address[dram_channel] += total_queue_size
        else:
            # Set new HOST location.
            queue.buffers_locations = [HexInt(self.curr_host_addr)]
            # Next queue will start where this one ends.
            self.curr_host_addr += total_queue_size

    def fix_queues_locations(self, netlist_dict: dict) -> None:
        """Goes through queues in netlist dictionary and replaces their locations with recalculated ones."""
        # For each new netlist we process, bring address counters to the beginning.
        self.reset_starting_addresses()

        for q_name, q_dict in netlist_dict["queues"].items():
            # Wrap queue data from netlist into an abstraction.
            queue = QueueNode(q_name, q_dict)
            # Based on new arch restrictions for queue addresses, recalculate buffers locations.
            self.calculate_new_buffers_locations(queue)

            # Overwrite old queue addresses with new data.
            netlist_dict["queues"][queue.name][queue.location] = queue.buffers_locations

            if not self.keep_old_arch:
                # Overwrite arch.
                netlist_dict["devices"]["arch"] = str(self.arch)

    def fix_netlists(self) -> None:
        for netlist in self.netlists:
            netlist_dict = self.read_netlist(netlist.path)
            self.fix_queues_locations(netlist_dict)
            self.overwrite_netlist(netlist.path, netlist_dict)
            netlist.store_in_zip()

    def overwrite_netlist(self, netlist_path: str, netlist_dict: dict) -> None:
        with open(netlist_path, "w") as output_yaml_file:
            self.yaml.dump(netlist_dict, output_yaml_file)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--path-to-netlists",
        required=True,
        type=str,
        help="Path to folder with netlist yamls, or separate folders with netlists in them, or zips with netlists",
    )
    parser.add_argument(
        "--keep-arch",
        required=False,
        action="store_true",
        default=False,
        help="If used, will keep arch used in netlist, otherwise will overwrite to 'blackhole'",
    )
    args = parser.parse_args()

    finder = NetlistFinder()
    netlists = finder.find_all_netlists(args.path_to_netlists)

    fixer = FixNetlistForBH(netlists, args.keep_arch)
    fixer.fix_netlists()
