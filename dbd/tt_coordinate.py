#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
This file contains the class for the coordinate object. As we use a number of coordinate systems, this class
is used to uniquely represent one grid location on a chip. Note that not all coordinate systems have a 1:1
mapping to the physical location on the chip. For example, not all noc0 coordinates have a valid netlist
coordinate. This is due to the fact that some locations contain non-Tensix tiles, and also since some Tensix
rows may be disabled due to harvesting.

The following coordinate systems are available to represent a grid location on the chip:

  - die:            represents a location on the die grid. This is a "geographic" coordinate and is
                    not really used in software. It is often shown in martketing materials, and shows the
                    layout in as in <arch>-Noc-Coordinates.xls spreadsheet, and on some T-shirts.
  - noc0:           NOC routing coordinate for NOC 0. Notation: X-Y
                    Represents the chip location on the NOC grid. A difference of 1 in NOC coordinate
                    represents a distance of 1 hop on the NOC. In other words, it takes one clock cycle
                    for the data to cross 1 hop on the NOC. Furthermore, the NOC 'wraps around' so that
                    the distance between 0 and NOC_DIM_SIZE-1 is also 1 hop. As we have 2 NOCs, we have
                    2 NOC coordinate systems: noc0 and noc1.
  - noc1:           NOC routing coordinate for NOC 1. Notation: X-Y
                    Same as noc0, but using the second NOC (which goes in the opposite direction).
  - tensix:         represents a full grid of Tensix cores (including the disabled rows due to harvesting).
                    A distance of 1 in this coordinate system shows the 4 closest cores in terms of the
                    NOC routing. Note that, due to presence of non-Tensix blocks, a distance of 1 in the
                    tensix grid does not necessarily equal to one hop on the NOC. Tensix location 0,0 is
                    anchored to noc0 coordinate of 1-1. Location 1,0 is noc0 1-2 (note that X in noc0
                    corresponds to C in tensix, and Y corresponds to R). Notation: R,C

  The above three do not depend on harvesting. They only depend on the chip architecture. Also, they share
  the extents in both X (column) and Y (row). The following coordinates depend on the harvesting mask on WH
  (i.e. when harvest_mast!=0) and harvested_workers on GS. Note, see HARVESTING_NOC_LOCATIONS for more
  details on how harvest_mask is used. Difference is that UMD returns noc0 coordinates for GS, while for WH
  if uses noc0Virt coordinates.

  - netlist:        Netlist grid coordinate. Notation: R,C
                    is similar to the 'tensix', but it does not include the disabled rows due to
                    harvesting. For a chip that does not have any harvested rows, it is exactly the same as tensix.
                    A distance of 1 in this coordinate system also shows the 4 closest cores in terms
                    of the NOC routing, but it connects only functioning cores. Thus, distance of one may incur even
                    more hops on the NOC as we need to route around the disabled rows. Again, if harvesting mask is 0,
                    meaning that all rows are functional, this coordinate will be the same as tensix coordinate.
                    This coordinate system is used inside the netlist (grid_loc, grid_size). Notation: R,C
  - noc0Virt:       Virtual NOC coordinate. Similar to noc0, but with the harvested rows removed, and the locations of
                    functioning rows shifted down to fill in the gap. This coordinate system is used to communicate with
                    the driver. Notation: X-Y
  - nocTr:          Translated NOC coordinate. Similar to noc0Virt, but offset by 16. Notation: X-Y
                    This system is used by the NOC hardware to account for harvesting. It is similar to the netlist
                    grid (distance one is next functioning core), but it is offset to not use the same range of
                    coordinates as the netlist grid, as that would cause confusion. The coordinates will start at 16, 16.
                    It is also constructed such that starting with (18, 18) it maps exactly to the netlist grid. It is
                    used by the NOC hardware and shows up in blob.yaml. These are also used in device_descs/n.yaml files.
                    Notation: X-Y

