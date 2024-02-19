# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os, sys

script_directory = os.path.dirname(os.path.abspath(__file__))
sys.path.append(script_directory + "/../../")

from tt_debuda_server import debuda_server_communication

server_port = 0
server_communication = None

def check_response(response, expected_response):
    response = response.decode("utf-8")
    if response != expected_response:
        print(f"Unexpected response. Expected '{expected_response}', got '{response}'")
    else:
        print(expected_response)


def ping():
    global server_communication
    check_response(server_communication.ping(), "- type: 1")


def get_runtime_data():
    global server_communication
    check_response(server_communication.get_runtime_data(), "- type: 101")


def get_cluster_description():
    global server_communication
    check_response(server_communication.get_cluster_description(), "- type: 102")


def get_device_ids():
    global server_communication
    check_response(server_communication.get_device_ids(), "- type: 104")


def pci_read4():
    global server_communication
    check_response(
        server_communication.pci_read4(1, 2, 3, 123456),
        "- type: 10\n  chip_id: 1\n  noc_x: 2\n  noc_y: 3\n  address: 123456",
    )


def pci_write4():
    global server_communication
    check_response(
        server_communication.pci_write4(1, 2, 3, 123456, 987654),
        "- type: 11\n  chip_id: 1\n  noc_x: 2\n  noc_y: 3\n  address: 123456\n  data: 987654",
    )


def pci_read():
    global server_communication
    check_response(
        server_communication.pci_read(1, 2, 3, 123456, 1024),
        "- type: 12\n  chip_id: 1\n  noc_x: 2\n  noc_y: 3\n  address: 123456\n  size: 1024",
    )


def pci_read4_raw():
    global server_communication
    check_response(
        server_communication.pci_read4_raw(1, 123456),
        "- type: 14\n  chip_id: 1\n  address: 123456",
    )


def pci_write4_raw():
    global server_communication
    check_response(
        server_communication.pci_write4_raw(1, 123456, 987654),
        "- type: 15\n  chip_id: 1\n  address: 123456\n  data: 987654",
    )


def dma_buffer_read4():
    global server_communication
    check_response(
        server_communication.dma_buffer_read4(1, 123456, 456),
        "- type: 16\n  chip_id: 1\n  address: 123456\n  channel: 456",
    )


def pci_read_tile():
    global server_communication
    check_response(
        server_communication.pci_read_tile(1, 2, 3, 123456, 1024, 14),
        "- type: 100\n  chip_id: 1\n  noc_x: 2\n  noc_y: 3\n  address: 123456\n  size: 1024\n  data_format: 14",
    )


def get_harvester_coordinate_translation():
    global server_communication
    check_response(
        server_communication.get_harvester_coordinate_translation(1),
        "- type: 103\n  chip_id: 1",
    )


def get_device_arch():
    global server_communication
    check_response(
        server_communication.get_device_arch(1),
        "- type: 105\n  chip_id: 1",
    )


def get_device_soc_description():
    global server_communication
    check_response(
        server_communication.get_device_soc_description(1),
        "- type: 106\n  chip_id: 1",
    )


def pci_write():
    global server_communication
    check_response(
        server_communication.pci_write(
            1, 2, 3, 123456, bytes([10, 11, 12, 13, 14, 15, 16, 17])
        ),
        "- type: 13\n  chip_id: 1\n  noc_x: 2\n  noc_y: 3\n  address: 123456\n  size: 8\n  data: [10, 11, 12, 13, 14, 15, 16, 17]",
    )


def main():
    # Check if at least two arguments are provided (script name + function name)
    if len(sys.argv) < 3:
        print(
            "Usage: python test_communication.py <server_port> <function_name> [args...]"
        )
        sys.exit(1)

    # Get server port and remove it from the arguments list
    port = sys.argv[1]
    del sys.argv[1]
    try:
        global server_port
        server_port = int(port)
    except ValueError:
        print(f"Couldn't parse port '{port}' as an int")
        sys.exit(1)

    # Try to connect to server
    try:
        global server_communication
        server_communication = debuda_server_communication("localhost", port)
    except:
        print(f"Couldn't connect to debuda server on port '{port}'")
        sys.exit(1)

    # Get the function name and remove it from the arguments list
    function_name = sys.argv[1]
    del sys.argv[1]

    # Check if the specified function exists
    if function_name not in globals() or not callable(globals()[function_name]):
        print(f"Error: Function '{function_name}' not found.")
        sys.exit(1)

    # Call the specified function with the remaining arguments
    globals()[function_name](*sys.argv[1:])


if __name__ == "__main__":
    main()
