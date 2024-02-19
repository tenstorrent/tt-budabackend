# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from functools import cached_property
import os, subprocess, time, struct, signal, re, zmq, pickle, atexit, ast
from typing import Sequence
from socket import timeout
from tabulate import tabulate
from debuda import Context
from tt_debuda_server import debuda_server
from tt_object import TTObject
import tt_util as util
from tt_coordinate import OnChipCoordinate, CoordinateTranslationError
from collections import namedtuple
from abc import ABC
from typing import Dict

#
# Communication with Buda (or debuda-server) over sockets (ZMQ).
# See struct BUDA_READ_REQ for protocol details
#
DEBUDA_SERVER = None  # The debuda server communication object
DEBUDA_STUB_PROCESS = (
    None  # The process ID of debuda-server spawned in connect_to_server
)


# The server needs the runtime_data.yaml to get the netlist path, arch, and device
def spawn_standalone_debuda_stub(port, runtime_data_yaml_filename):
    print("Spawning debuda-server...")

    # Check if we are executing from pybuda wheel and update paths accordingly
    from debuda import EXECUTING_FROM_PYBUDA_WHEEL

    debuda_server_standalone = "/debuda-server-standalone"
    if EXECUTING_FROM_PYBUDA_WHEEL:
        if "BUDA_HOME" not in os.environ:
            os.environ["BUDA_HOME"] = os.path.abspath(util.application_path() + "/../")
        debuda_server_standalone = f"/../build/bin{debuda_server_standalone}"

    debuda_stub_path = os.path.abspath(
        util.application_path() + debuda_server_standalone
    )
    library_path = os.path.abspath(os.path.dirname(debuda_stub_path))
    ld_lib_path = os.environ.get("LD_LIBRARY_PATH", "")
    os.environ["LD_LIBRARY_PATH"] = (
        library_path + ":" + ld_lib_path if ld_lib_path else library_path
    )

    try:
        global DEBUDA_STUB_PROCESS
        if runtime_data_yaml_filename is None:
            debuda_stub_args = [f"{port}"]
        else:
            debuda_stub_args = [f"{port}", f"{runtime_data_yaml_filename}"]
        DEBUDA_STUB_PROCESS = subprocess.Popen(
            [debuda_stub_path] + debuda_stub_args,
            preexec_fn=os.setsid,
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE,
            env=os.environ.copy(),
        )
    except:

        if os.path.islink(debuda_stub_path):
            if not os.path.exists(os.readlink(debuda_stub_path)):
                util.FATAL(f"Missing debuda-server-standalone. Try: make dbd")

        raise

    # Allow some time for debuda-server to start
    retry_times = 50
    sleep_time = 0.1
    debuda_stub_is_running = False
    util.INFO(
        f"Waiting for debuda-server to start for up to {retry_times * sleep_time} seconds..."
    )
    while retry_times > 0:
        time.sleep(sleep_time)
        debuda_stub_terminated = DEBUDA_STUB_PROCESS.poll() is not None
        if debuda_stub_terminated:
            util.ERROR(
                f"Debuda server terminated unexpectedly with code: {DEBUDA_STUB_PROCESS.returncode}"
            )
            process_stderr = DEBUDA_STUB_PROCESS.stderr.read().decode("utf-8")
            process_stdout = DEBUDA_STUB_PROCESS.stdout.read().decode("utf-8")
            if process_stdout:
                util.INFO(f"{process_stdout}")
            if process_stderr:
                if "does not exist" in process_stderr:
                    util.ERROR(
                        f"Make sure to set the environment variable BUDA_HOME to <python-path>/site-packages/budabackend"
                    )
                util.ERROR(f"{process_stderr}")
            break

        if not util.is_port_available(
            int(port)
        ):  # Then we assume debuda-server has started
            debuda_stub_is_running = True
            break
        retry_times -= 1

    if not debuda_stub_is_running:
        util.ERROR(
            f"Debuda server could not be spawned on localhost after {retry_times * sleep_time} seconds."
        )
        return False
    return True


# Spawns debuda-server and initializes the communication
def connect_to_server(ip="localhost", port=5555, spawning_debuda_stub=False):
    debuda_stub_address = f"tcp://{ip}:{port}"
    util.INFO(f"Connecting to debuda-server at {debuda_stub_address}...")

    try:
        global DEBUDA_SERVER
        DEBUDA_SERVER = debuda_server(ip, port)
        util.INFO("Connected to debuda-server.")
    except:
        if spawning_debuda_stub:
            terminate_standalone_debuda_stub()
        raise

    # Make sure to terminate communication client (debuda-server) on exit
    atexit.register(terminate_standalone_debuda_stub)

    # Allow the process to start before we start sending commands
    time.sleep(0.1)


# Terminates debuda-server spawned in connect_to_server
def terminate_standalone_debuda_stub():
    if DEBUDA_STUB_PROCESS is not None and DEBUDA_STUB_PROCESS.poll() is None:
        os.killpg(os.getpgid(DEBUDA_STUB_PROCESS.pid), signal.SIGTERM)
        time.sleep(0.1)
        if DEBUDA_STUB_PROCESS.poll() is None:
            util.VERBOSE("Debuda-server did not respond to SIGTERM. Sending SIGKILL...")
            os.killpg(os.getpgid(DEBUDA_STUB_PROCESS.pid), signal.SIGKILL)

        time.sleep(0.1)
        if DEBUDA_STUB_PROCESS.poll() is None:
            util.ERROR(
                f"Debuda-server did not respond to SIGKILL. The process {DEBUDA_STUB_PROCESS.pid} is still running. Please kill it manually."
            )
        else:
            util.INFO("Debuda-server terminated.")


# Attempt to unpack the data using the given format. If it fails, it assumes that the data
# is a string contining an error message from the server.
def try_unpack(fmt, data):
    try:
        u = struct.unpack(fmt, data)
        return u
    except:
        # Here we might have gotten an error string from the server. Unpack as string and print error
        util.ERROR(f"Debuda-server sent an invalid reply: {data}")
        return None


