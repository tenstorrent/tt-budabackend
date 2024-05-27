# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from enum import Enum
import sys
import struct
import zmq
import tt_util as util


class debuda_server_request_type(Enum):
    # Basic requests
    invalid = 0
    ping = 1

    # Device requests
    pci_read4 = 10
    pci_write4 = 11
    pci_read = 12
    pci_write = 13
    pci_read4_raw = 14
    pci_write4_raw = 15
    dma_buffer_read4 = 16

    # Runtime requests
    pci_read_tile = 100
    get_runtime_data = 101
    get_cluster_description = 102
    get_harvester_coordinate_translation = 103
    get_device_ids = 104
    get_device_arch = 105
    get_device_soc_description = 106


class debuda_server_bad_request(Exception):
    pass


class debuda_server_not_supported(Exception):
    pass


class debuda_server_communication:
    _BAD_REQUEST = b"BAD_REQUEST"
    _NOT_SUPPORTED = b"NOT_SUPPORTED"

    def __init__(self, address: str, port: int):
        self.address = address
        self.port = port
        self._context = zmq.Context()
        self._socket = self._context.socket(zmq.REQ)
        self._socket.connect(f"tcp://{self.address}:{self.port}")

    def _check(self, response: bytes):
        if response == debuda_server_communication._BAD_REQUEST:
            raise debuda_server_bad_request()
        if response == debuda_server_communication._NOT_SUPPORTED:
            raise debuda_server_not_supported()
        return response

    def ping(self):
        self._socket.send(bytes([debuda_server_request_type.ping.value]))
        return self._check(self._socket.recv())

    def pci_read4(self, chip_id: int, noc_x: int, noc_y: int, address: int):
        self._socket.send(
            struct.pack(
                "<BBBBQ",
                debuda_server_request_type.pci_read4.value,
                chip_id,
                noc_x,
                noc_y,
                address,
            )
        )
        return self._check(self._socket.recv())

    def pci_write4(self, chip_id: int, noc_x: int, noc_y: int, address: int, data: int):
        self._socket.send(
            struct.pack(
                "<BBBBQI",
                debuda_server_request_type.pci_write4.value,
                chip_id,
                noc_x,
                noc_y,
                address,
                data,
            )
        )
        return self._check(self._socket.recv())

    def pci_read(self, chip_id: int, noc_x: int, noc_y: int, address: int, size: int):
        self._socket.send(
            struct.pack(
                "<BBBBQI",
                debuda_server_request_type.pci_read.value,
                chip_id,
                noc_x,
                noc_y,
                address,
                size,
            )
        )
        return self._check(self._socket.recv())

    def pci_write(
        self, chip_id: int, noc_x: int, noc_y: int, address: int, data: bytes
    ):
        self._socket.send(
            struct.pack(
                f"<BBBBQI{len(data)}s",
                debuda_server_request_type.pci_write.value,
                chip_id,
                noc_x,
                noc_y,
                address,
                len(data),
                data,
            )
        )
        return self._check(self._socket.recv())

    def pci_read4_raw(self, chip_id: int, address: int):
        self._socket.send(
            struct.pack(
                "<BBI", debuda_server_request_type.pci_read4_raw.value, chip_id, address
            )
        )
        return self._check(self._socket.recv())

    def pci_write4_raw(self, chip_id: int, address: int, data: int):
        self._socket.send(
            struct.pack(
                "<BBII",
                debuda_server_request_type.pci_write4_raw.value,
                chip_id,
                address,
                data,
            )
        )
        return self._check(self._socket.recv())

    def dma_buffer_read4(self, chip_id: int, address: int, channel: int):
        self._socket.send(
            struct.pack(
                "<BBQH",
                debuda_server_request_type.dma_buffer_read4.value,
                chip_id,
                address,
                channel,
            )
        )
        return self._check(self._socket.recv())

    def pci_read_tile(
        self,
        chip_id: int,
        noc_x: int,
        noc_y: int,
        address: int,
        size: int,
        data_format: int,
    ):
        self._socket.send(
            struct.pack(
                "<BBBBQIB",
                debuda_server_request_type.pci_read_tile.value,
                chip_id,
                noc_x,
                noc_y,
                address,
                size,
                data_format,
            )
        )
        return self._check(self._socket.recv())

    def get_runtime_data(self):
        self._socket.send(bytes([debuda_server_request_type.get_runtime_data.value]))
        return self._check(self._socket.recv())

    def get_cluster_description(self):
        self._socket.send(
            bytes([debuda_server_request_type.get_cluster_description.value])
        )
        return self._check(self._socket.recv())

    def get_harvester_coordinate_translation(self, chip_id: int):
        self._socket.send(
            struct.pack(
                "<BB",
                debuda_server_request_type.get_harvester_coordinate_translation.value,
                chip_id,
            )
        )
        return self._check(self._socket.recv())

    def get_device_ids(self):
        self._socket.send(
            bytes([debuda_server_request_type.get_device_ids.value])
        )
        return self._check(self._socket.recv())

    def get_device_arch(self, chip_id: int):
        self._socket.send(
            struct.pack(
                "<BB",
                debuda_server_request_type.get_device_arch.value,
                chip_id,
            )
        )
        return self._check(self._socket.recv())

    def get_device_soc_description(self, chip_id: int):
        self._socket.send(
            struct.pack(
                "<BB",
                debuda_server_request_type.get_device_soc_description.value,
                chip_id,
            )
        )
        return self._check(self._socket.recv())


