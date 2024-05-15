# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os, sys
from typing import Any, Callable

script_directory = os.path.dirname(os.path.abspath(__file__))
sys.path.append(script_directory + "/../../")

from tt_debuda_server import debuda_server, debuda_server_not_supported

server_port = 0
server = None


def check_not_implemented_response(server_command: Callable[[], Any]):
    try:
        server_command()
        print("fail")
    except debuda_server_not_supported:
        print("pass")


def empty_get_runtime_data():
    global server
    check_not_implemented_response(lambda: server.get_runtime_data())


def empty_get_cluster_description():
    global server
    check_not_implemented_response(lambda: server.get_cluster_description())


def empty_pci_read4():
    global server
    check_not_implemented_response(lambda: server.pci_read4(1, 2, 3, 123456))


def empty_pci_write4():
    global server
    check_not_implemented_response(lambda: server.pci_write4(1, 2, 3, 123456, 987654))


def empty_pci_read():
    global server
    check_not_implemented_response(lambda: server.pci_read(1, 2, 3, 123456, 1024))


def empty_pci_read4_raw():
    global server
    check_not_implemented_response(lambda: server.pci_read4_raw(1, 123456))


def empty_pci_write4_raw():
    global server
    check_not_implemented_response(lambda: server.pci_write4_raw(1, 123456, 987654))


def empty_dma_buffer_read4():
    global server
    check_not_implemented_response(lambda: server.dma_buffer_read4(1, 123456, 456))


def empty_pci_read_tile():
    global server
    check_not_implemented_response(
        lambda: server.pci_read_tile(1, 2, 3, 123456, 1024, 14)
    )


def empty_get_harvester_coordinate_translation():
    global server
    check_not_implemented_response(
        lambda: server.get_harvester_coordinate_translation(1)
    )


def empty_pci_write():
    global server
    check_not_implemented_response(
        lambda: server.pci_write(
            1, 2, 3, 123456, bytes([10, 11, 12, 13, 14, 15, 16, 17])
        )
    )


def pci_write4_pci_read4():
    global server
    server.pci_write4(1, 2, 3, 123456, 987654)
    read = server.pci_read4(1, 2, 3, 123456)
    print("pass" if read == 987654 else "fail")


def pci_write_pci_read():
    global server
    server.pci_write(1, 2, 3, 123456, b"987654")
    read = server.pci_read(1, 2, 3, 123456, 6)
    print("pass" if read == b"987654" else "fail")


def pci_write4_raw_pci_read4_raw():
    global server
    server.pci_write4_raw(1, 123456, 987654)
    read = server.pci_read4_raw(1, 123456)
    print("pass" if read == 987654 else "fail")


def dma_buffer_read4():
    global server
    server.pci_write4_raw(1, 123456, 987654)
    read = server.dma_buffer_read4(1, 123456, 753)
    print("pass" if read == 987654 + 753 else "fail")


def pci_read_tile():
    global server
    read = server.pci_read_tile(1, 2, 3, 123456, 1024, 14)
    print("pass" if read == "pci_read_tile(1, 2, 3, 123456, 1024, 14)" else "fail")


def get_runtime_data():
    global server
    read = server.get_runtime_data()
    print("pass" if read == "get_runtime_data()" else "fail")


def get_cluster_description():
    global server
    read = server.get_cluster_description()
    print("pass" if read == "get_cluster_description()" else "fail")


def get_harvester_coordinate_translation():
    global server
    read = server.get_harvester_coordinate_translation(1)
    print("pass" if read == "get_harvester_coordinate_translation(1)" else "fail")


def get_device_ids():
    global server
    read = server.get_device_ids()
    print("pass" if read == b'\x00\x01' else "fail")


def get_device_arch():
    global server
    read = server.get_device_arch(1)
    print("pass" if read == "get_device_arch(1)" else "fail")


def get_device_soc_description():
    global server
    read = server.get_device_soc_description(1)
    print("pass" if read == "get_device_soc_description(1)" else "fail")


def main():
    # Check if at least two arguments are provided (script name + function name)
    if len(sys.argv) < 3:
        print("Usage: python test_server.py <server_port> <function_name> [args...]")
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
        global server
        server = debuda_server("localhost", port)
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
