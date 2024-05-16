# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import unittest

import tt_dbd_pybind as pb
from tt_dbd_pybind_unit_tests import set_debuda_test_implementation

from typing import Union

class TestBindings(unittest.TestCase):
    def __init__(self, methodName: str = "runTest") -> None:
        set_debuda_test_implementation()
        super().__init__(methodName)

    def test_pci_read_write4(self, data: int=2,):      
        assert pb.pci_read4(0, 1, 0, 0) is None, "Error: pci_read4 should return None before writing."
        assert pb.pci_write4(0, 1, 0, 0, data) == data, "Error: pci_write4 should return the data written."
        assert pb.pci_read4(0, 1, 0, 0) == data, "Error: pci_read4 should return the data written."

    def test_pci_read_write4_raw(self, data: int=4,):
        assert pb.pci_read4_raw(1, 1) is None, "Error: pci_read4_raw should return None before writing."
        assert pb.pci_write4_raw(1, 1, data) == data, "Error: pci_write4_raw should return the data written."
        assert pb.pci_read4_raw(1, 1) == data, "Error: pci_read4_raw should return the data written."

    def test_pci_read_write(self, data: Union[bytes, bytearray] = bytearray([1, 5, 3]), size = 3):
        assert pb.pci_read(3, 3, 3, 3, size) is None, "Error: pci_read should return None before writing."
        assert pb.pci_write(3, 3, 3, 3, data, 3) == size, "Error: pci_write should return the size of the data written."

        rlist = pb.pci_read(3, 3, 3, 3, 3)
        for x, y in zip(data, rlist):
            assert x == y, "Error: pci_read should return the data written."

    def pci_write_negative(self, data: list=[1, 2, -3], size = 3):
        assert pb.pci_write(5, 5, 5, 5, data, 3) is None, "Error: pci_write should return None if any of the data is negative."
        pb.pci_write(5, 5, 5, 5, [4, 5, 6], 3)
        rlist = pb.pci_read(5, 5, 5, 5, 3)
        for x, y in zip(data, rlist):
            assert x == y, "Error: pci_read should return the data written."

    def test_dma_buffer_read4(self, data: int=5, channel:int = 32):
        assert pb.dma_buffer_read4(2, 1, channel) is None, "Error: dma_buffer_read4 should return None before writing."
        pb.pci_write4_raw(2, 1, data)
        assert pb.dma_buffer_read4(2, 1, channel) == data + channel, "Error: dma_buffer_read4 should return the data written plus channel used for reading."

    def test_pci_read_tile(self):
        assert pb.pci_read_tile(1, 2, 3, 4, 5, 6) == "pci_read_tile(1, 2, 3, 4, 5, 6)", "Error: pci_read_tile(1, 2, 3, 4, 5, 6) should return 'pci_read_tile(1, 2, 3, 4, 5, 6)'."

    def test_get_runtime_data(self):
        assert pb.get_runtime_data() == "get_runtime_data()", "Error: get_runtime_data() should return 'get_runtime_data()'."

    def test_get_cluster_description(self):
        assert pb.get_cluster_description() == "get_cluster_description()", "Error: get_cluster_description() should return 'get_cluster_description()'."

    def test_get_harvester_coordinate_translation(self, id:int = 4):
        assert pb.get_harvester_coordinate_translation(id) == "get_harvester_coordinate_translation(" + str(id) + ")", \
            "Error: get_harvester_coordinate_translation() should return 'get_harvester_coordinate_translation(" + str(id) + ")'."

    def test_get_device_ids(self):
        assert pb.get_device_ids() == [0, 1], "Error: get_device_ids() should return [0, 1]."

    def test_get_device_arch(self, id:int = 3):
        assert pb.get_device_arch(id) == "get_device_arch(" + str(id) + ")", "Error: get_device_arch() should return 'get_device_arch(" + str(id) + ")'."

    def test_get_device_soc_description(self, id:int = 5):
        assert pb.get_device_soc_description(id) == "get_device_soc_description(" + str(id) + ")", \
            "Error: get_device_soc_description() should return 'get_device_soc_description(" + str(id) + ")'."

if __name__ == "__main__":
    unittest.main()