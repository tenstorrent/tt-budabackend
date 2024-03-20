#!/usr/bin/env python3
# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0

import os, yaml, json
from dataclasses import dataclass, field
from typing import Dict, List
from optparse import OptionParser

optparser = OptionParser()
optparser.add_option("--dump-silicon-events",
                    action="store_true",
                    dest = "silicon_events",
                    help = "If set, silicon perf event regs will be dumped first.")
optparser.add_option('--x',
                dest = "core_x",
                type=int,
                default=1,
                help = "If dump-silicon-events is set, --x determines the x-coor of target core.")
optparser.add_option('--y',
                dest = "core_y",
                type=int,
                default=1,
                help = "If dump-silicon-events is set, --y determines the y-coor of target core.")
optparser.add_option("--get-ops-names",
                action="store_true",
                dest = "get_ops_names",
                help = "If set, the script will search for the cores_to_ops_map yaml file to get the op name running on each core.")

(options, args) = optparser.parse_args()

if options.silicon_events:
    from tenstorrent import chip

thread_name_to_id = {
    'T0': 0,
    'T1': 1,
    'T2': 2,
}

EventType = {
    1: "wait-for-incoming-tiles",
    2: "wait-for-free-tiles",
    3: "packer-each-input",
    4: "math-perf-counter",
}

@dataclass
class EventInfo():
    name = ''
    operand_idx = ''
    num_tiles = ''
    outer_loop_idx = ''

    def gen_event_description(self):    
        event_str = self.name
        if self.outer_loop_idx != '':
            event_str += f"-outer-loop-{self.outer_loop_idx}"
        if self.name == EventType[1] or self.name == EventType[2]:
            if self.operand_idx != '':
                event_str += f"-operand-{self.operand_idx}"
            if self.num_tiles != '':
                event_str += f"-num-tiles-{self.num_tiles}"
        return event_str

PERF_LIB_PATH = os.path.dirname(os.path.realpath(__file__))
ROOT_PATH = PERF_LIB_PATH + "/../../"

NumEventsPerThread = 64
NumValuesPerEventUnpackPack = 2 # Event_id and its value
NumValuesPerEventMath = 4 # Event_id and its value
PerfValFirst = 0xBEEFF00D
PerfValLast = 0xBEEFF00D
DefaultEventValue = 0
BytesPerEvent = 4

NocId = 0
TriscPerfBufSize = 4*1024
PerfBufBaseAddr = 8*1024

@dataclass(init=True, repr=True)
class Event():
    id: int = field(init=True, default=-1)
    description: str = field(init=False, default='')
    start: int = field(init=True, default=-1)
    end: int = field(init=False, default=-1)

@dataclass(init=True, repr=True)
class Thread():
    thread_id: int = field(init=True, default=-1)
    events: Dict[int, List[Event]] = field(init=False, default_factory=dict) # Maximum NumEventsPerThread/4 events per thread.

@dataclass(init=True, repr=True)
class Core():
    core_x: int = field(init=True, default = -1)
    core_y: int = field(init=True, default = -1)
    threads: Dict[str, Thread] = field(init=False, default_factory=lambda: {i: Thread(thread_id = i) for i in thread_name_to_id})
    first_unpack: int = field(init=False, default=-1)
    last_recorded_pack: int = field(init=False, default=-1)

def decode_event_name(val: int, thread_id: int):
    event_id = (val >> 16) & 0xff
    if event_id not in EventType.keys():
        return val

    event_info = EventInfo()
    event_info.name = EventType[event_id]
    
    if thread_id == 'T0' or thread_id == 'T2':
        event_info.outer_loop_idx = (val >> 24) & 0xff
        event_info.num_tiles = (val >> 8) & 0xff
        event_info.operand_idx = val & 0xff
        # print("event_id = ", event_id, " num_tiles = ", event_info.num_tiles, " op-idx = ", event_info.operand_idx)
    else:
        event_info.outer_loop_idx = (val >> 24) & 0xff

    return event_info.gen_event_description()

# Workaround for the issue where in silicon yaml perf dump, hex values are dumped as strings.
def conver_str_to_hex(values):
    for value_idx, value in enumerate(values):
        if type(value) == str:
            values[value_idx] = int(value, 16)