# This is the interface to the debuda server
class DEBUDA_SERVER_SOCKET_IFC:
    enabled = True  # It can be disabled for offline operation (when working from cache)

    NOT_ENABLED_ERROR_MSG = "Access to a device is requested, but the debuda-server socket interface is not enabled (see --server-cache)"

    # PCI read/write functions. Given a noc0 location and addr, performs a PCI read/write
    # to the given location at the address.
    def pci_read_xy(chip_id, x, y, noc_id, reg_addr):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (pci_read_xy) with arguments: chip_id={chip_id}, x={x}, y={y}, noc_id={noc_id}, reg_addr=0x{reg_addr:x}"
        )
        return DEBUDA_SERVER.pci_read4(chip_id, x, y, reg_addr)

    def pci_write_xy(chip_id, x, y, noc_id, reg_addr, data):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (pci_write_xy) with arguments: chip_id={chip_id}, x={x}, y={y}, noc_id={noc_id}, reg_addr=0x{reg_addr:x}, data=0x{data:x}"
        )
        DEBUDA_SERVER.pci_write4(chip_id, x, y, reg_addr, data)
        return data

    def pci_read_buf(chip_id, x, y, noc_id, reg_addr, size):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (pci_read_buf) with arguments: chip_id={chip_id}, x={x}, y={y}, noc_id={noc_id}, reg_addr=0x{reg_addr:x}, size={size}"
        )
        return DEBUDA_SERVER.pci_read(chip_id, x, y, reg_addr, size)

    def pci_write_buf(chip_id, x, y, noc_id, reg_addr, data):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (pci_write_buf) with arguments: chip_id={chip_id}, x={x}, y={y}, noc_id={noc_id}, reg_addr=0x{reg_addr:x}, data={data}"
        )
        return DEBUDA_SERVER.pci_write(chip_id, x, y, reg_addr, data)

    def host_dma_read(chip_id, dram_addr, dram_chan):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (host_dma_read) with arguments: chip_id={chip_id}, dram_addr=0x{dram_addr:x}, dram_chan={dram_chan}"
        )
        return DEBUDA_SERVER.dma_buffer_read4(chip_id, dram_addr, dram_chan)

    def pci_read_tile(chip_id, x, y, z, reg_addr, msg_size, data_format):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (pci_read_tile) with arguments: chip_id={chip_id}, x={x}, y={y}, z={z}, reg_addr=0x{reg_addr:x}, msg_size={msg_size}, data_format={data_format}"
        )
        return DEBUDA_SERVER.pci_read_tile(
            chip_id, x, y, reg_addr, msg_size, data_format
        )

    def pci_raw_read(chip_id, reg_addr):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (pci_raw_read) with arguments: chip_id={chip_id}, reg_addr=0x{reg_addr:x}"
        )
        try:
            return DEBUDA_SERVER.pci_read4_raw(chip_id, reg_addr)
        except:
            util.ERROR(f"Cannot do PCI read")
            return 0

    def pci_raw_write(chip_id, reg_addr, data):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (pci_raw_write) with arguments: chip_id={chip_id}, reg_addr=0x{reg_addr:x}, data=0x{data:x}"
        )
        try:
            DEBUDA_SERVER.pci_write4_raw(chip_id, reg_addr, data)
            return data
        except:
            util.ERROR(f"Cannot do PCI write")
            return 0

    def get_runtime_data():
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG + f" (get_runtime_data)"
        )
        return DEBUDA_SERVER.get_runtime_data()

    def get_cluster_desc_path():
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG + f" (get_cluster_desc_path)"
        )
        return DEBUDA_SERVER.get_cluster_description()

    def get_harvested_coord_translation(chip_id):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (get_harvested_coord_translation) with arguments: chip_id={chip_id}"
        )
        return DEBUDA_SERVER.get_harvester_coordinate_translation(chip_id)

    def get_device_ids():
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG + f" (get_device_ids)"
        )
        return DEBUDA_SERVER.get_device_ids()

    def get_device_arch(chip_id):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (get_device_arch) with arguments: chip_id={chip_id}"
        )
        return DEBUDA_SERVER.get_device_arch(chip_id)

    def get_device_soc_description(chip_id):
        assert DEBUDA_SERVER_SOCKET_IFC.enabled, (
            DEBUDA_SERVER_SOCKET_IFC.NOT_ENABLED_ERROR_MSG
            + f" (get_device_soc_description) with arguments: chip_id={chip_id}"
        )
        return DEBUDA_SERVER.get_device_soc_description(chip_id)


# This interface is used to read cached values of device reads.
class DEBUDA_SERVER_CACHED_IFC:
    filepath = "debuda-server-cache.pkl"
    cache_store = dict()
    enabled = True

    def load():
        if DEBUDA_SERVER_CACHED_IFC.enabled:
            if os.path.exists(DEBUDA_SERVER_CACHED_IFC.filepath):
                util.INFO(
                    f"Loading server cache from file {DEBUDA_SERVER_CACHED_IFC.filepath}"
                )
                with open(DEBUDA_SERVER_CACHED_IFC.filepath, "rb") as f:
                    DEBUDA_SERVER_CACHED_IFC.cache_store = pickle.load(f)
                    util.INFO(
                        f"  Loaded {len(DEBUDA_SERVER_CACHED_IFC.cache_store)} entries"
                    )
            else:
                assert (
                    DEBUDA_SERVER_SOCKET_IFC.enabled
                ), f"Cache file {DEBUDA_SERVER_CACHED_IFC.filepath} does not exist"

    def save():
        if DEBUDA_SERVER_CACHED_IFC.enabled and DEBUDA_SERVER_SOCKET_IFC.enabled:
            util.INFO(
                f"Saving server cache to file {DEBUDA_SERVER_CACHED_IFC.filepath}"
            )
            with open(DEBUDA_SERVER_CACHED_IFC.filepath, "wb") as f:
                pickle.dump(DEBUDA_SERVER_CACHED_IFC.cache_store, f)
                util.INFO(
                    f"  Saved {len(DEBUDA_SERVER_CACHED_IFC.cache_store)} entries"
                )

    def pci_read_xy(chip_id, x, y, noc_id, reg_addr):
        key = (chip_id, x, y, noc_id, reg_addr)
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.pci_read_xy(chip_id, x, y, noc_id, reg_addr)
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]

    def pci_write_xy(chip_id, x, y, noc_id, reg_addr, data):
        if not DEBUDA_SERVER_CACHED_IFC.enabled:
            return DEBUDA_SERVER_SOCKET_IFC.pci_write_xy(
                chip_id, x, y, noc_id, reg_addr, data
            )

    def host_dma_read(chip_id, dram_addr, dram_chan):
        key = dram_addr
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.host_dma_read(chip_id, dram_addr, dram_chan)
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]

    def pci_read_tile(chip_id, x, y, z, reg_addr, msg_size, data_format):
        key = (chip_id, x, y, z, reg_addr, msg_size, data_format)
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.pci_read_tile(
                    chip_id, x, y, z, reg_addr, msg_size, data_format
                )
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]

    def pci_raw_read(chip_id, reg_addr):
        key = (chip_id, reg_addr)
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.pci_raw_read(chip_id, reg_addr)
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]

    def pci_raw_write(chip_id, reg_addr, data):
        if not DEBUDA_SERVER_CACHED_IFC.enabled:
            return DEBUDA_SERVER_SOCKET_IFC.pci_raw_write(chip_id, reg_addr, data)

    def get_runtime_data():
        key = "get_runtime_data"
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.get_runtime_data()
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]

    def get_cluster_desc_path():
        key = "cluster_desc_path"
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.get_cluster_desc_path()
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]

    def get_harvested_coord_translation(chip_id):
        key = f"harvested_coord_translation_{chip_id}"
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.get_harvested_coord_translation(chip_id)
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]

    def get_device_ids():
        key = "device_ids"
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.get_device_ids()
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]

    def get_device_arch(chip_id):
        key = f"device_arch_{chip_id}"
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.get_device_arch(chip_id)
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]

    def get_device_soc_description(chip_id):
        key = f"get_device_soc_description_{chip_id}"
        if (
            key not in DEBUDA_SERVER_CACHED_IFC.cache_store
            or not DEBUDA_SERVER_CACHED_IFC.enabled
        ):
            DEBUDA_SERVER_CACHED_IFC.cache_store[key] = (
                DEBUDA_SERVER_SOCKET_IFC.get_device_soc_description(chip_id)
            )
        return DEBUDA_SERVER_CACHED_IFC.cache_store[key]


