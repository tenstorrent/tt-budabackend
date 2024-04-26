# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  ha_v2 [ <verbose> ]

Arguments:
  verbose    Verbosity level (0 or 1) [default: 0]

Description:
  Traverses all devices looking for the point of failure in the netlist execution. The traversal is
  done in topological order with the intention of finding the first problematic core/stream.

Examples:
  ha_v2
"""

import sys
from tabulate import tabulate
from tt_object import TTObjectIDDict
import tt_util as util
import tt_device
from tt_graph import Queue
from tt_temporal_epoch import TemporalEpoch
from tt_coordinate import OnChipCoordinate
from docopt import docopt
from typing import List, Sequence, Set, Dict, Any
from tt_stream import Stream
import subprocess
from collections import defaultdict, Counter
from enum import Enum
import tempfile
import random

ha_v2_verbose = 3


def LOG(s, **kwargs):
    # if ha_v2_verbose >=0:
    if ha_v2_verbose > 1:
        util.PRINT(util.get_indent() + s, **kwargs)


def LOG2(s, **kwargs):
    # if ha_v2_verbose >=0:
    if ha_v2_verbose > 2:
        util.PRINT(util.get_indent() + s, **kwargs)


def WARN(s, **kwargs):
    if ha_v2_verbose > 0:
        util.WARN(util.get_indent() + s, **kwargs)


def REPORT_HANG(message):
    util.ERROR(f"**** POSSIBLE HANG DETECTED ****")
    util.ERROR(f"\t{message}")


command_metadata = {
    "short": "ha_v2",
    "long": "hang_analysis_v2",
    "type": "dev",
    "description": __doc__,
}

## Corresponds to UNIQUE_ID_ALIGN in net2pipe.
## TODO: Figure out where to put this constant for
## reuse in other debuda commands
UNIQUE_ID_ALIGN: int = 1000000000

import re

STREAM_DUMP_CAPTURE_PATTERN = (
    r"(Tensix|Ethernet) x=(\d{2}),y=(\d{2}) => stream (\d{2}) (.*) = (0x)?(\d+)"
)


class HAStreamId:
    """
    Hang Analysis Stream ID

    An object represents a stream on the silicon hardware (rather than the logical stream from say the blob yaml)
    """

    def __init__(self, location: OnChipCoordinate, stream_id: int):
        self.location: OnChipCoordinate = location
        assert stream_id is not None
        assert isinstance(stream_id, int)
        self.stream_id: int = stream_id
        self.labels = set()

    def __eq__(self, other):
        return (
            other is not None
            and self.stream_id == other.stream_id
            and self.location == other.location
        )

    def __hash__(self):
        return hash((self.location, self.stream_id))

    def get_device_id(self):
        return self.location._device.id()

    def as_blob_yaml_designator(self):
        noctr = self.location.to("nocTr")
        return f"chip_{self.get_device_id()}__y_{noctr[1]}__x_{noctr[0]}__stream_id_{self.stream_id}"

    def __str__(self):
        return f"HAStreamId(chip={self.location._device.id()},x={self.location._noc0_coord[0]},y={self.location._noc0_coord[1]},{self.stream_id})"

    @staticmethod
    def from_temporal_graph_stream_id(ha_context, temporal_graph_stream_id):
        """ """
        assert (
            isinstance(temporal_graph_stream_id, tuple)
            and len(temporal_graph_stream_id) == 5
        )
        chip_id = temporal_graph_stream_id[0]
        device = ha_context.context.devices[chip_id]
        core_x = temporal_graph_stream_id[1]
        core_y = temporal_graph_stream_id[2]
        stream_id = temporal_graph_stream_id[3]

        return HAStreamId(OnChipCoordinate(core_x, core_y, "nocTr", device), stream_id)

    @staticmethod
    def from_blob_yaml_designator(ha_context, stream_designator: str):
        """
        This function takes a stream designator and returns the corresponding HAStreamId.

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
        stream_id = int(stream_id_str.split("_")[2])

        # Create the coordinate
        device = ha_context.context.devices[chip_id]
        return HAStreamId(OnChipCoordinate(core_x, core_y, "nocTr", device), stream_id)

    def to_stream_tuple(self, phase: int):
        """
        Stream tuples take the form (device_id, core_x, core_y, stream_id, phase)
        """
        return Stream.get_stream_tuple_from_designator(
            self.as_blob_yaml_designator()
        ) + (phase,)


## --------------------- FUTURE IMPROVEMENTS: ------------------------- ##
## - If we see that for all the active epochs, there is a "front" and "back"
##   grouping with a gap in the middle, we should prefer to investigate those
##   streams in the "back" group. E.g. 32 epochs, currently epochs 0, 1, 30, 31 on
##   system, then check streams in 30,31 first
## - For every new starting point, clear the stream status history


class StreamTraversalResult(Enum):
    CLEAN = 1
    HANG = 2
    IN_PROGRESS = 3
    VISITED_IN_TRAVERSAL = 4
    UNVISITED = 5


class HangAnalysisTraversalGraph:
    def __init__(self):
        self.traversal_graph = dict()

    def add_edge(self, source: HAStreamId, dest: HAStreamId):
        assert dest is not None
        if source not in self.traversal_graph:
            self.traversal_graph[source] = []
        if dest not in self.traversal_graph:
            self.traversal_graph[dest] = []
        # LOG2(f"Add edge {str(source)} -> {str(dest)}")
        self.traversal_graph[source].append(dest)

    def run_dfs_with_backedge_edge(self, start):
        cycle = []
        visited_stack_set = defaultdict(int)
        visited_stack_set[start] = 1
        visited_stack = [(start, 0)]

        while len(visited_stack) > 0:
            assert len(visited_stack) <= len(self.traversal_graph)
            current_node, current_node_index = visited_stack[-1]

            if current_node not in self.traversal_graph or current_node_index >= len(
                self.traversal_graph[current_node]
            ):
                if current_node not in self.traversal_graph:
                    assert current_node is None
                    print(f"{str(current_node)} not in graph")
                visited_stack.pop()
                visited_stack_set[
                    current_node
                ] -= 1  # remove a visitor count for this node
                if len(visited_stack) > 0:
                    visited_stack[-1] = (visited_stack[-1][0], visited_stack[-1][1] + 1)
                continue

            # visited_stack[-1] = (current_node, current_node_index + 1)
            assert isinstance(self.traversal_graph[current_node], list)
            next_node = self.traversal_graph[current_node][current_node_index]
            if next_node in visited_stack_set and visited_stack_set[next_node] > 0:
                ## Found a cycle ... printing it
                start = 0
                for i in range(len(visited_stack)):
                    if visited_stack[i][0] == next_node:
                        start = i
                        break
                cycle = [str(s) for s, i in visited_stack[start:]] + [str(next_node)]
                return cycle
            else:
                visited_stack_set[next_node] += 1
                visited_stack.append((next_node, 0))

        return []

    def print_cycles(self, cycle):
        util.ERROR(f"Found cycle")
        util.ERROR(f"\t{','.join(cycle)}")

    def find_cycles(self):
        for s in self.traversal_graph.keys():
            if s is None:
                continue
            # print(f"-------ITERATION: {str(s)}--------")
            cycle = self.run_dfs_with_backedge_edge(s)
            if len(cycle) > 0:
                return cycle

        return []

    def run_cycle_analysis(self):
        node_values = defaultdict(int)

        detected_cycle = self.find_cycles()
        if len(detected_cycle) > 0:
            self.print_cycles(detected_cycle)


class ProgressReporter:
    def __init__(self, max):
        self.max = max
        self.count = 0
        self.count_per_percent = int(max) // 100

    def increment(self):
        self.count += 1
        if (
            self.count >= self.count_per_percent
            and self.count % self.count_per_percent == 0
        ):
            print(
                f"Visited {self.count} streams ({int(round(self.count / self.max * 100))}% of all active streams)"
            )


