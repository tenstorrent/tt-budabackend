# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  pipe <pipe-id>

Arguments:
  pipe-id    The ID of the pipe to show.

Description:
  Prints details on the pipe with a given ID.

Examples:
  pipe 125000000000
"""

from debuda import UIState
import tt_util as util
from docopt import docopt

command_metadata = {
    "long": "pipe",
    "short": "p",
    "type": "high-level",
    "description": __doc__,
}


def run(cmd_text, context, ui_state: UIState = None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    pipe_id = int(args["<pipe-id>"], 0) if args["<pipe-id>"] else 0

    graph_name = ui_state.current_graph_name
    graph = context.netlist.graph(graph_name)
    pipe = graph.get_pipes(pipe_id).first()
    navigation_suggestions = []
    if pipe:
        util.print_columnar_dicts(
            [pipe.root], [f"{util.CLR_INFO}Graph {graph_name}{util.CLR_END}"]
        )

        for input_buffer in pipe.input_buffers:
            navigation_suggestions.append(
                {"cmd": f"b {input_buffer}", "description": "Show src buffer"}
            )
        for input_buffer in pipe.output_buffers:
            navigation_suggestions.append(
                {"cmd": f"b {input_buffer}", "description": "Show dest buffer"}
            )
    else:
        util.WARN(f"Cannot find pipe {pipe_id} in graph {graph_name}")

    return navigation_suggestions
