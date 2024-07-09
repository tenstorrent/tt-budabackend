from __future__ import annotations


class MemoryRegion:
    def __init__(self, name, start, size=None, end=None, start_alignment=None, size_alignment=None) -> None:
        assert size != None or end != None, "Either size or end of the region must be provided"

        self.name = name
        self.start = start
        self.size = size if size != None else end - start
        self.end = end if end != None else (self.start + self.size)
        self.middle = (self.end + self.start) / 2
        self.bar_width = 1

        if start_alignment:
            if self.start % start_alignment != 0:
                print(f"WARNING: Region {name}'s start address {self.start} not aligned to {start_alignment}B.")
                self.start = self.__align(self.start, start_alignment)
                print(f"After alignment, region {name}'s start address is {self.start}.\n")

        if size_alignment:
            if self.size % size_alignment != 0:
                print(f"WARNING: Region {name}'s size {self.size} not aligned to {size_alignment}B.")
                self.size = self.__align(self.size, size_alignment)
                print(f"After alignment, region {name}'s size is {self.size}.\n")

    def __align(self, value, alignment_factor):
        return (value + alignment_factor - 1) // alignment_factor * alignment_factor

    def overlaps(self, region: MemoryRegion) -> bool:
        if (self.start < region.start and self.end > region.start) or (
            self.end > region.end and self.start < region.end
        ):
            print(f"Region {region} overlaps with {self}")
            return True

        return False

    def reduce_bar_width(self) -> None:
        self.bar_width -= 0.1

    def __repr__(self) -> str:
        return f"{self.name} {self.size}B {hex(self.start)}~{hex(self.end)}"