class HangAnalysisContext:
    def __init__(self, context):
        self.context = context
        self.visited_streams: Dict[HAStreamId, Set] = defaultdict(set)
        self.visited_ops = set()
        self.visited_cores = set()

        ## All the streams in silicon that are active
        self.all_streams_silicon = dict()
        self.num_active_streams = 0  ## How to get int max in python?

        ## The frontier_streams act as the queue for the hang analysis graph traversal
        ## We keep a separate set to speedup lookup because we don't want to double add
        ## streams to traversal queue since it's a) redundant, and b) complicates cycle analysis
        self.frontier_streams = []
        self.frontier_streams_set: Set[HAStreamId] = set()  ## To prevent

        ## Keeps tra
        self.connectivity_graph = HangAnalysisTraversalGraph()

        ## For every visted stream, we keep track of the streams that wanted to visit the keyed stream next
        ## For example, if we have an empty unpacker stream and another unpacker stream on the core has tiles,
        ## if we visited the unpacker stream with tiles first, we'd add the empty unpacker stream to the frontier
        ## Then `unpacker_stream_with_tiles is in self.stream_traversal_sources[empty_unpacker_stream]`
        ## Future work: see if we can move all usage of this datastructure to instead use the connectivity graph
        self.stream_traversal_sources: Dict[HAStreamId, List[HAStreamId]] = defaultdict(
            list
        )

        ## TODO: Cleanup/remove. Looks like it's a nop now but it should be removed carefully with adequate testing
        self.stream_traversal_result: Dict[HAStreamId, StreamTraversalResult] = dict()

        ## Primarily for error reporting/debug, this set contains all of the streams that have been visited in the
        ## current traversal. Keep in mind that we may need to run multiple traversals on the cluster cores in order
        ## to find the hang.
        self.current_traversal_streams: Set[HAStreamId] = set()

        ## (old) book-keeping: consider deleting
        self.current_traversal_all_clean = True

        ## An index into the global_workload_streams list. This points to the next stream to consider choosing for
        ## when we want to start a new traversal
        self.next_stream_index_to_check = 0

        ### Cached Device Read Datastructures
        self.cached_temporal_epoch_id_lookups = dict()
        self.cached_stream_register_data_lookups = dict()

        ## Used for some error conditions. If we are traversing up through a chain of empty streams and
        ## hit an empty input queue, then then it may or may not be a problem, depending on whether or
        ## not we hit other streams in this same traversal that aren't empty. If we haven't hit any
        ## non-backpressured streams in the current traversal, then hitting an empty input queue is fine
        ## because we may just be looking at a later epoch that was programmed earlier than the input
        ## host or e2e queue was populated (e.g. by earlier epoch, or while waiting for earlier loop
        ## iteration to finish). If we've hit backpressured streams, then we know the input queue
        ## should have been populated
        self.traversed_backpressured_stream_epochs = set()

        ## Track already reported objects so we don't report the same issue multiple times and spam output
        self.reported_hang_queues = set()
        self.reported_hang_streams = set()

        self.streams_processed = set()

        ## All workload streams that *may* be live and active in the system
        ## These are the workload streams (not live runtime streams on silicon) we'll need to visit
        ## during out hang analysis
        self.global_workload_streams: List[HAStreamId] = list()

        ## Datastructure initialization
        ## Figure out which temporal epochs are live on the system and load their blob and pipegen yamls
        ## Populate the `global_workload_streams_set` based on those loaded files
        live_temporal_epochs = set()
        for device_id, device in context.devices.items():
            core_to_epoch_map = device.read_core_to_epoch_mapping()
            live_temporal_epochs.update(set(int(e) for e in core_to_epoch_map.values()))

        global_workload_streams_set = set()
        for temporal_epoch, temporal_graph in context.netlist.temporal_epochs:
            ## Only load the pipegen and blob yamls for temporal epochs that are live on the system
            if int(temporal_epoch) not in live_temporal_epochs:
                continue
            self.context.netlist.temporal_graphs[
                str(temporal_epoch)
            ].load_pipegen_and_blob()
            blob_yaml = ha_context.context.netlist.temporal_graphs[
                int(temporal_epoch)
            ].blob_yaml
            for key, val in blob_yaml.items():
                if key.startswith("phase_"):
                    for stream_designator in val.keys():
                        stream_device_id = int(
                            stream_designator.split("__")[0].split("_")[1]
                        )
                        device = context.devices[stream_device_id]

                        _, core_y_str, core_x_str, stream_id_str = (
                            stream_designator.split("__")
                        )
                        # Extract the coordinates from the stream designator
                        core_x = int(core_x_str.split("_")[1])
                        core_y = int(core_y_str.split("_")[1])

                        on_chip_coord = OnChipCoordinate(
                            core_x, core_y, "nocTr", device
                        )
                        core_epoch = device.get_epoch_id(on_chip_coord)
                        self.cached_temporal_epoch_id_lookups[on_chip_coord] = (
                            core_epoch
                        )
                        core_is_on_stream_epoch = core_epoch == int(temporal_epoch)
                        if core_is_on_stream_epoch:
                            global_workload_streams_set.add(
                                HAStreamId.from_blob_yaml_designator(
                                    self, stream_designator
                                )
                            )
                        else:
                            # LOG(f"Ignoring stream {stream_designator} from temporal epoch {temporal_epoch} because that core is on a different epoch")
                            pass

        self.global_workload_streams = list(global_workload_streams_set)

        ## Otherwise we'll tend to visit lots of parameter reading operand queues first
        live_epoch_gap = any(
            e not in live_temporal_epochs
            for e in range(min(live_temporal_epochs), max(live_temporal_epochs))
        )

        ## We do this as a heuristic to avoid having a long sequence of weight reading unpacker streams... basically
        ## I noticed that doing this seems to result in earlier results. It's optional though...
        random.shuffle(self.global_workload_streams)

        ## UI: Used to track progress (as % completion) to the user
        self.progress_reporter = ProgressReporter(len(self.global_workload_streams))

        ## Load the cluster desc - we need this to traverse across ethernet links to know which chip to
        ## probe when we need to grab the stream on the other end of the link
        self.ethernet_connections = dict()
        cluster_desc = self.context.cluster_desc.root
        for endpoint_a, endpoint_b in cluster_desc["ethernet_connections"]:
            chip_a, chan_a = endpoint_a["chip"], endpoint_a["chan"]
            chip_b, chan_b = endpoint_b["chip"], endpoint_b["chan"]
            device_a = self.context.devices[chip_a]
            chan_a_core = device_a.get_block_locations("eth")[chan_a]
            core_a = OnChipCoordinate(
                chan_a_core._noc0_coord[0], chan_a_core._noc0_coord[1], "noc0", device_a
            )

            device_b = self.context.devices[chip_b]
            chan_b_core = device_a.get_block_locations("eth")[chan_b]
            core_b = OnChipCoordinate(
                chan_b_core._noc0_coord[0], chan_b_core._noc0_coord[1], "noc0", device_b
            )

            assert core_a._device.id() != core_b._device.id()
            self.ethernet_connections[core_a] = core_b
            self.ethernet_connections[core_b] = core_a

    def add_stream_processed(self, stream_id: HAStreamId):
        """
        Keep track of visited streams. Call when we've visited a stream (or considered)
        it inactive, so we don't visit it again.
        """
        if stream_id not in self.streams_processed:
            self.streams_processed.add(stream_id)
            self.progress_reporter.increment()

    def mark_found_backpressured_stream(self, stream_id: HAStreamId):
        """
        Helper: call when visiting a stream that we determine to be backpressured
        """
        ## Cleared the visited streams because we want to be able to traverse through
        ## empty streams that we may have visited already... This isn't a great solution
        ## and I think I'll need to figure out how to remove the concept of `visited_streams`
        ## altogether (for the sake of traversal), but keep it for the sake of choosing
        ## a starting stream for new traversall
        self.visited_streams.clear()
        temporal_epoch_id = self.get_stream_temporal_epoch_id(stream_id)
        self.traversed_backpressured_stream_epochs.add(temporal_epoch_id)

    def get_stream_traversal_result(self, stream_id: HAStreamId):
        return (
            self.stream_traversal_result[stream_id]
            if stream_id in self.stream_traversal_result
            else StreamTraversalResult.UNVISITED
        )

    def apply_traversal_result(
        self, stream_id: HAStreamId, traversal_result: StreamTraversalResult
    ):
        """
        TODO: see note on self.stream_traversal_result - removal comment also applies to this function
        This function was used in earlier implementations of this pass but not it should mostly be unneeded. We should be able to remove all
        uses of self.stream_traversal_result, but it will stay here for now.
        """

        assert (
            stream_id not in self.stream_traversal_result
            or self.stream_traversal_result[stream_id]
            == StreamTraversalResult.IN_PROGRESS
            or self.stream_traversal_result[stream_id] == StreamTraversalResult.CLEAN
        )
        self.stream_traversal_result[stream_id] = traversal_result
        current_stream_id = stream_id
        streams = [current_stream_id]
        while len(streams) > 0:
            current_stream_id = streams[0]
            streams.pop(0)
            if (
                current_stream_id in self.stream_traversal_sources
                and self.stream_traversal_result[current_stream_id]
                == StreamTraversalResult.IN_PROGRESS
            ):
                self.stream_traversal_result[current_stream_id] = traversal_result
                for next_stream_id in self.stream_traversal_sources[current_stream_id]:
                    streams.append(next_stream_id)

    def stream_sources_contain_duplicates(self, stream_id):
        contains_duplicates = any(
            count > 1
            for count in Counter(self.stream_traversal_sources[stream_id]).values()
        )
        return contains_duplicates

    def finish_current_traversal(self):
        LOG2(f"finish_current_traversal")
        LOG(
            f"Finished a traversal. Streams visited: {list(str(s) for s in self.current_traversal_streams)}"
        )
        for stream_id in self.current_traversal_streams:
            self.mark_stream_visited_from_epoch(
                stream_id, self.get_stream_temporal_epoch_id(stream_id)
            )
            if self.current_traversal_all_clean:
                LOG2(f"\t{stream_id} CLEAN")
                self.apply_traversal_result(stream_id, StreamTraversalResult.CLEAN)
            else:
                LOG2(f"\t{stream_id} NOT CLEAN")
                self.apply_traversal_result(stream_id, StreamTraversalResult.HANG)

        self.current_traversal_streams.clear()
        self.stream_traversal_sources.clear()
        self.current_traversal_all_clean = True
        self.frontier_streams.clear()
        self.frontier_streams_set.clear()
        self.traversed_backpressured_stream_epochs.clear()

    def stop_current_path_traversal(
        self, ending_stream_id: HAStreamId, traversal_result: StreamTraversalResult
    ):
        LOG2(f"stop_current_path_traversal {ending_stream_id} {traversal_result}")
        self.stream_traversal_result[
            ending_stream_id
        ] == StreamTraversalResult.VISITED_IN_TRAVERSAL

    def report_hang(self, stream_id: HAStreamId, message: str):
        """
        When a hang is found this function will report it to the user. It will also traverse the path that was taken,
        from start to finish, where this hang was detected (e.g. in case of something like fork-join)
        """
        self.current_traversal_all_clean = False
        visited = set()
        source_streams = []
        source_stream = stream_id
        traversal = [source_stream]
        while len(traversal) > 0:
            stream = traversal[0]
            traversal.pop(0)
            source_streams.append(str(stream))
            visited.add(stream)
            if stream is not None and stream in self.stream_traversal_sources:
                for source_stream in self.stream_traversal_sources[stream]:
                    if source_stream is None:
                        continue
                    is_starting_stream = source_stream == stream_id
                    if is_starting_stream:
                        source_streams.append(source_stream)
                    source_not_visited = source_stream not in visited
                    traversed_stream = source_stream in self.current_traversal_streams
                    if traversed_stream and source_not_visited:
                        traversal.append(source_stream)

        if stream_id not in self.reported_hang_streams:
            REPORT_HANG(
                f"Found possible hang at stream {stream_id}.\n\t{message}.\n\tPath = {source_streams}"
            )
            for stream, sources in self.stream_traversal_sources.items():
                line = f"\t{str(stream)} "
                LOG2(f"\t{str(stream)} <- {list(str(s) for s in sources)}")

            self.reported_hang_streams.add(stream_id)

        self.stop_current_path_traversal(
            stream_id, StreamTraversalResult.CLEAN
        )  # StreamTraversalResult.HANG)
        self.finish_current_traversal()

    def is_stream_active(self, stream_id: HAStreamId):
        """
        Returns true if the stream is alive *in silicon*.

        This does *not* indicate in any way whether the stream is specified in any workload blob yaml
        """
        stream_blob = self.get_stream_register_data(stream_id)
        return bool((int(stream_blob["DEBUG_STATUS[8]"]) & 0x5) != 0)

    def read_device_stream_regs(self, stream_id: HAStreamId):
        """
        Helper function - read stream registers into the hang analysis cache if they aren't already there
        """
        if stream_id not in self.cached_stream_register_data_lookups:
            device = self.context.devices[stream_id.get_device_id()]
            assert stream_id is not None and stream_id.stream_id is not None
            stream_regs = device.read_stream_regs(
                stream_id.location, stream_id.stream_id
            )
            self.cached_stream_register_data_lookups[stream_id] = stream_regs

        return self.cached_stream_register_data_lookups[stream_id]

    def get_stream_register_data(self, stream_id: HAStreamId):
        return self.read_device_stream_regs(stream_id)

    def is_stream_in_frontier(self, stream_id: HAStreamId):
        return stream_id in self.frontier_streams_set

    def add_stream_to_frontier(
        self, stream_id: HAStreamId, traversal_source_stream: HAStreamId
    ):
        assert traversal_source_stream is not None or (
            len(self.frontier_streams_set) == 0 and len(self.frontier_streams) == 0
        )
        assert stream_id is not None

        if (
            traversal_source_stream not in self.stream_traversal_sources[stream_id]
        ):  # , "already visited this stream... infinite loop"
            self.stream_traversal_sources[stream_id].append(traversal_source_stream)

        # self.add_stream_processed(stream_id)

        self.connectivity_graph.add_edge(traversal_source_stream, stream_id)

        if stream_id in self.visited_streams:
            LOG(
                f"\tAlready visited stream {str(stream_id)} - not adding it to frontier"
            )
            return

        assert stream_id is not None
        ## If the stream is already in the traversal set, it is pointless to add it again
        if not self.is_stream_in_frontier(stream_id):
            LOG(
                f"Adding stream {str(stream_id)} to frontier, from stream {str(traversal_source_stream)}"
            )
            self.frontier_streams.append((stream_id, traversal_source_stream))
            self.frontier_streams_set.add(stream_id)
            self.stream_traversal_result[stream_id] = StreamTraversalResult.IN_PROGRESS

    def choose_unvisited_starting_stream(self):
        """
        Call when starting a new traversal. Will choose a stream this is active, live (in silicon), unvisited, and not
        for an intermediate buffer
        """
        self.current_traversal_all_clean = True
        LOG(f"---------------------")
        LOG(f"---------------------")
        LOG(f"---------------------")
        LOG(f"---------------------")
        LOG(f"choose_unvisited_starting_stream")
        LOG(f"---------------------")
        LOG(f"---------------------")
        LOG(f"---------------------")
        LOG(f"---------------------")
        start = int(self.next_stream_index_to_check)
        end = int(len(self.global_workload_streams))
        for i in range(start, end):
            stream_id = self.global_workload_streams[i]
            self.next_stream_index_to_check = i + 1
            self.add_stream_processed(stream_id)

            if (
                stream_id in self.visited_streams
                and self.get_stream_temporal_epoch_id(stream_id)
                in self.visited_streams[stream_id]
            ):
                LOG(f"\tstream {stream_id} already visited - not choosing it")
                continue

            stream_regs = self.get_stream_register_data(stream_id)
            if not self.context.devices[stream_id.get_device_id()].is_stream_configured(
                stream_regs
            ):
                LOG(f"\tstream {stream_id} is not configured - not choosing it")
                continue

            stream_active = self.is_stream_active(stream_id)
            if not stream_active:
                LOG(f"\tstream {stream_id} is not active - not choosing it")
                continue

            is_intermediate_buffer = (
                stream_id.stream_id >= 32 and stream_id.stream_id < 40
            )
            if is_intermediate_buffer:
                LOG(f"\tstream {stream_id} is an intermediate buffer - not choosing it")
                continue

            LOG(
                f"Stream {stream_id} is active and unvisited and not an intermediate buffer - choosing it"
            )

            # print(f"{(i/len(self.global_workload_streams))*100}% through the stream list")
            return stream_id

        return None

    def get_stream_temporal_epoch_id(self, stream_id: HAStreamId):
        device = self.context.devices[stream_id.get_device_id()]
        on_chip_coord = OnChipCoordinate(
            stream_id.location._noc0_coord[0],
            stream_id.location._noc0_coord[1],
            "noc0",
            stream_id.location._device,
        )
        if on_chip_coord not in self.cached_temporal_epoch_id_lookups:
            self.cached_temporal_epoch_id_lookups[on_chip_coord] = device.get_epoch_id(
                stream_id.location
            )
        return self.cached_temporal_epoch_id_lookups[on_chip_coord]

    def is_stream_already_visited(self, stream_id: HAStreamId):
        return stream_id in self.visited_streams

    def is_stream_already_visited_from_epoch(
        self, stream_id: HAStreamId, epoch_id: int
    ):
        return (
            self.is_stream_already_visited(stream_id)
            and epoch_id in self.visited_streams[stream_id]
        )

    def mark_stream_visited_from_epoch(self, stream_id: HAStreamId, epoch: int):
        self.visited_streams[stream_id].add(epoch)

    def add_silicon_stream(self, stream_id: HAStreamId, silicon_stream_data):
        self.all_streams_silicon[stream_id] = silicon_stream_data

    def get_and_pop_next_stream_in_frontier(self) -> HAStreamId:
        stream_id, source_stream_id = self.frontier_streams[0]
        LOG(f"popping stream {str(stream_id)} from frontier")
        assert stream_id is not None
        self.frontier_streams.pop(0)
        self.frontier_streams_set.remove(stream_id)
        self.current_traversal_streams.add(stream_id)
        return stream_id, source_stream_id

    def num_active_silicon_streams(self):
        return len(self.all_streams_silicon)


