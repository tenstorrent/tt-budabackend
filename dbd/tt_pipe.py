# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
from tt_object import TTObject, TTObjectIDDict


# Constructed from epoch's pipegen.yaml. Contains information about a pipe.
class Pipe(TTObject):
    def __init__(self, graphs, data):
        self.root = data
        self._id = self.root["id"]
        self.graphs = graphs
        self.input_buffers = TTObjectIDDict()
        self.output_buffers = TTObjectIDDict()
        self.stream_id = None

    # Renderer
    def __str__(self):
        return f"{super().__str__()}, inputs: {self.input_buffers}, outputs: {self.output_buffers}"
