# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from tt_object import TTObject, TTObjectIDDict
import tt_util as util
from tt_coordinate import OnChipCoordinate


# Constructed from epoch's pipegen.yaml. Contains information about a buffer.
class Buffer(TTObject):
    def __init__(self, graph, data):
        data["core_coordinates"] = tuple(data["core_coordinates"])
        self.root = data
        self._id = self.root["uniqid"]
        self.replicated = False
        self.graph = graph
        self.stream_id = None
        self.input_of_pipes = TTObjectIDDict()
        self.output_of_pipes = TTObjectIDDict()

    # Renderer
    def __str__(self):
        r = self.root
        R = r["core_coordinates"][0]
        C = r["core_coordinates"][1]
        return f"{super().__str__()} {r['md_op_name']}:[{R},{C}]"

    def is_output_of_pipe(self):
        return len(self.output_of_pipes) > 0

    def is_input_of_pipe(self):
        return len(self.input_of_pipes) > 0

    def loc(self):
        return OnChipCoordinate(
            *self.root["core_coordinates"], "netlist", self.graph.device
        )