def is_stream_in_dummy_phase(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    device = ha_context.context.devices[stream_id.get_device_id()]
    assert stream_id is not None and stream_id.stream_id is not None
    stream_regs = ha_context.read_device_stream_regs(stream_id)
    phase = int(stream_regs["CURR_PHASE"])
    ## The lower 15 bits of curr phase reg are used for the true phase
    ## and dummy phase is encoded outside of that number range since it isn't
    ## a "true" phase.
    return (phase >> 15) == 0x1F


def get_stream_phase(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    """
    This stream phase is the phase as we'd  see it in silicon, which omits epoch info
    """
    device = ha_context.context.devices[stream_id.get_device_id()]
    assert stream_id is not None and stream_id.stream_id is not None
    stream_regs = ha_context.read_device_stream_regs(stream_id)
    phase = int(stream_regs["CURR_PHASE"])
    assert phase > 0, "Tried to get phase of inactive stream"
    return phase & 0x7FFF


def get_stream_temporal_epoch_id(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    return ha_context.get_stream_temporal_epoch_id(stream_id)


def get_stream_temporal_graph(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    epoch_id = get_stream_temporal_epoch_id(ha_context, stream_id)
    temporal_graph = ha_context.context.netlist.temporal_graphs[str(epoch_id)]

    return temporal_graph


def get_stream_blob_for_dummy_phase_stream(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    """ """
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
    blob_designator = stream_id.as_blob_yaml_designator()
    graph_stream_id = temporal_graph.stream_loc_id_map[blob_designator]
    stream_blob = temporal_graph.streams[graph_stream_id].root
    return stream_blob


def is_stream_active(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    return ha_context.is_stream_active(stream_id)


def get_stream_blob(ha_context: HangAnalysisContext, stream_id: HAStreamId, phase: int):
    """
    Get the blob yaml for this stream on silicon in its current phase
    """
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
    ## Mask out the epoch info
    phase_masked = phase & 0x7FFF
    stream_id_tuple = stream_id.to_stream_tuple(phase_masked)
    if stream_id_tuple not in temporal_graph.streams and is_stream_active(
        ha_context, stream_id
    ):
        util.ERROR(
            f"Stream {stream_id}'s `CURR_PHASE` register is reporting {hex(phase)} ({hex(phase_masked)} masked) and its core is reported as on temporal epoch {int(temporal_graph.id())} but the stream doesn't appear to have a blob yaml entry in that phase and epoch. There may be a stream programming issue"
        )
        return {}
    return temporal_graph.streams[stream_id_tuple].root


def get_stream_linked_buffer(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    """
    Lookup the buffer associated with this live silicon stream
    """
    stream_blob = get_stream_blob(
        ha_context, stream_id, get_stream_phase(ha_context, stream_id)
    )
    return stream_blob["buf_id"]


def get_device_of_stream(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    return ha_context.context.devices[stream_id.get_device_id()]


def is_stream_directly_feeding_other_stream(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    stream_register_data = ha_context.get_stream_register_data(stream_id)
    stream_blob = get_stream_blob(
        ha_context, stream_id, get_stream_phase(ha_context, stream_id)
    )
    return len((stream_blob["dest"])) > 0


def is_unpacker_stream(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    phase = get_stream_phase(ha_context, stream_id)
    if phase == 0:
        ## Since we are seeing if this stream is an unpacker, it's possible the current op is still "active" but this stream
        ## in silicon is done. Therefore, we may get an invalid phase (0). If we do, we want to pick a known safe phase
        ## that we can use for lookup in the blob yaml to determine if this is an unpacker stream
        phase = 1
    assert phase > 0
    stream_blob = get_stream_blob(ha_context, stream_id, phase)
    device = get_device_of_stream(ha_context, stream_id)
    assert isinstance(stream_blob["dest"], Sequence)
    is_dest_field_missing_or_empty = (
        "dest" not in stream_blob or len(stream_blob["dest"]) == 0
    )  # "[]")
    is_on_worker_core = stream_id.location in device.get_block_locations(
        "functional_workers"
    )
    is_receiver_endpoint = (
        "receiver_endpoint" in stream_blob
        and stream_blob["receiver_endpoint"].lower() == "true"
    )
    has_input_index = (
        "input_index" in stream_blob and int(stream_blob["input_index"]) >= 0
    )
    return (
        is_dest_field_missing_or_empty
        and is_on_worker_core
        and is_receiver_endpoint
        and has_input_index
    )


def is_packer_stream(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    phase = get_stream_phase(ha_context, stream_id)
    if phase == 0:
        ## Since we are seeing if this stream is an unpacker, it's possible the current op is still "active" but this stream
        ## in silicon is done. Therefore, we may get an invalid phase (0). If we do, we want to pick a known safe phase
        ## that we can use for lookup in the blob yaml to determine if this is a packer stream
        phase = 1  # pick an arbitrary phase
    assert phase > 0
    stream_blob = get_stream_blob(ha_context, stream_id, phase)
    device = get_device_of_stream(ha_context, stream_id)
    is_on_worker_core = stream_id.location in device.get_block_locations(
        "functional_workers"
    )
    is_source_endpoint = (
        "source_endpoint" in stream_blob
        and stream_blob["source_endpoint"].lower() == "true"
    )
    has_output_index = (
        "output_index" in stream_blob and int(stream_blob["output_index"]) >= 0
    )
    return is_on_worker_core and is_source_endpoint and has_output_index


def is_packer_fork_stream(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    phase = get_stream_phase(ha_context, stream_id)
    if phase == 0:
        ## Since we are seeing if this stream is an unpacker, it's possible the current op is still "active" but this stream
        ## in silicon is done. Therefore, we may get an invalid phase (0). If we do, we want to pick a known safe phase
        ## that we can use for lookup in the blob yaml to determine if this is a packer stream
        phase = 1  # pick an arbitrary phase
    assert phase > 0
    stream_blob = get_stream_blob(ha_context, stream_id, phase)
    device = get_device_of_stream(ha_context, stream_id)
    is_on_worker_core = stream_id.location in device.get_block_locations(
        "functional_workers"
    )
    is_source_endpoint = (
        "source_endpoint" in stream_blob
        and stream_blob["source_endpoint"].lower() == "true"
    )
    fork_streams_not_empty = (
        "fork_stream_ids" in stream_blob and len(stream_blob["fork_stream_ids"]) > 0
    )
    return is_on_worker_core and is_source_endpoint and fork_streams_not_empty


def get_blob_phase(temporal_epoch_id: int, stream_phase: int):
    """
    Returns the phase as it would be seen in the blob yaml file
    i.e., phase_{get_blob_phase(...)}:
    """
    return (int(temporal_epoch_id) << 32) | stream_phase


def get_stream_source(
    ha_context: HangAnalysisContext,
    temporal_graph: TemporalEpoch,
    stream_id: HAStreamId,
    phase_id: int,
) -> HAStreamId:
    """
    phase_id: This will be the phase ID from the stream reg. We'd need to mask it with (epoch_id << 32)
              to get the blob_phase
    """
    stream_id_in_temporal_graph = temporal_graph.stream_loc_id_map[
        stream_id.as_blob_yaml_designator()
    ]
    temporal_graph_source_stream_id = temporal_graph.stream_source_map[
        stream_id_in_temporal_graph
    ][get_blob_phase(int(temporal_graph.id()), phase_id)]
    source_stream = HAStreamId.from_temporal_graph_stream_id(
        ha_context, temporal_graph_source_stream_id
    )
    return source_stream


def get_source_stream(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    assert not is_packer_stream(
        ha_context, stream_id
    ), "For packer streams, call `get_unpacker_streams_from_core_packer_stream`"
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
    phase = get_stream_phase(ha_context, stream_id)
    producer_stream = get_stream_source(ha_context, temporal_graph, stream_id, phase)
    return producer_stream


def get_dest_streams(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    """
    Typically just one entry but could be multiple in case of multicast
    """
    stream_register_data = ha_context.get_stream_register_data(stream_id)
    stream_blob = get_stream_blob(
        ha_context, stream_id, get_stream_phase(ha_context, stream_id)
    )
    dest_stream_designators = stream_blob["dest"]

    if len(dest_stream_designators) > 1:
        ## Get all the dests in the multicast rectangle
        chip = int(dest_stream_designators[0].split("__")[0].split("_")[1])
        stream_id_number = int(dest_stream_designators[0].split("__")[3].split("_")[2])
        rect_start_x = int(dest_stream_designators[0].split("__")[2].split("_")[1])
        rect_start_y = int(dest_stream_designators[0].split("__")[1].split("_")[1])
        rect_end_x = int(dest_stream_designators[1].split("__")[2].split("_")[1])
        rect_end_y = int(dest_stream_designators[1].split("__")[1].split("_")[1])
        device = ha_context.context.devices[chip]
        worker_cores = device.get_block_locations(block_type="functional_workers")
        dest_stream_designators = [
            f"chip_{chip}__y_{y}__x_{x}__stream_id_{stream_id_number}"
            for x in range(rect_start_x, rect_end_x + 1)
            for y in range(rect_start_y, rect_end_y + 1)
            if OnChipCoordinate(x, y, "nocTr", device) in worker_cores
        ]

    return tuple(
        HAStreamId.from_blob_yaml_designator(ha_context, designator)
        for designator in dest_stream_designators
    )


def stream_writes_to_dram(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    stream_blob = get_stream_blob(
        ha_context, stream_id, get_stream_phase(ha_context, stream_id)
    )
    return "dram_output" in stream_blob and stream_blob["dram_output"].lower() == "true"


def queue_has_op_consumer(queue: Queue):
    return len(queue.output_ops) == 0


def get_stream_consumers_through_queue(
    ha_context: HangAnalysisContext,
    queue: Queue,
    consumer_queue_buffers: List[int],
    stream_id: HAStreamId,
) -> List[HAStreamId]:
    """
    TODO: Implement
    This function will return the consumer streams of this queue object. It will collect the set of all workload streams
    and then return the filtered list that contains the live silicon streams in that set
    """
    # We can actually ignore this for now since we can treat queues as sinks, more or less
    assert False, "unimplemented"

    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
    buffer_id = get_stream_linked_buffer(ha_context, stream_id)
    buffer_object = temporal_graph.get_buffer(buffer_id)

    queue_addresses = queue.get_mem_addr()
    queue_buffer_grid_indices = []
    # these consumer_queue_buffers should be the buffers for `queue`
    for consumer_queue_buffer_id in consumer_queue_buffers:
        buffer_dram_channel = int(
            temporal_graph.get_buffer(consumer_queue_buffer_id).root["dram_chan"]
        )
        buffer_dram_addr = int(
            hex(temporal_graph.get_buffer(consumer_queue_buffer_id).root["dram_addr"])
        )

        index = queue_addresses.index([buffer_dram_channel, buffer_dram_addr])
        assert index is not None
        queue_buffer_grid_indices.append(index)

    # We have then addresses and q from the producer temporal epoch, now for the consumer temporal epoch(s),
    # we need to find the appropriate buffer objects
    q_consumer_epochs = set(
        ha_context.context.netlist._op_epoch_map[c]
        for c in ha_context.context.netlist._name_consumers_map[queue]
    )
    buffer_epoch_pairs = []
    for queue_consumer_epoch in q_consumer_epochs:
        buffer = None
        assert False, "unimplemented"
        buffer_epoch_pairs.append((buffer, queue_consumer_epoch))

    ## TODO traverse downwards until we find a queue buffer
    ## Then find the buffer in the queue grid
    ## Then find all consumer ops of the queue
    ## Then for each consumer op, collect the set of epochs those ops cover
    ## then for each temporal epoch, find the queue buffer
    ## then find each stream consuming from that

    assert False, "unimplemented"


def get_first_consumer_buffer_objects(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    stream_blob = get_stream_blob(
        ha_context, stream_id, get_stream_phase(ha_context, stream_id)
    )
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)

    if "buf_id" in stream_blob.root:
        buffer = temporal_graph.get_buffer(stream_blob["buf_id"])
        dest_buffers = temporal_graph.get_fanout_buffer_level(buffer)
        return dest_buffers
    else:
        assert "pipe_id" in stream_blob, "stream is not connected to a buffer or pipe"
        pipe = temporal_graph.get_pipe(stream_blob["pipe_id"])
        dest_buffers = temporal_graph.get_buffers(pipe, "out")
        return dest_buffers


def get_queue_names_from_buffers(
    ha_context: HangAnalysisContext,
    temporal_graph: TemporalEpoch,
    consumer_queue_buffers: List[int],
):
    return list(
        set(
            temporal_graph.get_buffer(buffer_id).root["md_op_name"]
            for buffer_id in consumer_queue_buffers
        )
    )


def get_consumer_queue_buffers(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    """
    Will filter out non-queue buffers
    """
    consumer_buffers = get_first_consumer_buffer_objects(ha_context, stream_id)
    assert isinstance(consumer_buffers[0], int) or isinstance(consumer_buffers[0], str)
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)

    def is_queue_buffer(temporal_graph, buffer_id):
        buffer_root = temporal_graph.get_buffer(buffer_id).root
        has_dram_io_flag = (
            "dram_io_flag" in buffer_root and int(buffer_root["dram_io_flag"]) > 0
        )
        assert not has_dram_io_flag or buffer_root["buffer_type"] == "dram_io"
        return has_dram_io_flag

    consumer_buffers_filtered = tuple(
        id for id in consumer_buffers if is_queue_buffer(temporal_graph, id)
    )
    return consumer_buffers_filtered

    assert False, "unimplemented"


def get_stream_producer_through_queue(
    ha_context: HangAnalysisContext, queue: Queue, stream_id: HAStreamId
):
    assert False, "unimplemented"


def get_producer_queues(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    stream_blob = get_stream_blob(
        ha_context, stream_id, get_stream_phase(ha_context, stream_id)
    )
    has_buf_id = "buf_id" in stream_blob
    has_pipe_id = "pipe_id" in stream_blob
    assert has_buf_id or has_pipe_id, "stream is not connected to a buffer or pipe"
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
    if has_buf_id:
        buffer_id = stream_blob["buf_id"]
        return temporal_graph.buffer_q_map[buffer_id]

    else:
        assert has_pipe_id
        input_buffer_ids = temporal_graph.pipes[stream_blob["pipe_id"]].input_buffers
        base_buffer_ids = list(
            set(id - (id % UNIQUE_ID_ALIGN) for id in input_buffer_ids)
        )
        return [temporal_graph.buffer_q_map[buffer_id] for buffer_id in base_buffer_ids]


def get_consumer_queue(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    assert False, "unimplemented"


def is_output_queue(queue: Queue):
    return not queue_has_op_consumer(queue)


def get_forked_streams_from_packer_stream(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    is_in_dummy_phase = is_stream_in_dummy_phase(ha_context, stream_id)
    if is_in_dummy_phase:
        stream_blob = get_stream_blob_for_dummy_phase_stream(ha_context, stream_id)
    else:
        stream_blob = get_stream_blob(
            ha_context, stream_id, get_stream_phase(ha_context, stream_id)
        )

    if "fork_stream_ids" in stream_blob and stream_blob["fork_stream_ids"] != "[]":
        fork_stream_ids = [int(i) for i in stream_blob["fork_stream_ids"]]
        assert isinstance(fork_stream_ids, list)
        if "24" not in fork_stream_ids:
            fork_stream_ids.append(24)

        designator_prefix = stream_id.as_blob_yaml_designator().rstrip("0123456789")
        temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
        stream_ids = tuple(
            HAStreamId(stream_id.location, stream_id_number)
            for stream_id_number in fork_stream_ids
            if stream_id_number != stream_id.stream_id
        )
        return stream_ids

    return tuple()


# def get_associated_op(ha_context: HangAnalysisContext, stream_id: HAStreamId):
def get_buffers_on_same_core_as_stream(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    associated_core = stream_id.location
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
    return temporal_graph.get_core_buffers(associated_core)


def get_unpacker_buffers_from_buffers(
    ha_context: HangAnalysisContext,
    temporal_graph: TemporalEpoch,
    buffers: Dict[int, Any],
):
    assert isinstance(buffers, dict)
    return list(
        buffer
        for id, buffer in buffers.items()
        if buffer.root["buffer_type"] == "unpacker"
    )


def get_packer_buffers_from_buffers(
    ha_context: HangAnalysisContext,
    temporal_graph: TemporalEpoch,
    buffers: Dict[int, Any],
):
    assert isinstance(buffers, dict)
    return list(
        buffer
        for id, buffer in buffers.items()
        if buffer.root["buffer_type"] == "packer"
    )


def get_packer_stream_from_unpacker_stream(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    """
    Given an unpacker stream on the core, find all the packer streams for this op

    Doesn't necessarily return packer fork streams (call `get_forked_streams_from_packer_stream` for that)
    """
    buffers_on_same_core = get_buffers_on_same_core_as_stream(ha_context, stream_id)
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
    packer_buffers = get_packer_buffers_from_buffers(
        ha_context, temporal_graph, buffers_on_same_core
    )

    associated_stream_ids = set()
    for buffer in packer_buffers:
        if buffer.stream_id is not None:
            associated_stream_ids.add(buffer.stream_id)
        else:
            # We can't reliably get the unpacker buffer just from buffer or pipe so we have a traversal
            # This will typically be a DRAM read case
            if buffer.replicated:
                # This is a scatter buffer (not the base buffer), so we will skip it (the base buffer will be visited)
                continue
            entry_ids = set(temporal_graph.get_buffer_output_pipes(buffer))
            assert len(entry_ids) > 0
            entry_ids_next = set()
            buffer_visit = False
            while len(entry_ids) > 0:
                for entry_id in entry_ids:
                    entry = (
                        temporal_graph.temporal_epoch.buffers[int(entry_id)]
                        if buffer_visit
                        else temporal_graph.pipes[int(entry_id)]
                    )
                    if entry.stream_id is not None:
                        associated_stream_ids.add(entry.stream_id)
                    else:
                        if buffer_visit:
                            output_pipes = temporal_graph.get_buffer_output_pipes(
                                buffer
                            )
                            for output_pipe in output_pipes:
                                if output_pipe is None:
                                    # We've traversed to the packer buffer
                                    assert buffer.root["buffer_type"] == "packer"
                                    assert buffer.stream_id is not None
                                    associated_stream_ids.add(buffer.stream_id)
                                else:
                                    entry_ids_next.add(output_pipe)
                        else:
                            # input_buffers = [buffer for id, buffer in entry.input_buffers]
                            input_buffers = [
                                buffer for id, buffer in entry.output_buffers
                            ]
                            entry_ids_next.update(input_buffers)
                entry_ids = entry_ids_next
                buffer_visit = not buffer_visit

    assert len(associated_stream_ids) > 0
    packer_streams = [
        HAStreamId(stream_id.location, stream_id_number)
        for stream_id_number in associated_stream_ids
    ]
    assert all(packer_streams)
    return tuple(packer_streams)


def get_unpacker_streams_from_core_packer_stream(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    """
    Given a packer stream on the core, find all the unpacker streams for this op
    """
    buffers_on_same_core = get_buffers_on_same_core_as_stream(ha_context, stream_id)
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
    unpacker_buffers = get_unpacker_buffers_from_buffers(
        ha_context, temporal_graph, buffers_on_same_core
    )

    associated_stream_ids = set()
    for buffer in unpacker_buffers:
        if buffer.stream_id is not None:
            associated_stream_ids.add(buffer.stream_id)
        else:
            # We can't reliably get the unpacker buffer just from buffer or pipe so we have a traversal
            # This will typically be a DRAM read case
            if buffer.replicated:
                # This is a scatter buffer (not the base buffer), so we will skip it (the base buffer will be visited)
                continue

            entry_ids = set([temporal_graph.get_buffer_input_pipe(buffer)])
            assert len(entry_ids) > 0
            entry_ids_next = set()
            buffer_visit = False
            while len(entry_ids) > 0:
                for entry_id in entry_ids:
                    entry = (
                        temporal_graph.temporal_epoch.buffers[int(entry_id)]
                        if buffer_visit
                        else temporal_graph.pipes[int(entry_id)]
                    )
                    if entry.stream_id is not None:
                        associated_stream_ids.add(entry.stream_id)
                    else:
                        if buffer_visit:
                            input_pipe = temporal_graph.get_buffer_input_pipe(buffer)
                            if input_pipe is None:
                                # We've traversed to the packer buffer
                                assert buffer.root["buffer_type"] == "packer"
                                assert buffer.stream_id is not None
                                associated_stream_ids.add(buffer.stream_id)
                            else:
                                entry_ids_next.add(input_pipe)
                        else:
                            output_buffers = [
                                buffer for id, buffer in entry.input_buffers
                            ]
                            entry_ids_next.update(input_buffers)
                entry_ids = entry_ids_next
                buffer_visit = not buffer_visit

    assert all(associated_stream_ids)
    unpacker_streams = [
        HAStreamId(stream_id.location, stream_id_number)
        for stream_id_number in associated_stream_ids
    ]
    return unpacker_streams


def get_other_unpacker_streams_from_unpacker_stream(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    """
    Given an unpacker stream, return all other unpacker streams for the op on this core
    """
    op_unpacker_streams = get_unpacker_streams_from_core_packer_stream(
        ha_context, stream_id
    )

    assert len(op_unpacker_streams) == 0 or type(op_unpacker_streams[0]) == type(
        stream_id
    )
    # filter self out
    op_unpacker_streams = [s for s in op_unpacker_streams if s != stream_id]
    return op_unpacker_streams


def has_direct_producer_stream(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    """
    Returns true if this stream is directly fed by another stream (i.e. not a queue or a
    packer)
    """
    # Double check that this wasn't alraedy listed elsewhere... I feel like this was
    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
    current_stream_phase = get_stream_phase(ha_context, stream_id)

    stream_blob_yaml_designator = stream_id.as_blob_yaml_designator()

    if stream_blob_yaml_designator not in temporal_graph.stream_loc_id_map:
        return False

    temporal_graph_stream_id = temporal_graph.stream_loc_id_map[
        stream_blob_yaml_designator
    ]
    if temporal_graph_stream_id not in temporal_graph.stream_source_map:
        return False

    blob_phase = get_blob_phase(int(temporal_graph.id()), current_stream_phase)
    return blob_phase in temporal_graph.stream_source_map[temporal_graph_stream_id]


def queue_has_data(ha_context: HangAnalysisContext, q: Queue):
    """
    Checking if queue has data
    """
    context = ha_context.context
    device_id = q.device_id()
    device = context.devices[device_id]
    if not q.is_host() and not q.is_dram():
        WARN(f"Unknown memory location: {q.get_loc()}")
        return True

    for mem_addr in q.get_mem_addr():
        if q.is_dram():
            dram_chan, dram_addr = mem_addr[0], mem_addr[1]
            x, y = device.DRAM_CHANNEL_TO_NOC0_LOC[dram_chan]
            read_ptr = device.pci_read_xy(x, y, 0, dram_addr)
            write_ptr = device.pci_read_xy(x, y, 0, dram_addr + 4)
        if q.is_host():
            host_chan, host_addr = mem_addr[0], mem_addr[1]
            read_ptr = tt_device.SERVER_IFC.host_dma_read(
                device_id, host_addr, host_chan
            )
            write_ptr = tt_device.SERVER_IFC.host_dma_read(
                device_id, host_addr + 4, host_chan
            )

        # Get read and write pointers
        if Queue.occupancy(q.entries(), write_ptr, read_ptr) == 0:
            return False

    return True


def queue_is_full(ha_context: HangAnalysisContext, q: Queue):
    """
    Checking if queue has data
    """
    context = ha_context.context
    device_id = q.device_id()
    device = context.devices[device_id]
    if not q.is_host() and not q.is_dram():
        WARN(f"Unknown memory location: {q.get_loc()}")
        return True

    for mem_addr in q.get_mem_addr():
        if q.is_dram():
            dram_chan, dram_addr = mem_addr[0], mem_addr[1]
            x, y = device.DRAM_CHANNEL_TO_NOC0_LOC[dram_chan]
            read_ptr = device.pci_read_xy(x, y, 0, dram_addr)
            write_ptr = device.pci_read_xy(x, y, 0, dram_addr + 4)
        if q.is_host():
            host_chan, host_addr = mem_addr[0], mem_addr[1]
            read_ptr = tt_device.SERVER_IFC.host_dma_read(
                device_id, host_addr, host_chan
            )
            write_ptr = tt_device.SERVER_IFC.host_dma_read(
                device_id, host_addr + 4, host_chan
            )

        # Get read and write pointers
        return Queue.occupancy(q.entries(), write_ptr, read_ptr) == q.entries()

    return False


def read_device_epochs(device):
    """
    Read all cores on a device and return a dictionary of epoch_id to core locations
    :return: { epoch_id : [ core_loc ] }
    """
    cte = device.read_core_to_epoch_mapping()
    epoch_to_core_locs = dict()
    for loc, epoch_id in cte.items():
        if epoch_id not in epoch_to_core_locs:
            epoch_to_core_locs[epoch_id] = set()
        epoch_to_core_locs[epoch_id].add(loc)

    return epoch_to_core_locs


def read_current_epochs(context):
    """
    Probing all devices for all active cores and their current epochs
    :return: { device_id : { epoch_id : [ core_loc ] } }
    """
    device_id_to_epoch_dict = dict()

    for device_id, _ in context.netlist.device_graph_names().items():
        device = context.devices[device_id]
        device_id_to_epoch_dict[device_id] = read_device_epochs(device)
    return device_id_to_epoch_dict


####--------------------------------------------------------------


def is_empty_stream_backpressured(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    """
    If a stream is empty, it can still be backpressured. This function will return true if it
    sees that this is the case for the passed in stream

    This can often be seen when a stream has sent all of its tiles and it is waiting
    for an ack from the downstream stream. The consumer stream may not have acked yet becuase it
    is waiting for it's tiles to be able to be cleared out, so space can be freed and credits
    sent upstream to the producer
    """
    device = get_device_of_stream(ha_context, stream_id)
    stream_register_data = ha_context.get_stream_register_data(stream_id)
    if not device.is_stream_configured(stream_register_data):
        return False

    if not ha_context.is_stream_active(stream_id):
        return False

    if device.is_stream_holding_tiles(stream_register_data):
        return True

    # Empty but could still be backpressured if active (which it is). Check the consumer streams
    # for tiles
    if not is_stream_directly_feeding_other_stream(ha_context, stream_id):
        return False

    consumer_streams = get_dest_streams(ha_context, stream_id)
    return any(
        device.is_stream_holding_tiles(ha_context.get_stream_register_data(cs))
        for cs in consumer_streams
    )


def packer_stream_is_backpressured(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    assert is_packer_stream(ha_context, stream_id) or is_packer_fork_stream(
        ha_context, stream_id
    )
    return is_empty_stream_backpressured(ha_context, stream_id)


def any_packer_stream_is_backpressured(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    forked_stream_ids = get_forked_streams_from_packer_stream(ha_context, stream_id)
    return any(
        packer_stream_is_backpressured(ha_context, fs) for fs in forked_stream_ids
    )


def visit_unpacker_stream_with_tile_data(context, ha_context, stream_id):
    assert stream_id is not None
    packer_streams = get_packer_stream_from_unpacker_stream(ha_context, stream_id)
    forked_output_streams = get_forked_streams_from_packer_stream(
        ha_context, packer_streams[0]
    )

    output_streams = tuple(set(forked_output_streams + packer_streams))
    no_output_streams_are_backpressured = True
    for output_stream_id in output_streams:
        assert isinstance(output_stream_id, HAStreamId)
        output_stream_data = ha_context.get_stream_register_data(output_stream_id)
        device = get_device_of_stream(ha_context, output_stream_id)
        is_holding_tiles = device.is_stream_holding_tiles(output_stream_data)
        if is_holding_tiles or packer_stream_is_backpressured(
            ha_context, output_stream_id
        ):
            # The packer has tiles which aren't being sent out, then there must be some back-pressuring
            ha_context.add_stream_to_frontier(output_stream_id, stream_id)
            no_output_streams_are_backpressured = False

    if no_output_streams_are_backpressured:
        # We aren't receiving backpressure on the output side so we want to
        # Check other unpacker streams for this op - particularly ones that are unvisited.
        unpacker_streams = get_other_unpacker_streams_from_unpacker_stream(
            ha_context, stream_id
        )

        any_visited_are_clean = False
        core_epoch = get_stream_temporal_epoch_id(ha_context, stream_id)
        all_unpackers_checked = True
        found_other_empty_unpacker = False
        for unpacker_stream_id in unpacker_streams:
            device = get_device_of_stream(ha_context, unpacker_stream_id)
            if unpacker_stream_id != stream_id and not device.is_stream_holding_tiles(
                ha_context.get_stream_register_data(unpacker_stream_id)
            ):
                found_other_empty_unpacker = True

            if ha_context.is_stream_in_frontier(unpacker_stream_id):
                LOG("\t\t\tFound unchecked unpacker")
                continue
            all_unpackers_checked = False
            ha_context.add_stream_to_frontier(unpacker_stream_id, stream_id)

        if all_unpackers_checked:
            if not found_other_empty_unpacker:
                ha_context.report_hang(
                    stream_id,
                    f"When visiting unpacker stream {stream_id} in epoch {core_epoch}. The stream has tiles, but all of the op's packer streams are empty and we've visited all other unpacker buffers already",
                )
            else:
                ha_context.stop_current_path_traversal(
                    stream_id, StreamTraversalResult.CLEAN
                )

    else:
        # Atleast one stream has tiles so we should go down that path
        device = get_device_of_stream(ha_context, stream_id)
        packer_streams_with_data = (
            ps
            for ps in packer_streams
            if device.is_stream_holding_tiles(ha_context.get_stream_register_data(ps))
        )
        for ps in packer_streams_with_data:
            ha_context.add_stream_to_frontier(ps, stream_id)


def add_backpressured_packer_fork_streams_to_frontier(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    """
    returns True if no streams are backpressured
    """
    assert is_packer_stream(ha_context, stream_id) or is_packer_fork_stream(
        ha_context, stream_id
    )
    no_streams_backpressured = True
    forked_stream_ids = get_forked_streams_from_packer_stream(ha_context, stream_id)
    for forked_stream_id in forked_stream_ids:
        if packer_stream_is_backpressured(ha_context, forked_stream_id):
            no_streams_backpressured = False
            ha_context.mark_found_backpressured_stream(stream_id)
            ha_context.add_stream_to_frontier(forked_stream_id, stream_id)

    return no_streams_backpressured


def visit_packer_or_relay_stream_with_tile_data(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    no_streams_backpressured = True
    if is_packer_stream(ha_context, stream_id) or is_packer_fork_stream(
        ha_context, stream_id
    ):
        no_streams_backpressured = add_backpressured_packer_fork_streams_to_frontier(
            ha_context, stream_id
        )

    device = get_device_of_stream(ha_context, stream_id)
    stream_data = ha_context.get_stream_register_data(stream_id)
    if device.is_stream_holding_tiles(stream_data) or (
        is_empty_stream_backpressured(ha_context, stream_id)
        or packer_stream_is_backpressured(ha_context, stream_id)
    ):
        ## Go down
        no_streams_backpressured = False

    assert not no_streams_backpressured
    if stream_writes_to_dram(ha_context, stream_id):
        # if we are writing to DRAM then
        # we need to look past it (e2e queue case) or stop here
        # and report that an output queue isn't getting popped properly
        temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
        consumer_queue_buffers = get_consumer_queue_buffers(ha_context, stream_id)
        consumer_queue_names = get_queue_names_from_buffers(
            ha_context, temporal_graph, consumer_queue_buffers
        )
        assert (
            len(consumer_queue_buffers) == 1
        ), "unimplemented path when we fork to multiple queue buffers"
        for consumer_queue_name in consumer_queue_names:
            consumer_queue = temporal_graph.queues[consumer_queue_name]
            if queue_has_op_consumer(consumer_queue):
                # We can stop searching here because queues can be seen as sinks, more or less (in some
                # cases such as with buffering queues we need to be careful but to simplify for now we
                # can ignore such cases by visiting the consumer side as a separate traversal)
                # In a complete implementation, we'll need to traverse through the queues

                ha_context.stop_current_path_traversal(
                    stream_id, StreamTraversalResult.CLEAN
                )
                pass

            else:
                assert is_output_queue(consumer_queue)
                if queue_is_full(ha_context.context, consumer_queue):
                    ha_context.report_hang(
                        stream_id,
                        f"Output queue {consumer_queue} is not being drained and is causing backpressure to its producers. They are unable to advance",
                    )
    else:
        dest_stream_ids = get_dest_streams(ha_context, stream_id)
        for dest_stream_id in dest_stream_ids:
            ha_context.add_stream_to_frontier(dest_stream_id, stream_id)


def visit_dram_writing_stream(ha_context: HangAnalysisContext, stream_id: HAStreamId):
    assert False, "unimplemented"


def get_core_on_other_end_of_ethernet_link(
    ha_context: HangAnalysisContext, my_ethernet_core_location: OnChipCoordinate
):
    try:
        return ha_context.ethernet_connections[my_ethernet_core_location]
    except:
        return None


def visit_empty_stream_without_tiles_in_dummy_phase(
    context, ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    stream_register_data = ha_context.get_stream_register_data(stream_id)
    has_remote_receiver = (
        "REMOTE_RECEIVER" in stream_register_data
        and int(stream_register_data["REMOTE_RECEIVER"]) != 0
    )
    has_remote_source = (
        "REMOTE_SOURCE" in stream_register_data
        and int(stream_register_data["REMOTE_SOURCE"]) != 0
    )
    assert has_remote_receiver or has_remote_source

    stream_regs = ha_context.get_stream_register_data(stream_id)
    is_waiting_for_messages = int(
        stream_regs["CURR_PHASE_NUM_MSGS_REMAINING"]
    ) > 0 and int(stream_regs["NUM_MSGS_RECEIVED"] == 0)
    has_messages_remaining = int(stream_regs["CURR_PHASE_NUM_MSGS_REMAINING"]) > 0
    received_messages = int(stream_regs["NUM_MSGS_RECEIVED"] > 0)
    stream_active = is_stream_active(ha_context, stream_id)

    if is_waiting_for_messages:
        LOG("\t\t\twaiting_for_messages")
        if received_messages:
            assert false
            ## We are "done". We can look down further if we want but it's probably not much use.
            ## More likely, another stream on the core is still active. It may be worth adding those to the traversal
            ## but we'll risk polluting the stream source map which would be used for reconstructing the paths (rings)
            ## that cause fork-join related hangs
            if has_remote_receiver:
                remote_stream_id_number = stream_register_data["REMOTE_DEST_STREAM_ID"]
                remote_x = stream_register_data["REMOTE_DEST_X"]
                remote_y = stream_register_data["REMOTE_DEST_Y"]

                remote_receiver_is_ethernet = remote_x == 63 and remote_y == 63

                if remote_receiver_is_ethernet:
                    remote_core_location = get_core_on_other_end_of_ethernet_link(
                        ha_context, stream_id.location
                    )
                    remote_stream_id = HAStreamId(
                        remote_core_location, remote_stream_id_number
                    )
                    assert (
                        remote_stream_id.location._device.id()
                        != stream_id.location._device.id()
                    )
                    ha_context.add_stream_to_frontier(remote_stream_id, stream_id)

                else:
                    # TODO: This is a tricky case. We want to be able to look downward because we could be at a stream that terminated early (incorrectly)
                    #     so we don't want to miss looking down or upstream

                    remote_stream_id = HAStreamId(
                        OnChipCoordinate(
                            remote_x, remote_y, "nocTr", stream_id.location._device
                        ),
                        remote_stream_id_number,
                    )
                    ha_context.add_stream_to_frontier(remote_stream_id, stream_id)

            else:

                ha_context.stop_current_path_traversal(
                    stream_id, StreamTraversalResult.CLEAN
                )
                return

        else:
            LOG("\t\t\t\tnone received")
            # Hasn't received messages
            if has_remote_source:
                LOG("\t\t\t\t\thas remote source")
                remote_stream_id_number = stream_register_data["REMOTE_SRC_STREAM_ID"]
                remote_x = stream_register_data["REMOTE_SRC_X"]
                remote_y = stream_register_data["REMOTE_SRC_Y"]

                remote_source_is_ethernet = remote_x == 63 and remote_y == 63

                if remote_source_is_ethernet:
                    remote_core_location = get_core_on_other_end_of_ethernet_link(
                        ha_context, stream_id.location
                    )
                    remote_stream_id = HAStreamId(
                        remote_core_location, remote_stream_id_number
                    )
                    assert (
                        remote_stream_id.location._device.id()
                        != stream_id.location._device.id()
                    )
                    ha_context.add_stream_to_frontier(remote_stream_id, stream_id)

                else:
                    remote_stream_id = HAStreamId(
                        OnChipCoordinate(
                            remote_x, remote_y, "nocTr", stream_id.location._device
                        ),
                        remote_stream_id_number,
                    )
                    ha_context.add_stream_to_frontier(remote_stream_id, stream_id)

            else:
                LOG("\t\t\t\t\thas remote dest")
                # Has remote receiver
                device = context.devices[stream_id.get_device_id()]
                if stream_id.location in device.get_block_locations("eth"):
                    LOG("\t\t\t\t\t\teth")
                    ## Has remote dest so we are on the receiver side of the ethernet link.
                    ## Need to find the stream on the sender side

                    blob = get_stream_blob_for_dummy_phase_stream(ha_context, stream_id)
                    phase_id = blob["phase_id"]
                    temporal_graph = get_stream_temporal_graph(ha_context, stream_id)
                    stream_id_in_temporal_graph = temporal_graph.stream_loc_id_map[
                        stream_id.as_blob_yaml_designator()
                    ]
                    producer_stream = get_stream_source(
                        ha_context,
                        temporal_graph,
                        stream_id_in_temporal_graph,
                        phase_id,
                    )
                    producer_device = context.devices[producer_stream[0]]
                    producer_stream_id = HAStreamId(
                        OnChipCoordinate(
                            producer_stream[1],
                            producer_stream[2],
                            "nocTr",
                            producer_device,
                        ),
                        producer_stream[3],
                    )
                    producer_temporal_graph = get_stream_temporal_graph(
                        ha_context, producer_stream_id
                    )

                    if producer_temporal_graph.id() != temporal_graph.id():
                        ## This case is questionable. We shouldn't have been able to get into a dummy phase
                        ## and have no tiles while also having the producer not be in the same epoch
                        ha_context.report_hang(
                            stream_id,
                            f"Found stream {stream_id.as_blob_yaml_designator()} in epoch {temporal_graph.id()} in dummy phase but it's producer stream's ({producer_stream_id.as_blob_yaml_designator()}) core is not on the same temporal epoch ({producer_temporal_graph.id()})",
                        )

                    else:
                        ha_context.add_stream_to_frontier(producer_stream_id, stream_id)

                else:
                    LOG("\t\t\t\t\t\tnot eth")
                    ## Presumed packer - need to find the packer bufs
                    visit_packer_or_relay_stream_with_tile_data(ha_context, stream_id)

    else:
        LOG("\t\t\tnot waiting_for_messages")
        # not waiting for messages
        if received_messages:
            # invalid case
            if stream_active:
                assert (
                    false
                ), "Invalid scenario. Stream in dummy phase is active and not waiting for messages but has some messages"

        else:
            if (
                stream_active
                and (
                    is_packer_stream(ha_context, stream_id)
                    or is_packer_fork_stream(ha_context, stream_id)
                )
                or (is_empty_stream_backpressured(ha_context, stream_id))
            ):
                LOG("\t\t\t\thas no messages")
                visit_empty_packer_or_packer_fork_stream(ha_context, stream_id)
            else:
                LOG("\t\t\t\tnot packer")
                if stream_active:
                    assert false, "unimplemented"
                    # If it's packer or forked packer stream, we are probably waiting for the other forked streams
                else:
                    # Nothing to do here
                    pass


# Visited
def visit_stream_with_tiles_in_dummy_phase(
    context, ha_context: HangAnalysisContext, stream_id: HAStreamId
):
    """
    Likely receiving backpressure if we are in dummy phase and already have tiles
    """
    stream_register_data = ha_context.get_stream_register_data(stream_id)
    has_remote_receiver = (
        "REMOTE_RECEIVER" in stream_register_data
        and int(stream_register_data["REMOTE_RECEIVER"]) != 0
        or "REMOTE_DEST_X" in stream_register_data
    )
    has_remote_source = (
        "REMOTE_SOURCE" in stream_register_data
        and int(stream_register_data["REMOTE_SOURCE"]) != 0
    )
    assert has_remote_receiver or has_remote_source

    if has_remote_receiver:
        remote_stream_id_number = stream_register_data["REMOTE_DEST_STREAM_ID"]
        remote_x = stream_register_data["REMOTE_DEST_X"]
        remote_y = stream_register_data["REMOTE_DEST_Y"]

        remote_receiver_is_ethernet = remote_x == 63 and remote_y == 63

        if remote_receiver_is_ethernet:
            remote_core_location = get_core_on_other_end_of_ethernet_link(
                ha_context, stream_id.location
            )
            remote_stream_id = HAStreamId(remote_core_location, remote_stream_id_number)
            assert (
                remote_stream_id.location._device.id()
                != stream_id.location._device.id()
            )
            ha_context.add_stream_to_frontier(remote_stream_id, stream_id)

        else:
            remote_stream_id = HAStreamId(
                OnChipCoordinate(
                    remote_x, remote_y, "nocTr", stream_id.location._device
                ),
                remote_stream_id_number,
            )
            ha_context.add_stream_to_frontier(remote_stream_id, stream_id)

    else:
        device = context.devices[stream_id.get_device_id()]
        if stream_id.location in device.get_block_locations("eth"):
            ## Has remote dest so we are on the receiver side of the ethernet link.
            ## Need to find the stream on the sender side
            blob = get_stream_blob_for_dummy_phase_stream(ha_context, stream_id)
            phase_id = blob["phase_id"]
            dest_stream_designator = blob["dest"]
            assert len(dest_stream_designator) == 1
            dest_stream_designator = dest_stream_designator[0]

            ha_context.add_stream_to_frontier(
                HAStreamId.from_blob_yaml_designator(dest_stream_designator), stream_id
            )

        else:
            ## Presumed unpacker - need to find the packer bufs
            visit_unpacker_stream_with_tile_data(context, ha_context, stream_id)


def visit_empty_packer_or_packer_fork_stream(
    ha_context: HangAnalysisContext, stream_id: HAStreamId
):

    device = ha_context.context.devices[stream_id.get_device_id()]
    core_temporal_epoch = ha_context.get_stream_temporal_epoch_id(
        stream_id
    )  # device.get_epoch_id(stream_id.location)
    device = ha_context.context.devices[stream_id.get_device_id()]
    no_packer_streams_are_backpressured = not any_packer_stream_is_backpressured(
        ha_context, stream_id
    )
    if (
        no_packer_streams_are_backpressured
    ):  # all_packer_streams_empty(ha_context, stream_id):
        unpacker_streams_ids = get_unpacker_streams_from_core_packer_stream(
            ha_context, stream_id
        )
        all_unpacker_streams_already_visited = False
        # all_unpacker_streams_already_visited = True
        any_unpacker_streams_clean = True
        all_unpacker_streams_have_tiles = True
        all_unpack_streams_empty = True
        for unpacker_stream_id in unpacker_streams_ids:
            if device.is_stream_holding_tiles(
                ha_context.get_stream_register_data(unpacker_stream_id)
            ):
                all_unpack_streams_empty = False
            else:
                all_unpacker_streams_have_tiles = False
                ha_context.add_stream_to_frontier(
                    unpacker_stream_id, stream_id
                )  ## Remove me to go back

        if all_unpacker_streams_have_tiles:
            ha_context.report_hang(
                stream_id,
                f"Visited empty packer stream {stream_id} in epoch {core_temporal_epoch} where all unpacker streams have tiles. This packer stream also either doesn't fork or all forked output streams are also empty. There is likely a hang on this core",
            )
        elif all_unpacker_streams_already_visited:
            if any_unpacker_streams_clean or all_unpack_streams_empty:
                pass
                # ha_context.stop_current_path_traversal(stream_id, StreamTraversalResult.CLEAN)
            else:
                ha_context.report_hang(
                    stream_id,
                    f"Visited empty packer stream {stream_id} in epoch {core_temporal_epoch}. Either it doesn't fork or all forked packer streams are empty. However, all unpacker streams have already been visited. Therefore, there is likely a hang on this core",
                )

    else:
        all_fork_streams_empty = add_backpressured_packer_fork_streams_to_frontier(
            ha_context, stream_id
        )


def visit_inactive_stream(
    ha_context: HangAnalysisContext, stream_id: HAStreamId, source_stream: HAStreamId
):
    warn_only = False
    emit_message = True
    device = ha_context.context.devices[stream_id.get_device_id()]
    core_temporal_epoch = ha_context.get_stream_temporal_epoch_id(
        stream_id
    )  # device.get_epoch_id(stream_id.location)
    is_stream_in_epoch = (
        stream_id.as_blob_yaml_designator()
        in get_stream_temporal_graph(ha_context, stream_id).stream_loc_id_map
    )
    error_string = f"Visited an inactive stream during traversal."

    assert (
        source_stream is not None
    ), "Shouldn't have started a traversal from an inactive stream"
    source_stream_epoch = get_stream_temporal_epoch_id(ha_context, source_stream)
    if is_stream_in_epoch:
        if is_packer_stream(ha_context, stream_id) or is_packer_fork_stream(
            ha_context, stream_id
        ):
            ## This is an inactive packer stream that is defined in the current epoch. It is possible that this stream
            ## is one of multiple output streams on the core and is waiting to be reprogrammed to the start of a new
            ## phase loop iteration by FW because FW is waiting for other streams to finish. Those other streams may
            ## be experiencing backpressure
            visit_empty_packer_or_packer_fork_stream(ha_context, stream_id)
            emit_message = False

        elif source_stream_epoch == core_temporal_epoch:
            error_string += f" This stream and its source stream ({source_stream}) are both in the same epoch ({source_stream_epoch}) so there is likely a hang here because the current stream should still be live"
        else:
            warn_only = True
            error_string += f" However, the source stream ({source_stream}) is currently executing a different epoch ({source_stream_epoch}) so one of the two streams likely just hasn't reprogrammed to the next epoch due to other streams still running on their cores."
    else:
        assert source_stream_epoch != core_temporal_epoch
        warn_only = True

        error_string += f" However, this stream isn't configured in the current epoch {core_temporal_epoch}. The source stream ({source_stream}) is in epoch {source_stream_epoch} which is a different epoch which may explain this"
    if emit_message:
        if warn_only:
            WARN(error_string)
        else:
            ha_context.report_hang(stream_id, error_string)


def visit_stream(
    context,
    ha_context: HangAnalysisContext,
    stream_id: HAStreamId,
    source_stream: HAStreamId,
):
    """
    Top-level visit stream function

    It may be cleaner and easier to reason about if this function is structured to process each stream
    based on type (maybe not because some streams will share many common paths) but for now it's based
    on empty-non-empty checks and goes from there
    """
    assert stream_id is not None

    ha_context.add_stream_processed(stream_id)
    device = context.devices[stream_id.get_device_id()]
    core_temporal_epoch = ha_context.get_stream_temporal_epoch_id(
        stream_id
    )  # device.get_epoch_id(stream_id.location)
    temporal_graph = context.netlist.get_temporal_graph_name(core_temporal_epoch)
    LOG(f"Visiting stream {str(stream_id)}. Core is on epoch {core_temporal_epoch}")

    stream_register_data = ha_context.get_stream_register_data(stream_id)
    if device.is_stream_holding_tiles(stream_register_data):
        ha_context.mark_found_backpressured_stream(stream_id)
        LOG("\thas tile data")
        stream_id.labels.add("has_tiles")
        # We are one of 3 streams:
        # 1) Unpacker stream
        #    - Need to check if packer has packed/is blocked (if yes, check other streams, if no, go down the path of the unpacker)
        # 2) Stream (packer or relay) directly feeding another
        # 3) DRAM writer stream (writing to output queue or e2e queue)
        is_in_dummy_phase = is_stream_in_dummy_phase(ha_context, stream_id)
        # Dummy phase with tiles => go down (well technically consumer could be in dummy phase waiting for the producer)
        # to enter dummy phase
        if is_in_dummy_phase:
            LOG("\t\tstream in dummy phase")
            stream_id.labels.add("dummy_phase")
            visit_stream_with_tiles_in_dummy_phase(context, ha_context, stream_id)

        elif is_unpacker_stream(ha_context, stream_id):
            LOG("\t\tis unpacker")
            stream_id.labels.add("unpacker")
            visit_unpacker_stream_with_tile_data(context, ha_context, stream_id)

        elif is_stream_directly_feeding_other_stream(ha_context, stream_id):
            LOG("\t\tdirect producer of other stream")
            stream_id.labels.add("is_direct_producer")
            visit_packer_or_relay_stream_with_tile_data(ha_context, stream_id)

        elif stream_writes_to_dram(ha_context, stream_id):
            LOG("\t\twrites to DRAM")
            stream_id.labels.add("writes_to_dram")
            # we should visit the consumer side via another iteration
            ha_context.stop_current_path_traversal(
                stream_id, StreamTraversalResult.CLEAN
            )
            pass
            visit_dram_writing_stream(ha_context, stream_id)

        else:
            LOG("\t\tunsupported case")
            stream_id.labels.add("unsupported case")
            assert false, "Unknown/Unsupported case"

    else:

        LOG("\tstream empty")
        stream_id.labels.add("empty")
        # The stream has no tile data. This means the stream is waiting for data, so we should go up the graph
        # If this is a DRAM reader though, it's possible that the queue has no data - it's possible the hang is
        # due to the producer not pushing input data. However, we can do an easy check for that.
        #
        # If any streams have tile data, then we know that some inputs are being pushed (save for the possibility of
        # them being pushed unevenly. But regardless, that is more easily checked as a separate algorithm to avoid
        # overly complicating this one.
        # If the input is a queue, and the queue is produced by another op, add that op to the frontier
        #  if it's from host, then stop traversal here
        # else find the producer stream
        if not device.is_stream_configured(stream_register_data):
            LOG(f"\t\tstream unconfigured {stream_id}")
            stream_id.labels.add("unconfigured")
            ha_context.stop_current_path_traversal(
                stream_id, StreamTraversalResult.CLEAN
            )
            return

        is_in_dummy_phase = is_stream_in_dummy_phase(ha_context, stream_id)
        # Dummy phase with tiles => go down (well technically consumer could be in dummy phase waiting for the producer)
        # to enter dummy phase
        if is_in_dummy_phase:
            LOG("\t\tstream in dummy phase")
            stream_id.labels.add("dummy_phase")
            visit_empty_stream_without_tiles_in_dummy_phase(
                context, ha_context, stream_id
            )

        elif not is_stream_active(ha_context, stream_id):
            LOG("inactive")
            stream_id.labels.add("inactive")
            visit_inactive_stream(ha_context, stream_id, source_stream)

        elif is_empty_stream_backpressured(ha_context, stream_id):
            LOG("\t\tbackpressured 'empty' stream")
            stream_id.labels.add("backpressured 'empty' stream")
            visit_packer_or_relay_stream_with_tile_data(ha_context, stream_id)

        elif is_packer_stream(ha_context, stream_id) or is_packer_fork_stream(
            ha_context, stream_id
        ):
            LOG(f"\t\tis packer")
            stream_id.labels.add("packer")
            visit_empty_packer_or_packer_fork_stream(ha_context, stream_id)

        elif has_direct_producer_stream(ha_context, stream_id):
            LOG(f"\t\thas direct producer")
            stream_id.labels.add("has_direct_producer")
            producer_stream_id = get_source_stream(
                ha_context, stream_id
            )  # get_stream_producer_through_queue(ha_context, producer_queue, stream_id)
            ha_context.add_stream_to_frontier(producer_stream_id, stream_id)

        else:
            LOG(f"\t\tdram_reader")
            stream_id.labels.add("dram_reader")
            LOG(f"\t\tnot packer (or packer fork) and has no direct producer ")
            # The queue case

            producer_queues = get_producer_queues(ha_context, stream_id)
            queue_has_op_producer = any(q.has_op_producer() for q in producer_queues)
            if queue_has_op_producer or any(
                q.queue.root["type"] != "queue" for q in producer_queues
            ):
                LOG(
                    f"Found an input queue to stream {stream_id} - support for traversing upwards through queues isn't implemented yet."
                )
                LOG(
                    f"\tIf it's an input queue, then it's potentially a cause of the hang because we are currently traversing upwards looking for sample data."
                )
                LOG(
                    f"\tHowever, this could be an incorrect conclusion in the event that these streams are for a later epoch that is programmed early while waiting for the current epoch to finish"
                )
                ha_context.stop_current_path_traversal(
                    stream_id, StreamTraversalResult.CLEAN
                )
            else:
                producer_queue = producer_queues[0]
                device_id = stream_id.get_device_id()
                if len(
                    ha_context.traversed_backpressured_stream_epochs
                ) and not queue_has_data(ha_context, producer_queue.queue):
                    found_backpressured_stream_this_temporal_epoch = (
                        ha_context.get_stream_temporal_epoch_id(stream_id)
                        in ha_context.traversed_backpressured_stream_epochs
                    )
                    queue_hang_unreported = (
                        producer_queue.queue._id not in ha_context.reported_hang_queues
                    )
                    if (
                        found_backpressured_stream_this_temporal_epoch
                        and queue_hang_unreported
                    ):
                        ha_context.report_hang(
                            stream_id,
                            f'Found an empty input queue "{producer_queue.queue._id}" to stream {str(stream_id)}, which is waiting for tiles. This stream was traversed to as a result of hitting other streams that have tile data',
                        )
                        ha_context.reported_hang_queues.add(producer_queue.queue._id)

                else:
                    LOG(
                        f"Found an input queue to stream {stream_id} - support for traversing upwards through queues isn't implemented yet."
                    )
                    LOG(
                        f"\tIf it's an input queue, then it's potentially a cause of the hang because we are currently traversing upwards looking for sample data."
                    )
                    LOG(
                        f"\tHowever, this could be an incorrect conclusion in the event that these streams are for a later epoch that is programmed early while waiting for the current epoch to finish"
                    )


def process_already_visited_stream(
    context, ha_context: HangAnalysisContext, current_stream_id: HAStreamId
):
    device = get_device_of_stream(ha_context, current_stream_id)
    core_epoch = ha_context.get_stream_temporal_epoch_id(
        current_stream_id
    )  # device.get_epoch_id(current_stream_id.location)

    ## Next update to add. If we've revisited a stream, that is actually okay if we
    ## haven't visited every other packer/unpacker stream on the core yet as we may
    ## need to traverse through those to find the real source of the poblem
    is_in_dummy_phase = is_stream_in_dummy_phase(ha_context, current_stream_id)
    is_packer_dummy_phase = False
    is_unpacker_dummy_phase = False
    if is_in_dummy_phase:
        stream_register_data = ha_context.get_stream_register_data(current_stream_id)
        has_remote_receiver = (
            "REMOTE_RECEIVER" in stream_register_data
            and int(stream_register_data["REMOTE_RECEIVER"]) != 0
        )
        has_remote_source = (
            "REMOTE_SOURCE" in stream_register_data
            and int(stream_register_data["REMOTE_SOURCE"]) != 0
        )
        is_packer_dummy_phase = has_remote_receiver
        is_unpacker_dummy_phase = has_remote_source

    is_packer = (
        is_packer_dummy_phase
        if is_in_dummy_phase
        else is_packer_stream(ha_context, current_stream_id)
    )
    is_unpacker = (
        is_unpacker_dummy_phase
        if is_in_dummy_phase
        else is_unpacker_stream(ha_context, current_stream_id)
    )
    is_packer_fork = not is_in_dummy_phase and is_packer_fork_stream(
        ha_context, current_stream_id
    )
    if is_packer or is_unpacker or is_packer_fork:
        all_op_streams = set()
        is_eth_core = current_stream_id.location in device.get_block_locations(
            block_type="eth"
        )
        any_visited_op_streams_clean = True
        if is_eth_core:
            all_op_streams_processed = True
            all_visited_in_current_epoch = True
        else:
            if is_packer or is_packer_fork:
                all_op_streams.update(
                    set(
                        get_forked_streams_from_packer_stream(
                            ha_context, current_stream_id
                        )
                    )
                )
                all_op_streams.update(
                    set(
                        get_unpacker_streams_from_core_packer_stream(
                            ha_context, current_stream_id
                        )
                    )
                )
            elif is_unpacker:
                all_op_streams.update(
                    set(
                        get_packer_stream_from_unpacker_stream(
                            ha_context, current_stream_id
                        )
                    )
                )
                all_op_streams.update(
                    set(
                        get_other_unpacker_streams_from_unpacker_stream(
                            ha_context, current_stream_id
                        )
                    )
                )

            any_visited_op_streams_clean = any(
                ha_context.is_stream_already_visited(stream_id)
                and (
                    ha_context.get_stream_traversal_result(stream_id)
                    == StreamTraversalResult.CLEAN
                    or ha_context.get_stream_traversal_result(stream_id)
                    == StreamTraversalResult.UNVISITED
                )
                for stream_id in all_op_streams
            )

            all_op_streams_processed = all(
                ha_context.is_stream_already_visited(stream_id)
                for stream_id in all_op_streams
            )
            all_visited_in_current_epoch = all_op_streams_processed and all(
                ha_context.is_stream_already_visited_from_epoch(stream_id, core_epoch)
                for stream_id in all_op_streams
            )
        if (
            True
        ):  # all_visited_in_current_epoch and ha_context.is_stream_already_visited_from_epoch(current_stream_id, core_epoch) and not any_visited_op_streams_clean:
            ha_context.report_hang(
                current_stream_id,
                f"Visited stream {current_stream_id.as_blob_yaml_designator()} again. All other op buffers on the core were visited. Core is currently on epoch {core_epoch}. This may indicate a hang source. Look here.",
            )
    else:
        if (
            True
        ):  # current_stream_id in ha_context.current_traversal_streams and ha_context.stream_sources_contain_duplicates(current_stream_id):
            ## Else we may have visited it from an stream from another epoch blocked by this stream
            ## because it's not programmed to that epoch yet
            ha_context.report_hang(
                current_stream_id,
                f"Visited stream {current_stream_id.as_blob_yaml_designator()} again. Core is currently on epoch {core_epoch}. This may indicate a hang source. Look here.",
            )


def cleanup_packer_to_packer_connectivity(ha_context: HangAnalysisContext):
    """
    Removes false cycles that would otherwise cause problems for cycle detection, for fork-join type issues
    """
    remove_packer_to_packer_edge = set()
    remove_unpacker_to_unpacker_edge = set()
    removed_edges = set()
    for source, dests in ha_context.connectivity_graph.traversal_graph.items():
        if source is None:
            continue
        if (
            source in remove_packer_to_packer_edge
            or source in remove_unpacker_to_unpacker_edge
        ):
            continue

        if is_stream_active(ha_context, source) and (
            is_packer_stream(ha_context, source)
            or is_packer_fork_stream(ha_context, source)
        ):
            for dest in dests:
                if source in ha_context.connectivity_graph.traversal_graph.get(
                    dest, []
                ):
                    removed_edges.add((dest, source))
                    remove_packer_to_packer_edge.add(dest)

        if is_stream_active(ha_context, source) and is_unpacker_stream(
            ha_context, source
        ):
            for dest in dests:
                if source in ha_context.connectivity_graph.traversal_graph.get(
                    dest, []
                ):
                    removed_edges.add((dest, source))
                    remove_unpacker_to_unpacker_edge.add(dest)

    for source, dest in removed_edges:
        ha_context.connectivity_graph.traversal_graph[source].remove(dest)


def run(cmd_text, context, ui_state=None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    verbose = 0 if args["<verbose>"] is None else int(args["<verbose>"])
    global ha_v2_verbose
    ha_v2_verbose = verbose

    if verbose:
        # Turn on tracing for function calls in this module. This prints docstrings and function call argument values.
        # FIX: This is sticky across all calls of the same command
        util.decorate_all_module_functions_for_tracing(sys.modules[__name__])

    LOG(f"Running hang analysis (verbose={verbose})")

    ha_context = HangAnalysisContext(context)

    print(f"Total active streams: {len(ha_context.global_workload_streams)}")
    streams_per_percent = int(len(ha_context.global_workload_streams) / 100)
    while True:  # num_streams_processed < len(ha_context.global_workload_streams):
        starting_stream_id = ha_context.choose_unvisited_starting_stream()
        if starting_stream_id is None:
            if (
                len(ha_context.reported_hang_queues) == 0
                and len(ha_context.reported_hang_streams) == 0
            ):
                util.ERROR("Couldn't find source(s) of hang. Exiting hang analysis")
            break
        ha_context.add_stream_to_frontier(starting_stream_id, None)
        while len(ha_context.frontier_streams) > 0:
            current_stream_id, source_stream = (
                ha_context.get_and_pop_next_stream_in_frontier()
            )

            assert source_stream is not None or len(
                ha_context.current_traversal_streams
            )

            temporal_epoch_id = get_stream_temporal_epoch_id(
                ha_context, current_stream_id
            )
            already_visited = (
                len(ha_context.stream_traversal_sources[current_stream_id]) > 1
            )
            already_visited_through_same_path = (
                ha_context.stream_sources_contain_duplicates(current_stream_id)
            )

            if already_visited or already_visited_through_same_path:
                LOG(f"stream already visited through path")
                if already_visited_through_same_path:
                    process_already_visited_stream(
                        context, ha_context, current_stream_id
                    )
            else:
                visit_stream(context, ha_context, current_stream_id, source_stream)

    print(f"Done visiting streams")

    found_hang = (
        len(ha_context.reported_hang_queues) == 0
        and len(ha_context.reported_hang_streams) == 0
    )
    if not found_hang:
        ## If we didn't find a hang yet, we will run a cycle analysis which may identify fork-join related hangs

        ## Remove "false" cycles (e.g. mutual forked packer -> forked packer paths)
        cleanup_packer_to_packer_connectivity(ha_context)

        ha_context.connectivity_graph.run_cycle_analysis()
