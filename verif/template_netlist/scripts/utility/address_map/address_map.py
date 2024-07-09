from abc import ABC, abstractmethod
from dataclasses import dataclass

from memory_region import MemoryRegion


class AddressMap(ABC):
    @classmethod
    def get_attr_values(cls) -> dict[str, int]:
        attrs = {}
        for attr_name, value in vars(cls).items():
            if attr_name.isupper():
                attrs[attr_name] = value

        return attrs

    def find_overlaps(self, regions: list[MemoryRegion]) -> None:
        visited = set()

        for region in regions:
            for region2 in regions:
                if region.name == region2.name or region2.name in visited:
                    continue

                if region.overlaps(region2):
                    region2.reduce_bar_width()
                elif region2.overlaps(region):
                    region.reduce_bar_width()

            visited.add(region.name)

    def sort(self, regions: list[MemoryRegion]) -> list[MemoryRegion]:
        return sorted(regions, key=lambda region: (region.start, -region.size))

    @abstractmethod
    def get_alignment(self) -> int:
        pass

    @abstractmethod
    def get_regions(self) -> list[MemoryRegion]:
        pass

    @abstractmethod
    def get_figname(self) -> str:
        pass