class debuda_server:
    def __init__(self, address: str, port: int):
        self._communication = debuda_server_communication(address, port)

        # Check ping/pong to verify it is debuda server on the other end
        pong = self._communication.ping()
        if pong != b"PONG":
            raise ConnectionError()

    def parse_uint32_t(self, buffer: bytes):
        if len(buffer) != 4:
            raise ConnectionError()
        return struct.unpack("<I", buffer)[0]

    def parse_string(self, buffer: bytes):
        return buffer.decode()

    def pci_read4(self, chip_id: int, noc_x: int, noc_y: int, address: int):
        return self.parse_uint32_t(
            self._communication.pci_read4(chip_id, noc_x, noc_y, address)
        )

    def pci_write4(self, chip_id: int, noc_x: int, noc_y: int, address: int, data: int):
        buffer = self._communication.pci_write4(chip_id, noc_x, noc_y, address, data)
        bytes_written = self.parse_uint32_t(buffer)
        if bytes_written != 4:
            raise ValueError(
                f"Expected 4 bytes written, but {bytes_written} were written"
            )
        return bytes_written

    def pci_read(self, chip_id: int, noc_x: int, noc_y: int, address: int, size: int):
        buffer = self._communication.pci_read(chip_id, noc_x, noc_y, address, size)
        if len(buffer) != size:
            raise ValueError(f"Expected {size} bytes read, but {len(buffer)} were read")
        return buffer

    def pci_write(
        self, chip_id: int, noc_x: int, noc_y: int, address: int, data: bytes
    ):
        bytes_written = self.parse_uint32_t(
            self._communication.pci_write(chip_id, noc_x, noc_y, address, data)
        )
        if bytes_written != len(data):
            raise ValueError(
                f"Expected {len(data)} bytes written, but {bytes_written} were written"
            )
        return bytes_written

    def pci_read4_raw(self, chip_id: int, address: int):
        return self.parse_uint32_t(self._communication.pci_read4_raw(chip_id, address))

    def pci_write4_raw(self, chip_id: int, address: int, data: int):
        bytes_written = self.parse_uint32_t(
            self._communication.pci_write4_raw(chip_id, address, data)
        )
        if bytes_written != 4:
            raise ValueError(
                f"Expected 4 bytes written, but {bytes_written} were written"
            )
        return bytes_written

    def dma_buffer_read4(self, chip_id: int, address: int, channel: int):
        return self.parse_uint32_t(
            self._communication.dma_buffer_read4(chip_id, address, channel)
        )

    def pci_read_tile(
        self,
        chip_id: int,
        noc_x: int,
        noc_y: int,
        address: int,
        size: int,
        data_format: int,
    ):
        return self.parse_string(
            self._communication.pci_read_tile(
                chip_id, noc_x, noc_y, address, size, data_format
            )
        )

    def get_runtime_data(self):
        return self.parse_string(self._communication.get_runtime_data())

    def get_cluster_description(self):
        return self.parse_string(self._communication.get_cluster_description())

    def get_harvester_coordinate_translation(self, chip_id: int):
        return self.parse_string(
            self._communication.get_harvester_coordinate_translation(chip_id)
        )

    def get_device_ids(self):
        return self._communication.get_device_ids()

    def get_device_arch(self, chip_id: int):
        return self.parse_string(
            self._communication.get_device_arch(chip_id)
        )
    
    def get_device_soc_description(self, chip_id: int):
        return self.parse_string(
            self._communication.get_device_soc_description(chip_id)
        )