# PCI interface is a cached interface through Debuda server
class SERVER_IFC(DEBUDA_SERVER_CACHED_IFC):
    pass


# Prints contents of core's memory
def dump_memory(device_id, loc, addr, size):
    for k in range(0, size // 4 // 16 + 1):
        row = []
        for j in range(0, 16):
            if addr + k * 64 + j * 4 < addr + size:
                val = SERVER_IFC.pci_read_xy(
                    device_id, *loc.to("nocVirt"), 0, addr + k * 64 + j * 4
                )
                row.append(f"0x{val:08x}")
        s = " ".join(row)
        print(f"{loc.to_str()} 0x{(addr + k*64):08x} => {s}")


# Dumps tile in format received form tt_tile::get_string
def dump_tile(chip, loc, addr, size, data_format):
    s = SERVER_IFC.pci_read_tile(chip, *loc.to("nocVirt"), 0, addr, size, data_format)
    if type(s) == bytes:
        s = s.decode("utf-8")
    lines = s.split("\n")
    rows = []
    for line in lines:
        rows.append(line.split())
    print(tabulate(rows, tablefmt="plain", showindex=False))



BinarySlot = namedtuple("BinarySlot", ["offset_bytes", "size_bytes"])
        

class AddressMap(ABC):
    def __init__(self):
        self.binaries: Dict[str, BinarySlot]
        
class L1AddressMap(AddressMap):
    def __init__(self):
        super().__init__()
        

#
# Device class: generic API for talking to specific devices. This class is the parent of specific
# device classes (e.g. GrayskullDevice, WormholeDevice). The create class method is used to create
# a specific device.
#
class Device(TTObject):
    # NOC reg type
    class RegType:
        Cmd = 0
        Config = 1
        Status = 2

    # Class variable denoting the number of devices created
    num_devices = 0

    # See tt_coordinate.py for description of coordinate systems
    tensix_row_to_netlist_row = dict()
    netlist_row_to_tensix_row = dict()

    # Maps to store translation table from nocVirt to nocTr and vice versa
    nocVirt_to_nocTr_map = dict()
    nocTr_to_nocVirt_map = dict()

    # Maps to store translation table from noc0 to nocTr and vice versa
    nocTr_y_to_noc0_y = dict()
    noc0_y_to_nocTr_y = dict()
    
    def get_address_map(self, core_type: str) -> AddressMap:
        if core_type not in self._address_maps:
            raise RuntimeError(f"Core type {core_type} not found in address maps")
        return self._address_maps[core_type]
    
    def get_binary_size(self, core_loc, binary_type) -> int:
        core_type = self.get_block_type(core_loc)
        address_map = self.get_address_map(core_type)
        binary_size = address_map.binaries[binary_type].size_bytes
        assert binary_size >= 0
        return binary_size
    
    def get_binary_l1_offset(self, core_loc: OnChipCoordinate, binary_type: str) -> int:
        core_type = self.get_block_type(core_loc)
        address_map = self.get_address_map(core_type)
        offset = address_map.binaries[binary_type].offset_bytes
        assert offset >= 0
        return offset
        
    def get_dram_binary_offset_in_epoch_command_queue(self, core_loc: OnChipCoordinate, binary_type: str) -> int:
        address_map = self.get_address_map("dram")
        offset = address_map.binaries[binary_type].offset_bytes
        assert offset >= 0
        return offset
    

    # Class method to create a Device object given device architecture
    def create(arch, device_id, cluster_desc, device_desc_path: str, context: Context):
        dev = None
        if arch.lower() == "grayskull":
            import tt_grayskull

            dev = tt_grayskull.GrayskullDevice(
                id=device_id,
                arch=arch,
                cluster_desc=cluster_desc,
                device_desc_path=device_desc_path,
                context=context
            )
        if "wormhole" in arch.lower():
            import tt_wormhole

            dev = tt_wormhole.WormholeDevice(
                id=device_id,
                arch=arch,
                cluster_desc=cluster_desc,
                device_desc_path=device_desc_path,
                context=context
            )

        if dev is None:
            raise RuntimeError(f"Architecture {arch} is not supported")

        return dev

    def get_harvested_noc0_y_rows(self):
        harvested_noc0_y_rows = []
        if self._harvesting:
            bitmask = self._harvesting["harvest_mask"]
            for h_index in range(0, self.row_count()):
                if (1 << h_index) & bitmask:  # Harvested
                    harvested_noc0_y_rows.append(self.HARVESTING_NOC_LOCATIONS[h_index])
        return harvested_noc0_y_rows

    def _create_tensix_netlist_harvesting_map(self):
        tensix_row = 0
        netlist_row = 0
        self.tensix_row_to_netlist_row = dict()  # Clear any existing map
        self.netlist_row_to_tensix_row = dict()
        harvested_noc0_y_rows = self.get_harvested_noc0_y_rows()

        for noc0_y in range(0, self.row_count()):
            if noc0_y == 0 or noc0_y == 6:
                pass  # Skip Ethernet rows
            else:
                if noc0_y in harvested_noc0_y_rows:
                    pass  # Skip harvested rows
                else:
                    self.netlist_row_to_tensix_row[netlist_row] = tensix_row
                    self.tensix_row_to_netlist_row[tensix_row] = netlist_row
                    netlist_row += 1
                tensix_row += 1

    def _create_nocTr_noc0_harvesting_map(self):
        bitmask = self._harvesting["harvest_mask"] if self._harvesting else 0

        self.nocTr_y_to_noc0_y = dict()  # Clear any existing map
        self.noc0_y_to_nocTr_y = dict()
        for nocTr_y in range(0, self.row_count()):
            self.nocTr_y_to_noc0_y[nocTr_y] = nocTr_y  # Identity mapping for rows < 16

        num_harvested_rows = bin(bitmask).count("1")
        self._handle_harvesting_for_nocTr_noc0_map(num_harvested_rows)

        # 4. Create reverse map
        for nocTr_y in self.nocTr_y_to_noc0_y:
            self.noc0_y_to_nocTr_y[self.nocTr_y_to_noc0_y[nocTr_y]] = nocTr_y

        # 4. Print
        # for tr_row in reversed (range (16, 16 + self.row_count())):
        #     print(f"nocTr row {tr_row} => noc0 row {self.nocTr_y_to_noc0_y[tr_row]}")

        # print (f"Created nocTr to noc0 harvesting map for bitmask: {bitmask}")

    def _create_harvesting_maps(self):
        self._create_tensix_netlist_harvesting_map()
        self._create_nocTr_noc0_harvesting_map()

    def _create_nocVirt_to_nocTr_map(self):
        harvested_coord_translation_str = SERVER_IFC.get_harvested_coord_translation(0)
        self.nocVirt_to_nocTr_map = ast.literal_eval(
            harvested_coord_translation_str
        )  # Eval string to dict
        self.nocTr_to_nocVirt_map = {
            v: k for k, v in self.nocVirt_to_nocTr_map.items()
        }  # Create inverse map as well

    def tensix_to_netlist(self, tensix_loc):
        return (self.tensix_row_to_netlist_row[tensix_loc[0]], tensix_loc[1])

    def netlist_to_tensix(self, netlist_loc):
        return (self.netlist_row_to_tensix_row[netlist_loc[0]], netlist_loc[1])

    @cached_property
    def yaml_file(self):
        return util.YamlFile(self._device_desc_path)

    @cached_property
    def EPOCH_ID_ADDR(self):
        return self._context.epoch_id_address

    @cached_property
    def ETH_EPOCH_ID_ADDR(self):
        return self._context.eth_epoch_id_address

    def __init__(self, id, arch, cluster_desc, address_maps: Dict[str, AddressMap], device_desc_path: str, context: Context):
        """

        Args:
            address_maps (Dict[str, AddressMap]): map of core_type(str) -> AddressMap
        """
        self._id = id
        self._arch = arch
        self._has_mmio = False
        self._address_maps = address_maps
        self._device_desc_path = device_desc_path
        self._context = context
        for chip in cluster_desc["chips_with_mmio"]:
            if id in chip:
                self._has_mmio = True
                break

        # Check if harvesting_desc is an array and has id+1 entries at the least
        harvesting_desc = cluster_desc["harvesting"]
        if isinstance(harvesting_desc, Sequence) and len(harvesting_desc) > id:
            device_desc = harvesting_desc[id]
            if id not in device_desc:
                raise util.TTFatalException(f"Key {id} not found in: {device_desc}")
            self._harvesting = device_desc[id]
        elif arch.lower() == "grayskull":
            self._harvesting = None
        else:
            raise util.TTFatalException(
                f"Cluster description is not valid. It reads: {harvesting_desc}"
            )

        self._create_harvesting_maps()
        self._create_nocVirt_to_nocTr_map()
        util.DEBUG(
            "Opened device: id=%d, arch=%s, has_mmio=%s, harvesting=%s"
            % (id, arch, self._has_mmio, self._harvesting)
        )

        self.block_locations_cache = dict()

    # Coordinate conversion functions (see tt_coordinate.py for description of coordinate systems)
    def die_to_noc(self, phys_loc, noc_id=0):
        die_x, die_y = phys_loc
        if noc_id == 0:
            return (self.DIE_X_TO_NOC_0_X[die_x], self.DIE_Y_TO_NOC_0_Y[die_y])
        else:
            return (self.DIE_X_TO_NOC_1_X[die_x], self.DIE_Y_TO_NOC_1_Y[die_y])

    def noc_to_die(self, noc_loc, noc_id=0):
        noc_x, noc_y = noc_loc
        if noc_id == 0:
            return (self.NOC_0_X_TO_DIE_X[noc_x], self.NOC_0_Y_TO_DIE_Y[noc_y])
        else:
            return (self.NOC_1_X_TO_DIE_X[noc_x], self.NOC_1_Y_TO_DIE_Y[noc_y])

    def noc0_to_noc1(self, noc0_loc):
        phys_loc = self.noc_to_die(noc0_loc, noc_id=0)
        return self.die_to_noc(phys_loc, noc_id=1)

    def noc1_to_noc0(self, noc1_loc):
        phys_loc = self.noc_to_die(noc1_loc, noc_id=1)
        return self.die_to_noc(phys_loc, noc_id=0)

    def nocVirt_to_nocTr(self, noc0_loc):
        return self.nocVirt_to_nocTr_map[noc0_loc]

    def nocTr_to_nocVirt(self, nocTr_loc):
        return self.nocTr_to_nocVirt_map[nocTr_loc]

    def nocTr_to_noc0(self, nocTr_loc):
        noc0_y = self.nocTr_y_to_noc0_y[nocTr_loc[1]]
        noc0_x = (
            self.NOCTR_X_TO_NOC0_X[nocTr_loc[0]] if nocTr_loc[0] >= 16 else nocTr_loc[0]
        )
        return (noc0_x, noc0_y)

    def noc0_to_nocTr(self, noc0_loc):
        nocTr_y = self.noc0_y_to_nocTr_y[noc0_loc[1]]
        nocTr_x = self.NOC0_X_TO_NOCTR_X[noc0_loc[0]]
        return (nocTr_x, nocTr_y)

    def nocVirt_to_noc0(self, nocVirt_loc):
        nocTr_loc = self.nocVirt_to_nocTr(nocVirt_loc)
        return self.nocTr_to_noc0(nocTr_loc)

    def noc0_to_nocVirt(self, noc0_loc):
        nocTr_loc = self.noc0_to_nocTr(noc0_loc)
        try:
            nocVirt = self.nocTr_to_nocVirt(nocTr_loc)
        except KeyError:
            # DRAM locations are not in nocTr_to_nocVirt map. Use noc0 coordinates directly.
            nocVirt = noc0_loc
        return nocVirt

    def noc0_to_netlist(self, noc0_loc):
        try:
            c = self.tensix_to_netlist(self.noc0_to_tensix(noc0_loc))
            return (c[0], c[1])
        except KeyError:
            raise CoordinateTranslationError(
                f"noc0_to_netlist: noc0_loc {noc0_loc} does not translate to a valid netlist location"
            )

    def netlist_to_noc0(self, netlist_loc):
        try:
            c = self.tensix_to_noc0(self.netlist_to_tensix(netlist_loc))
            return (c[0], c[1])
        except KeyError:
            raise CoordinateTranslationError(
                f"netlist_to_noc0: netlist_loc {netlist_loc} does not translate to a valid noc0 location"
            )

    # For all cores read all 64 streams and populate the 'streams' dict. streams[x][y][stream_id] will
    # contain a dictionary of all register values as strings formatted to show in UI
    def read_all_stream_registers(self):
        streams = {}
        for loc in self.get_block_locations(block_type="functional_workers"):
            for stream_id in range(0, 64):
                regs = self.read_stream_regs(loc, stream_id)
                streams[
                    (
                        loc,
                        stream_id,
                    )
                ] = regs
        for loc in self.get_block_locations(block_type="eth"):
            for stream_id in range(0, 32):
                regs = self.read_stream_regs(loc, stream_id)
                streams[
                    (
                        loc,
                        stream_id,
                    )
                ] = regs
        return streams

    def read_core_to_epoch_mapping(self, block_type=None):
        """
        Reading current epoch for each functional worker
        :return: { loc : epoch_id }
        """
        epochs = {}
        if block_type == None or block_type == "functional_workers":
            for loc in self.get_block_locations(block_type="functional_workers"):
                epochs[loc] = self.get_epoch_id(loc)
        if block_type == None or block_type == "eth":
            for loc in self.get_block_locations(block_type="eth"):
                epochs[loc] = self.get_epoch_id(loc)

        return epochs

    # For a given core, read all 64 streams and populate the 'streams' dict. streams[stream_id] will
    # contain a dictionary of all register values as strings formatted to show in UI
    def read_core_stream_registers(self, loc):
        streams = {}
        for stream_id in range(0, 64):
            regs = self.read_stream_regs(loc, stream_id)
            streams[stream_id] = regs
        return streams

    # Returns core locations of cores that have programmed stream registers
    def get_configured_stream_locations(self, all_stream_regs):
        core_locations = []
        for loc, stream_regs in all_stream_regs.items():
            if self.is_stream_configured(stream_regs):
                core_locations.append(loc)
        return core_locations

    def get_block_locations(self, block_type="functional_workers"):
        """
        Returns locations of all blocks of a given type
        """
        locs = []
        dev = self.yaml_file.root

        for loc_or_list in dev[block_type]:
            if type(loc_or_list) != str and isinstance(loc_or_list, Sequence):
                for loc in loc_or_list:
                    locs.append(OnChipCoordinate.create(loc, self, "nocVirt"))
            else:
                locs.append(OnChipCoordinate.create(loc_or_list, self, "nocVirt"))
        return locs

    block_types = {
        "functional_workers": {"symbol": ".", "desc": "Functional worker"},
        "eth": {"symbol": "E", "desc": "Ethernet"},
        "arc": {"symbol": "A", "desc": "ARC"},
        "dram": {"symbol": "D", "desc": "DRAM"},
        "pcie": {"symbol": "P", "desc": "PCIE"},
        "router_only": {"symbol": " ", "desc": "Router only"},
        "harvested_workers": {"symbol": "-", "desc": "Harvested"},
    }

    def get_block_type(self, loc):
        """
        Returns the type of block at the given location
        """
        dev = self.yaml_file.root
        for block_type in self.block_types:
            block_locations = self.get_block_locations(block_type=block_type)
            if loc in block_locations:
                return block_type
        return None

    # Returns locations of metadata queue associated with a given core
    def get_t6_queue_location(self, arch_name, t6_locs):
        # IMPROVE: this should be handled in the respective arch files/classes
        dram_core = {"x": None, "y": None}
        if arch_name == "WORMHOLE" or arch_name == "WORMHOLE_B0":
            if t6_locs["y"] in [0, 11, 1, 7, 5, 6]:
                if t6_locs["x"] >= 5:
                    dram_core["x"] = 5
                else:
                    dram_core["x"] = 0
            else:
                dram_core["x"] = 5
            dram_core["y"] = t6_locs["y"]
        elif arch_name == "GRAYSKULL":
            if t6_locs["x"] in [1, 2, 3]:
                dram_core["x"] = 1
            elif t6_locs["x"] in [4, 5, 6]:
                dram_core["x"] = 4
            elif t6_locs["x"] in [7, 8, 9]:
                dram_core["x"] = 7
            elif t6_locs["x"] in [10, 11, 12]:
                dram_core["x"] = 10
            else:
                dram_core["x"] = -1
            if t6_locs["y"] <= 5:
                dram_core["y"] = 0
            else:
                dram_core["y"] = 6
        else:
            raise ValueError("Unsupported architecture")
        return dram_core

    # Returns a string representation of the device. When printed, the string will
    # show the device blocks ascii graphically. It will emphasize blocks with locations given by emphasize_loc_list
    # See tt_coordinates for valid values of axis_coordinates
    def render(self, axis_coordinate="die", cell_renderer=None, legend=None):
        dev = self.yaml_file.root
        rows = []

        # Retrieve all block locations
        all_block_locs = dict()
        hor_axis = OnChipCoordinate.horizontal_axis(axis_coordinate)
        ver_axis = OnChipCoordinate.vertical_axis(axis_coordinate)

        # Compute extents(range) of all coordinates in the UI
        ui_hor_range = (9999, -1)
        ui_ver_range = (9999, -1)
        for bt in self.block_types:
            b_locs = self.get_block_locations(block_type=bt)
            for loc in b_locs:
                try:
                    grid_loc = loc.to(axis_coordinate)
                    ui_hor = grid_loc[hor_axis]
                    ui_hor_range = (
                        min(ui_hor_range[0], ui_hor),
                        max(ui_hor_range[1], ui_hor),
                    )
                    ui_ver = grid_loc[ver_axis]
                    ui_ver_range = (
                        min(ui_ver_range[0], ui_ver),
                        max(ui_ver_range[1], ui_ver),
                    )
                    all_block_locs[(ui_hor, ui_ver)] = loc
                except:
                    pass

        screen_row_y = 0
        C = util.CLR_INFO
        E = util.CLR_END

        def append_horizontal_axis_labels(rows, ui_hor_range):
            row = [""] + [
                f"{C}%02d{E}" % i for i in range(ui_hor_range[0], ui_hor_range[1] + 1)
            ]  # This adds the X-axis labels
            rows.append(row)

        if OnChipCoordinate.vertical_axis_increasing_up(axis_coordinate):
            ver_range = reversed(range(ui_ver_range[0], ui_ver_range[1] + 1))
        else:
            ver_range = range(ui_ver_range[0], ui_ver_range[1] + 1)
            append_horizontal_axis_labels(rows, ui_hor_range)

        for ui_ver in ver_range:
            row = [f"{C}%02d{E}" % ui_ver]  # This adds the Y-axis label
            # 1. Add graphics
            for ui_hor in range(ui_hor_range[0], ui_hor_range[1] + 1):
                render_str = ""
                if (ui_hor, ui_ver) in all_block_locs:
                    if cell_renderer == None:
                        render_str = all_block_locs[(ui_hor, ui_ver)].to_str("netlist")
                    else:
                        render_str = cell_renderer(all_block_locs[(ui_hor, ui_ver)])
                row.append(render_str)

            # 2. Add legend
            legend_y = screen_row_y
            if legend and legend_y < len(legend):
                row = row + [util.CLR_INFO + "    " + legend[legend_y] + util.CLR_END]

            rows.append(row)
            screen_row_y += 1

        if OnChipCoordinate.vertical_axis_increasing_up(axis_coordinate):
            append_horizontal_axis_labels(rows, ui_hor_range)

        table_str = tabulate(rows, tablefmt="plain")
        return table_str

    def dump_memory(self, noc0_loc, addr, size):
        return dump_memory(self.id(), noc0_loc, addr, size)

    def dump_tile(self, noc0_loc, addr, size, data_format):
        return dump_tile(self.id(), noc0_loc, addr, size, data_format)

    # User friendly string representation of the device
    def __str__(self):
        return self.render()

    # Detailed string representation of the device
    def __repr__(self):
        return f"ID: {self.id()}, Arch: {self._arch}"

    # Reads and returns the Risc debug registers
    def get_debug_regs(self, loc):
        DEBUG_MAILBOX_BUF_BASE = 112
        DEBUG_MAILBOX_BUF_SIZE = 64
        THREAD_COUNT = 4

        debug_tables = [[] for i in range(THREAD_COUNT)]
        for thread_idx in range(THREAD_COUNT):
            for reg_idx in range(DEBUG_MAILBOX_BUF_SIZE // THREAD_COUNT):
                reg_addr = (
                    DEBUG_MAILBOX_BUF_BASE
                    + thread_idx * DEBUG_MAILBOX_BUF_SIZE
                    + reg_idx * 4
                )
                val = SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, reg_addr)
                debug_tables[thread_idx].append(
                    {"lo_val": val & 0xFFFF, "hi_val": (val >> 16) & 0xFFFF}
                )
        return debug_tables

    # Returns a stream type based on KERNEL_OPERAND_MAPPING_SCHEME
    def stream_type(self, stream_id):
        # From src/firmware/riscv/grayskull/stream_io_map.h
        # Kernel operand mapping scheme:
        KERNEL_OPERAND_MAPPING_SCHEME = [
            {
                "id_min": 0,
                "id_max": 7,
                "stream_id_min": 0,
                "short": "??",
                "long": "????? => streams 0-7",
            },  # FIX THIS
            {
                "id_min": 0,
                "id_max": 7,
                "stream_id_min": 8,
                "short": "input",
                "long": "(inputs, unpacker-only) => streams 8-15",
            },
            {
                "id_min": 8,
                "id_max": 15,
                "stream_id_min": 16,
                "short": "param",
                "long": "(params, unpacker-only) => streams 16-23",
            },
            {
                "id_min": 16,
                "id_max": 23,
                "stream_id_min": 24,
                "short": "output",
                "long": "(outputs, packer-only) => streams 24-31",
            },
            {
                "id_min": 24,
                "id_max": 31,
                "stream_id_min": 32,
                "short": "intermediate",
                "long": "(intermediates, packer/unpacker) => streams 32-39",
            },
            {
                "id_min": 32,
                "id_max": 63,
                "stream_id_min": 32,
                "short": "op-relay",
                "long": "(operand relay?) => streams 40-63",
            },  # CHECK THIS
        ]
        for ko in KERNEL_OPERAND_MAPPING_SCHEME:
            s_id_min = ko["stream_id_min"]
            s_id_count = ko["id_max"] - ko["id_min"]
            if stream_id >= s_id_min and stream_id < s_id_min + s_id_count:
                return ko
        util.WARN("no desc for stream_id=%s" % stream_id)
        return {
            "id_min": -1,
            "id_max": -1,
            "stream_id_min": -1,
            "short": "??",
            "long": "?????",
        }  # FIX THIS

    # Function to print a full dump of a location x-y
    def full_dump_xy(self, loc):
        for stream_id in range(0, 64):
            print()
            stream = self.read_stream_regs(loc, stream_id)
            for reg, value in stream.items():
                print(
                    f"Tensix x={loc.to_str()} => stream {stream_id:02d} {reg} = {value}"
                )

        for noc_id in range(0, 2):
            print()
            self.read_print_noc_reg(loc, noc_id, "nonposted write reqs sent", 0xA)
            self.read_print_noc_reg(loc, noc_id, "posted write reqs sent", 0xB)
            self.read_print_noc_reg(loc, noc_id, "nonposted write words sent", 0x8)
            self.read_print_noc_reg(loc, noc_id, "posted write words sent", 0x9)
            self.read_print_noc_reg(loc, noc_id, "write acks received", 0x1)
            self.read_print_noc_reg(loc, noc_id, "read reqs sent", 0x5)
            self.read_print_noc_reg(loc, noc_id, "read words received", 0x3)
            self.read_print_noc_reg(loc, noc_id, "read resps received", 0x2)
            print()
            self.read_print_noc_reg(loc, noc_id, "nonposted write reqs received", 0x1A)
            self.read_print_noc_reg(loc, noc_id, "posted write reqs received", 0x1B)
            self.read_print_noc_reg(loc, noc_id, "nonposted write words received", 0x18)
            self.read_print_noc_reg(loc, noc_id, "posted write words received", 0x19)
            self.read_print_noc_reg(loc, noc_id, "write acks sent", 0x10)
            self.read_print_noc_reg(loc, noc_id, "read reqs received", 0x15)
            self.read_print_noc_reg(loc, noc_id, "read words sent", 0x13)
            self.read_print_noc_reg(loc, noc_id, "read resps sent", 0x12)
            print()
            self.read_print_noc_reg(
                loc, noc_id, "router port x out vc full credit out vc stall", 0x24
            )
            self.read_print_noc_reg(
                loc, noc_id, "router port y out vc full credit out vc stall", 0x22
            )
            self.read_print_noc_reg(
                loc, noc_id, "router port niu out vc full credit out vc stall", 0x20
            )
            print()
            self.read_print_noc_reg(loc, noc_id, "router port x VC14 & VC15 dbg", 0x3D)
            self.read_print_noc_reg(loc, noc_id, "router port x VC12 & VC13 dbg", 0x3C)
            self.read_print_noc_reg(loc, noc_id, "router port x VC10 & VC11 dbg", 0x3B)
            self.read_print_noc_reg(loc, noc_id, "router port x VC8 & VC9 dbg", 0x3A)
            self.read_print_noc_reg(loc, noc_id, "router port x VC6 & VC7 dbg", 0x39)
            self.read_print_noc_reg(loc, noc_id, "router port x VC4 & VC5 dbg", 0x38)
            self.read_print_noc_reg(loc, noc_id, "router port x VC2 & VC3 dbg", 0x37)
            self.read_print_noc_reg(loc, noc_id, "router port x VC0 & VC1 dbg", 0x36)
            print()
            self.read_print_noc_reg(loc, noc_id, "router port y VC14 & VC15 dbg", 0x35)
            self.read_print_noc_reg(loc, noc_id, "router port y VC12 & VC13 dbg", 0x34)
            self.read_print_noc_reg(loc, noc_id, "router port y VC10 & VC11 dbg", 0x33)
            self.read_print_noc_reg(loc, noc_id, "router port y VC8 & VC9 dbg", 0x32)
            self.read_print_noc_reg(loc, noc_id, "router port y VC6 & VC7 dbg", 0x31)
            self.read_print_noc_reg(loc, noc_id, "router port y VC4 & VC5 dbg", 0x30)
            self.read_print_noc_reg(loc, noc_id, "router port y VC2 & VC3 dbg", 0x2F)
            self.read_print_noc_reg(loc, noc_id, "router port y VC0 & VC1 dbg", 0x2E)
            print()
            self.read_print_noc_reg(loc, noc_id, "router port niu VC6 & VC7 dbg", 0x29)
            self.read_print_noc_reg(loc, noc_id, "router port niu VC4 & VC5 dbg", 0x28)
            self.read_print_noc_reg(loc, noc_id, "router port niu VC2 & VC3 dbg", 0x27)
            self.read_print_noc_reg(loc, noc_id, "router port niu VC0 & VC1 dbg", 0x26)

        en = 1
        rd_sel = 0
        pc_mask = 0x7FFFFFFF
        daisy_sel = 7

        sig_sel = 0xFF
        rd_sel = 0
        SERVER_IFC.pci_write_xy(
            self.id(),
            *loc.to("nocVirt"),
            0,
            0xFFB12054,
            ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)),
        )
        test_val1 = SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0xFFB1205C)
        rd_sel = 1
        SERVER_IFC.pci_write_xy(
            self.id(),
            *loc.to("nocVirt"),
            0,
            0xFFB12054,
            ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)),
        )
        test_val2 = SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0xFFB1205C)

        rd_sel = 0
        sig_sel = 2 * self.SIG_SEL_CONST
        SERVER_IFC.pci_write_xy(
            self.id(),
            *loc.to("nocVirt"),
            0,
            0xFFB12054,
            ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)),
        )
        brisc_pc = (
            SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0xFFB1205C)
            & pc_mask
        )

        # Doesn't work - looks like a bug for selecting inputs > 31 in daisy stop
        # rd_sel = 0
        # sig_sel = 2*16
        # SERVER_IFC.pci_write_xy(self.id(), *loc.to("nocVirt"), 0, 0xffb12054, ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)))
        # nrisc_pc = SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0xffb1205c) & pc_mask

        rd_sel = 0
        sig_sel = 2 * 10
        SERVER_IFC.pci_write_xy(
            self.id(),
            *loc.to("nocVirt"),
            0,
            0xFFB12054,
            ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)),
        )
        trisc0_pc = (
            SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0xFFB1205C)
            & pc_mask
        )

        rd_sel = 0
        sig_sel = 2 * 11
        SERVER_IFC.pci_write_xy(
            self.id(),
            *loc.to("nocVirt"),
            0,
            0xFFB12054,
            ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)),
        )
        trisc1_pc = (
            SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0xFFB1205C)
            & pc_mask
        )

        rd_sel = 0
        sig_sel = 2 * 12
        SERVER_IFC.pci_write_xy(
            self.id(),
            *loc.to("nocVirt"),
            0,
            0xFFB12054,
            ((en << 29) | (rd_sel << 25) | (daisy_sel << 16) | (sig_sel << 0)),
        )
        trisc2_pc = (
            SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0xFFB1205C)
            & pc_mask
        )

        print()
        print(
            f"Tensix {loc.to_str()} => dbus_test_val1 (expect 7)={test_val1:x}, dbus_test_val2 (expect A5A5A5A5)={test_val2:x}"
        )
        print(
            f"Tensix {loc.to_str()} => brisc_pc=0x{brisc_pc:x}, trisc0_pc=0x{trisc0_pc:x}, trisc1_pc=0x{trisc1_pc:x}, trisc2_pc=0x{trisc2_pc:x}"
        )

        SERVER_IFC.pci_write_xy(self.id(), *loc.to("nocVirt"), 0, 0xFFB12054, 0)
        util.WARN(
            "full_dump_xy not fully tested (functionality might be device dependent)"
        )

    # Reads and immediately prints a value of a given NOC register
    def read_print_noc_reg(self, loc, noc_id, reg_name, reg_index):
        assert type(loc) == OnChipCoordinate
        reg_addr = 0xFFB20000 + (noc_id * 0x10000) + 0x200 + (reg_index * 4)
        val = SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, reg_addr)
        print(
            f"Tensix {loc.to_str()} => NOC{noc_id:d} {reg_name:s} = 0x{val:08x} ({val:d})"
        )
        return val

    # Extracts and returns a single field of a stream register
    def get_stream_reg_field(self, loc, stream_id, reg_index, start_bit, num_bits):
        assert type(loc) == OnChipCoordinate
        reg_addr = 0xFFB40000 + (stream_id * 0x1000) + (reg_index * 4)
        val = SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, reg_addr)
        mask = (1 << num_bits) - 1
        val = (val >> start_bit) & mask
        return val

    def get_stream_phase(self, loc, stream_id):
        assert type(loc) == OnChipCoordinate
        return self.get_stream_reg_field(loc, stream_id, 11, 0, 20) & 0x7FFF

    # This comes from src/firmware/riscv/common/epoch.h
    def get_epoch_id(self, loc):
        assert type(loc) == OnChipCoordinate
        if loc in self.get_block_locations("functional_workers"):
            epoch = (
                SERVER_IFC.pci_read_xy(
                    self.id(), *loc.to("nocVirt"), 0, self.EPOCH_ID_ADDR
                )
                & 0xFF
            )
        else:
            # old 0x1b00/0
            # 0x20080
            # + 0x28c
            # = 2030c
            # epoch = SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0x2030c) & 0xFF
            epoch = (
                SERVER_IFC.pci_read_xy(
                    self.id(), *loc.to("nocVirt"), 0, self.ETH_EPOCH_ID_ADDR
                )
                & 0xFF
            )

        return epoch

    # Returns whether the stream is configured
    def is_stream_configured(self, stream_data):
        return int(stream_data["CURR_PHASE"]) > 0

    def stream_has_remote_receiver(self, stream_data):
        return int(stream_data["REMOTE_RECEIVER"]) != 0

    def is_stream_idle(self, stream_data):
        return (stream_data["DEBUG_STATUS[7]"] & 0xFFF) == 0xC00

    def is_stream_holding_tiles(self, stream_data):
        return int(stream_data["CURR_PHASE"]) != 0 and (
            int(stream_data["NUM_MSGS_RECEIVED"]) > 0
            or (
                int(
                    stream_data["MSG_INFO_WR_PTR (word addr)"]
                    if "MSG_INFO_WR_PTR (word addr)" in stream_data
                    else 0
                )
                - int(
                    stream_data["MSG_INFO_PTR (word addr)"]
                    if "MSG_INFO_PTR (word addr)" in stream_data
                    else 0
                )
                > 0
            )
        )

    def is_stream_active(self, stream_data):
        return int(stream_data["CURR_PHASE"]) != 0 and (
            int(stream_data["NUM_MSGS_RECEIVED"]) > 0
            or (
                int(
                    stream_data["MSG_INFO_WR_PTR (word addr)"]
                    if "MSG_INFO_WR_PTR (word addr)" in stream_data
                    else 0
                )
                - int(
                    stream_data["MSG_INFO_PTR (word addr)"]
                    if "MSG_INFO_PTR (word addr)" in stream_data
                    else 0
                )
                > 0
            )
        )

    def is_stream_done(self, stream_data):
        return int(stream_data["NUM_MSGS_RECEIVED"]) == int(
            stream_data["CURR_PHASE_NUM_MSGS_REMAINING"]
        )

    def is_bad_stream(self, stream_data):
        # Only certain bits indicate a problem for debug status registers
        return (
            ((stream_data["DEBUG_STATUS[1]"] & 0xF0000) != 0)
            or (stream_data["DEBUG_STATUS[2]"] & 0x7) == 0x4
            or (stream_data["DEBUG_STATUS[2]"] & 0x7) == 0x2
        )

    def is_gsync_hung(self, loc):
        assert type(loc) == OnChipCoordinate
        return (
            SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0xFFB2010C)
            == 0xB0010000
        )

    def is_ncrisc_done(self, loc):
        assert type(loc) == OnChipCoordinate
        return (
            SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, 0xFFB2010C)
            == 0x1FFFFFF1
        )

    NCRISC_STATUS_REG_ADDR = 0xFFB2010C
    BRISC_STATUS_REG_ADDR = 0xFFB3010C

    def get_status_register_desc(self, register_address, reg_value_on_chip):
        STATUS_REG = {
            self.NCRISC_STATUS_REG_ADDR: [  # ncrisc
                {
                    "reg_val": [0xA8300000, 0xA8200000, 0xA8100000],
                    "description": "Prologue queue header load",
                    "mask": 0xFFFFF000,
                    "ver": 0,
                },
                {
                    "reg_val": [0x11111111],
                    "description": "Main loop begin",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xC0000000],
                    "description": "Load queue pointers",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xD0000000],
                    "description": "Which stream id will read queue",
                    "mask": 0xFFFFF000,
                    "ver": 0,
                },
                {
                    "reg_val": [0xD1000000],
                    "description": "Queue has data to read",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xD2000000],
                    "description": "Queue has l1 space",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xD3000000],
                    "description": "Queue read in progress",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xE0000000],
                    "description": "Which stream has data in l1 available to push",
                    "mask": 0xFFFFF000,
                    "ver": 0,
                },
                {
                    "reg_val": [0xE1000000],
                    "description": "Push in progress",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xF0000000],
                    "description": "Which stream will write queue",
                    "mask": 0xFFFFF000,
                    "ver": 0,
                },
                {
                    "reg_val": [0xF0300000],
                    "description": "Waiting for stride to be ready before updating wr pointer",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xF1000000],
                    "description": "Needs to write data to dram",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xF2000000],
                    "description": "Ready to write data to dram",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xF3000000],
                    "description": "Has data to write to dram",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xF4000000],
                    "description": "Writing to dram",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0x20000000],
                    "description": "Amount of written tiles that needs to be cleared",
                    "mask": 0xFFFFF000,
                    "ver": 0,
                },
                {
                    "reg_val": [0x22222222, 0x33333333, 0x44444444],
                    "description": "Epilogue",
                    "mask": 0xFFFFFFFF,
                    "ver": 1,
                },
                {
                    "reg_val": [0x10000006, 0x10000001],
                    "description": "Waiting for next epoch",
                    "mask": 0xFFFFFFFF,
                    "ver": 1,
                },
                {
                    "reg_val": [0x1FFFFFF1],
                    "description": "Done",
                    "mask": 0xFFFFFFFF,
                    "ver": 2,
                },
            ],
            self.BRISC_STATUS_REG_ADDR: [  # brisc
                {
                    "reg_val": [0xB0000000],
                    "description": "Stream restart check",
                    "mask": 0xFFFFF000,
                    "ver": 0,
                },
                {
                    "reg_val": [0xC0000000],
                    "description": "Check whether unpack stream has data",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xD0000000],
                    "description": "Clear unpack stream",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xE0000000],
                    "description": "Check and push pack stream that has data (TM ops only)",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xF0000000],
                    "description": "Reset intermediate streams",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0xF1000000],
                    "description": "Wait until all streams are idle",
                    "mask": 0xFFFFFFFF,
                    "ver": 0,
                },
                {
                    "reg_val": [0x21000000],
                    "description": "Waiting for next epoch",
                    "mask": 0xFFFFF000,
                    "ver": 1,
                },
                {
                    "reg_val": [0x10000001],
                    "description": "Waiting for next epoch",
                    "mask": 0xFFFFFFFF,
                    "ver": 1,
                },
                {
                    "reg_val": [0x22000000],
                    "description": "Done",
                    "mask": 0xFFFFFF00,
                    "ver": 2,
                },
            ],
        }

        if register_address in STATUS_REG:
            reg_value_desc_list = STATUS_REG[register_address]
            for reg_value_desc in reg_value_desc_list:
                mask = reg_value_desc["mask"]
                for reg_val_in_desc in reg_value_desc["reg_val"]:
                    if reg_value_on_chip & mask == reg_val_in_desc:
                        return [
                            reg_value_on_chip,
                            reg_value_desc["description"],
                            reg_value_desc["ver"],
                        ]
            return [reg_value_on_chip, "", 2]
        return []

    NCRISC_STATUS_REG_ADDR = NCRISC_STATUS_REG_ADDR
    BRISC_STATUS_REG_ADDR = BRISC_STATUS_REG_ADDR

    def read_stream_regs(self, loc, stream_id):
        return self.read_stream_regs_direct(loc, stream_id)

    def status_register_summary(self, addr, ver=0):
        coords = self.get_block_locations()
        status_descs = {}
        for loc in coords:
            status_descs[loc] = self.get_status_register_desc(
                addr, SERVER_IFC.pci_read_xy(self.id(), *loc.to("nocVirt"), 0, addr)
            )

        # Print register status
        status_descs_rows = []
        for loc in coords:
            if status_descs[loc] and status_descs[loc][2] <= ver:
                lxy = loc.to("nocTr")
                status_descs_rows.append(
                    [
                        f"{loc.to_str()}",
                        f"{status_descs[loc][0]:08x}",
                        f"{status_descs[loc][1]}",
                    ]
                )
        return status_descs_rows

    def pci_read_xy(self, x, y, noc_id, reg_addr):
        return SERVER_IFC.pci_read_xy(self.id(), x, y, noc_id, reg_addr)

    def pci_write_xy(self, x, y, noc_id, reg_addr, data):
        return SERVER_IFC.pci_write_xy(self.id(), x, y, noc_id, reg_addr, data)

    def pci_read_tile(self, x, y, z, reg_addr, msg_size, data_format):
        return SERVER_IFC.pci_read_tile(
            self.id(), x, y, z, reg_addr, msg_size, data_format
        )


