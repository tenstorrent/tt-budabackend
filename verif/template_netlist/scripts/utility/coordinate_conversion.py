from __future__ import annotations

from abc import ABC, abstractmethod
import os
from dataclasses import dataclass

from ruamel.yaml import YAML


@dataclass
class CoreLocation:
    x: int
    y: int

    def __str__(self) -> str:
        return f"(x = {self.x}, y = {self.y})"

    def convert_to_netlist_coordinates(self) -> NetlistCoordinates:
        return NetlistCoordinates(self.y, self.x)


@dataclass
class NetlistCoordinates:
    def __init__(self, r: int, c: int) -> None:
        self.r = r
        self.c = c

    def __str__(self) -> str:
        return f"(r = {self.r}, c = {self.c})"


class WorkerCores(ABC):
    def __init__(self) -> None:
        self.cores = self.get_worker_cores()
        self.worker_logical_to_physical_x = {}
        self.worker_logical_to_physical_y = {}
        self.worker_physical_to_logical_x = {}
        self.worker_physical_to_logical_y = {}
        self.map_logical_physical()

    @abstractmethod
    def get_worker_cores(self) -> list[CoreLocation]:
        pass

    def map_logical_physical(self) -> dict:
        def worker_x_coordinates() -> set[int]:
            worker_x = set()

            for worker_core in self.cores:
                worker_x.add(worker_core.x)

            return worker_x

        def worker_y_coordinates() -> set[int]:
            worker_y = set()

            for worker_core in self.cores:
                worker_y.add(worker_core.y)

            return worker_y

        for idx, worker_x in enumerate(worker_x_coordinates()):
            self.worker_logical_to_physical_x[idx] = worker_x
            self.worker_physical_to_logical_x[worker_x] = idx

        for idx, worker_y in enumerate(worker_y_coordinates()):
            self.worker_logical_to_physical_y[idx] = worker_y
            self.worker_physical_to_logical_y[worker_y] = idx

    def logical_to_physical_coordinates(self, logical_location: CoreLocation) -> CoreLocation:
        return CoreLocation(
            self.worker_logical_to_physical_x[logical_location.x],
            self.worker_logical_to_physical_y[logical_location.y],
        )

    def physical_to_logical_coordinates(self, physical_location: CoreLocation) -> CoreLocation:
        return CoreLocation(
            self.worker_physical_to_logical_x[physical_location.x],
            self.worker_physical_to_logical_y[physical_location.y],
        )

    def physical_to_netlist_coordinates(
        self, physical_location: CoreLocation
    ) -> NetlistCoordinates:
        return CoreLocation(
            self.worker_physical_to_logical_x[physical_location.x],
            self.worker_physical_to_logical_y[physical_location.y],
        ).convert_to_netlist_coordinates()

    @staticmethod
    def create_from_string(arch: str) -> WorkerCores:
        if arch == "grayskull":
            return GSWorkerCores()
        elif arch == "wormhole" or arch == "wormhole_b0":
            return WHWorkerCores()
        elif arch == "blackhole":
            return BHWorkerCores()
        else:
            raise RuntimeError(f"Invalid architecture {arch}")


class GSWorkerCores(WorkerCores):
    def __init__(self) -> None:
        super().__init__()

    def get_worker_cores(self) -> list[CoreLocation]:
        worker_cores = []

        for i in range(1, 13):
            for j in range(1, 12):
                if j == 6:
                    continue

                worker_cores.append(CoreLocation(i, j))

        return worker_cores


class WHWorkerCores(WorkerCores):
    def __init__(self) -> None:
        super().__init__()

    def get_worker_cores(self) -> list[CoreLocation]:
        worker_cores = []

        for j in range(1, 12):
            for i in range(1, 10):
                if i == 5 or j == 6:
                    continue

                worker_cores.append(CoreLocation(i, j))

        return worker_cores


class BHWorkerCores(WHWorkerCores):
    def __init__(self) -> None:
        super().__init__()


@dataclass
class SoCInfoConstants:
    wh_dram_cores_column = 5
    wh_eth_cores_first_row = 0
    wh_eth_cores_second_row = 6
    wh_harvested_translation_offset = 16
    wh_harvested_grid_size = 32