class EventTracker():

    def __init__(self):
        
        self.core_thread_timestamp = {}
        self.id_to_event = {}
        self.cores = {}
        self.cores_to_ops = {}
        self.num_epochs = None

    def calculate_first_to_last_outer_loop_cycles(self):
        for core_id, core in self.cores.items():
            for event_id, event in core.threads["T0"].events.items():
                outer_loop_idx = (event_id >> 24) & 0xff
                event_type = (event_id >> 16) & 0xff
                if event_type == 1 and outer_loop_idx == 0:
                    core.first_unpack = event[0].end
                    break
            
            packer_max_outer_loop = -1
            for event_id, event in core.threads["T2"].events.items():
                outer_loop_idx = (event_id >> 24) & 0xff
                event_type = (event_id >> 16) & 0xff
                if event_type == 3 and outer_loop_idx > packer_max_outer_loop:
                    core.last_recorded_pack = event[-1].end
                    packer_max_outer_loop = outer_loop_idx
    
    def parse_yaml(self):

        if options.silicon_events:
            data = yaml.safe_load(open(f"{ROOT_PATH}/silicon_perf_dump.yaml"))
        else:
            data = yaml.safe_load(open(f"{ROOT_PATH}/versim_perf_dump.yaml"))
        if options.get_ops_names:
            self.parse_cores_to_ops_map()
        for core_id, thread_info in data.items():
            TT_ASSERT len(core_id) <= 5, "core ids must have corex-corey format"
            delim_pos = core_id.find("-")
            core_y = int(core_id[:delim_pos])
            core_x = int(core_id[delim_pos+1:])
            current_core = Core(core_x = core_x, core_y = core_y)
            TT_ASSERT len(thread_info) == len(current_core.threads), "Number of threads in versim dump must be the same as the number of threads created in Core object."
            for thread_id, values in thread_info.items():
                # Workaround for the issue where in silicon yaml perf dump, hex values are dumped as strings.
                if type(values[0]) == str:
                    conver_str_to_hex(values)
                
                if values[0] != PerfValFirst:
                    continue
                current_thread = Thread(thread_id = thread_name_to_id[thread_id])
                len(values) == NumEventsPerThread
                current_core.threads[thread_id]
                # Record events in this map until both start/end values are found.
                # Afterwards, pop that key and push that into the threads.events dict.
                temp_events = {}
                for val_idx, val in enumerate(values):

                    # The first value is always PerfValFirst
                    if val_idx == 0:
                        if val == PerfValFirst:
                            continue
                        # If first value is not set to PerfValFirst, the current thread has not recorded any events.
                        else:
                            break
                    
                    # After the first register, if PerfValLast is seen, we have reached the end of the events.
                    if val == PerfValLast:
                        break
                    
                    if thread_id == "T1":
                        if val_idx % NumValuesPerEventMath == 0:
                            event_id = values[val_idx + 2]
                            print("core-id = ", core_id, " thread-id = ", thread_id, " event_idx = ", val_idx, " event_id = ", event_id)
                            if event_id in current_thread.events:
                                current_thread.events[event_id].append(Event(id = event_id, start = val))
                            else:
                                current_thread.events[event_id] = [Event(id = event_id, start = val)]
                            prev_idx = val_idx - 4 if val_idx - 4 > 0 else None
                            fpu_counter = values[val_idx + 1]
                            if prev_idx is not None:
                                fpu_counter -= values[val_idx - 3]
                            current_thread.events[event_id][-1].end = fpu_counter
                            current_thread.events[event_id][-1].description = decode_event_name(event_id, thread_id)
                    else:
                        # The very first value in register set is always PerfValFirst.
                        # The event labels will be on 1-3-5-...
                        if val_idx % NumValuesPerEventUnpackPack == 1:
                            
                            print("core-id = ", core_id, " thread-id = ", thread_id, " event_idx = ", val_idx, " event_id = ", val)
                            if val in temp_events:
                                TT_ASSERT temp_events[val].start >=0 and temp_events[val].end == -1
                                temp_events[val].end = values[val_idx + 1]
                                if val in current_thread.events:
                                    current_thread.events[val].append(temp_events.pop(val))
                                else:
                                    current_thread.events[val] = [temp_events.pop(val)]
                            else:
                                temp_events[val] = Event(id = val, start = values[val_idx + 1])
                                temp_events[val].description = decode_event_name(val, thread_id)

                if len(temp_events):
                    print(f"WARNING: Core {core_id}, thread {thread_id}, there is no end timestamps recorded for the following event_ids: {temp_events.keys()}")
                    for event_id in temp_events:
                        if event_id in current_thread.events:
                            current_thread.events[event_id].append(temp_events[event_id])
                        else:
                            current_thread.events[event_id] = [temp_events[event_id]]
                
                current_core.threads[thread_id] = current_thread
            self.cores[core_id] = current_core
        
        self.calculate_first_to_last_outer_loop_cycles()
        for core_id, core in self.cores.items():
            current_core_map = {}
            for thread_id, thread in core.threads.items():
                event_types = {}
                if thread_id == "T1":
                    first_val_name = "total activity"
                    second_val_name = "fpu activity"
                    third_val_name = "math utilization"
                else:
                    first_val_name = "start"
                    second_val_name = "end"
                    third_val_name = "diff"
                for event_id in thread.events:
                    event_ranges = {first_val_name : [], second_val_name: [], third_val_name: []}
                    event_description = thread.events[event_id][0].description
                    for event in thread.events[event_id]:
                        event_ranges[first_val_name].append(event.start)
                        event_ranges[second_val_name].append(event.end)
                        event_ranges[third_val_name].append(event.end / event.start if thread_id == "T1" else event.end - event.start)
                
                    event_types[event_description] = event_ranges
                current_core_map[thread_id] = event_types
            
            per_core_perf_events = {
                "unpacker-first-block-data-available": core.first_unpack if core.first_unpack != -1 else "Not recorded",
                "pack-finish-last-outer-loop": core.last_recorded_pack if core.last_recorded_pack != -1 else "Not recorded",
                "diff": core.last_recorded_pack - core.first_unpack if core.first_unpack != -1 and core.last_recorded_pack != -1 else "Not recorded",
            }
            current_core_map["per-thread-events"] = per_core_perf_events
            
            core_id_key = core_id
            if options.get_ops_names:
                if core_id in self.cores_to_ops:
                    core_id_key += "-" + self.cores_to_ops[core_id]
            self.core_thread_timestamp[core_id_key] = current_core_map

    def dump_json(self):
        
        output_path = f"{PERF_LIB_PATH}/perf_dump_postprocess.json"
        with open(output_path, 'w') as f:
            json.dump(self.core_thread_timestamp, f, indent=2)
            print("Successfully dumped the output json file in the following path:")
            print(f"--- {output_path}")

    def parse_cores_to_ops_map(self):
        file_exist = False
        if os.path.isfile(f"{ROOT_PATH}/cores_to_ops_map.yaml"):
            file_exist = True
        if options.get_ops_names:
            TT_ASSERT file_exist == True, "If get_ops_names option is set, cores_to_ops_map yaml file should exist."
        cores_to_ops = yaml.safe_load(open(f"{ROOT_PATH}/cores_to_ops_map.yaml"))
        self.num_epochs = cores_to_ops["Number_of_Epochs"]
        # Currently this script only supports a single epoch
        self.cores_to_ops = cores_to_ops["Epoch_Number_0"]