"""

from typing import Any

VALID_COORDINATE_TYPES = [
    "noc0",
    "noc1",
    "nocVirt",
    "nocTr",
    "die",
    "tensix",
    "netlist",
]


class CoordinateTranslationError(Exception):
    """
    This exception is thrown when a coordinate translation fails.
    """

    pass


class OnChipCoordinate:
    """
    This class represents a coordinate on the chip. It can be used to convert between the various
    coordinate systems we use.
    """

    _noc0_coord = (None, None)  # This uses noc0 coordinates: (X,Y)
    _device = None  # Used for conversions

    def __init__(self, x, y, input_type, device):
        """
        Constructor. The coordinates are stored as integers. It contains NOC0 coords. If device is
        not specified, we will not be able to convert to other coordinate systems.
        """
        assert device is not None
        self._device = device
        if input_type == "noc0":
            self._noc0_coord = (x, y)
        elif input_type == "nocVirt":
            self._noc0_coord = self._device.nocVirt_to_noc0((x, y))
        elif input_type == "noc1":
            self._noc0_coord = self._device.noc1_to_noc0((x, y))
        elif input_type == "die":
            self._noc0_coord = self._device.die_to_noc((x, y), noc_id=0)
        elif input_type == "tensix":
            self._noc0_coord = self._device.tensix_to_noc0((x, y))
        elif input_type == "netlist":
            self._noc0_coord = self._device.netlist_to_noc0((x, y))
        elif input_type == "nocTr":
            self._noc0_coord = self._device.nocTr_to_noc0((x, y))
        else:
            raise Exception("Unknown input coordinate system: " + input_type)

    @staticmethod
    def from_stream_designator(stream_designator: str):
        """
        This function takes a stream designator and returns the corresponding coordinate.

        Stream designators are the key names in the blob yaml and have the form:
            chip_<chip_id>__y_<core_y>__x_<core_x>__stream_<stream_id>
        """

        # Split the stream designator into its components
        chip_id_str, core_y_str, core_x_str, stream_id_str = stream_designator.split(
            "__"
        )

        # Extract the coordinates from the stream designator
        chip_id = int(chip_id_str.split("_")[1])
        core_x = int(core_x_str.split("_")[1])
        core_y = int(core_y_str.split("_")[1])

        # Create the coordinate
        return OnChipCoordinate(core_x, core_y, "nocTr", chip_id)

    # This returns a tuple with the coordinates in the specified coordinate system.
    # When doing pci_read_xy, we use nocVirt coordinates.
    def to(self, output_type):
        """
        Returns a tuple with the coordinates in the specified coordinate system.
        """
        if output_type == "noc0":
            return self._noc0_coord
        elif output_type == "nocVirt":
            return self._device.noc0_to_nocVirt(self._noc0_coord)
        elif output_type == "noc1":
            return self._device.noc0_to_noc1(self._noc0_coord)
        elif output_type == "die":
            return self._device.noc_to_die(self._noc0_coord, noc_id=0)
        elif output_type == "tensix":
            return self._device.noc0_to_tensix(self._noc0_coord)
        elif output_type == "netlist":
            return self._device.noc0_to_netlist(self._noc0_coord)
        elif output_type == "nocTr":
            return self._device.noc0_to_nocTr(self._noc0_coord)
        else:
            raise Exception("Unknown output coordinate system: " + output_type)

    # Which axis is used to advance in the horizontal direction when rendering the chip
    # For X-Y coordinates, this is the X, for R,C coordinates, this is the C.
    def horizontal_axis(coord_type):
        if "noc" in coord_type or "die" in coord_type:
            return 0
        else:
            return 1

    def vertical_axis(coord_type):
        return 1 - OnChipCoordinate.horizontal_axis(coord_type)

    def vertical_axis_increasing_up(coord_type):
        if "noc" in coord_type or "die" in coord_type:
            return True
        else:
            return False

    def to_str(self, output_type="nocTr"):
        """
        Returns a tuple with the coordinates in the specified coordinate system.
        """
        try:
            output_tuple = self.to(output_type)
        except CoordinateTranslationError as e:
            return "N/A"

        if "noc" in output_type:
            return f"{output_tuple[0]}-{output_tuple[1]}"
        elif (
            output_type == "die" or output_type == "tensix" or output_type == "netlist"
        ):
            return f"{output_tuple[0]},{output_tuple[1]}"
        else:
            raise Exception("Unknown output coordinate system: " + output_type)

    def to_user_str(self):
        """
        Returns a string representation of the coordinate that is suitable for the user.
        """
        virt_str = self.to_str("nocTr")
        try:
            netlist_str = self.to("netlist")
            return f"{virt_str} ({netlist_str})"
        except CoordinateTranslationError:
            pass
        return virt_str

    # The default string representation is the netlist coordinate. That's what the user deals with.
    def __str__(self) -> str:
        return self.to_str("netlist")

    def __hash__(self):
        return hash((self._noc0_coord, self._device._id))

    # The debug string representation also has the translated coordinate.
    def __repr__(self) -> str:
        return f"{self.to_str('netlist')} ({self.to_str('nocTr')})"

    def full_str(self) -> str:
        return f"noc0: {self.to_str('noc0')}, noc1: {self.to_str('noc1')}, die: {self.to_str('die')}, tensix: {self.to_str('tensix')}, netlist: {self.to_str('netlist')}, nocTr: {self.to_str('nocTr')}, nocVirt: {self.to_str('nocVirt')}"

    def is_nocTr(x) -> bool:
        return x >= 16

    # == operator
    def __eq__(self, other):
        # util.DEBUG("Comparing coordinates: " + str(self) + " ?= " + str(other))
        return (self._noc0_coord == other._noc0_coord) and (
            (self._device == other._device)
            or (self._device._arch == other._device._arch)
        )

    def __lt__(self, other):
        if self._device.id() == other._device.id():
            return self._noc0_coord < other._noc0_coord
        else:
            return self._device.id() < other._device.id()

    def create(coord_str, device, coord_type=None):
        """
        Creates a coordinate object from a string. The string can be in any of the supported coordinate systems.
        The nocTr and netlist coordinates are used for - and , separators, respectively, unless the coord_type
        is specified.
        """
        if "-" in coord_str:
            if coord_type is None:
                coord_type = "nocTr"
            x, y = coord_str.split("-")
            x = int(x.strip())
            y = int(y.strip())
        elif "," in coord_str:
            if coord_type is None:
                coord_type = "netlist"
            x, y = coord_str.split(",")
            x = int(x.strip())
            y = int(y.strip())
        elif coord_str[0:2].upper() == "CH": # This is a DRAM channel
            # Parse the digits after "CH"
            dram_chan = int(coord_str[2:])
            (x, y) = device.DRAM_CHANNEL_TO_NOC0_LOC[dram_chan]
            coord_type = 'noc0'
        else:
            raise Exception(
                "Unknown coordinate format: " + coord_str + ". Use either X-Y or R,C"
            )

        return OnChipCoordinate(x, y, coord_type, device)
