from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass


@dataclass
class CoreLocation:
    x: int
    y: int

    def __str__(self) -> str:
        return f"(x = {self.x}, y = {self.y})"


@dataclass
class NetlistCoordinates:
    def __init__(self, core_logical_location: CoreLocation) -> None:
        self.r = core_logical_location.y
        self.c = core_logical_location.x

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
        return NetlistCoordinates(
            CoreLocation(
                self.worker_physical_to_logical_x[physical_location.x],
                self.worker_physical_to_logical_y[physical_location.y],
            )
        )


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


if __name__ == "__main__":
    coords = [
        CoreLocation(18, 18),
        CoreLocation(19, 18),
        CoreLocation(20, 18),
        CoreLocation(21, 18),
        CoreLocation(22, 18),
        CoreLocation(23, 18),
        CoreLocation(20, 21),
        CoreLocation(21, 21),
        CoreLocation(22, 21),
        CoreLocation(23, 21),
        CoreLocation(24, 21),
        CoreLocation(19, 23),
        CoreLocation(18, 25),
        CoreLocation(19, 25),
        CoreLocation(20, 25),
        CoreLocation(21, 25),
        CoreLocation(24, 27),
        CoreLocation(25, 27),
    ]

    for coord in coords:
        unharv = SoCInfo.convert_harvested_core_location_to_unharvested(coord)
        wh = WHWorkerCores()
        print(
            wh.physical_to_netlist_coordinates(unharv)
            == NetlistCoordinates(CoreLocation(coord.y - 18, coord.x - 18))
        )