def dump_silicon_regs():
    my_chip = chip.Chip.create_chip ('grayskull', 'pci', 'direct')
    my_chip.setup_interface()
    my_chip.ttx_run_prepare(force_tensix_reset = False)
    
    silicon_yaml_dump = {}
    if os.path.isfile(f"{ROOT_PATH}/cores_to_ops_map.yaml"):
        cores_to_ops = yaml.safe_load(open(f"{ROOT_PATH}/cores_to_ops_map.yaml"))
        cores_to_ops = cores_to_ops["Epoch_Number_0"]
    else:
        cores_to_ops = {f"{options.core_x}-{options.core_y}": ""}
    for core_id in cores_to_ops:
        delim_pos = core_id.find("-")
        core_x = int(core_id[:delim_pos])
        core_y = int(core_id[delim_pos+1:])
        print(f"Reading events from core_y = {core_y}, core_x = {core_x}")
        silicon_yaml_dump[core_id] = {thread: [] for thread in thread_name_to_id}
        total_num_events_per_thread = TriscPerfBufSize // BytesPerEvent

        for thread, thread_id in thread_name_to_id.items():
            
            addr_base = PerfBufBaseAddr + thread_id * TriscPerfBufSize
            for event_idx in range(total_num_events_per_thread):
                event_val = my_chip.pci_read_xy(core_x, core_y, NocId, addr_base + event_idx * BytesPerEvent)
                silicon_yaml_dump[core_id][thread].append("0x{:08X}".format(event_val))
        
    with open(f"{ROOT_PATH}/silicon_perf_dump.yaml", "w") as f:
        yaml.dump(silicon_yaml_dump, f, Dumper=yaml.CSafeDumper)

def parse_perf_yaml_and_dump_events():
    tracker = EventTracker()
    tracker.parse_yaml()
    tracker.dump_json()

if __name__ == '__main__':

    if options.silicon_events:
        dump_silicon_regs()
    
    parse_perf_yaml_and_dump_events()