tt_dbd_pybind_path = util.application_path() + "/../build/lib"
binary_path = util.application_path() + "/../build/bin"
sys.path.append(tt_dbd_pybind_path)

import tt_dbd_pybind

class debuda_pybind:
    def __init__(self):
        if not tt_dbd_pybind.open_device(binary_path):
            raise Exception("Failed to open device using pybind library")
        print("Device opened")

    def _check_result(self, result):
        if result is None:
            raise debuda_server_not_supported()
        return result

    def pci_read4(self, chip_id: int, noc_x: int, noc_y: int, address: int):
        return self._check_result(tt_dbd_pybind.pci_read4(chip_id, noc_x, noc_y, address))

    def pci_write4(self, chip_id: int, noc_x: int, noc_y: int, address: int, data: int):
        return self._check_result(tt_dbd_pybind.pci_write4(chip_id, noc_x, noc_y, address, data))

    def pci_read(self, chip_id: int, noc_x: int, noc_y: int, address: int, size: int):
        return self._check_result(tt_dbd_pybind.pci_read(chip_id, noc_x, noc_y, address, size))

    def pci_write(
        self, chip_id: int, noc_x: int, noc_y: int, address: int, data: bytes
    ):
        return self._check_result(tt_dbd_pybind.pci_write(chip_id, noc_x, noc_y, address, data, len(data)))

    def pci_read4_raw(self, chip_id: int, address: int):
        return self._check_result(tt_dbd_pybind.pci_read4_raw(chip_id, address))

    def pci_write4_raw(self, chip_id: int, address: int, data: int):
        return self._check_result(tt_dbd_pybind.pci_write4_raw(chip_id, address, data))

    def dma_buffer_read4(self, chip_id: int, address: int, channel: int):
        return self._check_result(tt_dbd_pybind.dma_buffer_read4(chip_id, address, channel))

    def pci_read_tile(
        self,
        chip_id: int,
        noc_x: int,
        noc_y: int,
        address: int,
        size: int,
        data_format: int,
    ):
        return self._check_result(tt_dbd_pybind.pci_read_tile(chip_id, noc_x, noc_y, address, size, data_format))

    def get_runtime_data(self):
        return self._check_result(tt_dbd_pybind.get_runtime_data())

    def get_cluster_description(self):
        return self._check_result(tt_dbd_pybind.get_cluster_description())

    def get_harvester_coordinate_translation(self, chip_id: int):
        return self._check_result(tt_dbd_pybind.get_harvester_coordinate_translation(chip_id))

    def get_device_ids(self):
        return self._check_result(tt_dbd_pybind.get_device_ids())

    def get_device_arch(self, chip_id: int):
        return self._check_result(tt_dbd_pybind.get_device_arch(chip_id))
    
    def get_device_soc_description(self, chip_id: int):
        return self._check_result(tt_dbd_pybind.get_device_soc_description(chip_id))
