"""
Example usage:
python verif/template_netlist/scripts/utility/address_map/visualize_l1_address_map.py
"""

import matplotlib.pyplot as plt
import matplotlib.patheffects as PathEffects

from address_map import AddressMap
from memory_region import MemoryRegion
from gs_address_map import GSAddressMap
from wh_address_map import WHAddressMap
from bh_address_map import BHAddressMap


class AddressMapPlotter:
    def __init__(self):
        plt.figure(figsize=(15, 40))
        plt.rcParams.update({"font.size": 12})
        plt.title("Memory Regions - Stacked Bar Chart")
        plt.xlabel("Memory Region")
        plt.ylabel("Address Range")

    def __plot_ticks(self, regions):
        ticks = []
        labels = []

        for region in regions:
            ticks.append(region.start)
            labels.append(hex(region.start))
            ticks.append(region.end)
            labels.append(hex(region.end))

        plt.yticks(ticks, labels)

    def __plot_regions(self, regions):
        for region in regions:
            # Plot stacked bars
            plt.bar(
                0.5,
                region.size,
                bottom=region.start,
                label=repr(region),
                width=region.bar_width,
                edgecolor="black",
            )

            # Annotate the start and end points of each region
            text = plt.text(
                0.5,
                region.middle,
                repr(region),
                ha="center",
                va="center",
            )
            text.set_path_effects([PathEffects.withStroke(linewidth=4, foreground="w")])

    def __save_plot(self, filename):
        plt.tight_layout(rect=[0, 0, 0.6, 1])
        plt.legend(reverse=True, loc="center left", bbox_to_anchor=(1, 0.5))
        plt.savefig(filename)

    def plot_regions(self, addr_map: AddressMap):
        # Get the memory regions
        regions: list[MemoryRegion] = addr_map.get_regions()

        self.__plot_ticks(regions)
        self.__plot_regions(regions)
        self.__save_plot(addr_map.get_figname())


if __name__ == "__main__":
    # addr_map = GSAddressMap()
    wh_addr_map = WHAddressMap()
    bh_addr_map = BHAddressMap()

    plotter = AddressMapPlotter()
    # plotter.plot_regions(wh_addr_map)
    plotter.plot_regions(bh_addr_map)