class SoCInfo:
    @staticmethod
    def is_harvested(core_location: CoreLocation) -> bool:
        return (
            core_location.x > SoCInfoConstants.wh_harvested_translation_offset
            and core_location.y > SoCInfoConstants.wh_harvested_translation_offset
        )

    @staticmethod
    def convert_harvested_core_location_to_unharvested(
        harvested_core_location: CoreLocation,
    ) -> CoreLocation:
        unharvested_core_location = harvested_core_location

        # Shift back x coordinates, leaving space for dedicated dram cores.
        unharvested_core_location.x -= SoCInfoConstants.wh_harvested_translation_offset
        if unharvested_core_location.x <= SoCInfoConstants.wh_dram_cores_column:
            unharvested_core_location.x -= 1

        # Shift back translated ethernet rows to their default positions.
        if harvested_core_location.y == SoCInfoConstants.wh_harvested_translation_offset:
            unharvested_core_location.y = SoCInfoConstants.wh_eth_cores_first_row
        elif harvested_core_location.y == SoCInfoConstants.wh_harvested_translation_offset + 1:
            unharvested_core_location.y = SoCInfoConstants.wh_eth_cores_second_row
        else:
            # Shift back everything else, leaving space for dedicated ethernet cores.
            unharvested_core_location.y -= SoCInfoConstants.wh_harvested_translation_offset
            if unharvested_core_location.y <= SoCInfoConstants.wh_eth_cores_second_row:
                unharvested_core_location.y -= 1

        return unharvested_core_location

    @staticmethod
    def convert_unharvested_core_location_to_harvested(
        unharvested_core_location: CoreLocation,
    ) -> CoreLocation:
        harvested_core_location = unharvested_core_location

        # Shift all unharvested x coordinates to the right, eliminating dram cores from harvested coordinate system.
        harvested_core_location.x += SoCInfoConstants.wh_harvested_translation_offset
        if unharvested_core_location.x < SoCInfoConstants.wh_dram_cores_column:
            harvested_core_location.x += 1

        # Shift ethernet rows to the beginning of translated coordinate system.
        if unharvested_core_location.y == SoCInfoConstants.wh_eth_cores_first_row:
            harvested_core_location.y = SoCInfoConstants.wh_harvested_translation_offset
        elif unharvested_core_location.y == SoCInfoConstants.wh_eth_cores_second_row:
            harvested_core_location.y = SoCInfoConstants.wh_harvested_translation_offset + 1
        else:
            # Shift other cores to come after ethernet cores in harvested coordinate system.
            harvested_core_location.y += SoCInfoConstants.wh_harvested_translation_offset
            if unharvested_core_location.y < SoCInfoConstants.wh_eth_cores_second_row:
                harvested_core_location.y += 1

        return harvested_core_location


class OpNode:
    def __init__(self, op_name: str, graph_name: str, op_dict: dict) -> None:
        self.op_name = op_name
        self.graph_name = graph_name
        self.op_type = op_dict["type"]
        self.grid_loc = op_dict["grid_loc"]
        self.grid_size = op_dict["grid_size"]

    @property
    def name(self) -> str:
        return self.op_name

    @property
    def type(self) -> str:
        return self.op_type

    @property
    def grid_loc_r(self) -> int:
        return self.grid_loc[0]

    @property
    def grid_loc_c(self) -> int:
        return self.grid_loc[1]

    @property
    def grid_size_r(self) -> int:
        return self.grid_size[0]

    @property
    def grid_size_c(self) -> int:
        return self.grid_size[1]

    @property
    def spanning_cores(self) -> list[NetlistCoordinates]:
        cores = []
        for r in range(self.grid_loc_r, self.grid_loc_r + self.grid_size_r):
            for c in range(self.grid_loc_c, self.grid_loc_c + self.grid_size_c):
                cores.append(NetlistCoordinates(r, c))

        return cores


class ProblematicCoreFinder:
    def __init__(self, netlist_path: str) -> None:
        # Yaml settings.
        self.yaml = YAML(typ="rt")
        self.yaml.width = 200
        self.netlist_path = netlist_path
        self.netlist_dict = self.read_netlist()
        self.cores_to_ops_mapping = {}
        # We support only one arch.
        self.arch = self.netlist_dict["devices"]["arch"]
        self.arch = self.arch[0] if isinstance(self.arch, list) else self.arch

    def read_netlist(self) -> dict:
        assert os.path.exists(self.netlist_path), f"Netlist file: '{self.netlist_path}' not found."

        with open(self.netlist_path, "r") as file:
            netlist_dict = self.yaml.load(file)

        return netlist_dict

    def map_cores_to_op(self, op: OpNode) -> None:
        for core in op.spanning_cores:
            if op.graph_name not in self.cores_to_ops_mapping:
                self.cores_to_ops_mapping[op.graph_name] = {}

            self.cores_to_ops_mapping[op.graph_name][str(core)] = op

    def map_cores_to_ops(self) -> None:
        for graph_name in self.netlist_dict["graphs"]:
            for op_name in self.netlist_dict["graphs"][graph_name]:
                if not self.is_op(op_name):
                    continue

                op = OpNode(op_name, graph_name, self.netlist_dict["graphs"][graph_name][op_name])
                self.map_cores_to_op(op)

    def is_op(self, op_name: str) -> bool:
        return op_name != "target_device" and op_name != "input_count"

    def find_ops(self, problematic_core: NetlistCoordinates) -> list[OpNode]:
        ops = []
        for graph_name in self.cores_to_ops_mapping.keys():
            op = self.cores_to_ops_mapping[graph_name].get(str(problematic_core), None)
            if op:
                ops.append(op)

        return ops

    def convert_to_netlist_coordinates(
        self, core_physical_location: CoreLocation
    ) -> NetlistCoordinates:
        if SoCInfo.is_harvested(core_physical_location):
            core_physical_location = SoCInfo.convert_harvested_core_location_to_unharvested(
                core_physical_location
            )

        converter = WorkerCores.create_from_string(self.arch)
        return converter.physical_to_netlist_coordinates(core_physical_location)

    def find(self, problematic_core_physical_location: CoreLocation) -> None:
        problematic_core = self.convert_to_netlist_coordinates(problematic_core_physical_location)
        print(f"Netlist coordinates of problematic core: {problematic_core}")

        self.map_cores_to_ops()
        problematic_ops = self.find_ops(problematic_core)

        if len(problematic_ops) == 0:
            print(f"Problematic core not found in any OP in netlist!")
            return

        for op in problematic_ops:
            print(
                f"OP {op.name} spans across problematic core {problematic_core} in epoch {op.graph_name}"
            )


if __name__ == "__main__":
    finder = ProblematicCoreFinder("netlist.yaml")
    finder.find(CoreLocation(x=19, y=25))