# Initialize communication with the device. If the server is not running, it will be spawned.
def init_server_communication(server_cache, address, runtime_data_yaml_filename):
    DEBUDA_SERVER_CACHED_IFC.enabled = server_cache == "through" or server_cache == "on"
    DEBUDA_SERVER_SOCKET_IFC.enabled = (
        server_cache == "through" or server_cache == "off"
    )

    if DEBUDA_SERVER_CACHED_IFC.enabled:
        atexit.register(DEBUDA_SERVER_CACHED_IFC.save)
        DEBUDA_SERVER_CACHED_IFC.load()

    SERVER_IFC.spawning_debuda_stub = False
    if DEBUDA_SERVER_SOCKET_IFC.enabled:
        (ip, port) = address.split(":")
        spawning_debuda_stub = ip == "localhost" and util.is_port_available(int(port))

        if spawning_debuda_stub:
            success = spawn_standalone_debuda_stub(port, runtime_data_yaml_filename)
            if not success:
                raise Exception("Could not connect to debuda-server")
            SERVER_IFC.spawning_debuda_stub = True
        else:
            util.WARN(
                f"Port {port} is not available, assuming debuda-server is already running and we are connecting to it."
            )

        connect_to_server(ip=ip, port=port, spawning_debuda_stub=spawning_debuda_stub)

    return SERVER_IFC


# This is based on runtime_utils.cpp:get_soc_desc_path()
def get_soc_desc_path(chip, output_dir):
    if os.path.exists(os.path.join(output_dir, "device_desc_runtime", f"{chip}.yaml")):
        file_to_use = os.path.join(output_dir, "device_desc_runtime", f"{chip}.yaml")
    elif os.path.exists(os.path.join(output_dir, "device_descs")):
        file_to_use = os.path.join(output_dir, "device_descs", f"{chip}.yaml")
    else:
        file_to_use = os.path.join(output_dir, "device_desc.yaml")
    return file_to_use
