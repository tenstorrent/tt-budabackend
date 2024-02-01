# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
import os
from tt_object import TTObjectIDDict
import tt_util as util
from tt_graph import Graph, Queue
from tt_temporal_epoch import TemporalEpoch

from copy import copy
from collections import defaultdict


class QueueBufferMapQueue:
    def __init__(self, queue: Queue, grid_r, grid_c):
        self.queue = queue
        self.grid_r = None
        self.grid_c = None
        self.producer_buffer_id = None
        self.consumer_buffers = set()

    def id(self):
        return (self._queue.id(), self.grid_r, self.grid_c)

    def __hash__(self):
        return self.id()

    def set_producer_buffer(self, buffer_id):
        # TODO: assert self.producer_buffer_id is None
        self.producer_buffer_id = buffer_id

    def has_op_producer(self):
        return self.producer_buffer_id is not None

    def add_consumer_buffer(self, buffer_id):
        self.consumer_buffers.add(buffer_id)


class QueueBufferMap:
    def __init__(self):
        self.location_queue_map: Dict[QueueBufferMapQueue] = dict()
        self.queue_names = dict()
        self.queue_producer_map = dict()
        pass

    def get_queue_producer_buffer(self, queue_name, grid_r, grid_c):
        # Returns a tuple (temporal epoch id, buffer id)
        return self.queue_producer_map[queue_name][grid_r][grid_c]

    def get_queue_name(self, chip_id, dram_channel, dram_addr, temporal_epoch):
        return self.queue_names[(chip_id, dram_channel, dram_addr, temporal_epoch)]

    def get_queue_buffer(
        self, chip_id: int, dram_channel: int, dram_addr: int, temporal_epoch: int
    ):
        return self.location_queue_map[
            (chip_id, dram_channel, dram_addr, temporal_epoch)
        ]

    def add_queue_buffer(
        self,
        chip_id: int,
        dram_channel: int,
        dram_addr: int,
        temporal_epoch: int,
        queue: QueueBufferMapQueue,
    ):
        self.location_queue_map[(chip_id, dram_channel, dram_addr, temporal_epoch)] = (
            queue
        )

    def set_queue_producer_buffer(
        self, queue: QueueBufferMapQueue, producer_buffer_id: int
    ):
        self.location_queue_map[
            (chip_id, dram_channel, dram_addr, temporal_epoch)
        ].set_producer_buffer(producer_buffer_id)

    def add_queue_output_buffer(
        self,
        chip_id: int,
        dram_channel: int,
        dram_addr: int,
        temporal_epoch: int,
        buffer_id: int,
    ):
        self.location_queue_map[
            (chip_id, dram_channel, dram_addr, temporal_epoch)
        ].add_consumer_buffer(buffer_id)


