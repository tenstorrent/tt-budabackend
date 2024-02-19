# SPDX-FileCopyrightText: © 2024 Tenstorrent AI ULC

# SPDX-License-Identifier: Apache-2.0
"""
Usage:
  g <graph-name>

Arguments:
  graph-name    The name of the graph to show.

Description:
  Changes the currently active graph to the one with the given name. Some operations only
  work on the currently active graph.

Examples:
  g test_op
"""

command_metadata = {"short": "g", "type": "high-level", "description": __doc__}

from debuda import UIState
import tt_util as util
from docopt import docopt


def run(cmd_text, context, ui_state: UIState = None):
    args = docopt(__doc__, argv=cmd_text.split()[1:])
    gname = args["<graph-name>"]

    if gname not in context.netlist.graph_names():
        util.WARN(
            f"Invalid graph {gname}. Available graphs: {', '.join (list(context.netlist.graph_names()))}"
        )
    else:
        ui_state.current_graph_name = gname
        print(f"Changed current graph to '{gname}'")

    return None
