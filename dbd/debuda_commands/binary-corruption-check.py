# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
    bcc <core-loc> [-d <D>...] [ <verbose> ] 


Arguments:
<core-loc>   Core location in the form of x-y. Example: 1-1
  verbose    Verbosity level: if enabled will run command with function call tracing enabled.
             
Options:
  -d <D>        [Optional] Device ID. Default: current device  

Description:
  Given a device and core location, the binary corruption check will load the trisc0, trisc1, and trisc2 binaries from
  L1 and from the build directory and compare their contents. In the case of a mismatch, it will print the
  
  DRAM compares are not yet supported.
  
  Note that this command may fail if core is not running an op.

Examples:
  bcc 1-1
"""

command_metadata = {
    "short": "bcc",
    "long" : "binary-corruption-check",
    "type" : "high-level",
    "description": __doc__
}

import sys
from tabulate import tabulate
from debuda import UIState
from tt_object import TTObjectIDDict
import tt_util as util
import tt_device
from tt_graph import Queue
from tt_temporal_epoch import TemporalEpoch
from tt_coordinate import OnChipCoordinate
from docopt import docopt
from typing import List,Set,Dict,Any
from tt_stream import Stream
import subprocess
from collections import defaultdict, Counter
from enum import Enum
import tempfile
import random

bcc_verbose = 3
def LOG(s, **kwargs):
    if bcc_verbose > 1:
        util.PRINT (util.get_indent() + s, **kwargs)

def LOG2(s, **kwargs):
    if bcc_verbose > 2:
        util.PRINT (util.get_indent() + s, **kwargs)

def WARN(s, **kwargs):
    if bcc_verbose >= 0:
        util.WARN (util.get_indent() + s, **kwargs)

class EpochCmd:
    def __init__(self, EpochQueueCmd_val_to_str: Dict[int,str], epoch_cmd_payload: List[int]):
        # Fixme - make into separate subclasses based on epoch.
        self.cmd_str = "(raw)"
        cmd = (epoch_cmd_payload[1] >> 28) & 0xffff
        if cmd in EpochQueueCmd_val_to_str:
            self.cmd_str = EpochQueueCmd_val_to_str[cmd]
            if self.cmd_str == "Valid":
                self.address = int(epoch_cmd_payload[0])
                self.dram_x = (int(epoch_cmd_payload[1]) >> 16 & 0x3F)
                self.dram_y = (int(epoch_cmd_payload[1]) >> 22 & 0x3F)
            elif self.cmd_str == "NotValid":
                pass
            elif self.cmd_str == "EndProgram":
                pass
            elif self.cmd_str == "LoopStart":
                pass
            elif self.cmd_str == "LoopEnd":
                pass
            else:
                assert False, f"Command {self.cmd_str} support not implemented yet"
        
        self.epoch_cmd_payload = epoch_cmd_payload



class EpochCommandQueueReader:
    def __init__(self, context):
        runtime_data = context.netlist.runtime_data_yaml.root
        self.arch_name = runtime_data.get('arch_name').upper()
        self.EPOCH_Q_NUM_SLOTS = runtime_data.get('epoch_queue_num_slots', 64)
        self.DRAM_EPOCH_METADATA_LIMIT = runtime_data.get('dram_epoch_metadata_limit', 8 * 1024 * 1024)
        self.grid_size_row = runtime_data.get("grid_size_row", 12)
        self.grid_size_col = runtime_data.get("grid_size_col", 10)

        # From tt_epoch_dram_manager::tt_epoch_dram_manager and following the constants
        self.EPOCH_Q_SLOT_SIZE = 32
        self.EPOCH_Q_SLOTS_OFFSET = 32
        self.EPOCH_QUEUE_SIZE_BYTES = self.grid_size_row*self.grid_size_col*(self.EPOCH_Q_NUM_SLOTS*self.EPOCH_Q_SLOT_SIZE+self.EPOCH_Q_SLOTS_OFFSET)
        self.EPOCH_QUEUE_START_ADDR = self.DRAM_EPOCH_METADATA_LIMIT - self.EPOCH_QUEUE_SIZE_BYTES
        
        self.distribtued_eq = bool(runtime_data.get("distribute_epoch_tables", 1))
        
        if self.arch_name != "GRAYSKULL":
            # Get the enum mapping for EpochQueueCmd from ERISC ELF file
            self.EpochQueueCmd_val_to_str = context.elf.get_enum_mapping ("erisc_app.epoch_queue::EpochQueueCmd")
            # Remove "EpochCmd" from the value strings
            self.EpochQueueCmd_val_to_str = {k: v.replace("EpochCmd", "") for k, v in self.EpochQueueCmd_val_to_str.items()}
        else:
            self.EpochQueueCmd_val_to_str = None
        
    def compute_offset_of_table(self, core_loc: OnChipCoordinate) -> int:
        (x, y) = core_loc.to ("noc0")
        return (self.grid_size_col * y + x) * (self.EPOCH_Q_NUM_SLOTS * self.EPOCH_Q_SLOT_SIZE + self.EPOCH_Q_SLOTS_OFFSET)
        
    def get_rdptr_dram_addr(self, core_loc: OnChipCoordinate) -> int:
        offset = self.compute_offset_of_table(core_loc)
        return self.EPOCH_QUEUE_START_ADDR + offset
    
    def get_wrptr_dram_addr(self, core_loc: OnChipCoordinate) -> int:
        offset = self.compute_offset_of_table(core_loc)
        return self.EPOCH_QUEUE_START_ADDR + offset + 4

    def get_dram_core(self, device, core_loc: OnChipCoordinate) -> OnChipCoordinate:
        (x, y) = core_loc.to ("noc0")
        if self.distribtued_eq:
            dram_loc = device.get_t6_queue_location(self.arch_name, {'x': x, 'y': y})
        else:
            dram_loc = device.get_block_locations (block_type = "dram")[dram_chan]
        return dram_loc

    def get_address_at_rdptr(self, device, core_loc: OnChipCoordinate) -> int:
        dram_loc = self.get_dram_core(device, core_loc)
        dx, dy = dram_loc['x'], dram_loc['y']
        rdptr_addr = self.get_rdptr_dram_addr(core_loc)
        rdptr = device.pci_read_xy(dx, dy, 0, rdptr_addr) & 0xffff
        return self.EPOCH_QUEUE_START_ADDR + self.compute_offset_of_table(core_loc) + (self.EPOCH_Q_SLOT_SIZE * (rdptr % self.EPOCH_Q_NUM_SLOTS))

    def get_cmd_payload_at_rdptr(self, device, core_loc: OnChipCoordinate) -> int:
        addr_at_rdptr = self.get_address_at_rdptr(device, core_loc)
        dram_loc = self.get_dram_core(device, core_loc)
        dx, dy = dram_loc['x'], dram_loc['y']
        cmd_payload = []
        cmd_payload.append(device.pci_read_xy (dx, dy, 0, addr_at_rdptr))
        cmd_payload.append(device.pci_read_xy (dx, dy, 0, addr_at_rdptr+4))
        return cmd_payload



class NocAddress():
    def __init__(self, core_location: OnChipCoordinate, address: int):
        self.core_location = core_location
        self.address = address

class CoreBinaryOffsetsMap():
    # Offsets by type and core
    def __init__(self):
        self.dram: Dict[OnChipCoordinate, Dict[str,int]] = defaultdict(dict)
        self.l1 = Dict[OnChipCoordinate, Dict[str,int]] = defaultdict(dict)


def read_binary_from_device_memory(device, noc0_x: int, noc0_y: int, start_address: int, length_in_words: int) -> List[int]:
    """
    Reads a binary from the device memory. Returns a list of integers.
    """
    # Read the binary from the device memory
    # binary = device.read_memory(core_loc, start_address, length_in_words)
    loc = (noc0_x, noc0_y)
    assert len(loc) == 2
    binary_data = []
    for i in range(length_in_words):
        binary_data.append(device.pci_read_xy(loc[0], loc[1], 0, start_address + (4 * i)))
    return binary_data

def get_current_epoch_command(device, epoch_cmd_q_reader: EpochCommandQueueReader, worker_core_loc: OnChipCoordinate) -> int:
    ## DRAM offset of epoch binaries, pointed to by the epoch cmd @ the epoch cmd q rdptr
    cmd_payload = epoch_cmd_q_reader.get_cmd_payload_at_rdptr(device, worker_core_loc)
    epoch_cmd = EpochCmd(epoch_cmd_q_reader.EpochQueueCmd_val_to_str, cmd_payload)
    return epoch_cmd
    
def get_op_index(graph_directory_path, op_name) -> int:
    op_info_file_path = graph_directory_path + "/op_info.txt"
    with open(op_info_file_path, "r") as f:
        op_index_str, op_name_str = f.readline().split(":")
        op_name_str = op_name_str.strip()
        if op_name_str == op_name:
            return int(op_index_str.split("_")[1])
        
    assert False
    return None

def get_graph_name_for_op_on_core(context, device, core_loc: OnChipCoordinate) -> str:
    core_epoch = device.get_epoch_id(core_loc)
    graph_name = context.netlist.get_graph_name(core_epoch, device._id)
    return graph_name


def get_workload_build_directory(context) -> str:
    return context.netlist.rundir
    
def get_trisc_director(binary_type_str: str) -> str:
    core_type_str = None
    if binary_type == "trisc0":
        core_type_str = "tensix_thread0"
    elif binary_type == "trisc1":
        core_type_str = "tensix_thread1"
    elif binary_type == "trisc2":
        core_type_str = "tensix_thread2"
    else:
        throw(f"Unknown binary type {binary_type_str}")
        
    return core_type_str
    

def read_hex_binary(context, device, core_loc: OnChipCoordinate, binary_type: str):
    build_dir = get_workload_build_directory(context)
    
    graph_name = get_graph_name_for_op_on_core(context, device, core_loc)
    graph_directory_path = f"{build_dir}/graph_{graph_name}"
    
    # get the core type directory
    core_type_str = get_trisc_director(binary_type)
        
    # get the op directory
    graph = context.netlist.graph(graph_name)
    op_name = graph.location_to_op_name(core_loc)
    op_index = get_op_index(graph_directory_path, op_name)
    
    hex_file_path = f"{graph_directory_path}/op_{op_index}/{core_type_str}/{core_type_str}.hex"
    print(f"\tReading hex file from {hex_file_path}")
    
    hex_file_data = []
    with open(hex_file_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("@"):
                line = f"0x{line}"
                try: 
                    hex_file_data.append(int(line, 16))
                except:
                    breakpoint()
    
    return hex_file_data
    

def load_core_binaries(context, device, epoch_cmd_q_reader: EpochCommandQueueReader, worker_core_loc: OnChipCoordinate, binary_type: str):
    """Loads the binary data from the hex file, DRAM, and L1 for the specified core
    
    At the momemnt DRAM comparison is not supported.

    Args:
        context: debuda context
        device: silicon device handle
        epoch_cmd_q_reader: command queue reader for this commaand
        worker_core_loc (OnChipCoordinate): worker core to binary corruption check for 
        binary_type (str): which risc core we are checking for (accepted values are: "trisc0", "trisc1", "trisc2")

    Returns:
        tuple of list of binary data <hex, dram, L1>: Returns the binary data for the specified core from the hex file, DRAM, and L1
    """
    l1_offset = device.get_binary_l1_offset(worker_core_loc, binary_type)
    binary_offset_in_epoch = device.get_dram_binary_offset_in_epoch_command_queue(worker_core_loc, binary_type)
    binary_size_in_words = device.get_binary_size(worker_core_loc, binary_type) // 4
    
    print(f"\tReading L1 hex file core (x={worker_core_loc._noc0_coord[0]},y={worker_core_loc._noc0_coord[1]}) at {hex(l1_offset)}")
    raw_l1_binary = read_binary_from_device_memory(device, *worker_core_loc._noc0_coord, l1_offset, binary_size_in_words)
    dram_epoch_command = get_current_epoch_command(device, epoch_cmd_q_reader, worker_core_loc)
    print(f"\tReading L1 hex file core (x={dram_epoch_command.dram_x},y={dram_epoch_command.dram_y}) at {hex(dram_epoch_command.address)}")
    raw_dram_binary = read_binary_from_device_memory(device, dram_epoch_command.dram_x, dram_epoch_command.dram_y, dram_epoch_command.address, binary_size_in_words)
    
    raw_hex_binary = read_hex_binary(context, device, worker_core_loc, binary_type)
    
    return raw_hex_binary, raw_dram_binary, raw_l1_binary
    
def compare_dram_and_l1_binaries(build_dir_binary: List[int], dram_binary: List[int], l1_binary: List[int]) -> bool:
    """
    Compares two binaries and returns True if they are identical. False otherwise.
    
    At the moment, DRAM checks are disabled because there is a bug with it. L1 compares are working and DRAM compares
    are only needed when L1 checks fail so the DRAM comparison implementation is deferred.
    """
    matches = True
    offset = 0
    num_words = min(len(build_dir_binary), len(l1_binary))
    for offset in range(num_words):
        if build_dir_binary[offset] == 0:
            continue
        if build_dir_binary[offset] != l1_binary[offset]:
            if matches:
                matches = False
                WARN(f"Binaries do not match")
                WARN(f"Mismatches: (HEX | L1)")
                
            WARN(f"offset {offset * 4}: {hex(build_dir_binary[offset])} | {hex(l1_binary[offset])}")
    
    if matches:
        WARN(f"Binaries match")
    

def run(cmd_text, context, ui_state: UIState = None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    verbose = 0 if args['<verbose>'] is None else int(args['<verbose>'])
    global bcc_verbose
    bcc_verbose = verbose
    
    core_loc_str = args['<core-loc>']
    core_loc_str = args['<core-loc>']
    current_device_id = ui_state.current_device_id
    current_device = context.devices[current_device_id]
    core_loc = OnChipCoordinate.create(core_loc_str, device=current_device)
    
    epoch_cmd_q_reader = EpochCommandQueueReader(context)
    
    if verbose:
        # Turn on tracing for function calls in this module. This prints docstrings and function call argument values.
        # FIX: This is sticky across all calls of the same command
        util.decorate_all_module_functions_for_tracing(sys.modules[__name__])

    print("---------------------------Comparing trisc0 binaries---------------------------")
    hex_binary, trisc0_dram_binary, trisc0_l1_binary = load_core_binaries(context, current_device, epoch_cmd_q_reader, core_loc, "trisc0")
    compare_dram_and_l1_binaries(hex_binary, trisc0_dram_binary, trisc0_l1_binary)

    print("---------------------------Comparing trisc1 binaries---------------------------")
    hex_binary, trisc1_dram_binary, trisc1_l1_binary = load_core_binaries(context, current_device, epoch_cmd_q_reader, core_loc, "trisc1")
    compare_dram_and_l1_binaries(hex_binary, trisc1_dram_binary, trisc1_l1_binary)

    print("---------------------------Comparing trisc2 binaries---------------------------")
    hex_binary, trisc2_dram_binary, trisc2_l1_binary = load_core_binaries(context, current_device, epoch_cmd_q_reader, core_loc, "trisc2")
    compare_dram_and_l1_binaries(hex_binary, trisc2_dram_binary, trisc2_l1_binary)


    LOG(f"Running hang analysis (verbose={verbose})")