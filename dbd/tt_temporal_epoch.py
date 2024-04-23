# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from tt_object import TTObject, TTObjectIDDict
import tt_util as util
from tt_coordinate import OnChipCoordinate
from tt_stream import Stream
from tt_buffer import Buffer
from tt_pipe import Pipe
from tt_graph import Queue, Graph, Op
import itertools
from copy import copy
from collections import defaultdict

from typing import Dict, Sequence


class TemporalEpoch(TTObject):
    def __init__(self, id, netlist, pipegen_filename, blob_filename, graph_roots):
        # Pipegen.yaml is a special case since it contains multiple documents (separated by ---). It also depends on the
        # order of the documents. Each graph starts with "graph_name:" followed by graph's buffers. After all buffers are
        # loaded, we have an array of pipes.
        def post_process_pipegen_yaml(root):
            new_root = {}
            graphs = {}
            pipes = {}
            current_graph_name = None
            for i in root:
                if "graph_name" in i:
                    current_graph_name = i["graph_name"]
                    graphs[current_graph_name] = {}
                elif (
                    len(i.keys()) == 1 and "buffer" in list(i.keys())[0]
                ):  # Special case for buffers
                    graphs[current_graph_name] = {**graphs[current_graph_name], **i}
                elif (
                    len(i.keys()) == 1 and "pipe" in list(i.keys())[0]
                ):  # Special case for buffers
                    pipes = {**pipes, **i}
                else:
                    raise RuntimeError(
                        f"Cannot interpret {i} in file {self.pipegen_yaml.filepath}"
                    )
            new_root["graphs"] = graphs
            new_root["pipes"] = pipes
            return new_root

        self._id = id
        self.netlist = netlist  # Store netlist to have access to graphs.
        self.pipegen_yaml = util.YamlFile(pipegen_filename, post_process_pipegen_yaml)
        self.blob_yaml = util.YamlFile(blob_filename)
        self.graphs = None

        self.roots = graph_roots  # The entry in netlist file
        self.ops = TTObjectIDDict()
        self.op_device_map = {}
        self.op_name_id_map = {}

        for root in graph_roots:
            sorted_op_names = list(set(root.keys()) - Graph.non_op_keys)
            sorted_op_names.sort()
            for op_name in sorted_op_names:
                op = Op(self, op_name, root[op_name])
                id = op.id()
                self.ops[id] = op
                self.op_name_id_map[op_name] = id
                self.op_device_map[id] = root["target_device"]

    # Lazy loading of pipegen and blob files
    def load_pipegen(self):
        self.pipes = TTObjectIDDict()

        # map from core location Tuple[chip, x, y] -> list of buffer IDs
        self.core_buffers_map = defaultdict(list)
        self.buffers = TTObjectIDDict()
        self.buffer_q_map: Dict[int, QueueBufferMapQueue] = dict()

        pipes = self.pipegen_yaml.root["pipes"]

        for g_name, pipegen_graph in self.pipegen_yaml.root["graphs"].items():
            netlist_graph = self.netlist.graphs[g_name]
            netlist_graph.temporal_epoch.buffers = TTObjectIDDict()
            netlist_graph.temporal_epoch = self
            self.graphs.add(netlist_graph)

            for bid, val in pipegen_graph.items():
                b = Buffer(netlist_graph, val)
                netlist_graph.temporal_epoch.buffers.add(b)
                uniqid = val["uniqid"]

                ## IMPROVE: pull in the soc descriptor and query it for worker -> routing mapping
                self.buffers[b.id()] = b
                chip = int(val["chip_id"][0])
                x = int(val["core_coordinates"][1])
                y = int(val["core_coordinates"][0])
                if "ethernet_chan" not in val:
                    ## Hardcoded for now - should get from device instead
                    x = x + 2 if x > 3 else x + 1
                    y = y + 2 if y > 4 else y + 1

                self.core_buffers_map[(chip, x, y)].append(b.id())
                for r in range(
                    1, val["replicate"]
                ):  # Handle replicated buffers (see issue #326)
                    val_copy = copy(val)
                    val_copy["uniqid"] = uniqid + r * val["scatter_gather_num_tiles"]
                    b = Buffer(self, val_copy)
                    self.buffers[b.id()] = b
                    self.core_buffers_map[(chip, x, y)].append(b.id())
                    b.replicated = True

            for pid, val in pipes.items():
                p = Pipe(self, val)
                self.pipes[p.id()] = p
                output_buffers = val["output_list"]
                if isinstance(output_buffers[0], list):
                    output_buffers = tuple(
                        itertools.chain.from_iterable(output_buffers)
                    )

        def find_buffer_by_uniqid(uniqid):
            if uniqid in self.buffers:
                return self.buffers[uniqid]
            return None

        # Cache buffer to pipe and vice versa mapping
        for pipe_id, pipe in self.pipes.items():
            for input_buffer_id in pipe.root["input_list"]:
                input_buffer_id = input_buffer_id - (
                    input_buffer_id % 1000000000
                )  # Clear the offset
                b = self.buffers[input_buffer_id]
                # b = find_buffer_by_uniqid(input_buffer_id)
                if b is None:
                    assert b, "Cannot find buffer with uniqid {input_buffer_id}"
                b.input_of_pipes.add(pipe)
                pipe.input_buffers.add(b)
            for output_buffer_id_or_list in pipe.root["output_list"]:
                if isinstance(output_buffer_id_or_list, Sequence) and not isinstance(
                    output_buffer_id_or_list, str
                ):
                    # Flatten the list of lists
                    for buf_id in output_buffer_id_or_list:
                        b = find_buffer_by_uniqid(buf_id)
                        b.output_of_pipes.add(pipe)
                        pipe.output_buffers.add(b)
                else:  # This is a single buffer
                    b = find_buffer_by_uniqid(output_buffer_id_or_list)
                    b.output_of_pipes.add(pipe)
                    pipe.output_buffers.add(b)

        for b_id, b in self.buffers.items():
            ## Populate the queue buffer map
            is_dram_queue = (
                "dram_io_flag" in b.root and int(b.root["dram_io_flag"]) != 0
            )
            if is_dram_queue:
                temporal_epoch_id = int(self.id())

                target_device = int(b.root["chip_id"][0])
                dram_channel = int(b.root["dram_chan"])
                dram_addr = int(b.root["dram_addr"])
                # TODO: We need proper fix for not finding entry in self.netlist_addr_queue_map
                if (
                    target_device,
                    dram_channel,
                    dram_addr,
                ) in self.netlist._addr_queue_map:
                    queue_buffer_queue = self.netlist._addr_queue_map[
                        (target_device, dram_channel, dram_addr)
                    ]

                    self.buffer_q_map[b_id] = queue_buffer_queue
                    is_input_queue = len(b.input_of_pipes) > 0
                    is_output_queue = len(b.output_of_pipes) > 0
                    if is_input_queue:
                        queue_buffer_queue.add_consumer_buffer(b_id)

                    if is_output_queue:
                        queue_buffer_queue.set_producer_buffer(b_id)

    def load_blob(self):
        self.stream_source_map = defaultdict(dict)
        self.stream_loc_id_map = dict()
        self.streams = TTObjectIDDict()
        for key, val in self.blob_yaml.items():
            if key == "dram_blob":
                util.VERBOSE("- Skipping dram_blob")
            elif key == "dram_perf_dump_blob":
                util.VERBOSE("- Skipping dram_perf_dump_blob")
            elif key == "overlay_blob_extra_size":
                util.VERBOSE("- Skipping overlay_blob_extra_size")
            elif key == "global_info_blob":
                util.VERBOSE("- Skipping global_info_blob")
            elif key.startswith("phase_"):
                phase_id = int(key[6:])  # Skip "phase_" string to get the id
                for stream_designator, stream_data in val.items():
                    phase_id = (
                        phase_id & 0xFFFFFFFF
                    )  # Lower 32 bits are phase, upper 32 bits are epoch
                    stream_data["phase_id"] = phase_id
                    s = Stream(self, stream_designator, stream_data)
                    self.streams.add(s)
                    self.stream_loc_id_map[stream_designator] = s.id()
                    if "buf_id" in s.root:
                        self.buffers[int(s.root["buf_id"])].stream_id = s.stream_id()
                    if "pipe_id" in s.root:
                        self.pipes[int(s.root["pipe_id"])].stream_id = s.stream_id()

                    # # Add the stream to the corresponding buffer
                    # if "buf_id" in s.root:
                    #     if s.root["buf_id"] not in self.buffers:
                    #         util.WARN (f"Stream {s.id()} refers to buffer {s.root['buf_id']} which is not in pipegen.yaml")
                    #     if self.buffers[s.root["buf_id"]].stream_id is not None:
                    #         util.WARN (f"Stream {s.id()} refers to buffer {s.root['buf_id']} which is already in use by stream {self.buffers[s.root['buf_id']].stream_id}")
                    #     self.buffers[s.root["buf_id"]].stream_id = s.id()
            else:
                raise RuntimeError(
                    f"{self.blob_yaml.id()}: Cannot interpret {key}: {val}"
                )

        ## Populate stream_source_map
        for key, val in self.blob_yaml.items():
            if key == "dram_blob":
                util.VERBOSE("- Skipping dram_blob")
            elif key == "dram_perf_dump_blob":
                util.VERBOSE("- Skipping dram_perf_dump_blob")
            elif key == "overlay_blob_extra_size":
                util.VERBOSE("- Skipping overlay_blob_extra_size")
            elif key.startswith("phase_"):
                phase_id = int(key[6:])  # Skip "phase_" string to get the id
                for stream_designator, stream_data in val.items():
                    stream_id = self.stream_loc_id_map[stream_designator]
                    if "dest" not in stream_data:
                        continue

                    dest_stream_designators = copy(stream_data["dest"])
                    assert isinstance(dest_stream_designators, Sequence)

                    is_multicast = len(dest_stream_designators) > 1
                    if is_multicast:
                        # for multicast, we expect 2 dest entries, denoting the corners of the multicast rectangle/strip
                        assert len(dest_stream_designators) == 2
                        chip = int(
                            dest_stream_designators[0].split("__")[0].split("_")[1]
                        )
                        stream_id_number = int(
                            dest_stream_designators[0].split("__")[3].split("_")[2]
                        )
                        rect_start_x = int(
                            dest_stream_designators[0].split("__")[2].split("_")[1]
                        )
                        rect_start_y = int(
                            dest_stream_designators[0].split("__")[1].split("_")[1]
                        )
                        rect_end_x = int(
                            dest_stream_designators[1].split("__")[2].split("_")[1]
                        )
                        rect_end_y = int(
                            dest_stream_designators[1].split("__")[1].split("_")[1]
                        )

                        dest_stream_designators = [
                            f"chip_{chip}__y_{y}__x_{x}__stream_id_{stream_id_number}"
                            for x in range(rect_start_x, rect_end_x + 1)
                            for y in range(rect_start_y, rect_end_y + 1)
                        ]

                    for dest_stream_designator in dest_stream_designators:
                        dest_stream_id = self.stream_loc_id_map[dest_stream_designator]
                        self.stream_source_map[dest_stream_id][phase_id] = stream_id

    # Given a list of core coordinates, returns all buffers residing at those coordinates
    def get_core_buffers(self, core_coordinates_list_rc):
        if isinstance(core_coordinates_list_rc, OnChipCoordinate):
            loc = core_coordinates_list_rc  # rename
            device = loc._device
            buffers = {
                b_id: self.buffers[b_id]
                for b_id in self.core_buffers_map[
                    (device.id(), loc._noc0_coord[0], loc._noc0_coord[1])
                ]
            }
            return buffers
            for b_id, b in self.buffers.items():
                y = b.root["core_coordinates"][0]
                x = b.root["core_coordinates"][1]
                if "ethernet_chan" in b.root:
                    # ethernet_chan indicates the coordinates are noc0 coordinates
                    if (
                        y == loc._noc0_coord[1]
                        and x == loc._noc0_coord[0]
                        and device.id() == b.root["chip_id"][0]
                    ):
                        buffers[b_id] = b
                else:
                    # interpret coordinates as tensix workers
                    tensix_y, tensix_x = loc.to("tensix")
                    if (
                        y == tensix_y
                        and x == tensix_x
                        and device.id() == b.root["chip_id"][0]
                    ):
                        buffers[b_id] = b

            return buffers

        else:
            if type(core_coordinates_list_rc) != list:
                core_coordinates_list_rc = [
                    core_coordinates_list_rc
                ]  # If not a list, assume a single buffer id, and create a list from it

            buffer_set = util.set()
            for b_id, b in self.buffers.items():
                if b.root["core_coordinates"] in core_coordinates_list_rc:
                    buffer_set.add(b.root["uniqid"])
            return list(buffer_set)

    # in_or_out refers to the buffer being an input of a pipe or an output
    def get_buffers(self, where, in_or_out="all"):
        #### Only use where is Pipe and in_or_out=="all" whben called from ha_v2
        ret_val = TTObjectIDDict()
        if type(where) == str or type(where) == int:
            expected_id = int(where)
            ret_val.update(
                {b.id(): b for b_id, b in self.buffers.items() if b.id() == expected_id}
            )
        elif type(where) == Pipe:
            if in_or_out == "all" or in_or_out == "in":
                ret_val.update(
                    {
                        b.id(): b
                        for b_id, b in self.buffers.items()
                        if b.id() in where.input_buffers
                    }
                )
            if in_or_out == "all" or in_or_out == "out":
                ret_val.update(
                    {
                        b.id(): b
                        for b_id, b in self.buffers.items()
                        if b.id() in where.output_buffers
                    }
                )
        elif type(where) == Op or type(where) == Queue:
            if in_or_out == "all" or in_or_out == "in":
                for b_id, b in self.buffers.items():
                    if b.root["md_op_name"] == where.id() and b.is_input_of_pipe():
                        ret_val.add(b)
            if in_or_out == "all" or in_or_out == "out":
                for b_id, b in self.buffers.items():
                    if b.root["md_op_name"] == where.id() and b.is_output_of_pipe():
                        ret_val.add(b)
        elif util.is_iterable(where):
            if isinstance(where, dict):
                where = where.values()
            for o in where:
                ret_val.update(self.get_buffers(o, in_or_out))
        else:
            raise TypeError(f"Usupported object type: {type(where)}")
        return ret_val

    def get_buffer_input_pipe(self, where):
        ret_val = TTObjectIDDict()
        assert type(where) == Buffer
        for p_id, p in self.pipes.items():
            if where.id() in p.output_buffers:
                return p_id
        return None

    def get_buffer_output_pipes(self, where):
        ret_val = TTObjectIDDict()
        assert type(where) == Buffer
        ret_val.update(
            {
                p.id(): p
                for p_id, p in self.pipes.items()
                if where.id() in p.input_buffers
            }
        )
        return ret_val

    def get_fanout_buffer_level(self, where):
        if type(where) == Buffer:
            assert self.is_src_buffer(where), "fanout is valid only for src buffers"
            pipes = {
                p
                for p_id, p in self.pipes.items()
                if where.id() in p.root["input_list"]
            }
            dest_buffers = self.get_buffers(pipes)
            dest_buffers.keep(self.is_dest_buffer)
            return dest_buffers
        else:
            raise TypeError(f"Usupported object type: {type(where)}")

    def __getattr__(self, name):
        if name in ["pipes", "buffers"]:
            self.load_pipegen()
        elif name in ["streams", "stream_loc_id_map", "first_blob_of_each_stream"]:
            self.load_blob()
        return object.__getattribute__(self, name)
