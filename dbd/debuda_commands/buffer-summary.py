# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  buffer <buffer-id>

Arguments:
  buffer-id    The ID of the buffer to show.

Description:
  Prints details on the buffer with a given ID.

Examples:
  buffer 106000000000
"""

command_metadata = {
    "long": "buffer",
    "short": "b",
    "type": "low-level",
    "description": __doc__,
}

from docopt import docopt
from debuda import UIState
import tt_util as util


def run(cmd_text, context, ui_state: UIState =None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    try:
        buffer_id = int(args["<buffer-id>"])
    except ValueError:
        util.ERROR("Buffer ID must be an integer")
        return None

    navigation_suggestions = []

    graph_name = ui_state.current_graph_name
    graph = context.netlist.graph(graph_name)

    if type(buffer_id) == int:
        buffer = graph.get_buffers(buffer_id).first()
        if not buffer:
            util.WARN(f"Cannot find buffer {buffer_id} in graph {graph_name}")
        else:
            util.print_columnar_dicts(
                [buffer.root], [f"{util.CLR_INFO}Graph {graph_name}{util.CLR_END}"]
            )

            for pipe_id, pipe in graph.temporal_epoch.pipes.items():
                if buffer_id in pipe.root["input_list"]:
                    print(f"( {util.CLR_BLUE}Input{util.CLR_END} of pipe {pipe.id()} )")
                    navigation_suggestions.append(
                        {"cmd": f"p {pipe.id()}", "description": "Show pipe"}
                    )
                if buffer_id in pipe.root["output_list"]:
                    print(
                        f"( {util.CLR_BLUE}Output{util.CLR_END} of pipe {pipe.id()} )"
                    )
                    navigation_suggestions.append(
                        {"cmd": f"p {pipe.id()}", "description": "Show pipe"}
                    )
    else:
        util.WARN(f"Buffer ID must be an integer")

    return navigation_suggestions