# Wrapper for Buda run netlist.yaml and, currently, runtime_data.yaml files
class Netlist:
    def load_netlist_data(self):
        # 1. Cache epoch id, device id and graph names
        self._epoch_id_to_graph_names_map = dict()
        self._epoch_ids = util.set()
        self._map_graph_names = dict()
        self._name_consumers_map = defaultdict(list)
        self._op_epoch_map = dict()

        self._addr_queue_map = dict()

        for graph_name in self.graph_names():
            epoch_id = self.graph_name_to_epoch_id(graph_name)
            device_id = self.graph_name_to_device_id(graph_name)
            if device_id not in self._map_graph_names:
                self._map_graph_names[device_id] = dict()
            self._map_graph_names[device_id][epoch_id] = graph_name

            # assert (epoch_id not in self._epoch_ids)  # We do not support multiple graphs in the same epoch
            self._epoch_ids.add(epoch_id)

            if epoch_id not in self._epoch_id_to_graph_names_map:
                self._epoch_id_to_graph_names_map[epoch_id] = util.set()
            self._epoch_id_to_graph_names_map[epoch_id].add(graph_name)

        self._epoch_ids = list(self._epoch_ids)
        self._epoch_ids.sort()

        # 2. Load queues
        self._queues = TTObjectIDDict()
        for queue_name in self.queue_names():
            queue = Queue(queue_name, self.yaml_file.root["queues"][queue_name])
            self._queues[queue.id()] = queue
            q_yaml = self.yaml_file.root["queues"][queue_name]
            self._name_consumers_map[q_yaml["input"]].append(queue_name)

            is_in_dram = q_yaml["loc"] == "dram"
            if is_in_dram:
                target_device = q_yaml["target_device"]
                grid_size_r, grid_size_c = q_yaml["grid_size"]
                grid_r = 0
                grid_c = 0
                for dram_channel, dram_addr in q_yaml["dram"]:
                    self._addr_queue_map[(target_device, dram_channel, dram_addr)] = (
                        QueueBufferMapQueue(queue, grid_r, grid_c)
                    )
                    grid_c += 1
                    assert grid_r < grid_size_r
                    if grid_c == grid_size_c:
                        grid_c = 0
                        grid_r += 1

    # Initializes, but does not yet load pipegen and blob files
    def load_temporal_epochs(self, rundir):
        self.temporal_graphs = TTObjectIDDict()
        self.graphs = TTObjectIDDict()

        # 0. Create graphs
        for graph_name in self.graph_names():
            # Create the graph
            g = Graph(self, graph_name, self.yaml_file.root["graphs"][graph_name])
            self.graphs.add(g)

        # 1. Iterate over all temporal epochs
        #   a. Create a map from epoch id to all the graphs in the epoch
        graph_to_epoch_map = self.runtime_data_yaml.root["graph_to_epoch_map"]
        epoch_to_graph_list_map = dict()
        for graph_name, graph_info in graph_to_epoch_map.items():
            epoch_id = graph_info["epoch_id"]
            if epoch_id not in epoch_to_graph_list_map:
                epoch_to_graph_list_map[epoch_id] = list()
            epoch_to_graph_list_map[epoch_id].append(graph_name)

        #  b. Create a TemporalEpoch object for each epoch and link with graphs
        for epoch_id, graph_list in epoch_to_graph_list_map.items():
            graph_dir = f"{rundir}/temporal_epoch_{epoch_id}"
            if not os.path.isdir(graph_dir):
                util.FATAL(f"Error: cannot find directory {graph_dir}")

            pipegen_file = f"{graph_dir}/overlay/pipegen.yaml"
            blob_file = f"{graph_dir}/overlay/blob.yaml"
            te = TemporalEpoch(
                epoch_id,
                self,
                pipegen_file,
                blob_file,
                [self.graph(g_name).root for g_name in graph_list],
            )
            te.graphs = TTObjectIDDict()
            for g_name in graph_list:
                g = self.graph(g_name)
                te.graphs.add(g)
                g.temporal_epoch = te
            self.temporal_epochs.add(te)

            for op_name, op in te.ops.items():
                self._op_epoch_map[op_name] = te.id()
                for input_name in op.root["inputs"]:
                    self._name_consumers_map[input_name].append(op.id())

    def __init__(self, netlist_filepath, rundir, runtime_data_yaml):
        # 1. Set the file. It will be lazy loaded on first access
        assert runtime_data_yaml is not None
        self.runtime_data_yaml = runtime_data_yaml

        if netlist_filepath is None:
            netlist_filepath = self.get_netlist_path()

        # 2. Load the netlist itself
        self.yaml_file = util.YamlFile(netlist_filepath)
        self.load_netlist_data()

        # 3. Load pipegen/blob yamls
        self.temporal_epochs = TTObjectIDDict()
        self.load_temporal_epochs(rundir)

        # 4. Cache the output_ops for each queue
        all_queue_ids = self._queues.keys()
        for graph_id, graph in self.graphs.items():
            for op_id, op in graph.ops.items():
                for input in op.root["inputs"]:
                    if input in all_queue_ids:
                        self._queues[input].output_ops.add(op)

    # Accessors
    def epoch_ids(self):
        return self._epoch_ids

    # Returns names of graphs directly from the netlist yaml file
    def graph_names(self):
        return self.yaml_file.root["graphs"].keys()

    # Returns names of queues directly from the netlist yaml file
    def queue_names(self):
        return self.yaml_file.root["queues"].keys()

    # Given a graph name, returns the epoch id directly from the runtime_data yaml file
    def graph_name_to_epoch_id(self, graph_name):
        return self.runtime_data_yaml.root["graph_to_epoch_map"][graph_name]["epoch_id"]

    # Given a graph name, returns the device id directly from the runtime_data yaml file
    def graph_name_to_device_id(self, graph_name):
        return (
            self.runtime_data_yaml.root["graph_to_epoch_map"][graph_name][
                "target_device"
            ]
            if graph_name in self.runtime_data_yaml.root["graph_to_epoch_map"]
            else None
        )

    def epoch_id_to_graph_names(self, epoch_id):
        return (
            self._epoch_id_to_graph_names_map[epoch_id]
            if epoch_id in self._epoch_id_to_graph_names_map
            else None
        )

    def graph(self, graph_name):
        return self.graphs[graph_name]

    def temporal_graph(self, graph_name):
        return self.temporal_graphs[graph_name]

    def get_graph_name(self, epoch_id, device_id):
        if device_id in self._map_graph_names:
            if epoch_id in self._map_graph_names[device_id]:
                return self._map_graph_names[device_id][epoch_id]
        return None

    def get_temporal_graph_name(self, epoch_id):
        epoch_id = int(epoch_id)
        if epoch_id in self.temporal_graphs:
            return self.temporal_graphs[epoch_id]
        return None

    def graph_by_epoch_and_device(self, epoch_id, device_id):
        graph_name = self.get_graph_name(epoch_id, device_id)
        if graph_name:
            return self.graph(graph_name)
        return None

    def device_graph_names(self):
        return self._map_graph_names

    def devices(self):
        return self.yaml_file.root["devices"]

    def queue(self, queue_name):
        return self._queues[queue_name]

    def queues(self):
        return self._queues

    # Determines the architecture
    def get_arch(self):
        if "arch_name" in self.runtime_data_yaml.root:
            return self.runtime_data_yaml.root["arch_name"]
        return None

    # Returns all device ids used by the graphs and the queues in the netlist
    def get_device_ids(self):
        device_ids = util.set(
            q["target_device"] for _, q in self.yaml_file.root["queues"].items()
        )
        device_ids.update(
            util.set(
                g["target_device"] for _, g in self.yaml_file.root["graphs"].items()
            )
        )
        return device_ids

    # Determines the netlist file path
    def get_netlist_path(self):
        if "netlist_path" in self.runtime_data_yaml.root:
            return self.runtime_data_yaml.root["netlist_path"]
        return None

    # Renderer
    def __str__(self):
        return f"{type(self).__name__}: {self.yaml_file.filepath}. Graphs({len(self.graph_names())}): {' '.join (self.graph_names())}"

    def __repr__(self):
        return self.__str__()
